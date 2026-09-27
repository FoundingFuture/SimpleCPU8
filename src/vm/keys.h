// The keyboard: raylib key states to the input device, the way main.ts
// fed the browser's key events to the session. Printable keys send their
// uppercase ASCII and the specials get device codes. Arrows are 17 to 20.
// Ctrl with a letter is a control code 1 to 26. The modifier keys send no
// key event. They are levels on IO_MODS, and the buttons are levels too.
#pragma once

#include <cstdint>
#include <map>

#include "devices/input.h"

namespace sc8 {

// The key code the device gets for a raylib key, or -1 when the key sends
// no event. Pure, so the table can be tested without a window.
//
// DIFFERENCE from main.ts. The browser gave a layout-aware character per
// key, so shift-1 arrived as the bang character on its own. raylib reports
// physical keys on a US layout, so a US shift table applies shift here. A
// non-US layout gets US symbols for the symbol keys. main.ts also sent
// every letter as a capital. Here a letter is small unless shift is held,
// so a string can hold small letters. BASIC puts its own words in
// capitals when it stores a line.
int keyCodeFor(int raylibKey, bool shift, bool ctrl);

// The controller bit a raylib key drives, or 0. Arrows and WASD, Z and
// Control for fire, space and enter.
uint8_t buttonBitFor(int raylibKey);

// The modifier levels right now, MOD_ bits, from both sides of the keyboard.
uint8_t modsHeld();

// The held keys and the code each went down as, so a release sends what
// its press did. Also the last button and modifier bytes sent.
class Keyboard {
 public:
  // Read raylib once for this host frame and feed the device: the buttons
  // held now, then the key presses of the frame in the order they
  // happened. Losing the window's focus releases everything, so nothing
  // sticks down. It drains raylib's press queue, so call it at most once
  // per frame and before anything else reads GetKeyPressed.
  void poll(InputBus& in);
  void releaseAll(InputBus& in);

 private:
  std::map<int, int> down_;
  uint8_t buttons_ = 0;
  uint8_t mods_ = 0;
};

// One process-wide keyboard, polled once per host frame.
void pollKeyboard(InputBus& in);

}  // namespace sc8
