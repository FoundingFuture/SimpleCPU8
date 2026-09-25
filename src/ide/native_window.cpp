// Every system but macOS: no full screen of the window's own. See
// native_window.mm for the macOS one.
#include "ide/native_window.h"

namespace sc8::native {

bool isFullscreen(void*) { return false; }

void toggleFullscreen(void*) {}

}  // namespace sc8::native
