#include "vm/keys.h"

#include <vector>

#include "raylib.h"

namespace sc8 {

namespace {

// The specials from main.ts's KEYCODES table.
constexpr int CODE_ENTER = 13;
constexpr int CODE_ESCAPE = 27;
constexpr int CODE_BACKSPACE = 8;
constexpr int CODE_TAB = 9;
constexpr int CODE_UP = 17;
constexpr int CODE_DOWN = 18;
constexpr int CODE_LEFT = 19;
constexpr int CODE_RIGHT = 20;

// A raylib printable key is its unshifted US character. The shifted form
// for a symbol key comes from this table, the browser's job before.
int shifted(int c) {
  switch (c) {
    case '`': return '~';
    case '1': return '!';
    case '2': return '@';
    case '3': return '#';
    case '4': return '$';
    case '5': return '%';
    case '6': return '^';
    case '7': return '&';
    case '8': return '*';
    case '9': return '(';
    case '0': return ')';
    case '-': return '_';
    case '=': return '+';
    case '[': return '{';
    case ']': return '}';
    case '\\': return '|';
    case ';': return ':';
    case '\'': return '"';
    case ',': return '<';
    case '.': return '>';
    case '/': return '?';
    default: return c;
  }
}

// The keypad keys, which the browser reported as their characters.
int keypadChar(int key) {
  if (key >= KEY_KP_0 && key <= KEY_KP_9) return '0' + (key - KEY_KP_0);
  switch (key) {
    case KEY_KP_DECIMAL: return '.';
    case KEY_KP_DIVIDE: return '/';
    case KEY_KP_MULTIPLY: return '*';
    case KEY_KP_SUBTRACT: return '-';
    case KEY_KP_ADD: return '+';
    case KEY_KP_EQUAL: return '=';
    default: return -1;
  }
}

// Every key that sends an event or drives a button. The poll scans this
// short list rather than every raylib code.
const std::vector<int>& trackedKeys() {
  static const std::vector<int> keys = [] {
    std::vector<int> k;
    k.push_back(KEY_SPACE);
    k.push_back(KEY_APOSTROPHE);
    for (int c = KEY_COMMA; c <= KEY_NINE; c++) k.push_back(c);
    k.push_back(KEY_SEMICOLON);
    k.push_back(KEY_EQUAL);
    for (int c = KEY_A; c <= KEY_Z; c++) k.push_back(c);
    for (int c = KEY_LEFT_BRACKET; c <= KEY_RIGHT_BRACKET; c++) k.push_back(c);
    k.push_back(KEY_GRAVE);
    for (int c = KEY_ESCAPE; c <= KEY_BACKSPACE; c++) k.push_back(c);
    for (int c = KEY_RIGHT; c <= KEY_UP; c++) k.push_back(c);
    for (int c = KEY_KP_0; c <= KEY_KP_EQUAL; c++) k.push_back(c);
    k.push_back(KEY_LEFT_CONTROL);
    k.push_back(KEY_RIGHT_CONTROL);
    return k;
  }();
  return keys;
}

}  // namespace

int keyCodeFor(int key, bool shift, bool ctrl) {
  switch (key) {
    case KEY_ENTER:
    case KEY_KP_ENTER: return CODE_ENTER;
    case KEY_ESCAPE: return CODE_ESCAPE;
    case KEY_BACKSPACE: return CODE_BACKSPACE;
    case KEY_TAB: return CODE_TAB;
    case KEY_UP: return CODE_UP;
    case KEY_DOWN: return CODE_DOWN;
    case KEY_LEFT: return CODE_LEFT;
    case KEY_RIGHT: return CODE_RIGHT;
    default: break;
  }
  int c = keypadChar(key);
  if (c < 0) {
    // raylib's printable codes are the ASCII of the unshifted US key.
    // Letters are already uppercase, as the browser's toUpperCase gave.
    if (key < KEY_SPACE || key > KEY_GRAVE) return -1;
    c = shift ? shifted(key) : key;
  }
  c &= 0x7f;
  // Ctrl and a letter becomes a control code, the way every terminal has
  // delivered it. It has to be in the code because a key event is
  // buffered and a modifier is a level.
  if (ctrl && c >= 'A' && c <= 'Z') return c - 64;
  return c;
}

uint8_t buttonBitFor(int key) {
  switch (key) {
    case KEY_UP:
    case KEY_W: return 0x01;
    case KEY_DOWN:
    case KEY_S: return 0x02;
    case KEY_LEFT:
    case KEY_A: return 0x04;
    case KEY_RIGHT:
    case KEY_D: return 0x08;
    case KEY_Z:
    case KEY_LEFT_CONTROL:
    case KEY_RIGHT_CONTROL: return 0x10;
    case KEY_SPACE: return 0x20;
    case KEY_ENTER:
    case KEY_KP_ENTER: return 0x40;
    default: return 0;
  }
}

uint8_t modsHeld() {
  uint8_t m = 0;
  if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) m |= 0x01;
  if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) m |= 0x02;
  if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) m |= 0x04;
  if (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)) m |= 0x08;
  return m;
}

void Keyboard::poll(InputBus& in) {
  // A lost focus can swallow the release. Release everything, so no button
  // or key stays stuck down, and read nothing until focus returns.
  if (!IsWindowFocused()) {
    releaseAll(in);
    return;
  }
  const uint8_t mods = modsHeld();
  if (mods != mods_) in.mods = mods_ = mods;
  const bool shift = (mods & 0x01) != 0;
  const bool ctrl = (mods & 0x02) != 0;
  for (int key : trackedKeys()) {
    // IsKeyPressed fires once per physical press, so auto-repeat sends one
    // press event, the browser's downKeys rule.
    if (IsKeyPressed(key)) {
      // The button first, so a key with no code (Control) still fires.
      buttons_ = static_cast<uint8_t>(buttons_ | buttonBitFor(key));
      in.buttons = buttons_;
      const int code = keyCodeFor(key, shift, ctrl);
      if (code >= 0 && !down_.count(key)) {
        down_[key] = code;
        in.pushKey(static_cast<uint8_t>(code), false);
      }
    }
    if (IsKeyReleased(key)) {
      buttons_ = static_cast<uint8_t>(buttons_ & ~buttonBitFor(key));
      in.buttons = buttons_;
      auto it = down_.find(key);
      if (it != down_.end()) {
        in.pushKey(static_cast<uint8_t>(it->second), true);
        down_.erase(it);
      }
    }
  }
}

void Keyboard::releaseAll(InputBus& in) {
  for (const auto& [key, code] : down_) in.pushKey(static_cast<uint8_t>(code), true);
  down_.clear();
  if (buttons_ != 0 || mods_ != 0) {
    buttons_ = 0;
    mods_ = 0;
    in.buttons = 0;
    in.mods = 0;
  }
}

void pollKeyboard(InputBus& in) {
  static Keyboard keyboard;
  keyboard.poll(in);
}

}  // namespace sc8
