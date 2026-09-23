#include <doctest.h>

#include "asm/asm.h"
#include "core/machine.h"
#include "core/microcode.h"
#include "devices/input.h"

using namespace sc8;

TEST_SUITE("input bus") {
  TEST_CASE("returns the button byte at IO_CONTROLLER, forwards the rest") {
    LogIoBus fallback;
    InputBus bus(&fallback);
    bus.buttons = 0x10 | 0x04;
    CHECK_EQ(bus.read(0x20), 0x14);
    CHECK_EQ(bus.read(0x40), 0);
    bus.write(0x40, 7);
    CHECK(fallback.log == std::vector<LogIoBus::Entry>{{0x40, 7}});
  }

  TEST_CASE("packs seven distinct bits") {
    int all = 0;
    int count = 0;
    for (const auto& b : input::BUTTONS) {
      CHECK_EQ(all & b.value, 0);
      all |= b.value;
      count++;
    }
    CHECK_EQ(count, 7);
    CHECK_EQ(all, 0x7f);
  }

  TEST_CASE("buffers key events in order, release bit and all") {
    InputBus bus;
    bus.pushKey(65, false);
    bus.pushKey(65, true);
    bus.pushKey(17, false);
    CHECK_EQ(bus.read(0x21), 65);
    CHECK_EQ(bus.read(0x21), 0x80 | 65);
    CHECK_EQ(bus.read(0x21), 17);
    CHECK_EQ(bus.read(0x21), 0);
  }

  TEST_CASE("drops the oldest event past the buffer depth") {
    InputBus bus;
    for (int i = 0; i < input::KEY_QUEUE_MAX + 3; i++) bus.pushKey(static_cast<uint8_t>(i & 0x7f), false);
    CHECK_EQ(bus.read(0x21), 3);
  }

  TEST_CASE("a program reads the live buttons") {
    InputBus input;
    Assembled a = assemble("        IN A <- IO_CONTROLLER\n        HLT\n");
    REQUIRE(a.errors.empty());
    Machine m(a.program, buildNaive(), &input);
    input.buttons = 0x01 | 0x40;
    m.run(10);
    CHECK_EQ(m.acc, 0x41);
  }
}

TEST_SUITE("assembler input constants") {
  TEST_CASE("resolves IO_CONTROLLER and the BTN masks") {
    Assembled a = assemble(
        "        IN A <- IO_CONTROLLER\n        AND A <- BTN_FIRE\n        JZ done\n        AND A <- BTN_UP\ndone:   HLT\n");
    REQUIRE(a.errors.empty());
    CHECK_EQ(a.program[1].operand, 0x10);
  }

  TEST_CASE("the masks work as data and as OUT values too") {
    CHECK(assemble("        OUT 5, BTN_SPACE\n.ram\nm: db BTN_DOWN\n").errors.empty());
  }
}
