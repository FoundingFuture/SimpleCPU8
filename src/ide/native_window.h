// The window's own full screen on macOS: the green button, a Space of its
// own. GLFW and raylib do not report it, so IsWindowFullscreen() says
// false while the window fills the screen. These ask Cocoa directly.
// Elsewhere there is no such mode, and both do nothing.
#pragma once

namespace sc8::native {

// The window is in the system's own full screen.
bool isFullscreen(void* windowHandle);
// Enter or leave it, as the green button does. The change animates and
// lands a moment later.
void toggleFullscreen(void* windowHandle);

}  // namespace sc8::native
