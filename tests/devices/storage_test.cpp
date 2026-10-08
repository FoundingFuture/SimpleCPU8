#include <doctest.h>

#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/machine.h"
#include "devices/constants.h"
#include "core/cartridge.h"
#include "devices/storage.h"

using namespace sc8;
using namespace sc8::storage;

// The storage device is the fifth device on the bus. It is the running
// machine's one way at the cartridge's BAS chunk. These tests drive it the
// way a program does. Latch a block, a name and the room. Write STO_CMD.
// Read STO_STATUS and the block.

namespace {

class SpyBus : public IoBus {
 public:
  std::vector<std::pair<uint8_t, uint8_t>> writes;
  std::vector<uint8_t> reads;
  void write(uint8_t port, uint8_t value) override { writes.emplace_back(port, value); }
  uint8_t read(uint8_t port) override {
    reads.push_back(port);
    return 0x5a;
  }
};

constexpr int BLOCK = 0x4000;
constexpr int NAME = 0x2000;

// The device keeps a pointer into its own fallback, so it never moves. A
// rig owns it on the heap and is free to move itself.
struct Rig {
  std::unique_ptr<Storage> dev;
  std::vector<uint8_t> ram;
  BasicSlots slots;
  int changes = 0;

  Rig() : dev(std::make_unique<Storage>()), ram(RAM_SIZE, 0xaa) {
    dev->attachRam(ram.data());
    dev->attach(&slots, [this] { changes++; });
  }
  void out(int port, int value) { dev->write(static_cast<uint8_t>(port), static_cast<uint8_t>(value & 0xff)); }
  uint8_t in(int port) { return dev->read(static_cast<uint8_t>(port)); }

  void putText(int at, std::string_view text) {
    for (size_t i = 0; i < text.size(); i++) ram[static_cast<size_t>(at) + i] = static_cast<uint8_t>(text[i]);
    ram[static_cast<size_t>(at) + text.size()] = 0;
  }
  std::string textAt(int at) {
    std::string out;
    for (size_t i = static_cast<size_t>(at); i < ram.size() && ram[i] != 0; i++) out += static_cast<char>(ram[i]);
    return out;
  }
  void name(std::string_view n) {
    putText(NAME, n);
    out(STO_NAME_HI, NAME >> 8);
    out(STO_NAME_LO, NAME & 0xff);
  }
  void block(int at, int room) {
    out(STO_ADDR_HI, at >> 8);
    out(STO_ADDR_LO, at & 0xff);
    out(STO_LEN_HI, room >> 8);
    out(STO_LEN_LO, room & 0xff);
  }
  uint8_t status() { return in(STO_STATUS); }
  int moved() { return (in(STO_LEN_HI) << 8) | in(STO_LEN_LO); }
};

}  // namespace

TEST_SUITE("storage chain") {
  TEST_CASE("claims $50 to $5F and forwards the rest") {
    SpyBus spy;
    Storage dev(&spy);
    dev.write(0x30, 1);
    dev.write(0x60, 2);
    dev.write(0x50, 3);
    CHECK(spy.writes == std::vector<std::pair<uint8_t, uint8_t>>{{0x30, 1}, {0x60, 2}});
    CHECK(dev.read(0x21) == 0x5a);
    CHECK(spy.reads == std::vector<uint8_t>{0x21});
    CHECK(dev.read(STO_STATUS) == STO_OK);
  }

  TEST_CASE("an unclaimed port in the range reads as zero and takes a write") {
    Rig r;
    r.out(0x5f, 9);
    CHECK(r.in(0x5f) == 0);
  }
}

TEST_SUITE("storage commands") {
  TEST_CASE("SAVE creates a slot from the block up to its NUL") {
    Rig r;
    r.putText(BLOCK, "10 PRINT 1\n20 END\n");
    r.block(BLOCK, 0);
    r.name("PROG");
    r.out(STO_CMD, STO_SAVE);
    CHECK(r.status() == STO_OK);
    CHECK(r.moved() == 18);
    REQUIRE(r.slots.size() == 1);
    CHECK(r.slots[0].first == "PROG");
    CHECK(r.slots[0].second == "10 PRINT 1\n20 END\n");
    CHECK(r.changes == 1);
    CHECK(r.in(STO_COUNT) == 1);
  }

  TEST_CASE("SAVE replaces a slot with the same name in place") {
    Rig r;
    r.slots.emplace_back("A", "old");
    r.slots.emplace_back("B", "other");
    r.putText(BLOCK, "new");
    r.block(BLOCK, 0);
    r.name("A");
    r.out(STO_CMD, STO_SAVE);
    CHECK(r.status() == STO_OK);
    REQUIRE(r.slots.size() == 2);
    CHECK(r.slots[0].second == "new");
    CHECK(r.slots[1].second == "other");
    CHECK(r.changes == 1);
  }

  TEST_CASE("LOAD writes the text and a NUL, and reports the length") {
    Rig r;
    r.slots.emplace_back("A", "10 END\n");
    r.block(BLOCK, 100);
    r.name("A");
    r.out(STO_CMD, STO_LOAD);
    CHECK(r.status() == STO_OK);
    CHECK(r.moved() == 7);
    CHECK(r.textAt(BLOCK) == "10 END\n");
    CHECK(r.ram[BLOCK + 7] == 0);
    CHECK(r.ram[BLOCK + 8] == 0xaa);
    CHECK(r.changes == 0);
  }

  TEST_CASE("LOAD of a missing slot says so and writes nothing") {
    Rig r;
    r.block(BLOCK, 100);
    r.name("NOPE");
    r.out(STO_CMD, STO_LOAD);
    CHECK(r.status() == STO_NOT_FOUND);
    CHECK(r.moved() == 0);
    CHECK(r.ram[BLOCK] == 0xaa);
  }

  // The room counts the NUL. A seven byte text needs eight.
  TEST_CASE("LOAD refuses a block too short for the text and its NUL") {
    Rig r;
    r.slots.emplace_back("A", "10 END\n");
    r.name("A");
    r.block(BLOCK, 7);
    r.out(STO_CMD, STO_LOAD);
    CHECK(r.status() == STO_FULL);
    CHECK(r.ram[BLOCK] == 0xaa);
    r.block(BLOCK, 8);
    r.out(STO_CMD, STO_LOAD);
    CHECK(r.status() == STO_OK);
  }

  TEST_CASE("DELETE drops the named slot") {
    Rig r;
    r.slots.emplace_back("A", "1");
    r.slots.emplace_back("B", "2");
    r.name("A");
    r.out(STO_CMD, STO_DELETE);
    CHECK(r.status() == STO_OK);
    REQUIRE(r.slots.size() == 1);
    CHECK(r.slots[0].first == "B");
    CHECK(r.changes == 1);
    r.out(STO_CMD, STO_DELETE);
    CHECK(r.status() == STO_NOT_FOUND);
    CHECK(r.changes == 1);
  }

  TEST_CASE("CATALOG writes one name per line and counts them") {
    Rig r;
    r.slots.emplace_back("ONE", "1");
    r.slots.emplace_back("TWO", "2");
    r.block(BLOCK, 100);
    r.out(STO_CMD, STO_CATALOG);
    CHECK(r.status() == STO_OK);
    CHECK(r.textAt(BLOCK) == "ONE\nTWO\n");
    CHECK(r.moved() == 8);
    CHECK(r.in(STO_COUNT) == 2);
  }

  TEST_CASE("CATALOG of nothing is an empty text") {
    Rig r;
    r.block(BLOCK, 1);
    r.out(STO_CMD, STO_CATALOG);
    CHECK(r.status() == STO_OK);
    CHECK(r.ram[BLOCK] == 0);
    CHECK(r.in(STO_COUNT) == 0);
  }

  TEST_CASE("CATALOG refuses a short block") {
    Rig r;
    r.slots.emplace_back("ONE", "1");
    r.block(BLOCK, 4);
    r.out(STO_CMD, STO_CATALOG);
    CHECK(r.status() == STO_FULL);
    CHECK(r.ram[BLOCK] == 0xaa);
  }

  TEST_CASE("an unknown command reports STO_BAD_CMD") {
    Rig r;
    r.out(STO_CMD, 0x77);
    CHECK(r.status() == STO_BAD_CMD);
  }

  TEST_CASE("a command with no slots attached fails honestly") {
    Storage dev;
    std::vector<uint8_t> ram(RAM_SIZE, 0);
    dev.attachRam(ram.data());
    ram[NAME] = 'A';
    dev.write(STO_NAME_HI, NAME >> 8);
    dev.write(STO_NAME_LO, NAME & 0xff);
    dev.write(STO_CMD, STO_LOAD);
    CHECK(dev.read(STO_STATUS) == STO_NOT_FOUND);
    dev.write(STO_CMD, STO_SAVE);
    CHECK(dev.read(STO_STATUS) == STO_FULL);
    CHECK(dev.read(STO_COUNT) == 0);
  }
}

TEST_SUITE("storage names") {
  TEST_CASE("refuses an empty name") {
    Rig r;
    r.name("");
    r.out(STO_CMD, STO_SAVE);
    CHECK(r.status() == STO_BAD_NAME);
    CHECK(r.slots.empty());
  }

  TEST_CASE("takes sixteen bytes and refuses seventeen") {
    Rig r;
    r.putText(BLOCK, "x");
    r.block(BLOCK, 0);
    r.name("ABCDEFGHIJKLMNOP");
    r.out(STO_CMD, STO_SAVE);
    CHECK(r.status() == STO_OK);
    r.name("ABCDEFGHIJKLMNOPQ");
    r.out(STO_CMD, STO_SAVE);
    CHECK(r.status() == STO_BAD_NAME);
    CHECK(r.slots.size() == 1);
  }

  TEST_CASE("refuses a space or a control byte in a name") {
    Rig r;
    r.putText(BLOCK, "x");
    r.block(BLOCK, 0);
    r.name("A B");
    r.out(STO_CMD, STO_SAVE);
    CHECK(r.status() == STO_BAD_NAME);
    r.name("A\nB");
    r.out(STO_CMD, STO_SAVE);
    CHECK(r.status() == STO_BAD_NAME);
    CHECK(r.slots.empty());
  }

  // Sixteen bytes of name and no NUL anywhere near: the device stops
  // scanning rather than reading the whole of RAM.
  TEST_CASE("an unterminated name is a bad name") {
    Rig r;
    for (int i = 0; i < 64; i++) r.ram[static_cast<size_t>(NAME + i)] = 'A';
    r.out(STO_NAME_HI, NAME >> 8);
    r.out(STO_NAME_LO, NAME & 0xff);
    r.out(STO_CMD, STO_DELETE);
    CHECK(r.status() == STO_BAD_NAME);
  }
}

TEST_SUITE("storage caps") {
  TEST_CASE("holds 64 slots and refuses the 65th") {
    Rig r;
    r.putText(BLOCK, "x");
    r.block(BLOCK, 0);
    for (int i = 0; i < 64; i++) {
      r.name("S" + std::to_string(i));
      r.out(STO_CMD, STO_SAVE);
      CHECK(r.status() == STO_OK);
    }
    r.name("S64");
    r.out(STO_CMD, STO_SAVE);
    CHECK(r.status() == STO_FULL);
    CHECK(r.slots.size() == 64);
    // Replacing one of the 64 is not a 65th.
    r.name("S0");
    r.out(STO_CMD, STO_SAVE);
    CHECK(r.status() == STO_OK);
  }

  // A wrapping scan of 65535 bytes misses one byte, the one before the
  // block. The name's NUL sits there, so no NUL is in the cap at all.
  TEST_CASE("a block with no NUL inside the cap is not a program") {
    Rig r;
    std::fill(r.ram.begin(), r.ram.end(), 'x');
    r.putText(BLOCK - 2, "A");
    r.out(STO_NAME_HI, (BLOCK - 2) >> 8);
    r.out(STO_NAME_LO, (BLOCK - 2) & 0xff);
    r.block(BLOCK, 0);
    r.out(STO_CMD, STO_SAVE);
    CHECK(r.status() == STO_FULL);
    CHECK(r.slots.empty());
  }
}

TEST_SUITE("storage power on") {
  TEST_CASE("clears the latches and the status, and keeps the slots") {
    Rig r;
    r.slots.emplace_back("A", "1");
    r.block(BLOCK, 100);
    r.name("NOPE");
    r.out(STO_CMD, STO_LOAD);
    CHECK(r.status() == STO_NOT_FOUND);
    r.dev->powerOn();
    CHECK(r.status() == STO_OK);
    CHECK(r.moved() == 0);
    CHECK(r.in(STO_COUNT) == 1);
  }
}

// storage_ports.h publishes the names as text for the assembler.
// storage.h holds the numbers the device dispatches on. Neither can read
// the other. This test walks both and refuses a name with two values.
TEST_SUITE("storage enum matches the published tables") {
  struct Named {
    std::string_view name;
    int value;
  };
  constexpr Named PORT_ENUM[] = {
      {"STO_ADDR_HI", STO_ADDR_HI}, {"STO_ADDR_LO", STO_ADDR_LO}, {"STO_NAME_HI", STO_NAME_HI},
      {"STO_NAME_LO", STO_NAME_LO}, {"STO_LEN_HI", STO_LEN_HI},   {"STO_LEN_LO", STO_LEN_LO},
      {"STO_CMD", STO_CMD},         {"STO_STATUS", STO_STATUS},   {"STO_COUNT", STO_COUNT},
  };
  constexpr Named CMD_ENUM[] = {
      {"STO_LOAD", STO_LOAD},
      {"STO_SAVE", STO_SAVE},
      {"STO_DELETE", STO_DELETE},
      {"STO_CATALOG", STO_CATALOG},
      {"STO_FIND", STO_FIND},
  };
  constexpr Named STATUS_ENUM[] = {
      {"STO_OK", STO_OK},
      {"STO_NOT_FOUND", STO_NOT_FOUND},
      {"STO_FULL", STO_FULL},
      {"STO_BAD_NAME", STO_BAD_NAME},
      {"STO_BAD_CMD", STO_BAD_CMD},
  };

  template <size_t N, size_t M>
  void checkTable(const NamedValue (&table)[N], const Named (&enumerators)[M]) {
    CHECK_EQ(N, M);
    for (const NamedValue& entry : table) {
      bool found = false;
      for (const Named& e : enumerators) {
        if (e.name != entry.name) continue;
        found = true;
        CHECK_MESSAGE(e.value == entry.value, std::string(entry.name),
                      " differs between storage_ports.h and storage.h");
      }
      CHECK_MESSAGE(found, std::string(entry.name), " has no enumerator in storage.h");
    }
  }

  TEST_CASE("every port name resolves to the same number") { checkTable(PORTS, PORT_ENUM); }
  TEST_CASE("every command name resolves to the same number") { checkTable(CMDS, CMD_ENUM); }
  TEST_CASE("every status name resolves to the same number") { checkTable(STATUS, STATUS_ENUM); }

  TEST_CASE("gives every command its own opcode") {
    std::set<int> values;
    size_t count = 0;
    for (const auto& c : CMDS) {
      values.insert(c.value);
      count++;
    }
    CHECK_EQ(values.size(), count);
  }

  TEST_CASE("the claimed range matches the ports") {
    CHECK_EQ(PORT_LO, 0x50);
    CHECK_EQ(PORT_HI, 0x5f);
    for (const NamedValue& p : PORTS) {
      CHECK_GE(p.value, PORT_LO);
      CHECK_LE(p.value, PORT_HI);
    }
  }

  // The assembler and the C compiler read the registry. A table that never
  // reached it would leave STO_LOAD an unknown name.
  TEST_CASE("the registry knows every name") {
    for (const NamedValue& p : PORTS) CHECK(portNamed(p.name) == p.value);
    for (const NamedValue& c : CMDS) CHECK(commandNamed(c.name) == c.value);
    for (const NamedValue& s : STATUS) CHECK(systemConstant(s.name) == s.value);
    for (const NamedValue& k : KINDS) CHECK(systemConstant(k.name) == k.value);
  }

  TEST_CASE("names a kind for every kind the ASET chunk holds, STO_KIND_FONT first") {
    CHECK_EQ(KINDS[0].name, "STO_KIND_FONT");
    CHECK_EQ(KINDS[0].value, 1);
    for (const char* k : {"file", "font", "image", "palette", "sample", "sprite"}) {
      CAPTURE(k);
      CHECK_GT(kindCode(k), 0);
    }
    CHECK_EQ(kindCode("font"), 1);
    CHECK_EQ(kindCode("nonsense"), 0);
  }
}

TEST_SUITE("STO_FIND") {
  // An ASET list as a built ROM carries it.
  std::vector<RomAsset> table() {
    return {{"image", "ship", 0x000010, 64}, {"font", "small", 0x012345, 2050}, {"sample", "boom", 0x200, 0x10203}};
  }

  // The name through STO_NAME, the block at STO_ADDR, then STO_FIND.
  uint8_t find(Rig& r, std::string_view name) {
    r.name(name);
    r.block(BLOCK, 0);
    r.out(STO_CMD, STO_FIND);
    return r.status();
  }

  std::vector<int> answer(Rig& r) {
    std::vector<int> out;
    for (size_t i = 0; i < 7; i++) out.push_back(r.ram[BLOCK + i]);
    return out;
  }

  TEST_CASE("finds a name in any case, and answers kind, address and length, high byte first") {
    Rig r;
    std::vector<RomAsset> assets = table();
    r.dev->attachAssets(&assets);
    CHECK_EQ(find(r, "SMALL"), STO_OK);
    CHECK(answer(r) == std::vector<int>{1, 0x01, 0x23, 0x45, 0x00, 0x08, 0x02});
    CHECK_EQ(r.moved(), 7);
    CHECK_EQ(find(r, "small"), STO_OK);
    CHECK(answer(r) == std::vector<int>{1, 0x01, 0x23, 0x45, 0x00, 0x08, 0x02});
    CHECK_EQ(find(r, "Boom"), STO_OK);
    CHECK(answer(r) == std::vector<int>{kindCode("sample"), 0x00, 0x02, 0x00, 0x01, 0x02, 0x03});
  }

  TEST_CASE("a name the directory lacks is STO_NOT_FOUND, and writes nothing") {
    Rig r;
    std::vector<RomAsset> assets = table();
    r.dev->attachAssets(&assets);
    CHECK_EQ(find(r, "BIG"), STO_NOT_FOUND);
    for (size_t i = 0; i < 7; i++) CHECK_EQ(r.ram[BLOCK + i], 0xaa);
    CHECK_EQ(r.moved(), 0);
  }

  TEST_CASE("takes the name through STO_NAME and leaves it where it was") {
    Rig r;
    std::vector<RomAsset> assets = table();
    r.dev->attachAssets(&assets);
    CHECK_EQ(find(r, "small"), STO_OK);
    CHECK_EQ(r.textAt(NAME), "small");
    CHECK_EQ(r.ram[BLOCK], 1);
  }

  TEST_CASE("a ROM without ASET answers STO_NOT_FOUND for everything") {
    Rig r;
    CHECK_EQ(find(r, "SMALL"), STO_NOT_FOUND);
    std::vector<RomAsset> none;
    r.dev->attachAssets(&none);
    CHECK_EQ(find(r, "SMALL"), STO_NOT_FOUND);
  }

  TEST_CASE("a name of 16 bytes is found, and one of 17 is a bad name") {
    Rig r;
    std::vector<RomAsset> assets = {{"file", "ABCDEFGHIJKLMNOP", 5, 6}};
    r.dev->attachAssets(&assets);
    CHECK_EQ(find(r, "abcdefghijklmnop"), STO_OK);
    CHECK_EQ(find(r, "abcdefghijklmnopq"), STO_BAD_NAME);
    CHECK_EQ(find(r, ""), STO_BAD_NAME);
  }

  TEST_CASE("the answer matches the ASET line of a built cartridge") {
    Rig r;
    Cartridge c;
    c.data.assign(3000, 0);
    c.assets.push_back({"font", "small", 100, 2050});
    const Cartridge back = *decodeCartridge(encodeCartridge(c)).cartridge;
    r.dev->attachAssets(&back.assets);
    CHECK_EQ(find(r, "SMALL"), STO_OK);
    CHECK(answer(r) == std::vector<int>{1, 0, 0, 100, 0, 0x08, 0x02});
  }
}
