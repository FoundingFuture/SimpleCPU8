// A font asset: 256 glyphs of 8 by 8 and the cell they were drawn for. On
// disk a .font is text drawn as art, so it diffs in git and opens in any
// editor. docs/design/font-design.md, "The font asset", has the rules.
//
//   SimpleCPU-8 font
//   cell 6 8
//
//   $41 A
//   .XXX....     eight rows of eight, X set, the left one bit 0
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "assets/sprite.h"
#include "devices/gpu.h"

namespace sc8::font {

inline constexpr std::string_view MAGIC = "SimpleCPU-8 font";
inline constexpr std::string_view EXTENSION = ".font";

struct Font {
  int width = FONT_W;
  int height = FONT_H;
  // Code 0 first, one byte a row, bit 0 the left pixel.
  std::array<uint8_t, gpu::GLYPHS * gpu::GLYPH_BYTES> glyphs{};
  bool operator==(const Font&) const = default;
};

struct ReadResult {
  std::optional<Font> font;
  // "line N: ..." when the text is refused.
  std::string error;
};

// The text of a .font, every rule checked.
ReadResult read(std::string_view text);

// The text of a font, in the one form read accepts without blank lines
// beyond the one between glyphs. Writing what read gave returns that text.
std::string write(const Font& f);

// The built-in font as a new font starts: cell 6 by 8, the GPU's glyphs at
// $20 to $7F, every other code blank. The block at $7F fills all eight
// columns here, so a cursor fills any cell. The GPU's own block keeps six.
Font builtIn();

// What CMD_LOAD_FONT reads from the cartridge: the width, the height and
// the 2048 glyph bytes, code 0 first.
std::vector<uint8_t> blob(const Font& f);

// The font as the editor draws it: 256 frames of 8 by 8, code 0 first. A
// set pixel is INK and a clear one 0. The strip caps of sprite::load
// belong to sprites, so a font opens here instead.
inline constexpr uint8_t INK = 1;

struct Opened {
  sprite::Strip strip;
  int width = FONT_W;  // the cell
  int height = FONT_H;
};

// The bytes of a .font as frames. Nothing, with read's message in
// `error`, when the text is refused.
std::optional<Opened> open(const std::vector<uint8_t>& bytes, std::string* error);

// The text of the frames and the cell, as write gives it. Any pixel not 0
// is set.
std::vector<uint8_t> save(const sprite::Strip& strip, int width, int height);

// The text of a new font: builtIn(), so cell 6 by 8 and $7F eight wide.
std::vector<uint8_t> newFile();

// The asset name of a font: the name with EXTENSION added when it lacks it.
std::string fileName(const std::string& name);

}  // namespace sc8::font
