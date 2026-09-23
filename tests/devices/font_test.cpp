#include <doctest.h>

#include <string>
#include <string_view>
#include <vector>

#include "devices/font.h"
#include "devices/gpu.h"
#include "gpu_rig.h"

using namespace sc8;
using namespace sc8::gpu;

// The narrow 6 by 8 font, and the cell it brings with it. Five columns of
// face and a one pixel gap, so 42 columns across the screen where the 8 by 8
// font gives 32.

namespace {

std::vector<std::string_view> rowsOf(std::string_view glyph) {
  std::vector<std::string_view> rows;
  size_t start = 0;
  for (;;) {
    const size_t slash = glyph.find('/', start);
    if (slash == std::string_view::npos) {
      rows.push_back(glyph.substr(start));
      return rows;
    }
    rows.push_back(glyph.substr(start, slash - start));
    start = slash + 1;
  }
}

size_t at(char ch) { return static_cast<size_t>((ch - FONT_FIRST) * FONT_H); }

}  // namespace

TEST_SUITE("the art") {
  TEST_CASE("has a glyph for every printable code") {
    CHECK_EQ(std::size(FONT_ART), static_cast<size_t>(FONT_LAST - FONT_FIRST + 1));
  }

  TEST_CASE("gives every glyph eight rows of six") {
    for (std::string_view g : FONT_ART) {
      const auto rows = rowsOf(g);
      CHECK_EQ(rows.size(), static_cast<size_t>(FONT_H));
      for (std::string_view r : rows) CHECK_EQ(r.size(), static_cast<size_t>(FONT_W));
    }
  }

  TEST_CASE("keeps the rightmost column clear, so letters never touch") {
    for (size_t i = 0; i < std::size(FONT_ART); i++) {
      // The solid block is the one exception. A cursor has to fill its whole
      // cell, or it leaves a stripe of background down its right hand side.
      if (i + FONT_FIRST == 0x7f) continue;
      for (std::string_view r : rowsOf(FONT_ART[i])) CHECK_EQ(r[5], '.');
    }
  }

  TEST_CASE("fills the whole cell for the block, gap column and all") {
    const size_t block = static_cast<size_t>((0x7f - FONT_FIRST) * FONT_H);
    for (size_t r = 0; r < 8; r++) CHECK_EQ(FONT[block + r], 0b111111);
  }

  TEST_CASE("uses only the two characters the art is made of") {
    for (std::string_view g : FONT_ART) {
      for (char c : g) CHECK((c == '.' || c == 'X' || c == '/'));
    }
  }

  TEST_CASE("packs to one byte a row, bit 0 on the left") {
    CHECK_EQ(FONT.size(), std::size(FONT_ART) * FONT_H);
    // "X....." is bit 0 alone.
    CHECK_EQ(FONT[at('L') + 0], 0b00001);  // the stem, leftmost column
    CHECK_EQ(FONT[at('L') + 6], 0b11111);  // the foot, five wide
  }

  TEST_CASE("leaves space blank and the underscore on the bottom row") {
    for (size_t r = 0; r < 8; r++) CHECK_EQ(FONT[at(' ') + r], 0);
    CHECK_EQ(FONT[at('_') + 7], 0b11111);
  }

  TEST_CASE("gives the descenders their eighth row, and nothing else one") {
    for (char ch : std::string("gjpqy,;_")) CHECK_GT(FONT[at(ch) + 7], 0);
    for (char ch : std::string("ABCXYZaeo0189")) CHECK_EQ(FONT[at(ch) + 7], 0);
  }

  TEST_CASE("draws every letter and digit as something, not a blank") {
    for (char ch : std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")) {
      bool any = false;
      for (size_t r = 0; r < 8; r++) {
        if (FONT[at(ch) + r] != 0) any = true;
      }
      CHECK_MESSAGE(any, std::string(1, ch), " is blank");
    }
  }
}

TEST_SUITE("what the cell puts on the screen") {
  TEST_CASE("gives 42 columns across the screen") {
    CHECK_EQ(TEXT_COLS, SCREEN_W / FONT_W);
    CHECK_EQ(TEXT_COLS, 42);
  }

  TEST_CASE("draws the second character six pixels along, not eight") {
    gpu_rig::Clock c;
    Gpu& g = c.g;
    auto ram = gpu_rig::newRam();
    g.attachRam(ram->data());
    g.powerOn();
    g.write(GPU_DATA0, 0xf0);
    g.write(GPU_DATA1, 0x00);
    g.write(GPU_CMD, CMD_SET_TEXTMODE);
    // Two full blocks, so the lit columns are unmistakable.
    (*ram)[0xf000] = 0x7c;  // |
    (*ram)[0xf001] = 0x7c;
    const auto px = g.composeFrame();
    auto lit = [&](int x) {
      for (int y = 0; y < 8; y++) {
        if (gpu_rig::px(px, x, y) != 0) return true;
      }
      return false;
    };
    // The | glyph is a stem at column 2 of its cell.
    CHECK(lit(2));
    CHECK(lit(8));
    CHECK_FALSE(lit(10));  // where an 8 wide cell would put it
  }
}
