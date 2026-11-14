// Copyright 2026 Ardia. Apache-2.0.

#include "chrome/browser/ardia/ac_scripts_paths.h"

#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <utility>

#include "base/strings/string_split.h"

namespace ardia::scripts_paths {

std::optional<Kind> KindOf(const base::FilePath& path) {
  struct stat info;
  if (lstat(path.value().c_str(), &info) != 0) {
    return std::nullopt;
  }
  switch (info.st_mode & S_IFMT) {
    case S_IFREG:
      return Kind::kFile;
    case S_IFDIR:
      return Kind::kDirectory;
    default:
      return Kind::kOther;
  }
}

std::optional<base::FilePath> RealPath(const base::FilePath& path) {
  char resolved[PATH_MAX];
  if (!realpath(path.value().c_str(), resolved)) {
    return std::nullopt;
  }
  return base::FilePath(resolved);
}

bool IsLocal(const base::FilePath&) {
  return true;
}

std::optional<Absolute> AbsoluteParts(const base::FilePath& path) {
  std::string value = path.value();
  if (value == "~" || value.starts_with("~/")) {
    if (const char* home = getenv("HOME")) {
      value = std::string(home) + value.substr(1);
    }
  }
  if (value.empty() || value.front() != '/') {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
      return std::nullopt;
    }
    value = std::string(cwd) + "/" + value;
  }
  Absolute absolute{base::FilePath("/"), {}};
  for (std::string& part : base::SplitString(
           value, "/", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (part != ".") {
      absolute.parts.push_back(std::move(part));
    }
  }
  return absolute;
}

bool IsPlainName(std::string_view part) {
  return !part.empty() && part != "." && part != "..";
}

}
