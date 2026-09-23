#include <doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "asm/asm.h"
#include "core/machine.h"
#include "core/microcode.h"
#include "devices/gpu.h"
#include "devices/gpu_ports.h"
#include "gpu_rig.h"

using namespace sc8;
using namespace sc8::gpu;
using gpu_rig::at;
using gpu_rig::cat;
using gpu_rig::cmd;
using gpu_rig::cmdv;
using gpu_rig::moveTo;
using gpu_rig::vram;
using gpu_rig::xy;

namespace {

bool allZero(const std::vector<uint8_t>& v) { return std::all_of(v.begin(), v.end(), [](uint8_t b) { return b == 0; }); }

std::vector<int> pal3(const Palette& p, int index) {
  const size_t o = static_cast<size_t>(index) * 3;
  return {p[o], p[o + 1], p[o + 2]};
}

std::string readDoc(const char* name) {
  const std::filesystem::path doc =
      std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "docs" / "design" / name;
  std::ifstream in(doc);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

TEST_SUITE("gpu pixels") {
  TEST_CASE("plots a pixel at the latched coordinates") {
    gpu_rig::Clock c;
    cmdv(c.g, CMD_PLOT, cat(xy(10, 20), 0xe3));
    CHECK_EQ(vram(c.g, 10, 20), 0xe3);
  }

  TEST_CASE("reads a pixel back through a command, not a port") {
    gpu_rig::Clock c;
    cmdv(c.g, CMD_READ_PIXEL, xy(5, 5));
    CHECK_EQ(c.g.read(GPU_DATA0), 0);
    cmdv(c.g, CMD_PLOT, cat(xy(5, 5), 42));
    cmdv(c.g, CMD_READ_PIXEL, xy(5, 5));
    CHECK_EQ(c.g.read(GPU_DATA0), 42);
  }

  TEST_CASE("clips signed off-screen coordinates instead of wrapping or crashing") {
    gpu_rig::Clock c;
    cmdv(c.g, CMD_PLOT, cat(xy(-1, 0), 7));
    CHECK(allZero(c.g.vram));
    cmdv(c.g, CMD_READ_PIXEL, xy(-1, 0));
    CHECK_EQ(c.g.read(GPU_DATA0), 0);  // off-screen reads are 0
    cmdv(c.g, CMD_PLOT, cat(xy(255, 0), 7));
    CHECK_EQ(c.g.vram[0xff], 7);
  }

  TEST_CASE("clears the data ports after a command, so nothing carries over") {
    // DESIGN: the sticky high byte on the old coordinate latches was the bug
    // the knowledge base called the classic one. Plotting at x 300 and then
    // at x 5 used to leave the high byte set and plot at 261 instead.
    gpu_rig::Clock c;
    cmdv(c.g, CMD_PLOT, cat(xy(300, 0), 7));
    cmdv(c.g, CMD_PLOT, cat(xy(5, 0), 9));
    CHECK_EQ(c.g.vram[5], 9);
    CHECK_EQ(c.g.vram[261], 0);  // where the sticky latch would have put it
  }

  TEST_CASE("keeps them when MOD_STICKY says so") {
    gpu_rig::Clock c;
    c.g.write(GPU_CMD_MOD, MOD_STICKY);
    cmdv(c.g, CMD_PLOT, cat(xy(10, 10), 3));
    c.g.write(GPU_DATA3, 11);  // move y, leave everything else
    c.g.write(GPU_CMD, CMD_PLOT);
    CHECK_EQ(vram(c.g, 10, 10), 3);
    CHECK_EQ(vram(c.g, 10, 11), 3);
  }

  TEST_CASE("steps DATA0 when MOD_INC0 says so") {
    // DATA0 is the index on every command that takes one, so a walk sets
    // the arguments once and lets the modifier step the index.
    gpu_rig::Clock c;
    c.g.write(GPU_CMD_MOD, MOD_STICKY | MOD_INC0);
    c.g.write(GPU_DATA0, 5);
    c.g.write(GPU_CMD, CMD_SET_COLOR);
    CHECK_EQ(c.g.color, 5);
    c.g.write(GPU_CMD, CMD_SET_COLOR);
    CHECK_EQ(c.g.color, 6);
    c.g.write(GPU_CMD, CMD_SET_COLOR);
    CHECK_EQ(c.g.color, 7);
  }

  TEST_CASE("bumps the version on visible changes only") {
    gpu_rig::Clock c;
    const int64_t v0 = c.g.version;
    c.g.write(GPU_DATA0, 3);  // an argument: nothing visible yet
    CHECK_EQ(c.g.version, v0);
    cmdv(c.g, CMD_PLOT, cat(xy(0, 0), 1));
    CHECK_EQ(c.g.version, v0 + 1);
  }
}

TEST_SUITE("gpu palette") {
  TEST_CASE("defaults to 3-3-2 with true extremes") {
    gpu_rig::Clock c;
    CHECK(pal3(c.g.palette, 0) == std::vector<int>{0, 0, 0});  // index 0 black
    CHECK(pal3(c.g.palette, 255) == std::vector<int>{255, 255, 255});
    // 0xE0 = RRR only: pure red.
    CHECK(pal3(c.g.palette, 0xe0) == std::vector<int>{255, 0, 0});
  }

  TEST_CASE("dumps the live palette to RAM and takes one back") {
    // A palette is 768 bytes, so it moves through memory rather than a byte
    // at a time through a port. Small answers come back on a data port and
    // large ones go to RAM.
    gpu_rig::Clock c;
    auto ram = gpu_rig::newRam();
    c.g.attachRam(ram->data());
    const int BUF = 0x1000;
    cmd(c.g, CMD_MEMMAP, {MAP_PALETTE, BUF >> 8, BUF & 0xff});
    cmd(c.g, CMD_STORE_PALETTE);
    CHECK_EQ((*ram)[BUF + 30], c.g.palette[30]);
    (*ram)[BUF + 30] = 1;
    (*ram)[BUF + 31] = 2;
    (*ram)[BUF + 32] = 3;
    cmd(c.g, CMD_FETCH_PALETTE);
    CHECK_EQ(c.g.palette[30], 1);
    CHECK_EQ(c.g.palette[31], 2);
    CHECK_EQ(c.g.palette[32], 3);
  }

  TEST_CASE("keeps the source and the destination apart when asked") {
    // Two types exist so a program can read the live palette into one buffer
    // while a prepared one waits in another.
    gpu_rig::Clock c;
    auto ram = gpu_rig::newRam();
    c.g.attachRam(ram->data());
    cmd(c.g, CMD_MEMMAP, {MAP_PALETTE_OUT, 0x10, 0x00});
    cmd(c.g, CMD_MEMMAP, {MAP_PALETTE_IN, 0x20, 0x00});
    (*ram)[0x2000] = 42;
    cmd(c.g, CMD_STORE_PALETTE);
    cmd(c.g, CMD_FETCH_PALETTE);
    CHECK_EQ(c.g.palette[0], 42);  // came from IN
    CHECK_EQ((*ram)[0x1000], 0);   // the dump went to OUT, the old entry 0
  }

  TEST_CASE("moves nothing with no mapping") {
    gpu_rig::Clock c;
    auto ram = gpu_rig::newRam();
    c.g.attachRam(ram->data());
    cmd(c.g, CMD_STORE_PALETTE);
    CHECK(allZero(std::vector<uint8_t>(ram->begin(), ram->begin() + 1024)));
  }
}

TEST_SUITE("gpu clock and bus") {
  TEST_CASE("frame counter is machine cycles over CYCLES_PER_FRAME") {
    gpu_rig::Clock c;
    CHECK_EQ(c.g.read(GPU_FRAME), 0);
    c.setCycles(CYCLES_PER_FRAME * 3 + 5);
    CHECK_EQ(c.g.read(GPU_FRAME), 3);
    c.setCycles(CYCLES_PER_FRAME * 260);
    CHECK_EQ(c.g.read(GPU_FRAME), 260 & 0xff);
  }

  TEST_CASE("fast-frame forces fresh frames, rationed by the wall clock") {
    gpu_rig::Clock c;
    double t = 0;
    c.g.now = [&t] { return t; };
    CHECK_EQ(c.g.read(GPU_FRAME), 0);
    CHECK_EQ(c.g.read(GPU_FRAME), 0);  // off: the clock is honest
    c.g.fastFrame = true;
    CHECK_EQ(c.g.read(GPU_FRAME), 1);  // the act of asking advanced it
    CHECK_EQ(c.g.read(GPU_FRAME), 1);  // same instant: rationed
    t += 120;
    CHECK_EQ(c.g.read(GPU_FRAME), 2);  // the gap passed: forced again
    CHECK_EQ(c.g.frame(), 2);          // frame() agrees with what reads saw
    c.g.fastFrame = false;
    t += 1000;
    CHECK_EQ(c.g.read(GPU_FRAME), 2);  // boost stays, no further bumps
  }

  TEST_CASE("gap 0 forces on every read, the deterministic setting") {
    gpu_rig::Clock c;
    c.g.fastFrame = true;
    c.g.fastFrameGapMs = 0;
    CHECK_EQ(c.g.read(GPU_FRAME), 1);
    CHECK_EQ(c.g.read(GPU_FRAME), 2);
  }

  TEST_CASE("powerOn clears the boost but keeps the switch") {
    gpu_rig::Clock c;
    c.g.fastFrame = true;
    c.g.fastFrameGapMs = 0;
    c.g.read(GPU_FRAME);
    c.g.read(GPU_FRAME);
    c.g.powerOn();
    CHECK(c.g.fastFrame);       // the switch belongs to the user
    CHECK_EQ(c.g.frame(), 0);  // the forced frames do not survive restart
  }

  TEST_CASE("forwards unclaimed ports to the fallback bus") {
    LogIoBus fallback;
    Gpu g([] { return uint64_t{0}; }, &fallback);
    g.write(0x40, 99);
    CHECK(fallback.log == std::vector<LogIoBus::Entry>{{0x40, 99}});
    CHECK_EQ(g.read(0x40), 0);
  }

  TEST_CASE("powerOn restores power-on state") {
    gpu_rig::Clock c;
    cmdv(c.g, CMD_PLOT, cat(xy(1, 0), 5));
    auto ram = gpu_rig::newRam();
    c.g.attachRam(ram->data());
    (*ram)[0] = 123;
    cmd(c.g, CMD_MEMMAP, {MAP_PALETTE_IN, 0, 0});
    cmd(c.g, CMD_FETCH_PALETTE);
    c.g.powerOn();
    CHECK(allZero(c.g.vram));
    CHECK_EQ(c.g.palette[0], 0);
    CHECK_EQ(c.g.x(), 0);
  }

  TEST_CASE("composes the frame through the effective palette as RGBA") {
    gpu_rig::Clock c;
    cmdv(c.g, CMD_PLOT, cat(xy(1, 0), 0xe0));  // pure red
    std::vector<uint8_t> rgba(static_cast<size_t>(SCREEN_W) * SCREEN_H * 4);
    c.g.compose(rgba);
    CHECK(std::vector<uint8_t>(rgba.begin(), rgba.begin() + 4) == std::vector<uint8_t>{0, 0, 0, 255});
    CHECK(std::vector<uint8_t>(rgba.begin() + 4, rgba.begin() + 8) == std::vector<uint8_t>{255, 0, 0, 255});
  }
}

TEST_SUITE("gpu from assembly") {
  TEST_CASE("a program plots through the data ports and one command") {
    // Load the arguments, then run the command. Every drawing the machine
    // can do has this one shape now.
    const Assembled a = assemble(
        "        OUT GPU_DATA1, 100\n"
        "        OUT GPU_DATA3, 50\n"
        "        OUT GPU_DATA4, 7\n"
        "        OUT GPU_CMD, CMD_PLOT\n"
        "        LD A <- 200\n"
        "        OUTA GPU_DATA1\n"
        "        OUTA GPU_DATA4\n"
        "        OUT GPU_DATA3, 50\n"
        "        OUT GPU_CMD, CMD_PLOT\n"
        "        HLT\n");
    REQUIRE(a.errors.empty());
    Machine m(a.program, buildOptimal());
    Gpu g([&m] { return m.cycles; });
    m.setIo(&g);
    m.run(100);
    CHECK_EQ(m.status, Status::Halted);
    CHECK_EQ(vram(g, 100, 50), 7);
    CHECK_EQ(vram(g, 200, 50), 200);
  }

  TEST_CASE("a loop walks an index with MOD_INC0 and MOD_STICKY") {
    // The idiom the modifier exists for. DATA0 is the index on every command
    // that takes one, so a walk sets the arguments once and lets the
    // modifier step the index.
    const Assembled a = assemble(
        "        OUT GPU_CMD_MOD, MOD_STICKY | MOD_INC0\n"
        "        OUT GPU_DATA0, 5\n"
        "        LD A <- 3\n"
        "        LD [n] <- A\n"
        "loop:   OUT GPU_CMD, CMD_SET_COLOR\n"
        "        LD A <- [n]\n"
        "        SUB A <- 1\n"
        "        LD [n] <- A\n"
        "        JZ done\n"
        "        JMP loop\n"
        "done:   HLT\n"
        ".ram\n"
        "n:      db 0\n");
    REQUIRE(a.errors.empty());
    Machine m(a.program, buildOptimal());
    Gpu g([&m] { return m.cycles; });
    m.setIo(&g);
    m.run(400);
    CHECK_EQ(m.status, Status::Halted);
    // Three runs from 5: the colour ends at 7 and DATA0 stands at 8.
    CHECK_EQ(g.color, 7);
    CHECK_EQ(g.read(GPU_DATA0), 8);
  }
}

TEST_SUITE("gpu drawing commands") {
  TEST_CASE("CLEAR takes its own colour, leaving the pen's alone") {
    gpu_rig::Clock c;
    cmd(c.g, CMD_SET_COLOR, {3});
    cmd(c.g, CMD_CLEAR, {0x55});
    CHECK_EQ(c.g.color, 3);
    CHECK_EQ(c.g.vram[0], 0x55);
    CHECK_EQ(c.g.vram[65535], 0x55);
  }

  TEST_CASE("LINE_TO draws from the pen and the pen follows") {
    gpu_rig::Clock c;
    cmd(c.g, CMD_SET_COLOR, {9});
    moveTo(c.g, 0, 0);
    cmdv(c.g, CMD_LINE_TO, xy(10, 0));
    for (int x = 0; x <= 10; x++) CHECK_EQ(c.g.vram[static_cast<size_t>(x)], 9);
    CHECK_EQ(c.g.penX, 10);
    // Second segment continues without MOVE_TO.
    cmdv(c.g, CMD_LINE_TO, xy(10, 5));
    CHECK_EQ(vram(c.g, 10, 5), 9);
  }

  TEST_CASE("lines clip off-screen instead of wrapping") {
    gpu_rig::Clock c;
    cmd(c.g, CMD_SET_COLOR, {3});
    moveTo(c.g, -10, 0);
    // Nothing to clear: the data ports cleared when MOVE_TO ran.
    cmdv(c.g, CMD_LINE_TO, xy(10, 0));
    CHECK_EQ(c.g.vram[0], 3);
    CHECK_EQ(c.g.vram[10], 3);
    // Nothing landed on the right edge from the negative x walk.
    CHECK_EQ(c.g.vram[SCREEN_W - 1], 0);
  }

  TEST_CASE("RECT fills between pen and target corners, clipped") {
    gpu_rig::Clock c;
    cmd(c.g, CMD_SET_COLOR, {7});
    moveTo(c.g, 250, 250);
    cmdv(c.g, CMD_RECT, xy(266, 252));  // 266 is off the right edge
    CHECK_EQ(vram(c.g, 250, 250), 7);
    CHECK_EQ(vram(c.g, 255, 252), 7);
    CHECK_EQ(vram(c.g, 250, 253), 0);
  }

  TEST_CASE("CIRCLE fills and RING outlines around the pen") {
    gpu_rig::Clock c;
    cmd(c.g, CMD_SET_COLOR, {5});
    moveTo(c.g, 100, 100);
    cmd(c.g, CMD_CIRCLE, {10});
    CHECK_EQ(vram(c.g, 100, 100), 5);  // center
    CHECK_EQ(vram(c.g, 110, 100), 5);  // rightmost point
    CHECK_EQ(vram(c.g, 111, 100), 0);  // just outside
    cmd(c.g, CMD_SET_COLOR, {8});
    cmd(c.g, CMD_RING, {12});
    CHECK_EQ(vram(c.g, 112, 100), 8);  // on the ring
    CHECK_EQ(vram(c.g, 100, 100), 5);  // center untouched
  }

  TEST_CASE("SAVE and RESTORE shift the screen by the latched offset, clipped") {
    gpu_rig::Clock c;
    cmdv(c.g, CMD_PLOT, cat(xy(10, 10), 0x42));
    cmd(c.g, CMD_SAVE_SCREEN);
    cmd(c.g, CMD_CLEAR, {0});
    cmdv(c.g, CMD_RESTORE_SCREEN, xy(5, -3));
    CHECK_EQ(vram(c.g, 15, 7), 0x42);
  }
}

TEST_SUITE("gpu palette rotation") {
  TEST_CASE("manual rotate steps the effective palette within the range") {
    gpu_rig::Clock c;
    // Entries 10..12 get distinct reds, written through RAM.
    auto ram = gpu_rig::newRam();
    c.g.attachRam(ram->data());
    cmd(c.g, CMD_MEMMAP, {MAP_PALETTE, 0, 0});
    cmd(c.g, CMD_STORE_PALETTE);
    (*ram)[10 * 3] = 100;
    (*ram)[11 * 3] = 110;
    (*ram)[12 * 3] = 120;
    cmd(c.g, CMD_FETCH_PALETTE);
    // One command where there were two: setting half a range was never
    // useful on its own.
    cmd(c.g, CMD_ROTATE_RANGE, {10, 12});
    c.g.write(GPU_CMD, CMD_ROTATE_LEFT);
    const Palette pal = c.g.effectivePalette();
    CHECK_EQ(pal[10 * 3], 110);  // entry 10 now shows entry 11
    CHECK_EQ(pal[12 * 3], 100);  // wrapped around
    CHECK_EQ(pal[13 * 3], c.g.palette[13 * 3]);  // outside the range: untouched
  }

  TEST_CASE("auto rotation is clocked by frames and moves the display key") {
    gpu_rig::Clock c;
    cmd(c.g, CMD_ROTATE_SPEED, {2});  // one step every 2 frames
    const std::string k0 = c.g.displayKey();
    c.setCycles(CYCLES_PER_FRAME * 1);
    CHECK_EQ(c.g.displayKey(), k0);  // frame 1: not yet
    c.setCycles(CYCLES_PER_FRAME * 2);
    CHECK_NE(c.g.displayKey(), k0);  // frame 2: stepped, no writes at all
  }

  TEST_CASE("rotate direction flips the sign of auto steps") {
    gpu_rig::Clock c;
    cmd(c.g, CMD_ROTATE_SPEED, {1});
    cmd(c.g, CMD_ROTATE_DIR, {1});  // right
    c.setCycles(CYCLES_PER_FRAME * 3);
    CHECK_EQ(c.g.rotationSteps(), -3);
  }
}

TEST_SUITE("gpu commands from assembly") {
  TEST_CASE("named commands assemble and draw") {
    const Assembled a = assemble(
        "        OUT GPU_DATA0, 200\n"
        "        OUT GPU_CMD, CMD_SET_COLOR\n"
        "        OUT GPU_DATA1, 20\n"
        "        OUT GPU_DATA3, 20\n"
        "        OUT GPU_CMD, CMD_MOVE_TO\n"
        "        OUT GPU_DATA1, 40\n"
        "        OUT GPU_DATA3, 20\n"
        "        OUT GPU_CMD, CMD_LINE_TO\n"
        "        HLT\n");
    REQUIRE(a.errors.empty());
    Machine m(a.program, buildOptimal());
    Gpu g([&m] { return m.cycles; });
    m.setIo(&g);
    m.run(100);
    CHECK_EQ(m.status, Status::Halted);
    CHECK_EQ(vram(g, 30, 20), 200);
  }
}

namespace {

// A sprite blob leads with its frame count, width and height. One command
// defines a plain sprite and an animated strip alike, because the blob says
// which it is.
std::vector<uint8_t> blob(int frames, int w, int h, const std::vector<int>& px) {
  std::vector<uint8_t> out = {static_cast<uint8_t>(frames), static_cast<uint8_t>(w), static_cast<uint8_t>(h)};
  for (int p : px) out.push_back(static_cast<uint8_t>(p));
  return out;
}

struct SpriteRig {
  gpu_rig::Clock clock;
  std::vector<uint8_t> cart;
  Gpu& g = clock.g;
  explicit SpriteRig(std::vector<uint8_t> bytes) : cart(std::move(bytes)) { g.attachCart(cart); }
  void defSprite(int id, int cartAt = 0) { cmdv(g, CMD_SPRITE_DEF, cat({id}, at(cartAt))); }
  void place(int id, int x, int y, int cartAt = 0) {
    defSprite(id, cartAt);
    cmdv(g, CMD_SPRITE_MOVE, cat({id}, xy(x, y)));
    cmd(g, CMD_SPRITE_SHOW, {id});
  }
};

}  // namespace

TEST_SUITE("gpu sprites") {
  TEST_CASE("defines, shows, and composes a sprite with transparency") {
    SpriteRig r(blob(1, 2, 2, {0, 5, 5, 0}));  // diagonal holes
    r.place(3, 0, 0);
    const auto out = r.g.composeFrame();
    CHECK_EQ(out[0], 0);  // transparent
    CHECK_EQ(out[1], 5);
    CHECK_EQ(out[SCREEN_W], 5);
    CHECK_EQ(out[SCREEN_W + 1], 0);
  }

  TEST_CASE("hidden sprites do not compose") {
    SpriteRig r(blob(1, 1, 1, {7}));
    r.defSprite(0);
    cmdv(r.g, CMD_SPRITE_MOVE, cat({0}, xy(4, 4)));
    CHECK_EQ(gpu_rig::px(r.g.composeFrame(), 4, 4), 0);
    cmd(r.g, CMD_SPRITE_SHOW, {0});
    CHECK_EQ(gpu_rig::px(r.g.composeFrame(), 4, 4), 7);
  }

  TEST_CASE("clamps a blob that claims a size past the maximum") {
    // The header is a byte, so it can say 255. The sprite store cannot.
    SpriteRig r(blob(1, 255, 255, {3}));
    r.defSprite(0);
    CHECK_EQ(r.g.sprites[0]->w, 64);
    CHECK_EQ(r.g.sprites[0]->h, 64);
  }

  TEST_CASE("sprites clip at negative coordinates") {
    SpriteRig r(blob(1, 4, 4, std::vector<int>(16, 6)));
    r.place(0, -2, -2);
    const auto out = r.g.composeFrame();
    CHECK_EQ(out[0], 6);                // the visible quarter
    CHECK_EQ(out[2 * SCREEN_W - 1], 0);  // nothing wrapped to the far edge
  }

  TEST_CASE("scroll moves the background but not the sprites") {
    SpriteRig r(blob(1, 1, 1, {5}));
    cmdv(r.g, CMD_PLOT, cat(xy(100, 0), 7));  // background pixel at (100, 0)
    r.place(0, 200, 0);
    r.g.scrollX = 10;
    const auto out = r.g.composeFrame();
    CHECK_EQ(out[90], 7);   // background shifted left by 10
    CHECK_EQ(out[200], 5);  // sprite stays in screen space
  }

  TEST_CASE("defines an animated strip and shows the selected frame") {
    // The same command. Two frames, one pixel each.
    SpriteRig r(blob(2, 1, 1, {5, 6}));
    r.place(0, 40, 40);
    CHECK_EQ(r.g.sprites[0]->frames, 2);
    CHECK_EQ(gpu_rig::px(r.g.composeFrame(), 40, 40), 5);
    cmd(r.g, CMD_SPRITE_FRAME, {0, 1});
    CHECK_EQ(gpu_rig::px(r.g.composeFrame(), 40, 40), 6);
    // The frame wraps, so a counter can run free.
    cmd(r.g, CMD_SPRITE_FRAME, {0, 2});
    CHECK_EQ(gpu_rig::px(r.g.composeFrame(), 40, 40), 5);
  }

  TEST_CASE("flips a sprite horizontally and vertically") {
    // DESIGN: flips, and no rotation. A quarter turn suits a radially
    // symmetric sprite and breaks any sprite with a fixed bottom.
    SpriteRig r(blob(1, 2, 2, {1, 2, 3, 4}));
    r.place(0, 0, 0);
    auto px = [&]() {
      const auto o = r.g.composeFrame();
      return std::vector<int>{o[0], o[1], o[SCREEN_W], o[SCREEN_W + 1]};
    };
    CHECK(px() == std::vector<int>{1, 2, 3, 4});
    cmd(r.g, CMD_SPRITE_FLIP, {0, 1});  // horizontal
    CHECK(px() == std::vector<int>{2, 1, 4, 3});
    cmd(r.g, CMD_SPRITE_FLIP, {0, 2});  // vertical
    CHECK(px() == std::vector<int>{3, 4, 1, 2});
    cmd(r.g, CMD_SPRITE_FLIP, {0, 3});  // both
    CHECK(px() == std::vector<int>{4, 3, 2, 1});
    cmd(r.g, CMD_SPRITE_FLIP, {0, 0});  // back
    CHECK(px() == std::vector<int>{1, 2, 3, 4});
  }

  TEST_CASE("collision: HIT_TEST for a pair, HIT_SCAN for the lowest hit") {
    SpriteRig r(blob(1, 4, 4, std::vector<int>(16, 9)));
    // 0 and 2 overlap, 1 is away.
    r.place(0, 10, 10);
    r.place(1, 100, 100);
    r.place(2, 12, 12);
    // The answer comes back on DATA0, and it exists because the command
    // produced it. The old read port answered whether or not one had run.
    cmd(r.g, CMD_HIT_TEST, {0, 2});
    CHECK_EQ(r.g.read(GPU_DATA0), 1);
    cmd(r.g, CMD_HIT_TEST, {0, 1});
    CHECK_EQ(r.g.read(GPU_DATA0), 0);
    cmd(r.g, CMD_HIT_SCAN, {2});
    CHECK_EQ(r.g.read(GPU_DATA0), 0);  // 2 hits sprite 0, the lowest
    cmd(r.g, CMD_SPRITE_HIDE, {0});
    cmd(r.g, CMD_HIT_SCAN, {2});
    CHECK_EQ(r.g.read(GPU_DATA0), 255);  // none
  }

  TEST_CASE("CMD_COLLIDE_ALL writes the whole table into RAM") {
    // A table is 256 bytes, so it goes to RAM rather than a byte at a time
    // through a port. The program then walks it with a pointer, which is one
    // instruction per sprite instead of two.
    SpriteRig r(blob(1, 4, 4, std::vector<int>(16, 9)));
    auto ram = gpu_rig::newRam();
    r.g.attachRam(ram->data());
    const int TABLE = 0x0400;
    cmd(r.g, CMD_MEMMAP, {MAP_COLLIDE, TABLE >> 8, TABLE & 0xff});
    r.place(1, 10, 10);
    r.place(2, 12, 12);
    r.place(3, 100, 100);
    cmd(r.g, CMD_COLLIDE_ALL);
    CHECK_EQ((*ram)[TABLE + 1], 2);  // 1 overlaps 2
    CHECK_EQ((*ram)[TABLE + 2], 1);  // 2 overlaps 1, the lowest
    CHECK_EQ((*ram)[TABLE + 3], 0);  // 3 is alone: zero means none
    CHECK_EQ((*ram)[TABLE + 0], 0);  // sprite 0 unused: it is the none value
  }

  TEST_CASE("CMD_COLLIDE_ALL does nothing with no mapping, rather than guessing") {
    SpriteRig r(blob(1, 4, 4, std::vector<int>(16, 9)));
    auto ram = gpu_rig::newRam();
    r.g.attachRam(ram->data());
    r.place(1, 10, 10);
    r.place(2, 12, 12);
    cmd(r.g, CMD_COLLIDE_ALL);
    CHECK(allZero(std::vector<uint8_t>(ram->begin(), ram->begin() + 512)));
  }

  TEST_CASE("CMD_COLLIDE_ALL sees an invader bomb hit the cannon on a shared column") {
    // The Space Invaders case Eddie asked to pin down. A falling bomb and the
    // cannon share an x column, and their y spans overlap.
    const auto cannon = blob(1, 11, 7, std::vector<int>(77, 9));
    const auto bomb = blob(1, 2, 6, std::vector<int>(12, 9));
    std::vector<uint8_t> both = cannon;
    both.insert(both.end(), bomb.begin(), bomb.end());
    SpriteRig r(both);
    auto ram = gpu_rig::newRam();
    r.g.attachRam(ram->data());
    const int TABLE = 0x0400;
    cmd(r.g, CMD_MEMMAP, {MAP_COLLIDE, TABLE >> 8, TABLE & 0xff});
    r.place(19, 100, 200, 0);
    r.place(21, 104, 203, static_cast<int>(cannon.size()));
    cmd(r.g, CMD_COLLIDE_ALL);
    CHECK_EQ((*ram)[TABLE + 19], 21);  // the cannon reports the bomb
    CHECK_EQ((*ram)[TABLE + 21], 19);  // the bomb reports the cannon
    // Move the bomb one column past the cannon's right edge: no overlap.
    cmdv(r.g, CMD_SPRITE_MOVE, cat({21}, xy(111, 203)));
    cmd(r.g, CMD_COLLIDE_ALL);
    CHECK_EQ((*ram)[TABLE + 19], 0);
    CHECK_EQ((*ram)[TABLE + 21], 0);
  }

  TEST_CASE("STAMP bakes into vram, and a palette blob carries its entry count") {
    const auto sprite = blob(1, 2, 2, {8, 8, 8, 8});
    std::vector<uint8_t> pal = {256 & 0xff};  // 256 entries, as a byte: 0 means 256
    for (int i = 0; i < 768; i++) pal.push_back(static_cast<uint8_t>(i & 0xff));
    std::vector<uint8_t> both = sprite;
    both.insert(both.end(), pal.begin(), pal.end());
    SpriteRig r(both);
    r.defSprite(0);
    cmdv(r.g, CMD_STAMP, cat({0}, xy(30, 30)));
    CHECK_EQ(vram(r.g, 30, 30), 8);  // baked in, no sprite needed
    cmdv(r.g, CMD_LOAD_PALETTE, at(static_cast<int>(sprite.size())));
    CHECK_EQ(r.g.palette[0], 0);
    CHECK_EQ(r.g.palette[10], 10);
    CHECK_EQ(r.g.palette[300], 300 & 0xff);
  }

  TEST_CASE("blits a cartridge image at the pen, with no sprite spent on it") {
    // The background case, which had no command at all. The blob leads with
    // its width and height, so the command takes only a pointer.
    SpriteRig r({3, 2, 1, 2, 3, 4, 5, 6});
    moveTo(r.g, 10, 20);
    cmdv(r.g, CMD_BLIT, at(0));
    CHECK_EQ(vram(r.g, 10, 20), 1);
    CHECK_EQ(vram(r.g, 12, 20), 3);
    CHECK_EQ(vram(r.g, 11, 21), 5);
  }
}

TEST_SUITE("gpu paths") {
  TEST_CASE("the sine table hits the cardinal points") {
    gpu_rig::Clock c;
    c.g.rotZA = 0;
    CHECK(c.g.project2(255, 128) == std::array<int, 2>{c.g.x() + (126 * 255) / 256, c.g.y()});
  }

  TEST_CASE("DRAW_PATH draws a transformed polyline from the cartridge") {
    // The blob leads with its point count, so the command takes a pointer
    // and nothing else. The path draws at the pen, like every other drawing
    // command.
    SpriteRig r({2, 0, 128, 255, 128});  // left mid, right mid
    moveTo(r.g, 128, 128);
    cmd(r.g, CMD_SET_COLOR, {6});
    cmdv(r.g, CMD_DRAW_PATH, at(0));
    const auto p0 = r.g.project2(0, 128);
    const auto p1 = r.g.project2(255, 128);
    CHECK_EQ(vram(r.g, p0[0], p0[1]), 6);
    CHECK_EQ(vram(r.g, p1[0], p1[1]), 6);
    CHECK_EQ(vram(r.g, 128, 128), 6);  // passes through the center
  }

  TEST_CASE("z rotation by a quarter turn makes a horizontal path vertical") {
    gpu_rig::Clock c;
    moveTo(c.g, 128, 128);
    cmd(c.g, CMD_ROT_Z, {64});
    const auto p = c.g.project2(255, 128);
    CHECK_EQ(p[0], 128);  // no horizontal reach left
    CHECK_GT(p[1], 240);  // it all went vertical
  }

  TEST_CASE("scale shrinks the drawn shape") {
    gpu_rig::Clock c;
    moveTo(c.g, 128, 128);
    cmd(c.g, CMD_SET_SCALE, {64});
    const auto p = c.g.project2(255, 128);
    CHECK_LT(p[0] - 128, 35);  // quarter of the full-scale reach
  }

  TEST_CASE("perspective grows near vertices and shrinks far ones") {
    gpu_rig::Clock c;
    moveTo(c.g, 128, 128);
    const int nearX = c.g.project3(255, 128, 0)[0];    // z = -128: close to the eye
    const int farX = c.g.project3(255, 128, 255)[0];   // z = +127: far away
    CHECK_GT(nearX - 128, farX - 128);
    CHECK_GT(farX, 128);
  }

  TEST_CASE("DRAW_PATH3D draws clipped and never crashes on wild values") {
    SpriteRig r({0, 0, 0, 255, 255, 255});
    moveTo(r.g, 0, 0);  // shape center at the corner: most of it clips
    cmd(r.g, CMD_SET_COLOR, {9});
    cmd(r.g, CMD_DRAW_PATH3D, {2});
    CHECK_GT(r.g.version, 0);  // drew without incident
  }

  TEST_CASE("powerOn resets the transform") {
    gpu_rig::Clock c;
    cmd(c.g, CMD_ROT_Z, {33});
    cmd(c.g, CMD_SET_SCALE, {10});
    c.g.powerOn();
    CHECK_EQ(c.g.rotZA, 0);
    CHECK_EQ(c.g.pathScale, 255);
  }

  TEST_CASE("a sin-generated circle feeds DRAW_PATH directly") {
    // 9 points around the circle, closed: radius 0.5 around center 0.5.
    std::string items;
    for (int k = 0; k <= 8; k++) {
      char t[16];
      std::snprintf(t, sizeof t, "%.3f", k / 8.0);
      items += std::string(k ? ", " : "") + "db cos(" + t + "), db sin(" + t + ")";
    }
    // The blob leads with its point count, authored beside the points
    // rather than typed into the source that draws them.
    const Assembled a = assemble("        HLT\n.data\ncirc: db 9, " + items + "\n");
    REQUIRE(a.errors.empty());
    gpu_rig::Clock c;
    c.g.attachCart(a.cart);
    moveTo(c.g, 128, 128);
    cmd(c.g, CMD_SET_COLOR, {4});
    cmdv(c.g, CMD_DRAW_PATH, at(0));
    // The first point: cos(0) = 255, sin(0) = 128, wherever project2 puts it.
    const auto p = c.g.project2(255, 128);
    CHECK_EQ(vram(c.g, p[0], p[1]), 4);
    CHECK_EQ(vram(c.g, 128, 128), 0);  // hollow circle
  }
}

TEST_SUITE("the default palette") {
  TEST_CASE("is 3-3-2 and is handed out as a fresh copy") {
    Palette a = default332();
    CHECK_EQ(a.size(), 768u);
    CHECK(pal3(a, 0) == std::vector<int>{0, 0, 0});        // index 0 is black
    CHECK(pal3(a, 255) == std::vector<int>{255, 255, 255});  // 255 is white
    // Index 0b11100000 is full red, nothing else.
    CHECK(pal3(a, 224) == std::vector<int>{255, 0, 0});
    a[0] = 99;
    CHECK_EQ(default332()[0], 0);  // callers cannot poison the next copy
  }
}

// gpu_ports.h publishes the names as text for the assembler and gpu.h holds
// the numbers the implementation dispatches on. This walks every table and
// refuses a name that resolves to two values, in both directions.
TEST_SUITE("the command and port tables") {
  struct Named {
    std::string_view name;
    int value;
  };
  constexpr Named PORT_ENUM[] = {
      {"GPU_CMD", GPU_CMD},     {"GPU_CMD_MOD", GPU_CMD_MOD}, {"GPU_DATA0", GPU_DATA0}, {"GPU_DATA1", GPU_DATA1},
      {"GPU_DATA2", GPU_DATA2}, {"GPU_DATA3", GPU_DATA3},     {"GPU_DATA4", GPU_DATA4}, {"GPU_DATA5", GPU_DATA5},
      {"GPU_DATA6", GPU_DATA6}, {"GPU_RAND", GPU_RAND},       {"GPU_FRAME", GPU_FRAME},
  };
  constexpr Named ALIAS_ENUM[] = {
      {"GPU_X_HI", GPU_X_HI},
      {"GPU_X", GPU_X},
      {"GPU_Y_HI", GPU_Y_HI},
      {"GPU_Y", GPU_Y},
      {"GPU_PIXEL", GPU_PIXEL},
      {"GPU_COLOR", GPU_COLOR},
      {"GPU_RADIUS", GPU_RADIUS},
      {"GPU_CART_BANK", GPU_CART_BANK},
      {"GPU_CART_HI", GPU_CART_HI},
      {"GPU_CART_LO", GPU_CART_LO},
      {"GPU_SRC_BANK", GPU_SRC_BANK},
      {"GPU_SRC_HI", GPU_SRC_HI},
      {"GPU_SRC_LO", GPU_SRC_LO},
      {"GPU_ADDR_HI", GPU_ADDR_HI},
      {"GPU_ADDR_LO", GPU_ADDR_LO},
      {"GPU_COUNT_HI", GPU_COUNT_HI},
      {"GPU_COUNT_LO", GPU_COUNT_LO},
      {"GPU_FROM_HI", GPU_FROM_HI},
      {"GPU_FROM_LO", GPU_FROM_LO},
      {"GPU_DEST_HI", GPU_DEST_HI},
      {"GPU_DEST_LO", GPU_DEST_LO},
      {"GPU_LEN_HI", GPU_LEN_HI},
      {"GPU_LEN_LO", GPU_LEN_LO},
      {"GPU_MAP", GPU_MAP},
      {"GPU_MAP_HI", GPU_MAP_HI},
      {"GPU_MAP_LO", GPU_MAP_LO},
      {"GPU_SPRITE", GPU_SPRITE},
      {"GPU_SPRITE_B", GPU_SPRITE_B},
      {"GPU_SPRITE_FRAME", GPU_SPRITE_FRAME},
      {"GPU_SPRITE_FLIP", GPU_SPRITE_FLIP},
      {"GPU_SPRITE_X_HI", GPU_SPRITE_X_HI},
      {"GPU_SPRITE_X", GPU_SPRITE_X},
      {"GPU_SPRITE_Y_HI", GPU_SPRITE_Y_HI},
      {"GPU_SPRITE_Y", GPU_SPRITE_Y},
      {"GPU_HIT", GPU_HIT},
      {"GPU_GROUP", GPU_GROUP},
      {"GPU_GROUP_B", GPU_GROUP_B},
      {"GPU_HIT_FROM", GPU_HIT_FROM},
      {"GPU_SPRITE_GROUP", GPU_SPRITE_GROUP},
      {"GPU_ANGLE", GPU_ANGLE},
      {"GPU_SCALE", GPU_SCALE},
      {"GPU_SPEED", GPU_SPEED},
      {"GPU_DIR", GPU_DIR},
      {"GPU_FIRST", GPU_FIRST},
      {"GPU_LAST", GPU_LAST},
      {"GPU_TEXT_COLOR", GPU_TEXT_COLOR},
      {"GPU_TEXT_BG", GPU_TEXT_BG},
      {"GPU_TEXT_FLAGS", GPU_TEXT_FLAGS},
      {"GPU_TEXT_COL", GPU_TEXT_COL},
      {"GPU_TEXT_ROW", GPU_TEXT_ROW},
      {"GPU_TEXT_CHAR", GPU_TEXT_CHAR},
      {"GPU_TEXT_ARG_HI", GPU_TEXT_ARG_HI},
      {"GPU_TEXT_ARG_LO", GPU_TEXT_ARG_LO},
      {"GPU_MESH", GPU_MESH},
      {"GPU_MESH_BYTE", GPU_MESH_BYTE},
      {"GPU_RAMP", GPU_RAMP},
      {"GPU_RED", GPU_RED},
      {"GPU_GREEN", GPU_GREEN},
      {"GPU_BLUE", GPU_BLUE},
  };
  constexpr Named CMD_ENUM[] = {
      {"CMD_CLEAR", CMD_CLEAR},
      {"CMD_SET_COLOR", CMD_SET_COLOR},
      {"CMD_MOVE_TO", CMD_MOVE_TO},
      {"CMD_LINE_TO", CMD_LINE_TO},
      {"CMD_RECT", CMD_RECT},
      {"CMD_CIRCLE", CMD_CIRCLE},
      {"CMD_RING", CMD_RING},
      {"CMD_PLOT", CMD_PLOT},
      {"CMD_READ_PIXEL", CMD_READ_PIXEL},
      {"CMD_SAVE_SCREEN", CMD_SAVE_SCREEN},
      {"CMD_RESTORE_SCREEN", CMD_RESTORE_SCREEN},
      {"CMD_RESET_PALETTE", CMD_RESET_PALETTE},
      {"CMD_LOAD_PALETTE", CMD_LOAD_PALETTE},
      {"CMD_STORE_PALETTE", CMD_STORE_PALETTE},
      {"CMD_FETCH_PALETTE", CMD_FETCH_PALETTE},
      {"CMD_ROTATE_LEFT", CMD_ROTATE_LEFT},
      {"CMD_ROTATE_RIGHT", CMD_ROTATE_RIGHT},
      {"CMD_ROTATE_SPEED", CMD_ROTATE_SPEED},
      {"CMD_ROTATE_DIR", CMD_ROTATE_DIR},
      {"CMD_ROTATE_RANGE", CMD_ROTATE_RANGE},
      {"CMD_SPRITE_DEF", CMD_SPRITE_DEF},
      {"CMD_SPRITE_MOVE", CMD_SPRITE_MOVE},
      {"CMD_SPRITE_SHOW", CMD_SPRITE_SHOW},
      {"CMD_SPRITE_HIDE", CMD_SPRITE_HIDE},
      {"CMD_SPRITE_FRAME", CMD_SPRITE_FRAME},
      {"CMD_SPRITE_FLIP", CMD_SPRITE_FLIP},
      {"CMD_STAMP", CMD_STAMP},
      {"CMD_BLIT", CMD_BLIT},
      {"CMD_HIT_TEST", CMD_HIT_TEST},
      {"CMD_HIT_SCAN", CMD_HIT_SCAN},
      {"CMD_COLLIDE_ALL", CMD_COLLIDE_ALL},
      {"CMD_SPRITE_HITS", CMD_SPRITE_HITS},
      {"CMD_GROUP_HITS", CMD_GROUP_HITS},
      {"CMD_HIT_IN_GROUP", CMD_HIT_IN_GROUP},
      {"CMD_COLLIDE_GROUP_ALL", CMD_COLLIDE_GROUP_ALL},
      {"CMD_ROT_X", CMD_ROT_X},
      {"CMD_ROT_Y", CMD_ROT_Y},
      {"CMD_ROT_Z", CMD_ROT_Z},
      {"CMD_SET_SCALE", CMD_SET_SCALE},
      {"CMD_DRAW_PATH", CMD_DRAW_PATH},
      {"CMD_DRAW_PATH3D", CMD_DRAW_PATH3D},
      {"CMD_SET_TEXTMODE", CMD_SET_TEXTMODE},
      {"CMD_SET_GRAPHICSMODE", CMD_SET_GRAPHICSMODE},
      {"CMD_TEXT_STYLE", CMD_TEXT_STYLE},
      {"CMD_TEXT_AT", CMD_TEXT_AT},
      {"CMD_TEXT_CHAR", CMD_TEXT_CHAR},
      {"CMD_TEXT_CLEAR", CMD_TEXT_CLEAR},
      {"CMD_PRINTF", CMD_PRINTF},
      {"CMD_LOAD_FONT", CMD_LOAD_FONT},
      {"CMD_SET_WORLDMODE", CMD_SET_WORLDMODE},
      {"CMD_MESH_LOAD", CMD_MESH_LOAD},
      {"CMD_MESH_WRITE", CMD_MESH_WRITE},
      {"CMD_WORLD_RAMP", CMD_WORLD_RAMP},
      {"CMD_MATRIX_MAP", CMD_MATRIX_MAP},
      {"CMD_COPY", CMD_COPY},
      {"CMD_MEMMAP", CMD_MEMMAP},
      {"CMD_RAM_MOVE", CMD_RAM_MOVE},
  };
  constexpr Named MOD_ENUM[] = {{"MOD_STICKY", MOD_STICKY}, {"MOD_INC0", MOD_INC0}};
  constexpr Named MAP_ENUM[] = {
      {"MAP_PALETTE_IN", MAP_PALETTE_IN}, {"MAP_PALETTE_OUT", MAP_PALETTE_OUT}, {"MAP_PALETTE", MAP_PALETTE},
      {"MAP_COLLIDE", MAP_COLLIDE},       {"MAP_GROUPS", MAP_GROUPS},
  };
  constexpr Named MODE_ENUM[] = {
      {"MODE_GRAPHICS", MODE_GRAPHICS}, {"MODE_TEXT", MODE_TEXT}, {"MODE_WORLD", MODE_WORLD}};
  constexpr Named WORLD_FLAG_ENUM[] = {
      {"OBJ_FLAG_ACTIVE", OBJ_FLAG_ACTIVE},
      {"OBJ_FLAG_MATRIX", OBJ_FLAG_MATRIX},
      {"CAM_FLAG_VIEW_MATRIX", CAM_FLAG_VIEW_MATRIX},
      {"CAM_FLAG_PROJ_MATRIX", CAM_FLAG_PROJ_MATRIX},
  };

  template <size_t N, size_t M>
  void checkTable(const NamedValue (&table)[N], const Named (&enumerators)[M]) {
    CHECK_EQ(N, M);
    for (const NamedValue& entry : table) {
      bool found = false;
      for (const Named& e : enumerators) {
        if (e.name != entry.name) continue;
        found = true;
        CHECK_MESSAGE(e.value == entry.value, std::string(entry.name), " differs between gpu_ports.h and gpu.h");
      }
      CHECK_MESSAGE(found, std::string(entry.name), " is published but gpu.h has no enumerator for it");
    }
    for (const Named& e : enumerators) {
      bool found = false;
      for (const NamedValue& entry : table) {
        if (entry.name == e.name) found = true;
      }
      CHECK_MESSAGE(found, std::string(e.name), " is in gpu.h but gpu_ports.h does not publish it");
    }
  }

  TEST_CASE("every port name resolves to the same number") { checkTable(PORTS, PORT_ENUM); }
  TEST_CASE("every alias resolves to the same port") { checkTable(ALIASES, ALIAS_ENUM); }
  TEST_CASE("every command name resolves to the same number") { checkTable(CMDS, CMD_ENUM); }
  TEST_CASE("every modifier, map, mode and flag resolves to the same number") {
    checkTable(MODS, MOD_ENUM);
    checkTable(MAPS, MAP_ENUM);
    checkTable(MODES, MODE_ENUM);
    checkTable(WORLD_FLAGS, WORLD_FLAG_ENUM);
  }

  // A duplicated opcode is silent: the first matching case in the dispatch
  // switch wins and the later command becomes unreachable. CMD_ROT_X sat on
  // CMD_COLLIDE_ALL's number for exactly that reason, so the cube demo never
  // rotated on x.
  TEST_CASE("gives every command its own opcode") {
    std::map<int, std::string_view> seen;
    for (const NamedValue& c : CMDS) {
      CHECK_MESSAGE(seen.count(c.value) == 0, std::string(c.name), " and ", std::string(seen[c.value]), " share an opcode");
      seen[c.value] = c.name;
    }
  }

  TEST_CASE("gives every port its own number") {
    std::map<int, std::string_view> seen;
    for (const NamedValue& p : PORTS) {
      CHECK_MESSAGE(seen.count(p.value) == 0, std::string(p.name), " and ", std::string(seen[p.value]), " share a port");
      seen[p.value] = p.name;
    }
  }

  TEST_CASE("lists the arguments of every command, and every alias it names") {
    std::set<std::string_view> cmds;
    for (const NamedValue& c : CMDS) cmds.insert(c.name);
    std::set<std::string_view> aliases;
    for (const NamedValue& a : ALIASES) aliases.insert(a.name);
    CHECK_EQ(CMD_ARGS.size(), cmds.size());
    for (const CmdArgs& entry : CMD_ARGS) {
      CHECK_MESSAGE(cmds.count(entry.cmd) == 1, std::string(entry.cmd), " is not a command");
      for (std::string_view a : entry.args()) CHECK_MESSAGE(aliases.count(a) == 1, std::string(a), " is not an alias");
    }
    for (std::string_view r : RESULT_CMDS) CHECK_MESSAGE(cmds.count(r) == 1, std::string(r), " is not a command");
  }
}

// docs/design/gpu-ports.md is the reference that travels with the
// repository, and it is written by hand. Pin it: every port and command
// appears, at the number the machine gives it.
TEST_SUITE("docs/design/gpu-ports.md") {
  std::map<std::string, std::string> rowsOf(const std::string& doc) {
    std::map<std::string, std::string> rows;
    const std::regex row(R"(^\|\s*(0x[0-9a-fA-F]+)\s*\|\s*([A-Z_0-9]+)\s*\|)");
    std::istringstream in(doc);
    std::string line;
    while (std::getline(in, line)) {
      std::smatch m;
      if (std::regex_search(line, m, row)) {
        std::string num = m[1].str();
        std::transform(num.begin(), num.end(), num.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        rows[m[2].str()] = num;
      }
    }
    return rows;
  }

  std::string hex(int v) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "0x%02x", v);
    return buf;
  }

  TEST_CASE("lists every GPU command at its real opcode") {
    const auto rows = rowsOf(readDoc("gpu-ports.md"));
    REQUIRE_FALSE(rows.empty());
    for (const NamedValue& c : CMDS) {
      REQUIRE_MESSAGE(rows.count(std::string(c.name)) == 1, std::string(c.name), " missing from the reference");
      CHECK_MESSAGE(rows.at(std::string(c.name)) == hex(c.value), std::string(c.name), " documented at the wrong opcode");
    }
  }

  TEST_CASE("lists every GPU port at its real number") {
    const auto rows = rowsOf(readDoc("gpu-ports.md"));
    for (const NamedValue& p : PORTS) {
      REQUIRE_MESSAGE(rows.count(std::string(p.name)) == 1, std::string(p.name), " missing from the reference");
      CHECK_MESSAGE(rows.at(std::string(p.name)) == hex(p.value), std::string(p.name), " documented at the wrong port");
    }
  }
}

TEST_SUITE("CMD_COPY, cartridge to data RAM") {
  // The CPU cannot read the cartridge. This is how ROM data reaches it: the
  // CPU names the addresses, the GPU moves the bytes, the CPU reads RAM.
  struct Copier {
    gpu_rig::Clock clock;
    std::vector<uint8_t> cart;
    std::unique_ptr<gpu_rig::Bytes> ram = gpu_rig::newRam();
    Gpu& g = clock.g;
    explicit Copier(std::vector<uint8_t> bytes, uint8_t fill = 0) : cart(std::move(bytes)) {
      std::fill(ram->begin(), ram->end(), fill);
      g.powerOn();
      g.attachCart(cart);
      g.attachRam(ram->data());
    }
    // The widest command in the machine, and the reason there are seven data
    // ports: a raw copy has no blob to carry a header, so its length is an
    // argument. Everything high byte first, as the CPU stores a word.
    void copy(int src, int dst, int len) {
      cmdv(g, CMD_COPY, cat(at(src), {(dst >> 8) & 0xff, dst & 0xff, (len >> 8) & 0xff, len & 0xff}));
    }
    std::vector<int> slice(int from, int to) const {
      return std::vector<int>(ram->begin() + from, ram->begin() + to);
    }
  };

  TEST_CASE("moves the bytes, and exactly the length asked for") {
    std::vector<uint8_t> cart(64);
    for (size_t i = 0; i < 64; i++) cart[i] = static_cast<uint8_t>(i + 1);
    Copier c(cart);
    c.copy(0, 0x800, 4);
    CHECK(c.slice(0x7ff, 0x805) == std::vector<int>{0, 1, 2, 3, 4, 0});
  }

  TEST_CASE("copies a whole maze in one command") {
    // 868 bytes is a 28 by 31 Pac-Man maze, and the reason this exists.
    Copier c(std::vector<uint8_t>(1024, 0x23));  // '#'
    c.copy(0, 0x1000, 868);
    CHECK_EQ((*c.ram)[0x1000], 0x23);
    CHECK_EQ((*c.ram)[0x1000 + 867], 0x23);
    CHECK_EQ((*c.ram)[0x1000 + 868], 0);  // and not one byte more
  }

  TEST_CASE("takes its source from the bank byte too") {
    std::vector<uint8_t> cart(0x10004);
    cart[0x10000] = 0xab;
    Copier c(cart);
    c.copy(0x10000, 0x40, 1);
    CHECK_EQ((*c.ram)[0x40], 0xab);
  }

  TEST_CASE("wraps the destination at the top of RAM") {
    // No address is illegal, so a copy that runs off the top continues at 0.
    Copier c({1, 2, 3, 4});
    c.copy(0, 0xfffe, 4);
    CHECK(c.slice(0xfffe, 0x10000) == std::vector<int>{1, 2});
    CHECK(c.slice(0, 2) == std::vector<int>{3, 4});
  }

  TEST_CASE("reads zero past the end of the cartridge") {
    Copier c({9, 9}, 0xff);
    c.copy(0, 0x200, 4);
    CHECK(c.slice(0x200, 0x204) == std::vector<int>{9, 9, 0, 0});
  }

  TEST_CASE("copies nothing when the length is zero") {
    Copier c({7, 7, 7}, 0x55);
    c.copy(0, 0x300, 0);
    CHECK_EQ((*c.ram)[0x300], 0x55);
  }

  TEST_CASE("does nothing at all when no RAM is attached") {
    gpu_rig::Clock c;
    const std::vector<uint8_t> cart = {1, 2, 3};
    c.g.powerOn();
    c.g.attachCart(cart);
    cmd(c.g, CMD_COPY, {0, 0, 0, 0, 0, 0, 3});
    CHECK_EQ(c.g.read(GPU_DATA6), 0);  // ran, and cleared its arguments
  }
}
