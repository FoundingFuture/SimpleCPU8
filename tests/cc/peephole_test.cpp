// The peephole pass: what it removes, and what it must leave alone.

#include <doctest.h>

#include <string>
#include <vector>

#include "cc/peephole.h"

using namespace sc8::cc;

namespace {

std::vector<std::string> lines(std::initializer_list<const char*> in) {
  std::vector<std::string> out;
  for (const char* l : in) out.emplace_back(l[0] == ':' ? std::string(l + 1) : "        " + std::string(l));
  return out;
}

}  // namespace

TEST_SUITE("peephole") {
  TEST_CASE("a byte stored and loaded straight back is not loaded") {
    // The temp is then never read, and the end of the text ends the
    // statement, so its store goes too.
    CHECK(peephole(lines({"LD A <- [x]", "LD [__t0+1] <- A", "LD A <- [__t0+1]", "ADD A <- 1", "LD [y] <- A"})) ==
          lines({"LD A <- [x]", "ADD A <- 1", "LD [y] <- A"}));
  }

  TEST_CASE("the reload stays when a jump reads its flags") {
    const auto in = lines({"LD [x] <- A", "LD [y] <- A", "LD A <- [x]", "JZ out", "RET", ":out:", "RET"});
    CHECK(peephole(in) == in);
  }

  TEST_CASE("a temp written twice keeps the second write") {
    CHECK(peephole(lines({"LD [__t1+1] <- A", "LD A <- 3", "LD [__t1+1] <- A", "LD A <- [__t1+1]", "OUTA 5"})) ==
          lines({"LD A <- 3", "LD [__t1+1] <- A", "OUTA 5"}));
  }

  TEST_CASE("a temp read on one path keeps its store") {
    const auto in = lines({"LD [__t0+1] <- A", "JZ there", "RET", ":there:", "LD A <- [__t0+1]", "OUTA 1", "RET"});
    CHECK(peephole(in) == in);
  }

  TEST_CASE("__t1 is not __t12") {
    const auto in = lines({"LD [__t1+1] <- A", "LD A <- 5", "LD [__t12+1] <- A", "LD A <- [__t1+1]", "OUTA 1",
                           "LD A <- [__t12+1]", "OUTA 2"});
    CHECK(peephole(in) == in);
  }

  TEST_CASE("a temp is dead where the next statement starts") {
    auto in = lines({"LD [__t0+1] <- A", "LD [x] <- A"});
    in.push_back(STMT_MARK);
    CHECK(peephole(in) == lines({"LD [x] <- A"}));
  }

  TEST_CASE("what A holds in a temp is forgotten at a statement") {
    // The first statement's store is dead at the boundary and goes. The
    // second statement's store of the same value must then stay, or its
    // read of the temp finds nothing written.
    std::vector<std::string> in = lines({"LD A <- [c]", "LD [__t0+1] <- A"});
    in.emplace_back(STMT_MARK);
    const auto tail = lines({"LD A <- [c]", "LD [__t0+1] <- A", "LD A <- 1", "OUTA 2", "LD A <- [__t0+1]", "OUTA 3"});
    in.insert(in.end(), tail.begin(), tail.end());
    CHECK(peephole(in) ==
          lines({"LD A <- [c]", "LD [__t0+1] <- A", "LD A <- 1", "OUTA 2", "LD A <- [__t0+1]", "OUTA 3"}));
  }

  TEST_CASE("a jump to the next line goes") {
    CHECK(peephole(lines({"JMP next", ":next:", "RET"})) == lines({":next:", "RET"}));
  }

  TEST_CASE("a device may write RAM, so its OUT ends what is known") {
    // The first load is dead, since the OUT takes an immediate. The second
    // stays: the device may have moved new bytes into the temp.
    CHECK(peephole(lines({"LD A <- [__t0+1]", "OUT GPU_CMD, CMD_RAM_MOVE", "LD A <- [__t0+1]", "OUTA 3"})) ==
          lines({"OUT GPU_CMD, CMD_RAM_MOVE", "LD A <- [__t0+1]", "OUTA 3"}));
  }

  TEST_CASE("the map follows the lines that survive") {
    std::vector<int> remap;
    const auto out = peephole(lines({"LD [__t0+1] <- A", "LD A <- [__t0+1]", "ADD A <- 1", "LD [x] <- A", "RET"}), &remap);
    REQUIRE_EQ(out.size(), 3u);
    CHECK_EQ(remap[0], 0);
    CHECK_EQ(remap[1], 0);
    CHECK_EQ(remap[2], 0);
    CHECK_EQ(remap[3], 1);
    CHECK_EQ(remap[4], 2);
  }
}
