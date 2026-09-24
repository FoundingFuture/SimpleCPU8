// The Pac-Man ROM's three screens, driven through the virtual computer: the
// attract screen at power on, a key into the game, the last life into the
// game over screen, and a key back round to the attract screen. Nothing here
// knows a RAM address. The screens are read off the GPU: the text overlay for
// the lines they print and video RAM for the colours only one of them draws.

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

#include "doctest.h"

#include "core/cartridge.h"
#include "devices/input_ports.h"
#include "vm/computer.h"

using namespace sc8;

namespace {

// The text overlay, 42 by 32 cells, as one string with newlines.
std::string overlayText(const Gpu& g) {
  std::string out;
  for (int row = 0; row < 32; row++) {
    for (int col = 0; col < 42; col++) {
      const uint8_t c = g.overlayChar[static_cast<size_t>(row * 42 + col)];
      out += (c >= 32 && c < 127) ? static_cast<char>(c) : ' ';
    }
    out += '\n';
  }
  return out;
}

bool vramHas(const Gpu& g, uint8_t index) {
  return std::find(g.vram.begin(), g.vram.end(), index) != g.vram.end();
}

// The game over letters are the only large patch of red in video RAM.
// Blinky's roster stamp is red too, and a hundred pixels of it.
bool overLetters(const Gpu& g) {
  return std::count(g.vram.begin(), g.vram.end(), static_cast<uint8_t>(0xE0)) > 500;
}

// The first and last screen columns holding the palette index, or -1 when
// none does. Video RAM is 256 by 256, one byte a pixel.
std::pair<int, int> columnSpan(const Gpu& g, uint8_t index) {
  int first = -1;
  int last = -1;
  for (int y = 0; y < 256; y++) {
    for (int x = 0; x < 256; x++) {
      if (g.vram[static_cast<size_t>(y * 256 + x)] != index) continue;
      if (first < 0 || x < first) first = x;
      if (x > last) last = x;
    }
  }
  return {first, last};
}

void runFrames(Computer& c, int frames) {
  for (int i = 0; i < frames; i++) {
    c.pumpTyping();
    c.runToNextFrame();
  }
}

constexpr uint8_t MAZE_WALL = 1;  // palette entry 1 is the wall's blue
constexpr uint8_t BTN_FIRE = 0x10;

}  // namespace

#if defined(SC8_ROM_DIR)
TEST_CASE("pacman: attract screen, a key into the game, game over, a key back round") {
  std::ifstream in(std::string(SC8_ROM_DIR) + "/pacman.rom", std::ios::binary);
  REQUIRE_MESSAGE(in, "pacman.rom is not built");
  std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(in), {});
  CartridgeResult r = decodeCartridge(bytes);
  REQUIRE_MESSAGE(r.cartridge, r.error);

  Computer c;
  c.setSeed(1);
  c.insert(std::move(*r.cartridge));

  // Power on lands on the attract screen: the roster and the prompt, no
  // maze. Frame 70 is inside an "on" half of the prompt's blink.
  runFrames(c, 70);
  std::string screen = overlayText(c.gpu());
  CHECK_MESSAGE(screen.find("PRESS A KEY TO START") != std::string::npos, screen);
  CHECK_MESSAGE(screen.find("BLINKY") != std::string::npos, screen);
  CHECK_FALSE(vramHas(c.gpu(), MAZE_WALL));
  CHECK(c.machine().status == Status::Running);

  // A key starts the game: the strip goes up and the maze is drawn.
  c.typeText(" ");
  runFrames(c, 80);
  screen = overlayText(c.gpu());
  CHECK_MESSAGE(screen.find("SCORE 00000") != std::string::npos, screen);
  CHECK_MESSAGE(screen.find("LIVES 3") != std::string::npos, screen);
  CHECK_MESSAGE(screen.find("PRESS A KEY") == std::string::npos, screen);
  CHECK(vramHas(c.gpu(), MAZE_WALL));

  // The maze is centred: 28 tiles of 8 pixels on a 256 pixel screen leave
  // 16 pixels each side. The outer wall's line sits two pixels inside its
  // tile, so the leftmost blue is column 18 and the rightmost its mirror.
  const auto [wallLeft, wallRight] = columnSpan(c.gpu(), MAZE_WALL);
  CHECK(wallLeft == 18);
  CHECK(wallLeft + wallRight == 255);
  // The strip is the light grey, not the maze's yellow.
  CHECK(c.gpu().textColor == 0xDB);

  // Nobody steers, so Blinky catches him three times. Fire is held down the
  // whole way, which is the held button the game over screen must ignore.
  c.input().buttons = BTN_FIRE;
  int frames = 0;
  while (frames < 6000 && !overLetters(c.gpu())) {
    runFrames(c, 30);
    frames += 30;
  }
  screen = overlayText(c.gpu());
  REQUIRE_MESSAGE(overLetters(c.gpu()), "no game over within 6000 frames: " << screen);
  CHECK_MESSAGE(screen.find("LIVES 0") != std::string::npos, screen);
  CHECK_MESSAGE(screen.find("PRESS A KEY") != std::string::npos, screen);
  CHECK_FALSE(vramHas(c.gpu(), MAZE_WALL));
  CHECK(c.machine().status == Status::Running);

  // The held button does not leave the screen. A fresh press does.
  runFrames(c, 120);
  CHECK(overLetters(c.gpu()));
  c.input().buttons = 0;
  runFrames(c, 5);
  c.input().buttons = BTN_FIRE;
  runFrames(c, 10);
  c.input().buttons = 0;
  screen = overlayText(c.gpu());
  CHECK_FALSE(overLetters(c.gpu()));
  CHECK_MESSAGE(screen.find("PRESS A KEY TO START") != std::string::npos, screen);
  CHECK_MESSAGE(screen.find("SCORE") == std::string::npos, screen);

  // And the next game starts fresh.
  c.typeText("\n");
  runFrames(c, 80);
  screen = overlayText(c.gpu());
  CHECK_MESSAGE(screen.find("SCORE 00000") != std::string::npos, screen);
  CHECK_MESSAGE(screen.find("LIVES 3") != std::string::npos, screen);
  CHECK_MESSAGE(screen.find("LEVEL 1") != std::string::npos, screen);
  CHECK(vramHas(c.gpu(), MAZE_WALL));
  CHECK(c.machine().status == Status::Running);
}
#endif
