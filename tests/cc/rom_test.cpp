#include <doctest.h>

#include <string>

#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

// __ROM. The cartridge is the device, ROM is what is on it, and C has no
// storage class for memory the CPU cannot see. So the compiler adds one.

namespace {

cc::Program prog(const std::string& src) { return cc::compileProgram({{"main.c", src}}); }

}  // namespace

TEST_SUITE("a __ROM object goes on the cartridge") {
  TEST_CASE("emits its bytes into .data, not into RAM") {
    const std::string a = compile(R"(
      __ROM const unsigned char ship[] = { 1, 8, 8 };
      int main(void) { return 0; }
    )");
    CHECK(has(a.substr(a.find(".data")), "ship:"));
    CHECK(!has(a.substr(0, a.find(".data")), "ship:"));
  }

  TEST_CASE("takes a string, terminator and all") {
    const cc::Program p = prog(R"(
      __ROM const char fmt[] = "SCORE %u";
      int main(void) { return 0; }
    )");
    REQUIRE(p.rom.size() == 1);
    CHECK(p.rom[0].name == "fmt");
    CHECK(p.rom[0].addr == 0);
    CHECK(p.rom[0].size == 9);
  }

  TEST_CASE("lays several out in order") {
    const cc::Program p = prog(R"(
      __ROM const unsigned char a[] = { 1, 2, 3 };
      __ROM const unsigned char b[] = { 9 };
      int main(void) { return 0; }
    )");
    REQUIRE(p.rom.size() == 2);
    CHECK(p.rom[0].name == "a");
    CHECK(p.rom[0].addr == 0);
    CHECK(p.rom[0].size == 3);
    CHECK(p.rom[1].name == "b");
    CHECK(p.rom[1].addr == 3);
    CHECK(p.rom[1].size == 1);
  }

  TEST_CASE("gives two objects with identical bytes one address") {
    const cc::Program p = prog(R"(
      __ROM const unsigned char a[] = { 7, 7 };
      __ROM const unsigned char b[] = { 7, 7 };
      int main(void) { return 0; }
    )");
    REQUIRE(p.rom.size() == 2);
    CHECK(p.rom[0].addr == 0);
    CHECK(p.rom[1].addr == 0);
    CHECK(p.rom[1].sameAs == "a");
  }

  TEST_CASE("implies const, so writing const too is allowed") {
    CHECK(prog(R"(
      __ROM const unsigned char a[] = { 1 };
      int main(void) { return 0; }
    )").rom.size() == 1);
  }
}

TEST_SUITE("ROM.h is the cartridge map") {
  TEST_CASE("gives every object its size and the three address bytes") {
    const std::string h = prog(R"(
      __ROM const unsigned char ship[] = { 1, 2, 3 };
      int main(void) { return 0; }
    )").romHeader;
    for (const char* s : {"ROM_ship_SIZE 3", "ROM_ship_BANK 0x00", "ROM_ship_HI   0x00", "ROM_ship_LO   0x00"}) {
      CHECK_MESSAGE(has(h, s), s);
    }
  }

  TEST_CASE("says where each object was declared") {
    CHECK(has(prog("\n__ROM const unsigned char ship[] = { 1 };\nint main(void) { return 0; }\n").romHeader,
              "declared in main.c line 2"));
  }

  TEST_CASE("says so when two labels share an address") {
    const std::string h = prog(R"(
      __ROM const unsigned char a[] = { 5 };
      __ROM const unsigned char b[] = { 5 };
      int main(void) { return 0; }
    )").romHeader;
    CHECK(has(h, "the same bytes as a"));
  }

  // rom.h holds the address macros and ROM.h is the generated map. The two
  // names differ only in case, so a guard spelled from the filename gave them
  // the same one and the second include did nothing at all.
  TEST_CASE("guards itself under a different name from rom.h") {
    const Ran r = ran(R"(
      #include <rom.h>
      #include "ROM.h"
      __ROM const unsigned char b[] = { 1 };
      unsigned char n, lo;
      int main(void) { n = ROM_b_SIZE; lo = ROM_LO(ROM_b); return 0; }
    )");
    CHECK(r.u8("n") == 1);
    CHECK(r.u8("lo") == 0);
  }

  TEST_CASE("says the cartridge is empty when nothing is __ROM") {
    CHECK(has(prog("int main(void) { return 0; }").romHeader, "cartridge is empty"));
  }

  TEST_CASE("can be included, and its numbers are real by the time codegen reads them") {
    CHECK(ran(R"(
      #include "ROM.h"
      __ROM const unsigned char blob[] = { 1, 2, 3, 4, 5 };
      unsigned char n;
      int main(void) { n = ROM_blob_SIZE; return 0; }
    )").u8("n") == 5);
  }

  TEST_CASE("guards itself, so including it twice is harmless") {
    CHECK(ran(R"(
      #include "ROM.h"
      #include "ROM.h"
      __ROM const unsigned char b[] = { 1 };
      unsigned char n;
      int main(void) { n = ROM_b_SIZE; return 0; }
    )").u8("n") == 1);
  }
}

TEST_SUITE("the CPU cannot read the cartridge, and the error says what to do") {
  TEST_CASE("names rom_copy when a ROM object is indexed") {
    CHECK(has(refuses(R"(
      __ROM const unsigned char ship[] = { 1, 2 };
      unsigned char r;
      int main(void) { r = ship[0]; return 0; }
    )"), "rom_copy"));
  }

  TEST_CASE("names rom_copy when a ROM object is used as a value") {
    CHECK(has(refuses(R"(
      __ROM const unsigned char ship[] = { 1 };
      unsigned char *p;
      int main(void) { p = ship; return 0; }
    )"), "rom_copy"));
  }

  TEST_CASE("refuses a __ROM object with no initializer, and says why") {
    CHECK(has(refuses(R"(
      __ROM const unsigned char ship[4];
      int main(void) { return 0; }
    )"), "assembly time"));
  }
}

TEST_SUITE("rom_copy brings it into RAM, which is the one way to read it") {
  TEST_CASE("copies the bytes and the CPU then reads them") {
    const Ran r = ran(R"(
      #include <sys.h>
      #include <gpu.h>
      #include "ROM.h"
      __ROM const unsigned char maze[] = { 11, 22, 33, 44 };
      unsigned char work[4];
      unsigned char a, b;
      int main(void) {
        rom_copy(work, ROM_maze, ROM_maze_SIZE);
        a = work[0];
        b = work[3];
        return 0;
      }
    )");
    CHECK(r.u8("a") == 11);
    CHECK(r.u8("b") == 44);
  }

  TEST_CASE("splits an address with the three macros") {
    const Ran r = ran(R"(
      #include <rom.h>
      #include "ROM.h"
      __ROM const unsigned char pad[300] = { 1 };
      __ROM const unsigned char later[] = { 9 };
      unsigned char bank, hi, lo;
      int main(void) {
        bank = ROM_BANK(ROM_later);
        hi = ROM_HI(ROM_later);
        lo = ROM_LO(ROM_later);
        return 0;
      }
    )");
    CHECK(r.u8("bank") == 0);
    CHECK(r.u8("hi") == 1);
    CHECK(r.u8("lo") == 44);
  }
}
