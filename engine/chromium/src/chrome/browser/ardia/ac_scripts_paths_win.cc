// Copyright 2026 Ardia. Apache-2.0.

#include "chrome/browser/ardia/ac_scripts_paths.h"

#include <windows.h>

#include <stdint.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "base/files/file_util.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/win/scoped_handle.h"

namespace ardia::scripts_paths {
namespace {

constexpr std::wstring_view kLongPrefix = L"\\\\?\\";
constexpr std::wstring_view kLongUncPrefix = L"\\\\?\\UNC\\";

template <typename CharT>
uint32_t Unit(CharT c) {
  return static_cast<std::make_unsigned_t<CharT>>(c);
}

template <typename CharT>
bool IsAsciiLetter(CharT c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

template <typename CharT>
bool HasDriveRoot(std::basic_string_view<CharT> value) {
  return value.size() >= 3 && IsAsciiLetter(value[0]) && value[1] == ':' &&
         value[2] == '\\';
}

template <typename CharT>
bool HasForbiddenCharacter(std::basic_string_view<CharT> part) {
  for (CharT c : part) {
    if (Unit(c) < 0x20 || c == ':' || c == '\\' || c == '/' || c == '<' ||
        c == '>' || c == '"' || c == '|' || c == '?' || c == '*') {
      return true;
    }
  }
  return false;
}

template <typename CharT>
bool EndsWithDotOrSpace(std::basic_string_view<CharT> part) {
  return !part.empty() && (part.back() == '.' || part.back() == ' ');
}

template <typename CharT>
bool IsSuperscriptDigit(std::basic_string_view<CharT> tail) {
  if constexpr (sizeof(CharT) == 1) {
    return tail == "\xc2\xb9" || tail == "\xc2\xb2" || tail == "\xc2\xb3";
  } else {
    return tail.size() == 1 &&
           (tail[0] == 0x00b9 || tail[0] == 0x00b2 || tail[0] == 0x00b3);
  }
}

template <typename CharT>
bool NamesDevice(std::basic_string_view<CharT> part) {
  std::basic_string_view<CharT> stem = part.substr(0, part.find('.'));
  while (!stem.empty() && stem.back() == ' ') {
    stem.remove_suffix(1);
  }
  std::string lowered;
  for (CharT c : stem.substr(0, std::min<size_t>(stem.size(), 3))) {
    if (Unit(c) > 0x7f) {
      return false;
    }
    lowered.push_back(base::ToLowerASCII(static_cast<char>(c)));
  }
  if (stem.size() == 3) {
    return lowered == "con" || lowered == "prn" || lowered == "aux" ||
           lowered == "nul";
  }
  if (lowered == "com" || lowered == "lpt") {
    const std::basic_string_view<CharT> tail = stem.substr(3);
    return (tail.size() == 1 && tail[0] >= '0' && tail[0] <= '9') ||
           IsSuperscriptDigit(tail);
  }
  if (lowered == "con") {
    std::string rest;
    for (CharT c : stem.substr(3)) {
      if (Unit(c) > 0x7f) {
        return false;
      }
      rest.push_back(base::ToLowerASCII(static_cast<char>(c)));
    }
    return rest == "in$" || rest == "out$";
  }
  return false;
}

template <typename CharT>
bool IsPlainComponent(std::basic_string_view<CharT> part) {
  return !part.empty() && !HasForbiddenCharacter(part) &&
         !EndsWithDotOrSpace(part) && !NamesDevice(part);
}

}

std::optional<Kind> KindOf(const base::FilePath& path) {
  const DWORD attributes = ::GetFileAttributesW(path.value().c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    return std::nullopt;
  }
  if (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) {
    return Kind::kOther;
  }
  return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? Kind::kDirectory
                                                 : Kind::kFile;
}

base::expected<base::FilePath, Unresolved> ResolvePath(
    const base::FilePath& path) {
  base::win::ScopedHandle handle(::CreateFileW(
      path.value().c_str(), 0,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  if (!handle.is_valid()) {
    const DWORD error = ::GetLastError();
    return base::unexpected(
        error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
            ? Unresolved::kMissing
            : Unresolved::kUnreadable);
  }
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    const DWORD length = ::GetFinalPathNameByHandleW(
        handle.get(), buffer.data(), static_cast<DWORD>(buffer.size()),
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (length == 0) {
      return base::unexpected(Unresolved::kUnreadable);
    }
    if (length < buffer.size()) {
      buffer.resize(length);
      break;
    }
    buffer.resize(length + 1);
  }
  std::wstring_view final_path = buffer;
  if (final_path.starts_with(kLongUncPrefix)) {
    return base::FilePath(L"\\\\" +
                          std::wstring(final_path.substr(kLongUncPrefix.size())));
  }
  if (final_path.starts_with(kLongPrefix)) {
    final_path.remove_prefix(kLongPrefix.size());
  }
  return base::FilePath(final_path);
}

bool IsLocal(const base::FilePath& path) {
  return HasDriveRoot(std::wstring_view(path.value()));
}

base::FilePath Canonical(const base::FilePath& path) {
  std::optional<base::FilePath> real = RealPath(path);
  return real && IsLocal(*real) ? *real : path;
}

std::optional<Absolute> AbsoluteParts(const base::FilePath& path) {
  std::wstring value = path.value();
  std::replace(value.begin(), value.end(), L'/', L'\\');
  if (value.size() >= 2 && value[1] == L':') {
    if (!HasDriveRoot(std::wstring_view(value))) {
      return std::nullopt;
    }
  } else if (value.empty() || value.front() == L'\\') {
    return std::nullopt;
  } else {
    base::FilePath cwd;
    if (!base::GetCurrentDirectory(&cwd) ||
        !HasDriveRoot(std::wstring_view(cwd.value()))) {
      return std::nullopt;
    }
    value = cwd.StripTrailingSeparators().value() + L"\\" + value;
  }
  Absolute absolute{base::FilePath(value.substr(0, 3)), {}};
  for (std::wstring& part :
       base::SplitString(std::wstring_view(value).substr(3), L"\\",
                         base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (part == L".") {
      continue;
    }
    if (part != L".." && !IsPlainComponent(std::wstring_view(part))) {
      return std::nullopt;
    }
    absolute.parts.push_back(std::move(part));
  }
  return absolute;
}

bool IsPlainName(std::string_view part) {
  return part != "." && part != ".." && IsPlainComponent(part);
}

}
