#include "ardia/core/ac_launch_switches.h"

#include <windows.h>

#include <softpub.h>
#include <wintrust.h>

#include "base/files/file_path.h"
#include "base/path_service.h"

namespace ardiacore {

namespace {

bool UnsignedAnswer(LONG status) {
  return status == TRUST_E_NOSIGNATURE || status == TRUST_E_SUBJECT_FORM_UNKNOWN ||
         status == TRUST_E_PROVIDER_UNKNOWN;
}

}

bool SignedForDistribution() {
  static const bool distribution = [] {
    base::FilePath exe;
    if (!base::PathService::Get(base::FILE_EXE, &exe)) {
      return true;
    }
    WINTRUST_FILE_INFO file = {};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = exe.value().c_str();
    WINTRUST_DATA data = {};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = ::WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE),
                                         &action, &data);
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    ::WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return !UnsignedAnswer(status);
  }();
  return distribution;
}

}
