// Copyright 2026 Ardia. Apache-2.0.

#include "chrome/browser/ardia/ac_cli_control_file.h"

#include <windows.h>

#include <sddl.h>

#include <algorithm>
#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/win/scoped_handle.h"
#include "base/win/win_util.h"

namespace ardia::cli_control_internal {
namespace {

constexpr int kReplaceAttempts = 5;
constexpr base::TimeDelta kReplaceRetryDelay = base::Milliseconds(20);

struct LocalFreeDeleter {
  void operator()(void* memory) const { ::LocalFree(memory); }
};

using ScopedSecurityDescriptor =
    std::unique_ptr<void, LocalFreeDeleter>;

std::string ErrorText(DWORD error) {
  return base::ToLowerASCII(logging::SystemErrorCodeToString(error));
}

std::string CouldntWrite(const base::FilePath& path, DWORD error) {
  return base::StrCat(
      {"Couldn't write ", path.AsUTF8Unsafe(), ": ", ErrorText(error)});
}

ScopedSecurityDescriptor OwnerOnlyDescriptor() {
  std::wstring user_sid;
  if (!base::win::GetUserSidString(&user_sid) || user_sid.empty()) {
    return nullptr;
  }
  const std::wstring sddl = L"D:P(A;;FA;;;" + user_sid + L")";
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
          sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
    return nullptr;
  }
  return ScopedSecurityDescriptor(descriptor);
}

bool WriteAll(HANDLE file, const std::string& contents) {
  size_t offset = 0;
  while (offset < contents.size()) {
    const DWORD chunk = static_cast<DWORD>(
        std::min<size_t>(contents.size() - offset, 1 << 20));
    DWORD written = 0;
    if (!::WriteFile(file, contents.data() + offset, chunk, &written,
                     nullptr) ||
        written == 0) {
      return false;
    }
    offset += written;
  }
  return true;
}

bool Transient(DWORD error) {
  return error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION;
}

DWORD ReplaceWith(const base::FilePath& temp, const base::FilePath& path) {
  DWORD error = ERROR_SUCCESS;
  for (int attempt = 0; attempt < kReplaceAttempts; ++attempt) {
    if (::MoveFileExW(temp.value().c_str(), path.value().c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      return ERROR_SUCCESS;
    }
    error = ::GetLastError();
    if (!Transient(error)) {
      break;
    }
    base::PlatformThread::Sleep(kReplaceRetryDelay);
  }
  return error;
}

}

std::string WriteOwnerOnlyFile(const base::FilePath& path,
                               const std::string& contents,
                               base::Lock& publish_lock,
                               base::FunctionRef<bool()> still_wanted) {
  const base::FilePath temp = path.DirName().Append(
      L"." + path.BaseName().value() + L"." +
      base::ASCIIToWide(base::HexEncode(base::RandBytesAsVector(8))));
  ScopedSecurityDescriptor descriptor = OwnerOnlyDescriptor();
  if (!descriptor) {
    return CouldntWrite(path, ::GetLastError());
  }
  SECURITY_ATTRIBUTES attributes = {};
  attributes.nLength = sizeof(attributes);
  attributes.lpSecurityDescriptor = descriptor.get();
  attributes.bInheritHandle = FALSE;
  base::win::ScopedHandle file(::CreateFileW(
      temp.value().c_str(), GENERIC_WRITE, 0, &attributes, CREATE_NEW,
      FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!file.is_valid()) {
    return CouldntWrite(path, ::GetLastError());
  }
  DWORD error = ERROR_SUCCESS;
  if (!WriteAll(file.get(), contents) || !::FlushFileBuffers(file.get())) {
    error = ::GetLastError();
  }
  file.Close();
  if (error != ERROR_SUCCESS) {
    ::DeleteFileW(temp.value().c_str());
    return CouldntWrite(path, error);
  }
  {
    base::AutoLock hold(publish_lock);
    if (!still_wanted()) {
      ::DeleteFileW(temp.value().c_str());
      return "The control server stopped.";
    }
    error = ReplaceWith(temp, path);
    if (error != ERROR_SUCCESS) {
      ::DeleteFileW(temp.value().c_str());
      return CouldntWrite(path, error);
    }
  }
  return std::string();
}

void DeleteOwnerOnlyFile(const base::FilePath& path) {
  if (path.empty() || ::DeleteFileW(path.value().c_str())) {
    return;
  }
  const DWORD error = ::GetLastError();
  if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND &&
      error != ERROR_ACCESS_DENIED) {
    LOG(ERROR) << "[ardia] can't delete " << path << ": " << ErrorText(error);
  }
}

}
