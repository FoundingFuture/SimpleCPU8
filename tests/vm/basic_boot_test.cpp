// Booting the BASIC ROM through the virtual computer, the path simplecpu
// --basic takes, with a program typed in through the key queue.

#include <string>

#include "doctest.h"

#include "core/cartridge.h"
#include "vm/computer.h"

#if SC8_HAVE_BASIC
#include "basic/basic_rom.h"
#endif

using namespace sc8;

namespace {

// The text screen, 42 x 32 cells at $FAC0, as one string with newlines.
std::string screenText(const Machine& m) {
  std::string out;
  for (int row = 0; row < 32; row++) {
    std::string line;
    for (int col = 0; col < 42; col++) {
      const uint8_t c = m.ram[static_cast<size_t>(0xFAC0 + row * 42 + col)];
      line += (c >= 32 && c < 127) ? static_cast<char>(c) : ' ';
    }
    while (!line.empty() && line.back() == ' ') line.pop_back();
    out += line + "\n";
  }
  return out;
}

void runFrames(Computer& c, int frames) {
  for (int i = 0; i < frames; i++) {
    c.pumpTyping();
    c.runToNextFrame();
  }
}

}  // namespace

#if SC8_HAVE_BASIC
TEST_SUITE("basic boot") {
  TEST_CASE("boots to READY and runs a typed program") {
    std::vector<uint8_t> bytes(basicRom().begin(), basicRom().end());
    CartridgeResult r = decodeCartridge(bytes);
    REQUIRE(r.cartridge);
    Computer c;
    c.setSeed(1);
    c.insert(std::move(*r.cartridge));
    runFrames(c, 20);
    CHECK(screenText(c.machine()).find("READY") != std::string::npos);

    c.typeText("PRINT 6*7\n");
    runFrames(c, 40);
    CHECK(screenText(c.machine()).find("42") != std::string::npos);
  }

  TEST_CASE("a program in a slot loads with !LOAD, the --basic file path") {
    std::vector<uint8_t> bytes(basicRom().begin(), basicRom().end());
    CartridgeResult r = decodeCartridge(bytes);
    REQUIRE(r.cartridge);
    r.cartridge->basic.emplace_back("HELLO", "10 PRINT \"HELLO FROM A FILE\"\n20 END\n");
    Computer c;
    c.setSeed(1);
    c.insert(std::move(*r.cartridge));
    c.typeText("!LOAD \"HELLO\"\nRUN\n");
    runFrames(c, 120);
    CHECK_MESSAGE(screenText(c.machine()).find("HELLO FROM A FILE") != std::string::npos, screenText(c.machine()));
  }
}
#endif

#if SC8_HAVE_BASIC && defined(SC8_ROM_DIR)
#include <fstream>
#include <iterator>

TEST_CASE("the driver example answers !HELLO from a second PROG segment") {
  std::ifstream in(std::string(SC8_ROM_DIR) + "/basic-driver.rom", std::ios::binary);
  REQUIRE_MESSAGE(in, "basic-driver.rom is not built");
  std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(in), {});
  CartridgeResult r = decodeCartridge(bytes);
  REQUIRE_MESSAGE(r.cartridge, r.error);
  CHECK_EQ(r.cartridge->program[0xF000].op, opByName("LD A <- [D1]+")->op);
  CHECK_EQ(r.cartridge->program[0xEFFF].op, UNLOADED_OP);
  REQUIRE_EQ(r.cartridge->basic.size(), 1u);
  CHECK_EQ(r.cartridge->basic[0].first, "DEMO");

  Computer c;
  c.setSeed(1);
  c.insert(std::move(*r.cartridge));
  c.typeText("!LOAD \"DEMO\"\nRUN\n");
  runFrames(c, 150);
  const std::string screen = screenText(c.machine());
  CHECK_MESSAGE(screen.find("HELLO FROM ASSEMBLY") != std::string::npos, screen);
  CHECK_MESSAGE(screen.find("THE DRIVER ANSWERED") != std::string::npos, screen);
}
#endif
