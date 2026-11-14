// Copyright 2026 Ardia. Apache-2.0.

#ifndef CHROME_BROWSER_ARDIA_AC_SCRIPTS_PATHS_H_
#define CHROME_BROWSER_ARDIA_AC_SCRIPTS_PATHS_H_

#include <optional>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"

namespace ardia::scripts_paths {

enum class Kind { kFile, kDirectory, kOther };

std::optional<Kind> KindOf(const base::FilePath& path);

std::optional<base::FilePath> RealPath(const base::FilePath& path);

bool IsLocal(const base::FilePath& real);

struct Absolute {
  base::FilePath root;
  std::vector<base::FilePath::StringType> parts;
};

std::optional<Absolute> AbsoluteParts(const base::FilePath& path);

bool IsPlainName(std::string_view part);

}

#endif
