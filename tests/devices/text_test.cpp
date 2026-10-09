#include <doctest.h>

#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include "asm/asm.h"
#include "core/machine.h"
#include "core/microcode.h"
#include "devices/font.h"
#include "devices/gpu.h"
#include "gpu_rig.h"

using namespace sc8;
using namespace sc8::gpu;
using gpu_rig::cmd;
using gpu_rig::overlayText;

namespace {

// Where printf reads its arguments from. The template is ROM and the
// arguments are RAM, so their number is no longer bounded by patience.
constexpr int ARGS = 0x0800;

// A Gpu that owns its cartridge and, when asked, a data RAM.
struct Rig {
  gpu_rig::Clock clock;
  std::vector<uint8_t> cart;
  std::unique_ptr<gpu_rig::Bytes> ram;
  Gpu& g = clock.g;
  void withRam() {
    ram = gpu_rig::newRam();
    g.attachRam(ram->data());
  }
  void tpl(const std::string& s) {
    cart.assign(s.begin(), s.end());
    cart.push_back(0);
    g.attachCart(cart);
  }
  // Put a template in the cartridge and run printf on it, with any arguments
  // written into RAM first.
  void printf(const std::string& s, const std::vector<int>& args = {}) {
    tpl(s);
    if (ram) {
      for (size_t i = 0; i < args.size(); i++) (*ram)[ARGS + i] = static_cast<uint8_t>(args[i] & 0xff);
    }
    cmd(g, CMD_PRINTF, {0, 0, 0, ARGS >> 8, ARGS & 0xff});
  }
  // A screen mapped at an arbitrary 16-bit base, text mode already on.
  void mapped(int base) {
    g.powerOn();
    cmd(g, CMD_SET_TEXTMODE, {(base >> 8) & 0xff, base & 0xff});
  }
};

// The CELL, whatever the font says it is, rather than eight by eight.
bool cellShowsGlyph(const Gpu::Frame& out, int col, int row, int code, int fg) {
  for (int gy = 0; gy < FONT_H; gy++) {
    for (int gx = 0; gx < FONT_W; gx++) {
      const bool lit = gpu_rig::px(out, col * FONT_W + gx, row * FONT_H + gy) == fg;
      if (glyphPixel(code, gx, gy) != lit) return false;
    }
  }
  return true;
}

std::vector<int> draw(Gpu& g, int n) {
  std::vector<int> out;
  for (int i = 0; i < n; i++) out.push_back(g.read(GPU_RAND));
  return out;
}

}  // namespace

TEST_SUITE("printf overlay") {
  TEST_CASE("draws a plain template at the cursor") {
    Rig r;
    r.printf("Hi");
    CHECK_EQ(overlayText(r.g, 0), "Hi");
  }

  TEST_CASE("formats parameters read from RAM") {
    Rig r;
    r.withRam();
    r.printf("Add %hhu + %hhu = %hd", {5, 3, 0x00, 0x08});
    CHECK_EQ(overlayText(r.g, 0), "Add 5 + 3 = 8");
  }

  TEST_CASE("reads the same arguments again, because they live in RAM") {
    // There is no queue to drain. The arguments are bytes at an address, so
    // running the same printf twice prints the same thing.
    Rig r;
    r.withRam();
    r.printf("%hhu", {42});
    cmd(r.g, CMD_TEXT_AT, {0, 1});
    cmd(r.g, CMD_PRINTF, {0, 0, 0, ARGS >> 8, ARGS & 0xff});
    CHECK_EQ(overlayText(r.g, 0), "42");
    CHECK_EQ(overlayText(r.g, 1), "42");
  }

  TEST_CASE("positions the cursor in character cells") {
    Rig r;
    r.tpl("X");
    cmd(r.g, CMD_TEXT_AT, {4, 2});
    cmd(r.g, CMD_PRINTF, {0, 0, 0, ARGS >> 8, ARGS & 0xff});
    CHECK_EQ(r.g.overlayChar[2 * POWER_ON_COLS + 4], 'X');
  }

  TEST_CASE("wraps a newline to the next row") {
    Rig r;
    r.printf("a\nb");
    CHECK_EQ(overlayText(r.g, 0), "a");
    CHECK_EQ(overlayText(r.g, 1), "b");
  }

  TEST_CASE("scrolls the plane up past the bottom") {
    Rig r;
    r.printf("top");
    cmd(r.g, CMD_TEXT_AT, {0, 31});
    cmd(r.g, CMD_TEXT_CHAR, {'\n'});
    CHECK_NE(overlayText(r.g, 0), "top");
    CHECK_EQ(r.g.textRow, 31);
  }

  TEST_CASE("draws text in the foreground color over graphics") {
    Rig r;
    cmd(r.g, CMD_CLEAR, {3});  // graphics fill index 3
    r.tpl("A");
    cmd(r.g, CMD_TEXT_STYLE, {7});
    cmd(r.g, CMD_PRINTF, {0, 0, 0, ARGS >> 8, ARGS & 0xff});
    const auto out = r.g.composeFrame();
    CHECK(cellShowsGlyph(out, 0, 0, 'A', 7));
    CHECK_EQ(gpu_rig::px(out, 100, 100), 3);  // graphics still show elsewhere
  }

  TEST_CASE("clears the overlay on CMD_TEXT_CLEAR") {
    Rig r;
    r.printf("gone");
    r.g.write(GPU_CMD, CMD_TEXT_CLEAR);
    CHECK_EQ(overlayText(r.g, 0), "");
    CHECK_EQ(r.g.textCol, 0);
  }

  TEST_CASE("reads a zero-terminated string from RAM through %s") {
    Rig r;
    r.withRam();
    (*r.ram)[0x100] = 'h';
    (*r.ram)[0x101] = 'i';  // ram[0x102] stays 0, the terminator
    r.printf("say %s", {0x01, 0x00});
    CHECK_EQ(overlayText(r.g, 0), "say hi");
  }

  TEST_CASE("prints exactly N bytes with %r from RAM") {
    Rig r;
    r.withRam();
    const std::string s = "ABCDE";
    for (size_t i = 0; i < s.size(); i++) (*r.ram)[0x200 + i] = static_cast<uint8_t>(s[i]);
    r.printf("%3r", {0x02, 0x00});
    CHECK_EQ(overlayText(r.g, 0), "ABC");
  }

  TEST_CASE("wraps a %s pointer at the top of RAM instead of stopping at 0") {
    Rig r;
    r.withRam();
    (*r.ram)[0xffff] = 'h';
    (*r.ram)[0x0000] = 'i';  // ram[0x0001] stays 0, the terminator
    r.printf("say %s", {0xff, 0xff});
    CHECK_EQ(overlayText(r.g, 0), "say hi");
  }
}

TEST_SUITE("video mode and text mode") {
  TEST_CASE("starts in graphics mode") {
    Rig r;
    CHECK_EQ(r.g.videoMode, 0);  // MODE_GRAPHICS, the power-on mode
  }

  TEST_CASE("shows characters from mapped data RAM after switching to text mode") {
    Rig r;
    r.withRam();
    (*r.ram)[0x400] = 'O';
    (*r.ram)[0x401] = 'K';
    cmd(r.g, CMD_TEXT_STYLE, {5});
    cmd(r.g, CMD_SET_TEXTMODE, {0x04, 0x00});  // base 0x0400, the second 1KB
    const auto out = r.g.composeFrame();
    CHECK(cellShowsGlyph(out, 0, 0, 'O', 5));
    CHECK(cellShowsGlyph(out, 1, 0, 'K', 5));
  }

  TEST_CASE("cannot be entered without a framebuffer, so the snow is gone") {
    // One command sets the mode and the address together. Text mode with
    // nothing mapped used to show crawling snow to say so, and that state
    // is now unreachable rather than merely signposted.
    Rig r;
    r.withRam();
    cmd(r.g, CMD_SET_TEXTMODE, {0x04, 0x00});
    const auto out = r.g.composeFrame();
    CHECK_LT(std::set<uint8_t>(out.begin(), out.end()).size(), 4u);  // a blank screen, not snow
  }

  TEST_CASE("refreshes the display key when the mapped RAM changes") {
    Rig r;
    r.withRam();
    cmd(r.g, CMD_SET_TEXTMODE, {0, 0});
    const std::string before = r.g.displayKey();
    (*r.ram)[5] = '!';
    CHECK_NE(r.g.displayKey(), before);
  }

  // Text mode used to paint the whole screen in the text background, so a
  // program that drew and then printed saw only the printing. The VRAM
  // now shows through wherever a glyph pixel is not lit.
  TEST_CASE("shows the VRAM under the text") {
    Rig r;
    r.withRam();
    cmd(r.g, CMD_CLEAR, {4});
    gpu_rig::cmdv(r.g, CMD_PLOT, gpu_rig::cat(gpu_rig::xy(100, 100), 9));
    (*r.ram)[0] = 'A';
    cmd(r.g, CMD_TEXT_STYLE, {7, 2, 0});
    cmd(r.g, CMD_SET_TEXTMODE, {0, 0});
    const auto out = r.g.composeFrame();
    CHECK(cellShowsGlyph(out, 0, 0, 'A', 7));
    CHECK_EQ(gpu_rig::px(out, 100, 100), 9);  // the plotted pixel
    CHECK_EQ(gpu_rig::px(out, 200, 200), 4);  // the cleared VRAM elsewhere
    // A cell's unlit pixels show the VRAM too: the background is transparent.
    CHECK_EQ(gpu_rig::px(out, 5, 0), 4);  // the blank column of the cell
  }

  TEST_CASE("fills a cell with the text background only when the style says opaque") {
    // The same rule as the overlay over graphics: the opaque flag is what
    // asks for a background, and the default is transparent.
    Rig r;
    r.withRam();
    cmd(r.g, CMD_CLEAR, {4});
    (*r.ram)[0] = 'A';
    cmd(r.g, CMD_TEXT_STYLE, {7, 2, 1});
    cmd(r.g, CMD_SET_TEXTMODE, {0, 0});
    const auto out = r.g.composeFrame();
    CHECK(cellShowsGlyph(out, 0, 0, 'A', 7));
    CHECK_EQ(gpu_rig::px(out, 5, 0), 2);  // the cell's blank column, in the background
    CHECK_EQ(gpu_rig::px(out, 5, 8), 2);  // an empty cell is opaque too
    CHECK_EQ(gpu_rig::px(out, 200, 200), 2);
  }

  TEST_CASE("returns to graphics on a mode switch") {
    Rig r;
    r.withRam();
    cmd(r.g, CMD_CLEAR, {4});
    cmd(r.g, CMD_SET_TEXTMODE, {0, 0});
    cmd(r.g, CMD_SET_GRAPHICSMODE);
    CHECK_EQ(gpu_rig::px(r.g.composeFrame(), 100, 100), 4);
  }

  // Draw the same base twice, once with a byte set and once without. Any
  // difference proves the GPU read that address.
  auto reads = [](int base, int addr) {
    Rig r;
    r.withRam();
    r.mapped(base);
    const auto blank = r.g.composeFrame();
    (*r.ram)[static_cast<size_t>(addr)] = 90;  // 'Z'
    const auto drawn = r.g.composeFrame();
    return blank != drawn;
  };

  TEST_CASE("fits a screen mapped at $FC00 exactly under the top of RAM") {
    CHECK(reads(0xfc00, 0xfc00));  // the first cell
    CHECK(reads(0xfc00, 0xffff));  // the last cell, at the top
  }

  TEST_CASE("wraps a text base past the top instead of reading zeros") {
    CHECK(reads(0xffff, 0xffff));  // cell 0 sits at the top
    CHECK(reads(0xffff, 0x0000));  // cell 1 wraps to the bottom
  }

  TEST_CASE("keys the display on a wrapped cell too") {
    Rig r;
    r.withRam();
    r.mapped(0xffff);
    const std::string before = r.g.displayKey();
    (*r.ram)[0] = 42;
    CHECK_NE(r.g.displayKey(), before);
  }
}

TEST_SUITE("the text cell") {
  // A font blob as CMD_LOAD_FONT reads it: width, height, 2048 glyph bytes.
  std::vector<uint8_t> fontBlob(int w, int h, int code = -1, uint8_t row = 0xff) {
    std::vector<uint8_t> b(2 + GLYPHS * GLYPH_BYTES, 0);
    b[0] = static_cast<uint8_t>(w);
    b[1] = static_cast<uint8_t>(h);
    if (code >= 0) {
      for (size_t r = 0; r < GLYPH_BYTES; r++) b[2 + static_cast<size_t>(code) * GLYPH_BYTES + r] = row;
    }
    return b;
  }

  void load(Rig& r, const std::vector<uint8_t>& blob) {
    r.cart = blob;
    r.g.attachCart(r.cart);
    cmd(r.g, CMD_LOAD_FONT, {0, 0, 0});
  }

  // Pixels at the fg colour inside the w by h cell at col, row.
  int litInCell(const Gpu::Frame& out, int col, int row, int w, int h, int fg) {
    int n = 0;
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        if (gpu_rig::px(out, col * w + x, row * h + y) == fg) n++;
      }
    }
    return n;
  }

  TEST_CASE("power on is the built-in font at 6 by 8, 42 by 32") {
    Rig r;
    CHECK_EQ(r.g.read(GPU_TEXT_COLS), 42);
    CHECK_EQ(r.g.read(GPU_TEXT_ROWS), 32);
    CHECK_EQ(r.g.glyphRowOf('A', 0), glyphRow('A', 0));
  }

  TEST_CASE("CMD_LOAD_FONT stores 256 glyphs of 8 bytes and draws one in a 6 by 8 cell") {
    Rig r;
    load(r, fontBlob(6, 8, 65));
    cmd(r.g, CMD_TEXT_STYLE, {6});
    cmd(r.g, CMD_TEXT_CHAR, {65});
    const auto out = r.g.composeFrame();
    CHECK_EQ(litInCell(out, 0, 0, 6, 8, 6), 6 * 8);
    // The two bits past the cell belong to the next character, and stay dark.
    CHECK_EQ(gpu_rig::px(out, 6, 0), 0);
    CHECK_EQ(r.g.glyphRowOf(65, 7), 0xff);
    CHECK_EQ(r.g.glyphRowOf(66, 0), 0);
  }

  TEST_CASE("CMD_LOAD_FONT sets the cell from its header, every size 4 to 8") {
    for (int w = TEXT_CELL_MIN; w <= TEXT_CELL_MAX; w++) {
      for (int h = TEXT_CELL_MIN; h <= TEXT_CELL_MAX; h++) {
        Rig r;
        gpu_rig::Faults faults;
        r.g.attachFaultSink(&faults);
        load(r, fontBlob(w, h, 0x41));
        CAPTURE(w);
        CAPTURE(h);
        CHECK_EQ(r.g.read(GPU_TEXT_COLS), 256 / w);
        CHECK_EQ(r.g.read(GPU_TEXT_ROWS), 256 / h);
        CHECK_EQ(r.g.glyphRowOf(0x41, 7), 0xff);
        CHECK(faults.got.empty());
      }
    }
  }

  TEST_CASE("the reads give 32 by 32 at 8 by 8 and 64 by 64 at 4 by 4") {
    Rig r;
    cmd(r.g, CMD_TEXT_CELL, {8, 8});
    CHECK_EQ(r.g.read(GPU_TEXT_COLS), 32);
    CHECK_EQ(r.g.read(GPU_TEXT_ROWS), 32);
    cmd(r.g, CMD_TEXT_CELL, {4, 4});
    CHECK_EQ(r.g.read(GPU_TEXT_COLS), 64);
    CHECK_EQ(r.g.read(GPU_TEXT_ROWS), 64);
  }

  TEST_CASE("CMD_TEXT_CELL keeps the glyphs and changes the grid") {
    Rig r;
    load(r, fontBlob(6, 8, 65));
    cmd(r.g, CMD_TEXT_CELL, {8, 4});
    CHECK_EQ(r.g.read(GPU_TEXT_COLS), 32);
    CHECK_EQ(r.g.read(GPU_TEXT_ROWS), 64);
    CHECK_EQ(r.g.glyphRowOf(65, 0), 0xff);
  }

  TEST_CASE("CMD_RESET_FONT brings back the built-in glyphs and 6 by 8") {
    Rig r;
    load(r, fontBlob(8, 8, 'A', 0x81));
    cmd(r.g, CMD_RESET_FONT);
    CHECK_EQ(r.g.read(GPU_TEXT_COLS), 42);
    CHECK_EQ(r.g.read(GPU_TEXT_ROWS), 32);
    for (int row = 0; row < GLYPH_BYTES; row++) CHECK_EQ(r.g.glyphRowOf('A', row), glyphRow('A', row));
    CHECK_EQ(r.g.glyphRowOf(0x80, 0), 0);
  }

  TEST_CASE("a cell change clears the overlay and homes its cursor, by any of the three commands") {
    Rig r;
    const auto check = [&](const char* what) {
      CAPTURE(what);
      CHECK_EQ(overlayText(r.g, 0), "");
      cmd(r.g, CMD_TEXT_CHAR, {'X'});
      CHECK_EQ(r.g.overlayChar[0], 'X');
    };
    r.printf("HELLO\nWORLD");
    cmd(r.g, CMD_TEXT_CELL, {8, 8});
    check("CMD_TEXT_CELL");
    r.printf("HELLO\nWORLD");
    load(r, fontBlob(5, 7));
    check("CMD_LOAD_FONT");
    r.printf("HELLO\nWORLD");
    cmd(r.g, CMD_RESET_FONT);
    check("CMD_RESET_FONT");
  }

  TEST_CASE("a width or height outside 4 to 8 reports bad-text-cell at once, and changes nothing") {
    Rig r;
    gpu_rig::Faults faults;
    r.g.attachFaultSink(&faults);
    size_t n = 0;
    for (const auto& [w, h] : std::vector<std::pair<int, int>>{{3, 8}, {9, 8}, {6, 3}, {6, 9}, {0, 0}}) {
      CAPTURE(w);
      CAPTURE(h);
      cmd(r.g, CMD_TEXT_CELL, {w, h});
      REQUIRE_EQ(faults.got.size(), ++n);
      CHECK_EQ(faults.got.back().kind, CrashKind::BadTextCell);
      CHECK_EQ(r.g.read(GPU_TEXT_COLS), 42);
    }
    load(r, fontBlob(9, 8, 'A'));
    REQUIRE_EQ(faults.got.size(), ++n);
    CHECK_EQ(faults.got.back().kind, CrashKind::BadTextCell);
    CHECK(faults.got.back().message.find("9 by 8") != std::string::npos);
    CHECK_EQ(r.g.glyphRowOf('A', 0), glyphRow('A', 0));
    // A good cell reports nothing.
    cmd(r.g, CMD_TEXT_CELL, {8, 8});
    CHECK_EQ(faults.got.size(), n);
  }

  TEST_CASE("a fault reaches the machine through every device in the chain") {
    gpu_rig::Faults faults;
    LogIoBus end;
    Gpu inner([] { return uint64_t{0}; }, &end);
    Gpu outer([] { return uint64_t{0}; }, &inner);
    outer.attachFaultSink(&faults);
    inner.write(GPU_DATA0, 3);
    inner.write(GPU_DATA1, 8);
    inner.write(GPU_CMD, CMD_TEXT_CELL);
    REQUIRE_EQ(faults.got.size(), 1u);
    outer.detachFaultSink(&faults);
    inner.write(GPU_DATA0, 3);
    inner.write(GPU_CMD, CMD_TEXT_CELL);
    CHECK_EQ(faults.got.size(), 1u);
  }

  TEST_CASE("a glyph draws only inside its cell") {
    Rig r;
    load(r, fontBlob(5, 7, 65));
    cmd(r.g, CMD_TEXT_STYLE, {6});
    cmd(r.g, CMD_TEXT_CHAR, {65});
    cmd(r.g, CMD_TEXT_CHAR, {65});
    const auto out = r.g.composeFrame();
    CHECK_EQ(litInCell(out, 0, 0, 5, 7, 6), 5 * 7);
    CHECK_EQ(litInCell(out, 1, 0, 5, 7, 6), 5 * 7);
    CHECK_EQ(gpu_rig::px(out, 0, 7), 0);   // below the cell
    CHECK_EQ(gpu_rig::px(out, 10, 0), 0);  // past the second cell
    CHECK_EQ(gpu_rig::litPixels(out), static_cast<size_t>(2 * 5 * 7));
  }

  TEST_CASE("text mode reads the grid at the stride of the cell") {
    Rig r;
    r.withRam();
    r.mapped(0x4000);
    load(r, fontBlob(8, 8, 65));
    cmd(r.g, CMD_TEXT_STYLE, {6});
    // Row 1, column 2 of a 32 column grid.
    (*r.ram)[0x4000 + 1 * 32 + 2] = 65;
    const auto out = r.g.composeFrame();
    CHECK_EQ(litInCell(out, 2, 1, 8, 8, 6), 64);
    CHECK_EQ(gpu_rig::litPixels(out), 64u);
    // The last cell of a 64 by 64 grid is byte 4095.
    cmd(r.g, CMD_TEXT_CELL, {4, 4});
    (*r.ram)[0x4000 + 4095] = 65;
    CHECK_EQ(litInCell(r.g.composeFrame(), 63, 63, 4, 4, 6), 16);
  }

  TEST_CASE("the overlay wraps at the cell's columns and scrolls at its rows") {
    Rig r;
    cmd(r.g, CMD_TEXT_CELL, {8, 8});
    r.printf(std::string(33, 'A'));
    CHECK_EQ(overlayText(r.g, 0), std::string(32, 'A'));
    CHECK_EQ(overlayText(r.g, 1), "A");
    cmd(r.g, CMD_TEXT_CELL, {8, 8});
    r.printf("TOP" + std::string(32, '\n') + "END");
    // 32 rows: the newline past the last one scrolled TOP away.
    CHECK_EQ(overlayText(r.g, 31), "END");
    CHECK_EQ(overlayText(r.g, 0), "");
    cmd(r.g, CMD_TEXT_AT, {40, 70});
    cmd(r.g, CMD_TEXT_CHAR, {'Q'});
    CHECK_EQ(overlayText(r.g, 70 % 32).substr(40 % 32), "Q");
  }
}

TEST_SUITE("a bad text cell through the bus") {
  TEST_CASE("crashes the machine with bad-text-cell") {
    const Assembled a = assemble(
        "        OUT GPU_DATA0, 3\n"
        "        OUT GPU_DATA1, 8\n"
        "        OUT GPU_CMD, CMD_TEXT_CELL\n"
        "        HLT\n");
    REQUIRE(a.errors.empty());
    Machine m(a.program, buildOptimal());
    Gpu g([&m] { return m.cycles; });
    m.setIo(&g);
    m.run(50);
    CHECK_EQ(m.status, Status::Crashed);
    REQUIRE(m.crash);
    CHECK_EQ(m.crash->kind, CrashKind::BadTextCell);
    CHECK_EQ(crashKindName(m.crash->kind), "bad-text-cell");
    // The crash names the OUT that ran the command. The HLT never ran.
    CHECK_EQ(m.crash->lastInstrPc, 2);
    CHECK_EQ(m.instructions, 3u);
  }

  TEST_CASE("a machine that goes away does not cut off the one that replaced it") {
    const Assembled a = assemble(
        "        OUT GPU_DATA0, 9\n"
        "        OUT GPU_DATA1, 8\n"
        "        OUT GPU_CMD, CMD_TEXT_CELL\n"
        "        HLT\n");
    REQUIRE(a.errors.empty());
    uint64_t cycles = 0;
    Gpu g([&cycles] { return cycles; });
    auto first = std::make_unique<Machine>(a.program, buildOptimal(), &g);
    Machine second(a.program, buildOptimal(), &g);
    first.reset();
    // Stepped, as the IDE steps, which does not report to the devices anew.
    for (int i = 0; i < 4; i++) second.instructionStep();
    CHECK_EQ(second.status, Status::Crashed);
  }
}

TEST_SUITE("gpu random") {
  TEST_CASE("returns a deterministic stream that a reseed changes") {
    Rig a;
    Rig b;
    const auto seqA = draw(a.g, 8);
    const auto seqB = draw(b.g, 8);
    CHECK(seqA == seqB);  // same seed, same stream
    CHECK_GT(std::set<int>(seqA.begin(), seqA.end()).size(), 1u);  // not a constant
    b.g.write(GPU_RAND, 42);  // reseed
    const auto seqC = draw(b.g, 8);
    CHECK(seqC != seqA);
  }

  TEST_CASE("matches the browser's xorshift byte for byte") {
    // The first bytes of the default stream, so a port of the generator
    // that drifts by one shift is caught here rather than in a demo.
    Rig r;
    uint32_t x = 0x1d872b41;
    std::vector<int> want;
    for (int i = 0; i < 8; i++) {
      x ^= x << 13;
      x ^= x >> 17;
      x ^= x << 5;
      want.push_back(static_cast<int>(x >> 24));
    }
    CHECK(draw(r.g, 8) == want);
  }

  TEST_CASE("reads a 16-bit random into D2 with INW, high byte first") {
    // Expected: two reads of a fresh generator, hi then lo.
    Rig ref;
    const int hi = ref.g.read(GPU_RAND);
    const int lo = ref.g.read(GPU_RAND);
    const Assembled a = assemble(
        "        INW GPU_RAND -> D2\n"
        "        LD D1 <- $200\n"
        "        LD [D1] <- D2\n"
        "        HLT\n");
    REQUIRE(a.errors.empty());
    Machine m(a.program, buildOptimal());
    Gpu g([&m] { return m.cycles; });  // same fixed seed as ref
    m.setIo(&g);
    m.run(50);
    CHECK_EQ(m.status, Status::Halted);
    CHECK_EQ(m.d2, (hi << 8) | lo);
    CHECK_EQ(m.ram[0x200], hi);  // stored big-endian at D1
    CHECK_EQ(m.ram[0x201], lo);
  }

  TEST_CASE("sets Z on INW when the word is zero") {
    const Assembled a = assemble("        INW GPU_RAND -> D1\n        HLT\n");
    REQUIRE(a.errors.empty());
    Machine m(a.program, buildOptimal());
    Gpu g([&m] { return m.cycles; });
    m.setIo(&g);
    m.run(50);
    CHECK_EQ(m.status, Status::Halted);
    // The default seed's first word is nonzero, so Z is clear.
    CHECK_EQ(m.flags.z, m.d1 == 0);
  }
}

TEST_SUITE("text through the bus") {
  TEST_CASE("runs printf from an assembled program") {
    const Assembled a = assemble(
        "        LD A <- 7\n"
        "        LD [n] <- A            ; the argument lives in RAM\n"
        "        OUT GPU_DATA4, n\n"
        "        OUT GPU_CMD, CMD_PRINTF\n"
        "        HLT\n"
        ".ram\n"
        "n:      db 0\n"
        ".data\n"
        "msg: db \"n=%hhu\", 0\n");
    REQUIRE(a.errors.empty());
    Machine m(a.program, buildOptimal());
    Gpu g([&m] { return m.cycles; });
    g.attachCart(a.cart);
    g.attachRam(m.ram.data());  // printf reads its arguments from RAM
    m.setIo(&g);
    m.run(200);
    CHECK_EQ(m.status, Status::Halted);
    CHECK_EQ(overlayText(g, 0), "n=7");
  }
}

TEST_SUITE("gpu random seeding") {
  TEST_CASE("takes its seed from the injected source, like the cycle clock") {
    // The seed is a dependency, not a constant. Tests get a fixed default so
    // a stream is reproducible. The app injects the wall clock.
    auto drawSeeded = [](uint32_t seed) {
      Gpu g([] { return uint64_t{0}; }, nullptr, [seed] { return seed; });
      return draw(g, 8);
    };
    // Each call builds a fresh Gpu, so nothing carries between draws.
    CHECK(drawSeeded(99) == drawSeeded(99));
    CHECK(drawSeeded(12345) != drawSeeded(99));
  }

  TEST_CASE("draws a fresh seed on every power-on") {
    // powerOn runs on Restart and on program load, so it must ask the source
    // again. A source that moves then gives a new stream every run.
    int ticks = 0;
    Gpu g([] { return uint64_t{0}; }, nullptr, [&ticks] { return static_cast<uint32_t>(++ticks * 7919); });
    const auto first = draw(g, 8);
    g.powerOn();
    const auto second = draw(g, 8);
    CHECK(second != first);
    CHECK_EQ(ticks, 2);  // asked once per power-on, not once per read
  }

  TEST_CASE("survives a seed of zero, which would freeze an xorshift") {
    // Zero is not a legal xorshift state, so it falls back to the default
    // stream rather than freezing. Documented, not clever.
    Gpu zero([] { return uint64_t{0}; }, nullptr, [] { return uint32_t{0}; });
    const auto seq = draw(zero, 8);
    CHECK_GT(std::set<int>(seq.begin(), seq.end()).size(), 1u);
    Gpu dflt([] { return uint64_t{0}; });
    CHECK(seq == draw(dflt, 8));
  }
}
