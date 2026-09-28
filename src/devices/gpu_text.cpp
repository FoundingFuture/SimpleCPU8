// The GPU's text: the printf overlay, text mode and the font RAM.
#include <algorithm>
#include <vector>

#include "devices/gpu.h"
#include "devices/printf.h"

namespace sc8 {

using namespace sc8::gpu;

namespace {

// How far printf reads for its arguments. A conversion consumes as many bytes
// as its length says, so the run is as long as the template needs. This is
// only the ceiling that stops a runaway read.
constexpr uint32_t PRINTF_ARG_MAX = 256;

}  // namespace

// Copy the built-in font into the active font RAM. Codes outside the
// printable range stay blank.
//
// The layout is one byte a row with bit 0 on the left, 8 rows a glyph, and
// the blitter reads as much of it as the cell says. A font change is a
// change of cell rather than of format.
void Gpu::seedFont() {
  fontRam.fill(0);
  for (int code = FONT_FIRST; code <= FONT_LAST; code++) {
    for (int r = 0; r < FONT_H; r++) {
      fontRam[static_cast<size_t>(code * GLYPH_BYTES + r)] = FONT[static_cast<size_t>((code - FONT_FIRST) * FONT_H + r)];
    }
  }
}

uint8_t Gpu::glyphRowOf(int code, int row) const {
  if (row < 0 || row >= GLYPH_BYTES) return 0;
  return fontRam[static_cast<size_t>((code & 0xff) * GLYPH_BYTES + row)];
}

bool Gpu::setCell(int w, int h) {
  const auto fits = [](int n) { return n >= TEXT_CELL_MIN && n <= TEXT_CELL_MAX; };
  if (!fits(w) || !fits(h)) {
    fault_ = DeviceFault{CrashKind::BadTextCell, "the text cell is 4 to 8 pixels each way, and " + std::to_string(w) +
                                                     " by " + std::to_string(h) + " was asked for"};
    return false;
  }
  textCellW = w;
  textCellH = h;
  // A new cell moves every overlay cell and can leave the cursor outside
  // the grid, so the overlay starts over.
  overlayChar.fill(0);
  textCol = 0;
  textRow = 0;
  return true;
}

std::optional<DeviceFault> Gpu::takeFault() {
  if (!fault_) return fallback_->takeFault();
  std::optional<DeviceFault> f = std::move(fault_);
  fault_.reset();
  return f;
}

// Read the NUL-terminated template at the cartridge address, format it with
// the arguments at the RAM address, and draw the result.
void Gpu::printf(uint32_t base, int argAddr) {
  std::vector<uint8_t> tpl;
  uint32_t addr = base;
  for (int n = 0; n < 4096; n++) {
    const uint8_t b = cartByte(addr++);
    if (b == 0) break;
    tpl.push_back(b);
  }
  // %s and %r point into data RAM, read through the same second port that
  // text mode uses. The template is in ROM, but its strings live in RAM.
  // Wrap, do not clamp: a pointer near the top of RAM must read the bottom,
  // not a false terminator.
  const PrintfReader reader = [this](uint32_t off) { return ramByte(off); };
  // The arguments are a run of bytes in RAM. formatTemplate consumes as
  // many as the conversions ask for, so the run is as long as it needs to
  // be and nothing has to say how many there are.
  std::vector<uint8_t> args;
  if (ram_) {
    for (uint32_t i = 0; i < PRINTF_ARG_MAX; i++) args.push_back(ramByte(static_cast<uint32_t>(argAddr) + i));
  }
  for (uint8_t code : formatTemplate(tpl, args, reader)) putChar(code);
}

// Write one output byte to the overlay at the cursor and advance. A newline
// moves to the next row. Running off the bottom scrolls the plane up.
void Gpu::putChar(uint8_t code) {
  if (code == 0x0a) {
    textNewline();
    return;
  }
  const size_t cell = static_cast<size_t>(textRow * textCols() + textCol);
  overlayChar[cell] = code == 0 ? 0x20 : code;
  textCol++;
  if (textCol >= textCols()) textNewline();
}

void Gpu::textNewline() {
  textCol = 0;
  textRow++;
  if (textRow >= textRows()) {
    scrollText();
    textRow = textRows() - 1;
  }
}

// Shift the overlay up by one row. The top row is lost, the bottom clears.
void Gpu::scrollText() {
  const auto cols = static_cast<std::ptrdiff_t>(textCols());
  const auto grid = cols * textRows();
  std::copy(overlayChar.begin() + cols, overlayChar.begin() + grid, overlayChar.begin());
  std::fill(overlayChar.begin() + grid - cols, overlayChar.begin() + grid, 0);
}

// Draw one glyph into a screen buffer at a cell, in the given foreground.
// An opaque background fills the whole cell first.
void Gpu::drawGlyph(std::span<uint8_t> dest, int col, int row, int code, uint8_t fg, bool opaque) const {
  const int px0 = col * textCellW;
  const int py0 = row * textCellH;
  for (int gy = 0; gy < textCellH; gy++) {
    const int py = py0 + gy;
    if (py < 0 || py >= SCREEN_H) continue;
    const uint8_t bits = glyphRowOf(code, gy);
    for (int gx = 0; gx < textCellW; gx++) {
      const int px = px0 + gx;
      if (px < 0 || px >= SCREEN_W) continue;
      const size_t at = static_cast<size_t>(py * SCREEN_W + px);
      if ((bits >> gx) & 1) dest[at] = fg;
      else if (opaque) dest[at] = static_cast<uint8_t>(textBg);
    }
  }
}

// The printf overlay: a plane of character cells composited over whatever
// was drawn, in one colour. A cell holding zero paints nothing, so the
// plane is transparent everywhere a program has not written.
//
// DESIGN: it rides over MODE_WORLD as well as over graphics. A flight
// through a world with no way to show a score or a position readout was a
// real gap. Text mode is left out on purpose: that mode IS characters, and
// a second layer of them would mean nothing.
void Gpu::drawOverlay(std::span<uint8_t> out) const {
  const int cols = textCols();
  for (int row = 0; row < textRows(); row++) {
    for (int col = 0; col < cols; col++) {
      const uint8_t code = overlayChar[static_cast<size_t>(row * cols + col)];
      if (code != 0) drawGlyph(out, col, row, code, static_cast<uint8_t>(textColor), textOpaqueBg);
    }
  }
}

// Text mode: the whole screen is characters read from the mapped region of
// data RAM, drawn over the graphics VRAM. A pixel that is not a glyph pixel
// shows the VRAM pixel under it, so a program can draw and print at once.
//
// DESIGN: the background follows the same rule as the printf overlay. The
// style's opaque flag fills each cell with the text background first, and
// without it the cell is transparent. Text mode used to paint the whole
// screen in the text background, so BASIC's PLOT and CIRCLE drew into a
// VRAM nobody saw. Black text on black is still the default: VRAM starts
// black and BASIC clears it to its PAPER colour.
//
// The VRAM is shown as it is, without the scroll registers and without the
// sprites. Those belong to graphics mode, and a mode that reads its screen
// from RAM has no use for them.
void Gpu::composeText(std::span<uint8_t> out) const {
  if (!textMapped || !ram_) {
    // Text mode without a mapping: the screen has no buffer to read, so it
    // shows crawling snow. Unreachable through the commands, since the mode
    // command maps the buffer, and kept for a host with no RAM attached.
    fillNoise(out);
    return;
  }
  std::copy(vram.begin(), vram.end(), out.begin());
  const int cols = textCols();
  for (int row = 0; row < textRows(); row++) {
    for (int col = 0; col < cols; col++) {
      // DESIGN: wrap, do not clamp. The CPU wraps at 16 bits, so a screen
      // mapped near the top reads the bottom of RAM rather than zeros.
      const uint8_t code = ramByte(static_cast<uint32_t>(textBase + row * cols + col));
      drawGlyph(out, col, row, code, static_cast<uint8_t>(textColor), textOpaqueBg);
    }
  }
}

// Crawling snow for a mode with no mapping. A cheap hash of pixel and frame
// gives a deterministic pattern that churns as the frame counter ticks.
void Gpu::fillNoise(std::span<uint8_t> out) const {
  const uint32_t f = static_cast<uint32_t>(frame());
  for (size_t i = 0; i < out.size(); i++) {
    // The TypeScript multiplied the frame in doubles before the xor; the
    // low 32 bits of that product are what the xor sees, which is the
    // wrapping product here.
    uint32_t h = static_cast<uint32_t>(i) ^ (f * 2654435761u);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    out[i] = static_cast<uint8_t>(h & 0xff);
  }
}

}  // namespace sc8
