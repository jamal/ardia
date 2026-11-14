// Copyright 2026 Ardia. Apache-2.0.

#include "chrome/browser/ardia/ac_cli_control_file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/synchronization/lock.h"

namespace ardia::cli_control_internal {
namespace {

std::string ErrnoText() {
  return base::ToLowerASCII(strerror(errno));
}

}

std::string WriteOwnerOnlyFile(const base::FilePath& path,
                               const std::string& contents,
                               base::Lock& publish_lock,
                               base::FunctionRef<bool()> still_wanted) {
  const base::FilePath temp = path.DirName().Append(
      "." + path.BaseName().value() + "." +
      base::HexEncode(base::RandBytesAsVector(8)));
  const int fd = HANDLE_EINTR(
      open(temp.value().c_str(),
           O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
  if (fd < 0) {
    return base::StrCat({"Couldn't write ", path.value(), ": ", ErrnoText()});
  }
  bool written = base::WriteFileDescriptor(fd, contents) && fsync(fd) == 0;
  std::string error = written ? std::string() : ErrnoText();
  if (IGNORE_EINTR(close(fd)) != 0 && written) {
    written = false;
    error = ErrnoText();
  }
  if (!written) {
    unlink(temp.value().c_str());
    return base::StrCat({"Couldn't write ", path.value(), ": ", error});
  }
  {
    base::AutoLock hold(publish_lock);
    if (!still_wanted()) {
      unlink(temp.value().c_str());
      return "The control server stopped.";
    }
    if (rename(temp.value().c_str(), path.value().c_str()) != 0) {
      error = ErrnoText();
      unlink(temp.value().c_str());
      return base::StrCat({"Couldn't write ", path.value(), ": ", error});
    }
  }
  return std::string();
}

void DeleteOwnerOnlyFile(const base::FilePath& path) {
  if (!path.empty() && unlink(path.value().c_str()) != 0 && errno != ENOENT &&
      errno != EISDIR && errno != EPERM) {
    PLOG(ERROR) << "[ardia] can't delete " << path;
  }
}

}
