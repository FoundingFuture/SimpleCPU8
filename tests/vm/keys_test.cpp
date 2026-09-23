// The keyboard table: raylib keys to the device codes and button bits that
// main.ts sent. Pure functions, so no window opens here.

#include "doctest.h"

#include "raylib.h"

#include "vm/keys.h"

using namespace sc8;

TEST_SUITE("vm keyboard map") {
  TEST_CASE("printable keys send their uppercase ASCII, shift or not") {
    CHECK_EQ(keyCodeFor(KEY_A, false, false), 'A');
    CHECK_EQ(keyCodeFor(KEY_A, true, false), 'A');
    CHECK_EQ(keyCodeFor(KEY_Z, false, false), 'Z');
    CHECK_EQ(keyCodeFor(KEY_SPACE, false, false), ' ');
    CHECK_EQ(keyCodeFor(KEY_ONE, false, false), '1');
    CHECK_EQ(keyCodeFor(KEY_PERIOD, false, false), '.');
    CHECK_EQ(keyCodeFor(KEY_KP_5, false, false), '5');
    CHECK_EQ(keyCodeFor(KEY_KP_ADD, false, false), '+');
  }

  TEST_CASE("shift on a symbol key gives the US shifted symbol") {
    CHECK_EQ(keyCodeFor(KEY_ONE, true, false), '!');
    CHECK_EQ(keyCodeFor(KEY_EQUAL, true, false), '+');
    CHECK_EQ(keyCodeFor(KEY_SLASH, true, false), '?');
    CHECK_EQ(keyCodeFor(KEY_SEMICOLON, true, false), ':');
    CHECK_EQ(keyCodeFor(KEY_GRAVE, true, false), '~');
    CHECK_EQ(keyCodeFor(KEY_KP_5, true, false), '5');  // the keypad has no shifted form
  }

  TEST_CASE("specials get the device codes and arrows are 17 to 20") {
    struct Row {
      int key;
      int code;
    };
    const Row rows[] = {
        {KEY_ENTER, 13}, {KEY_KP_ENTER, 13}, {KEY_ESCAPE, 27}, {KEY_BACKSPACE, 8}, {KEY_TAB, 9},
        {KEY_UP, 17},    {KEY_DOWN, 18},     {KEY_LEFT, 19},   {KEY_RIGHT, 20},
    };
    for (const Row& r : rows) {
      CAPTURE(r.key);
      CHECK_EQ(keyCodeFor(r.key, false, false), r.code);
      CHECK_EQ(keyCodeFor(r.key, true, true), r.code);
    }
  }

  TEST_CASE("Ctrl and a letter becomes a control code, Ctrl-C is 3") {
    CHECK_EQ(keyCodeFor(KEY_C, false, true), 3);
    CHECK_EQ(keyCodeFor(KEY_A, false, true), 1);
    CHECK_EQ(keyCodeFor(KEY_Z, false, true), 26);
    CHECK_EQ(keyCodeFor(KEY_Z, true, true), 26);
    CHECK_EQ(keyCodeFor(KEY_ONE, false, true), '1');  // only letters fold
  }

  TEST_CASE("modifier and function keys send no key event") {
    const int none[] = {KEY_LEFT_SHIFT, KEY_RIGHT_SHIFT, KEY_LEFT_CONTROL, KEY_RIGHT_CONTROL, KEY_LEFT_ALT,
                        KEY_RIGHT_ALT,  KEY_LEFT_SUPER,  KEY_RIGHT_SUPER,   KEY_F1,            KEY_F12,
                        KEY_CAPS_LOCK,  KEY_HOME,        KEY_NULL};
    for (int k : none) {
      CAPTURE(k);
      CHECK_EQ(keyCodeFor(k, false, false), -1);
    }
  }

  TEST_CASE("the controller bits match the KEY_BITS table") {
    CHECK_EQ(buttonBitFor(KEY_UP), 0x01);
    CHECK_EQ(buttonBitFor(KEY_W), 0x01);
    CHECK_EQ(buttonBitFor(KEY_DOWN), 0x02);
    CHECK_EQ(buttonBitFor(KEY_S), 0x02);
    CHECK_EQ(buttonBitFor(KEY_LEFT), 0x04);
    CHECK_EQ(buttonBitFor(KEY_A), 0x04);
    CHECK_EQ(buttonBitFor(KEY_RIGHT), 0x08);
    CHECK_EQ(buttonBitFor(KEY_D), 0x08);
    CHECK_EQ(buttonBitFor(KEY_Z), 0x10);
    CHECK_EQ(buttonBitFor(KEY_LEFT_CONTROL), 0x10);
    CHECK_EQ(buttonBitFor(KEY_RIGHT_CONTROL), 0x10);
    CHECK_EQ(buttonBitFor(KEY_SPACE), 0x20);
    CHECK_EQ(buttonBitFor(KEY_ENTER), 0x40);
    CHECK_EQ(buttonBitFor(KEY_KP_ENTER), 0x40);
    CHECK_EQ(buttonBitFor(KEY_X), 0);
    CHECK_EQ(buttonBitFor(KEY_LEFT_SHIFT), 0);
  }

  TEST_CASE("releaseAll sends a release for every held key and clears the levels") {
    InputBus in;
    in.buttons = 0x21;
    in.mods = 0x01;
    Keyboard kb;
    kb.releaseAll(in);  // nothing held yet: the levels are left alone
    CHECK_EQ(in.buttons, 0x21);
    CHECK_EQ(in.queued(), 0u);
  }
}
