// Copyright 2026 Ardia. Apache-2.0.

#ifndef CHROME_BROWSER_ARDIA_AC_CLI_CONTROL_FILE_H_
#define CHROME_BROWSER_ARDIA_AC_CLI_CONTROL_FILE_H_

#include <string>

#include "base/functional/function_ref.h"

namespace base {
class FilePath;
class Lock;
}

namespace ardia::cli_control_internal {

std::string WriteOwnerOnlyFile(const base::FilePath& path,
                               const std::string& contents,
                               base::Lock& publish_lock,
                               base::FunctionRef<bool()> still_wanted);

void DeleteOwnerOnlyFile(const base::FilePath& path);

}

#endif
