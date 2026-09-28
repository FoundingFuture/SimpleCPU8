// The .font file: 256 glyphs of 8 by 8 drawn as text, and the cell they
// were drawn for. docs/design/font-design.md, "The font asset".

#include <doctest.h>

#include <string>

#include "assets/font.h"
#include "devices/font.h"

using namespace sc8;

namespace {

// The text of a font whose every glyph is blank, in the cell given.
std::string blankText(int w, int h) {
  font::Font f;
  f.width = w;
  f.height = h;
  return font::write(f);
}

// The text with the line numbered `n`, counting from 1, replaced.
std::string withLine(const std::string& text, int n, const std::string& line) {
  size_t at = 0;
  for (int i = 1; i < n; i++) at = text.find('\n', at) + 1;
  const size_t end = text.find('\n', at);
  return text.substr(0, at) + line + text.substr(end);
}

// The error a text gives, or "" when it reads.
std::string refusal(const std::string& text) {
  const font::ReadResult r = font::read(text);
  if (r.font) return "";
  return r.error;
}

}  // namespace

TEST_SUITE("a .font file") {
  TEST_CASE("starts with its magic line and its cell, then glyph $00") {
    const std::string t = blankText(6, 8);
    CHECK(t.starts_with("SimpleCPU-8 font\ncell 6 8\n\n$00\n........\n"));
  }

  TEST_CASE("labels the printable codes with their character") {
    const std::string t = blankText(6, 8);
    CHECK(t.find("\n$41 A\n") != std::string::npos);
    CHECK(t.find("\n$7E ~\n") != std::string::npos);
    CHECK(t.find("\n$20\n") != std::string::npos);
    CHECK(t.find("\n$7F\n") != std::string::npos);
    CHECK(t.find("\n$FF\n") != std::string::npos);
  }

  TEST_CASE("reads back what it writes, text for text and glyph for glyph") {
    font::Font f = font::builtIn();
    f.width = 5;
    f.height = 7;
    f.glyphs[0x80 * 8 + 3] = 0xA5;
    const std::string t = font::write(f);
    const font::ReadResult r = font::read(t);
    REQUIRE_MESSAGE(r.font, r.error);
    CHECK(*r.font == f);
    CHECK_EQ(font::write(*r.font), t);
  }

  TEST_CASE("draws bit 0 as the left pixel") {
    font::Font f;
    f.glyphs[0x41 * 8] = 0x01;
    f.glyphs[0x41 * 8 + 1] = 0x80;
    const std::string t = font::write(f);
    CHECK(t.find("$41 A\nX.......\n.......X\n") != std::string::npos);
  }

  TEST_CASE("takes blank lines between glyphs, and none at all") {
    const std::string t = blankText(6, 8);
    std::string spaced;
    std::string packed;
    for (size_t at = 0; at < t.size();) {
      const size_t nl = t.find('\n', at);
      const std::string line = t.substr(at, nl - at);
      at = nl + 1;
      if (line.starts_with("$")) spaced += "\n\n";
      spaced += line + "\n";
      if (!line.empty()) packed += line + "\n";
    }
    CHECK(font::read(spaced).font);
    CHECK(font::read(packed).font);
  }
}

TEST_SUITE("a .font file is refused") {
  // The text is blankText(6, 8): line 1 the magic, 2 the cell, 3 blank,
  // 4 "$00", 5 to 12 its rows, 13 blank, 14 "$01", and so on.
  TEST_CASE("without its magic line") {
    CHECK_EQ(refusal(withLine(blankText(6, 8), 1, "SimpleCPU-8 fonts")),
             "line 1: a font starts with the line SimpleCPU-8 font");
  }

  TEST_CASE("with a cell outside 4 to 8, or no cell") {
    CHECK_EQ(refusal(withLine(blankText(6, 8), 2, "cell 3 8")),
             "line 2: the cell is a width and a height, each 4 to 8, as cell 6 8");
    CHECK_EQ(refusal(withLine(blankText(6, 8), 2, "cell 6 9")),
             "line 2: the cell is a width and a height, each 4 to 8, as cell 6 8");
    CHECK_EQ(refusal(withLine(blankText(6, 8), 2, "cell 6")),
             "line 2: the cell is a width and a height, each 4 to 8, as cell 6 8");
  }

  TEST_CASE("with a glyph out of order") {
    CHECK_EQ(refusal(withLine(blankText(6, 8), 14, "$02")), "line 14: glyph $01 comes next, not $02");
  }

  TEST_CASE("with a code line that is not $ and two hex digits") {
    CHECK_EQ(refusal(withLine(blankText(6, 8), 4, "00")),
             "line 4: a glyph starts with $ and its code in two hex digits, as $41 A");
    CHECK_EQ(refusal(withLine(blankText(6, 8), 4, "$00 AB")),
             "line 4: a glyph starts with $ and its code in two hex digits, as $41 A");
  }

  TEST_CASE("with a row that is not eight of X and .") {
    CHECK_EQ(refusal(withLine(blankText(6, 8), 5, ".......")),
             "line 5: a row is eight characters of X and ., the left one bit 0");
    CHECK_EQ(refusal(withLine(blankText(6, 8), 5, "...o....")),
             "line 5: a row is eight characters of X and ., the left one bit 0");
  }

  TEST_CASE("with a glyph cut short") {
    CHECK_EQ(refusal(withLine(blankText(6, 8), 12, "")), "line 12: glyph $00 has 7 rows, and needs 8");
  }

  TEST_CASE("when it ends before glyph $FF") {
    const std::string t = blankText(6, 8);
    const std::string cut = t.substr(0, t.find("$FF"));
    CHECK(refusal(cut).ends_with(": the font ends before glyph $FF"));
  }

  TEST_CASE("with anything after glyph $FF") {
    CHECK(refusal(blankText(6, 8) + "$00\n").ends_with(": nothing may follow glyph $FF"));
  }
}

TEST_SUITE("the built-in font as a .font") {
  TEST_CASE("is cell 6 by 8, with the GPU's glyphs at $20 to $7E") {
    const font::Font f = font::builtIn();
    CHECK_EQ(f.width, 6);
    CHECK_EQ(f.height, 8);
    for (int code = FONT_FIRST; code < FONT_LAST; code++) {
      for (int row = 0; row < 8; row++) {
        CHECK_EQ(f.glyphs[static_cast<size_t>(code * 8 + row)], glyphRow(code, row));
      }
    }
  }

  TEST_CASE("widens the block at $7F to all eight columns") {
    const font::Font f = font::builtIn();
    for (int row = 0; row < 8; row++) CHECK_EQ(f.glyphs[static_cast<size_t>(0x7f * 8 + row)], 0xff);
    // The GPU's own block stays six columns wide.
    CHECK_EQ(glyphRow(0x7f, 0), 0x3f);
  }

  TEST_CASE("leaves every other code blank") {
    const font::Font f = font::builtIn();
    for (int code = 0; code < 256; code++) {
      if (code >= FONT_FIRST && code <= FONT_LAST) continue;
      for (int row = 0; row < 8; row++) CHECK_EQ(f.glyphs[static_cast<size_t>(code * 8 + row)], 0);
    }
  }
}

TEST_SUITE("a font's blob") {
  TEST_CASE("is the width, the height and 2048 glyph bytes, code 0 first") {
    font::Font f;
    f.width = 5;
    f.height = 7;
    f.glyphs[0] = 0x11;
    f.glyphs[255 * 8 + 7] = 0x22;
    const std::vector<uint8_t> b = font::blob(f);
    REQUIRE_EQ(b.size(), 2u + 2048u);
    CHECK_EQ(b[0], 5);
    CHECK_EQ(b[1], 7);
    CHECK_EQ(b[2], 0x11);
    CHECK_EQ(b[2 + 2047], 0x22);
  }
}
