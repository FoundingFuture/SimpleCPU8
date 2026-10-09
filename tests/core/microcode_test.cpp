#include <doctest.h>

#include <algorithm>
#include <ostream>

#include "core/conflicts.h"
#include "core/isa.h"
#include "core/mcparse.h"
#include "core/microcode.h"

using namespace sc8;

namespace {

bool anyError(const McParsed& p, std::string_view needle) {
  return std::any_of(p.errors.begin(), p.errors.end(),
                     [&](const McError& e) { return e.message.find(needle) != std::string::npos; });
}

}  // namespace

TEST_SUITE("shipped microcode") {
  TEST_CASE("covers fetch and every opcode, every row legal, every program under the cap") {
    for (const auto& [label, mc] : {std::pair{"naive", buildNaive()}, std::pair{"optimal", buildOptimal()}}) {
      CAPTURE(label);
      CHECK(mc.has("fetch"));
      for (const OpDef& def : ops()) {
        CAPTURE(def.name);
        CHECK(mc.has(def.name));
      }
      for (const auto& section : mc.sections()) {
        CAPTURE(section.name);
        CHECK_LE(section.rows.size(), static_cast<size_t>(ROW_CAP));
        for (const Row& row : section.rows) {
          auto conflict = checkRow(row);
          CHECK_MESSAGE(!conflict, rowText(row), " -> ", conflict ? conflict->message : "");
        }
      }
    }
  }

  TEST_CASE("survives a serialize and parse round trip") {
    Microcode naive = buildNaive();
    McParsed p = parseMicrocode(serializeMicrocode(naive));
    CHECK(p.errors.empty());
    CHECK_EQ(p.microcode.size(), naive.size());
    for (const auto& section : naive.sections()) {
      const Rows* rows = p.microcode.get(section.name);
      REQUIRE(rows);
      CHECK(*rows == section.rows);
    }
  }
}

TEST_SUITE("microcode parser errors") {
  TEST_CASE("flags unknown signals") {
    CHECK(anyError(parseMicrocode("fetch:\n  FETCH\nNOP:\n  WARP_DRIVE\n"), "unknown signal"));
  }

  TEST_CASE("flags unknown sections and duplicates") {
    McParsed p = parseMicrocode("fetch:\n  FETCH\nBOGUS OP:\n  PC_INC\nfetch:\n  FETCH\n");
    CHECK(anyError(p, "unknown instruction section"));
    CHECK(anyError(p, "duplicate section"));
  }

  TEST_CASE("requires the fetch section") {
    CHECK(anyError(parseMicrocode("NOP:\n  PC_INC\n"), "fetch section"));
  }

  TEST_CASE("enforces the row cap") {
    std::string rows;
    for (int i = 0; i < ROW_CAP + 1; i++) rows += "  PC_INC\n";
    CHECK(anyError(parseMicrocode("fetch:\n  FETCH\nNOP:\n" + rows), "row cap"));
  }

  TEST_CASE("a missing opcode section parses fine and crashes on execute") {
    McParsed p = parseMicrocode("fetch:\n  FETCH\nNOP:\n  PC_INC\n");
    CHECK(p.errors.empty());
    CHECK(!p.microcode.has("HLT"));
  }

  TEST_CASE("signal names are case insensitive and comments are dropped") {
    McParsed p = parseMicrocode("fetch: # the fetch\n  fetch, pc_inc\n");
    CHECK(p.errors.empty());
    CHECK(*p.microcode.get("fetch") == Rows{rowOf({"FETCH", "PC_INC"})});
  }
}
