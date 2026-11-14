#include "ardia/core/ac_launch_switches.h"

#include <Security/Security.h>

#include "base/apple/scoped_cftyperef.h"

namespace ardiacore {

bool SignedForDistribution() {
  static const bool distribution = [] {
    base::apple::ScopedCFTypeRef<SecCodeRef> self;
    if (SecCodeCopySelf(kSecCSDefaultFlags, self.InitializeInto()) !=
        errSecSuccess) {
      return true;
    }
    base::apple::ScopedCFTypeRef<SecRequirementRef> requirement;
    if (SecRequirementCreateWithString(
            CFSTR("anchor apple generic and "
                  "(certificate leaf[field.1.2.840.113635.100.6.1.9] exists or "
                  "certificate 1[field.1.2.840.113635.100.6.2.6] exists and "
                  "certificate leaf[field.1.2.840.113635.100.6.1.13] exists)"),
            kSecCSDefaultFlags,
            requirement.InitializeInto()) != errSecSuccess) {
      return true;
    }
    return SecCodeCheckValidity(self.get(), kSecCSDefaultFlags,
                                requirement.get()) == errSecSuccess;
  }();
  return distribution;
}

}
