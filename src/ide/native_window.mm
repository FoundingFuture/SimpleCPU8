#include "ide/native_window.h"

#import <Cocoa/Cocoa.h>

namespace sc8::native {

bool isFullscreen(void* windowHandle) {
  NSWindow* w = static_cast<NSWindow*>(windowHandle);
  return w != nil && ([w styleMask] & NSWindowStyleMaskFullScreen) != 0;
}

void toggleFullscreen(void* windowHandle) {
  NSWindow* w = static_cast<NSWindow*>(windowHandle);
  if (w != nil) [w toggleFullScreen:nil];
}

}  // namespace sc8::native
