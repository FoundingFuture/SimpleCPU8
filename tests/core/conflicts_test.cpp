#include <doctest.h>

#include "core/conflicts.h"

using namespace sc8;

namespace {

void ok(std::initializer_list<std::string_view> names) {
  auto c = checkRow(rowOf(names));
  CHECK_MESSAGE(!c, (c ? c->message : std::string()));
}

void bad(std::initializer_list<std::string_view> names, int rule) {
  auto c = checkRow(rowOf(names));
  REQUIRE(c);
  CHECK_EQ(c->rule, rule);
}

}  // namespace

TEST_SUITE("conflict rules") {
  TEST_CASE("accepts the optimal fetch row") { ok({"FETCH", "PC_INC"}); }
  TEST_CASE("accepts a post-increment read") { ok({"RAM_TO_B", "ADDR_D1", "D1_INC"}); }
  TEST_CASE("accepts the JSR merge row") { ok({"STK_WRITE_PCL", "SP_DEC", "PC_LOAD"}); }

  TEST_CASE("rule 1: two writers on one register") { bad({"PC_INC", "PC_LOAD"}, 1); }
  TEST_CASE("rule 1: gated loads count as writers") { bad({"PC_LOAD_Z", "PC_LOAD_C"}, 1); }
  TEST_CASE("rule 1: TSTZ and FLAGS_LOAD collide") {
    bad({"ALU_ADD", "FLAGS_LOAD", "D1_TSTZ"}, 1);
  }
  TEST_CASE("rule 2: two data RAM accesses") { bad({"RAM_TO_B", "RAM_WRITE_ACC", "ADDR_OP8"}, 2); }
  TEST_CASE("rule 3: two fetches cannot happen") { bad({"FETCH", "FETCH"}, 1); }
  TEST_CASE("rule 4: two stack accesses") { bad({"STK_TO_B", "STK_WRITE_ACC"}, 4); }
  TEST_CASE("rule 5: two ALU op selects") { bad({"ALU_ADD", "ALU_SUB"}, 5); }
  TEST_CASE("rule 6: ALU write without an op select") { bad({"ACC_LOAD_ALU"}, 6); }
  TEST_CASE("rule 7: two steps fight over the shared unit") { bad({"D1_INC", "D2_INC"}, 7); }
  TEST_CASE("rule 7: PC_INC uses the shared unit too") { bad({"PC_INC", "SP_DEC"}, 7); }
  TEST_CASE("rule 8: two IO signals") { bad({"IO_WRITE_IMM", "IO_WRITE_ACC"}, 8); }
  TEST_CASE("rule 8: RAM access without an address base") { bad({"RAM_TO_B"}, 8); }
  TEST_CASE("rule 8: two address bases") { bad({"RAM_TO_B", "ADDR_D1", "ADDR_D2"}, 8); }
  TEST_CASE("unknown signals never become a row") {
    CHECK(!signalByName("NO_SUCH_SIGNAL"));
    CHECK_THROWS(rowOf({"NO_SUCH_SIGNAL"}));
  }
}
