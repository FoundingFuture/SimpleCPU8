// The build's check of a .bas file: BASIC's own CHECK, run on a machine
// with no window, once for each mistake it finds.

#include <doctest.h>

#include <string>
#include <vector>

#include "basic/basic_rom.h"
#include "basic/check.h"
#include "core/cartridge.h"

using namespace sc8;

namespace {

Cartridge interpreter() {
  const CartridgeResult r = decodeCartridge(std::vector<uint8_t>(basicRom().begin(), basicRom().end()));
  REQUIRE(r.cartridge);
  return *r.cartridge;
}

}  // namespace

TEST_SUITE("checkProgram") {
  TEST_CASE("a program without mistakes has no issues, an endless loop included") {
    const basic::Checked c = basic::checkProgram(interpreter(), "10 PRINT \"HELLO\"\n20 GOTO 10\n");
    CHECK(c.failure.empty());
    CHECK(c.issues.empty());
  }

  TEST_CASE("RUN in a line is reported with its line and BASIC's words") {
    const basic::Checked c = basic::checkProgram(interpreter(), "100 PRINT \"TEST\";\n110 RUN\n");
    CHECK(c.failure.empty());
    REQUIRE(c.issues.size() == 1);
    CHECK(c.issues[0].line == 110);
    CHECK(c.issues[0].message == "UNKNOWN WORD RUN IN LINE 110");
  }

  TEST_CASE("every mistake is reported, in line order") {
    const basic::Checked c = basic::checkProgram(interpreter(), "10 GOTO 999\n20 PRINT \"OK\"\n30 RUN\n40 LIST\n");
    CHECK(c.failure.empty());
    REQUIRE(c.issues.size() == 3);
    CHECK(c.issues[0].line == 10);
    CHECK(c.issues[0].message == "THERE IS NO LINE 999 IN LINE 10");
    CHECK(c.issues[1].line == 30);
    CHECK(c.issues[2].line == 40);
  }

  TEST_CASE("a message longer than a screen row reads whole") {
    const basic::Checked c = basic::checkProgram(interpreter(), "10 FOR I = 1 10\n");
    REQUIRE(c.issues.size() == 1);
    CHECK(c.issues[0].message.find("SYNTAX ERROR IN LINE 10: EXPECTED TO BUT FOUND") == 0);
    CHECK(c.issues[0].message.find("  ") == std::string::npos);
  }

  TEST_CASE("a line BASIC refuses to store is reported where LOAD stopped") {
    const std::string longLine = "10 PRINT \"" + std::string(260, 'X') + "\"\n";
    const basic::Checked c = basic::checkProgram(interpreter(), longLine);
    REQUIRE(c.issues.size() == 1);
    CHECK(c.issues[0].message.find("THE LINE IS TOO LONG") != std::string::npos);
  }
}
