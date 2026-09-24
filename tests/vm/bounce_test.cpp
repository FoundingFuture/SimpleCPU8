// The bounce example: a C project in the larger layout, built by
// simplecpu-make through the examples target, run through Computer.

#include <fstream>
#include <iterator>
#include <string>

#include "doctest.h"

#include "core/cartridge.h"
#include "vm/computer.h"

using namespace sc8;

#if defined(SC8_ROM_DIR)
TEST_CASE("the bounce example draws its ball and bat in the machine's colours") {
  std::ifstream in(std::string(SC8_ROM_DIR) + "/bounce.rom", std::ios::binary);
  REQUIRE_MESSAGE(in, "bounce.rom is not built");
  std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(in), {});
  CartridgeResult r = decodeCartridge(bytes);
  REQUIRE_MESSAGE(r.cartridge, r.error);
  Computer c;
  c.setSeed(1);
  c.insert(std::move(*r.cartridge));
  for (int i = 0; i < 30; i++) c.runToNextFrame();
  CHECK(c.machine().status == Status::Running);
  // The composed frame is RGBA. Yellow means the ball sprite, cyan the bat.
  const auto& f = c.frame();
  int yellow = 0;
  int cyan = 0;
  for (size_t i = 0; i + 3 < f.size(); i += 4) {
    if (f[i] > 200 && f[i + 1] > 200 && f[i + 2] < 80) yellow++;
    if (f[i] < 80 && f[i + 1] > 200 && f[i + 2] > 200) cyan++;
  }
  CHECK(yellow > 50);
  CHECK(cyan > 50);
}
#endif
