// Copyright 2026 Ardia. Apache-2.0.

#ifndef CHROME_BROWSER_ARDIA_AC_SCRIPTS_PATHS_H_
#define CHROME_BROWSER_ARDIA_AC_SCRIPTS_PATHS_H_

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/types/expected.h"

namespace ardia::scripts_paths {

enum class Kind { kFile, kDirectory, kOther };

std::optional<Kind> KindOf(const base::FilePath& path);

enum class Unresolved { kMissing, kUnreadable };

base::expected<base::FilePath, Unresolved> ResolvePath(
    const base::FilePath& path);

inline std::optional<base::FilePath> RealPath(const base::FilePath& path) {
  base::expected<base::FilePath, Unresolved> resolved = ResolvePath(path);
  if (!resolved.has_value()) {
    return std::nullopt;
  }
  return std::move(resolved).value();
}

bool IsLocal(const base::FilePath& path);

base::FilePath Canonical(const base::FilePath& path);

struct Absolute {
  base::FilePath root;
  std::vector<base::FilePath::StringType> parts;
};

std::optional<Absolute> AbsoluteParts(const base::FilePath& path);

bool IsPlainName(std::string_view part);

}

#endif
