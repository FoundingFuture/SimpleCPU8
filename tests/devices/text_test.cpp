#include <doctest.h>

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
    CHECK_EQ(r.g.overlayChar[2 * TEXT_COLS + 4], 'X');
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

TEST_SUITE("custom font") {
  TEST_CASE("loads a 256-glyph font from the cartridge") {
    Rig r;
    // A font blob leads with its glyph count and its height, then the
    // glyphs. All blank except code 65 as a solid block.
    r.cart.assign(2 + 256 * 8, 0);
    r.cart[0] = 0;  // 0 means 256 glyphs: a byte cannot say 256
    r.cart[1] = 8;
    for (size_t row = 0; row < 8; row++) r.cart[2 + 65 * 8 + row] = 0xff;
    r.g.attachCart(r.cart);
    cmd(r.g, CMD_LOAD_FONT, {0, 0, 0});
    cmd(r.g, CMD_TEXT_STYLE, {6});
    cmd(r.g, CMD_TEXT_CHAR, {65});
    const auto out = r.g.composeFrame();
    // A loaded glyph row is eight bits wide, and the CELL reads the first
    // six of them. The two past the cell belong to the next character.
    bool allFg = true;
    for (int gy = 0; gy < FONT_H; gy++) {
      for (int gx = 0; gx < FONT_W; gx++) {
        if (gpu_rig::px(out, gx, gy) != 6) allFg = false;
      }
    }
    CHECK(allFg);
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
