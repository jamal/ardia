#include "ardia/core/ac_host_visibility.h"

namespace ardiacore {

bool TabWasInShownSlot(content::WebContents*) {
  return false;
}

bool IsAutomationWindow(const BrowserWindowInterface*) {
  return false;
}

bool InAutomationWindow(content::WebContents*) {
  return false;
}

}
