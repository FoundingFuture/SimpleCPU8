// The text font: 6 by 8. Five columns of face with a one pixel gap on the
// right, so letters never touch, in a cell six wide. That gives 42 columns
// across the 256 pixel screen.
//
// It replaced an 8 by 8 font that gave 32. Ten more characters a line is
// worth more on a 256 pixel screen than the extra weight was.
//
// The shape follows the one an Oric-1 or an Oric Atmos put on a television.
// That is a narrow upright face with flat terminals, in a 6 by 8 cell.
//
// It is DRAWN here rather than transcribed. The Oric font is published as a
// ROM listing in the Advanced User Guide and is free to use. The bytes are
// not to hand, though, and a font recalled from memory and labelled Oric
// would be neither drawn nor transcribed. Swapping in the real table is a
// change to the data below and to nothing else. One byte a row, bit 0 the
// left pixel.
//
// The zero keeps a slash the Oric's has not got. At five pixels wide a plain
// zero and a capital O are the same glyph, and this is a tool for reading
// code.
//
// Written as art on purpose. A font you cannot read is a font nobody will
// fix. The one thing a bitmap font needs is for somebody to look at it.
// An X is a pixel, a dot is not. The rightmost column is the gap, and a
// test refuses a glyph that uses it. The solid block at 0x7F is the one
// exception. A cursor has to fill its whole cell, or it leaves a stripe of
// background down its right hand side.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace sc8 {

constexpr int FONT_W = 6;
constexpr int FONT_H = 8;
constexpr int FONT_FIRST = 0x20;
constexpr int FONT_LAST = 0x7f;
constexpr int FONT_GLYPHS = FONT_LAST - FONT_FIRST + 1;

// clang-format off
constexpr std::string_view FONT_ART[FONT_GLYPHS] = {
    "....../....../....../....../....../....../....../......",  // 0x20 space
    "..X.../..X.../..X.../..X.../..X.../....../..X.../......",  // 0x21 exclamation mark
    ".X.X../.X.X../....../....../....../....../....../......",  // 0x22 "
    ".X.X../.X.X../XXXXX./.X.X../XXXXX./.X.X../.X.X../......",  // 0x23 #
    "..X.../.XXXX./X.X.../.XXX../..X.X./XXXX../..X.../......",  // 0x24 $
    "XX..X./XX.X../...X../..X.../.X.X../X..XX./...XX./......",  // 0x25 %
    ".XX.../X..X../X.X.../.X..../X.X.X./X..X../.XX.X./......",  // 0x26 &
    "..X.../..X.../....../....../....../....../....../......",  // 0x27 '
    "...X../..X.../.X..../.X..../.X..../..X.../...X../......",  // 0x28 (
    ".X..../..X.../...X../...X../...X../..X.../.X..../......",  // 0x29 )
    "....../X.X.X./.XXX../XXXXX./.XXX../X.X.X./....../......",  // 0x2a *
    "....../..X.../..X.../XXXXX./..X.../..X.../....../......",  // 0x2b +
    "....../....../....../....../....../..X.../..X.../.X....",  // 0x2c ,
    "....../....../....../XXXXX./....../....../....../......",  // 0x2d -
    "....../....../....../....../....../....../..X.../......",  // 0x2e .
    "....X./....X./...X../..X.../.X..../X...../X...../......",  // 0x2f /
    ".XXX../X...X./X..XX./X.X.X./XX..X./X...X./.XXX../......",  // 0x30 0
    "..X.../.XX.../..X.../..X.../..X.../..X.../.XXX../......",  // 0x31 1
    ".XXX../X...X./....X./...X../..X.../.X..../XXXXX./......",  // 0x32 2
    "XXXXX./...X../..XX../....X./....X./X...X./.XXX../......",  // 0x33 3
    "...XX./..X.X./.X..X./X...X./XXXXX./....X./....X./......",  // 0x34 4
    "XXXXX./X...../XXXX../....X./....X./X...X./.XXX../......",  // 0x35 5
    "..XX../.X..../X...../XXXX../X...X./X...X./.XXX../......",  // 0x36 6
    "XXXXX./....X./...X../..X.../.X..../.X..../.X..../......",  // 0x37 7
    ".XXX../X...X./X...X./.XXX../X...X./X...X./.XXX../......",  // 0x38 8
    ".XXX../X...X./X...X./.XXXX./....X./...X../.XX.../......",  // 0x39 9
    "....../....../..X.../....../....../..X.../....../......",  // 0x3a :
    "....../....../..X.../....../....../..X.../..X.../.X....",  // 0x3b ;
    "...X../..X.../.X..../X...../.X..../..X.../...X../......",  // 0x3c <
    "....../....../XXXXX./....../XXXXX./....../....../......",  // 0x3d =
    ".X..../..X.../...X../....X./...X../..X.../.X..../......",  // 0x3e >
    ".XXX../X...X./....X./...X../..X.../....../..X.../......",  // 0x3f ?
    ".XXX../X...X./X.XXX./X.X.X./X.XXX./X...../.XXXX./......",  // 0x40 @
    ".XXX../X...X./X...X./XXXXX./X...X./X...X./X...X./......",  // 0x41 A
    "XXXX../X...X./X...X./XXXX../X...X./X...X./XXXX../......",  // 0x42 B
    ".XXX../X...X./X...../X...../X...../X...X./.XXX../......",  // 0x43 C
    "XXXX../X...X./X...X./X...X./X...X./X...X./XXXX../......",  // 0x44 D
    "XXXXX./X...../X...../XXXX../X...../X...../XXXXX./......",  // 0x45 E
    "XXXXX./X...../X...../XXXX../X...../X...../X...../......",  // 0x46 F
    ".XXX../X...X./X...../X..XX./X...X./X...X./.XXXX./......",  // 0x47 G
    "X...X./X...X./X...X./XXXXX./X...X./X...X./X...X./......",  // 0x48 H
    ".XXX../..X.../..X.../..X.../..X.../..X.../.XXX../......",  // 0x49 I
    "...XX./....X./....X./....X./X...X./X...X./.XXX../......",  // 0x4a J
    "X...X./X..X../X.X.../XX..../X.X.../X..X../X...X./......",  // 0x4b K
    "X...../X...../X...../X...../X...../X...../XXXXX./......",  // 0x4c L
    "X...X./XX.XX./X.X.X./X.X.X./X...X./X...X./X...X./......",  // 0x4d M
    "X...X./XX..X./X.X.X./X..XX./X...X./X...X./X...X./......",  // 0x4e N
    ".XXX../X...X./X...X./X...X./X...X./X...X./.XXX../......",  // 0x4f O
    "XXXX../X...X./X...X./XXXX../X...../X...../X...../......",  // 0x50 P
    ".XXX../X...X./X...X./X...X./X.X.X./X..X../.XX.X./......",  // 0x51 Q
    "XXXX../X...X./X...X./XXXX../X.X.../X..X../X...X./......",  // 0x52 R
    ".XXXX./X...../X...../.XXX../....X./....X./XXXX../......",  // 0x53 S
    "XXXXX./..X.../..X.../..X.../..X.../..X.../..X.../......",  // 0x54 T
    "X...X./X...X./X...X./X...X./X...X./X...X./.XXX../......",  // 0x55 U
    "X...X./X...X./X...X./X...X./X...X./.X.X../..X.../......",  // 0x56 V
    "X...X./X...X./X...X./X.X.X./X.X.X./XX.XX./X...X./......",  // 0x57 W
    "X...X./X...X./.X.X../..X.../.X.X../X...X./X...X./......",  // 0x58 X
    "X...X./X...X./.X.X../..X.../..X.../..X.../..X.../......",  // 0x59 Y
    "XXXXX./....X./...X../..X.../.X..../X...../XXXXX./......",  // 0x5a Z
    ".XXX../.X..../.X..../.X..../.X..../.X..../.XXX../......",  // 0x5b [
    "X...../X...../.X..../..X.../...X../....X./....X./......",  // 0x5c backslash
    ".XXX../...X../...X../...X../...X../...X../.XXX../......",  // 0x5d ]
    "..X.../.X.X../X...X./....../....../....../....../......",  // 0x5e ^
    "....../....../....../....../....../....../....../XXXXX.",  // 0x5f _
    ".X..../..X.../....../....../....../....../....../......",  // 0x60 `
    "....../....../.XXX../....X./.XXXX./X...X./.XXXX./......",  // 0x61 a
    "X...../X...../XXXX../X...X./X...X./X...X./XXXX../......",  // 0x62 b
    "....../....../.XXXX./X...../X...../X...../.XXXX./......",  // 0x63 c
    "....X./....X./.XXXX./X...X./X...X./X...X./.XXXX./......",  // 0x64 d
    "....../....../.XXX../X...X./XXXXX./X...../.XXXX./......",  // 0x65 e
    "..XX../.X..X./.X..../XXX.../.X..../.X..../.X..../......",  // 0x66 f
    "....../....../.XXXX./X...X./X...X./.XXXX./....X./.XXX..",  // 0x67 g
    "X...../X...../XXXX../X...X./X...X./X...X./X...X./......",  // 0x68 h
    "..X.../....../.XX.../..X.../..X.../..X.../.XXX../......",  // 0x69 i
    "...X../....../..XX../...X../...X../...X../X..X../.XX...",  // 0x6a j
    "X...../X...../X..X../X.X.../XX..../X.X.../X..X../......",  // 0x6b k
    ".XX.../..X.../..X.../..X.../..X.../..X.../.XXX../......",  // 0x6c l
    "....../....../XX.X../X.X.X./X.X.X./X...X./X...X./......",  // 0x6d m
    "....../....../XXXX../X...X./X...X./X...X./X...X./......",  // 0x6e n
    "....../....../.XXX../X...X./X...X./X...X./.XXX../......",  // 0x6f o
    "....../....../XXXX../X...X./X...X./XXXX../X...../X.....",  // 0x70 p
    "....../....../.XXXX./X...X./X...X./.XXXX./....X./....X.",  // 0x71 q
    "....../....../X.XXX./XX..../X...../X...../X...../......",  // 0x72 r
    "....../....../.XXXX./X...../.XXX../....X./XXXX../......",  // 0x73 s
    ".X..../.X..../XXX.../.X..../.X..../.X..X./..XX../......",  // 0x74 t
    "....../....../X...X./X...X./X...X./X...X./.XXXX./......",  // 0x75 u
    "....../....../X...X./X...X./X...X./.X.X../..X.../......",  // 0x76 v
    "....../....../X...X./X...X./X.X.X./X.X.X./.X.X../......",  // 0x77 w
    "....../....../X...X./.X.X../..X.../.X.X../X...X./......",  // 0x78 x
    "....../....../X...X./X...X./X...X./.XXXX./....X./.XXX..",  // 0x79 y
    "....../....../XXXXX./...X../..X.../.X..../XXXXX./......",  // 0x7a z
    "...XX./..X.../..X.../.X..../..X.../..X.../...XX./......",  // 0x7b {
    "..X.../..X.../..X.../..X.../..X.../..X.../..X.../......",  // 0x7c |
    ".XX.../...X../...X../....X./...X../...X../.XX.../......",  // 0x7d }
    "....../....../.X..X./X.XX../....../....../....../......",  // 0x7e ~
    "XXXXXX/XXXXXX/XXXXXX/XXXXXX/XXXXXX/XXXXXX/XXXXXX/XXXXXX",  // 0x7f the block, a cursor
};
// clang-format on

// Art to bytes. One byte per row, bit 0 the left pixel, which is the layout
// the blitter reads. The high two bits of a row go unread at this cell width.
constexpr std::array<uint8_t, FONT_GLYPHS * FONT_H> packFont() {
  std::array<uint8_t, FONT_GLYPHS * FONT_H> out{};
  size_t n = 0;
  for (std::string_view glyph : FONT_ART) {
    unsigned bits = 0;
    int c = 0;
    for (char ch : glyph) {
      if (ch == '/') {
        out[n++] = static_cast<uint8_t>(bits);
        bits = 0;
        c = 0;
        continue;
      }
      if (ch == 'X') bits |= 1u << c;
      c++;
    }
    out[n++] = static_cast<uint8_t>(bits);
  }
  return out;
}

constexpr std::array<uint8_t, FONT_GLYPHS * FONT_H> FONT = packFont();

// One row of a glyph, as the byte the blitter reads. Codes outside the
// printable range are blank.
constexpr uint8_t glyphRow(int code, int row) {
  if (code < FONT_FIRST || code > FONT_LAST) return 0;
  if (row < 0 || row >= FONT_H) return 0;
  return FONT[static_cast<size_t>((code - FONT_FIRST) * FONT_H + row)];
}

// Is one pixel of a glyph set? Column 0 is the left one.
constexpr bool glyphPixel(int code, int col, int row) {
  if (col < 0 || col >= FONT_W) return false;
  return ((glyphRow(code, row) >> col) & 1) == 1;
}

}  // namespace sc8
