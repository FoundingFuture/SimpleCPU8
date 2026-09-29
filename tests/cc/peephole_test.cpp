// The peephole pass: what it removes, and what it must leave alone.

#include <doctest.h>

#include <algorithm>
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
    const auto in = lines({"LD [__t0+1] <- A", "JZ there", "RET", ":there:", "LD A <- 5", "ADD A <- [__t0+1]", "OUTA 1",
                           "RET"});
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

  TEST_CASE("a word stored from D2 is not loaded back into D2") {
    CHECK(peephole(lines({"LD D2 <- [p]", "LD D2 <- D2+1", "LD [__t0] <- D2", "LD D2 <- [__t0]", "LD [q] <- D2",
                          "RET"})) == lines({"LD D2 <- [p]", "LD D2 <- D2+1", "LD [q] <- D2", "RET"}));
  }

  TEST_CASE("a byte store into the word ends what D2 is known to hold") {
    const auto in = lines({"LD D2 <- [p]", "LD [p+1] <- A", "LD D2 <- [p]", "LD [q] <- D2", "RET"});
    CHECK(peephole(in) == in);
  }

  TEST_CASE("a label only one jump reaches starts with what the jump knew") {
    CHECK(peephole(lines({"LD A <- [c]", "LD [d] <- A", "JNZ there", "RET", ":there:", "LD A <- [c]", "OUTA 1",
                          "RET"})) == lines({"LD A <- [c]", "LD [d] <- A", "JNZ there", "RET", ":there:", "OUTA 1", "RET"}));
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

  // docs/design/basic-speed.md, proposal 3: an inlined test's answer, built
  // as 0 or 1 in a temp only for a jump to test it.

  TEST_CASE("an answer tested by JZ jumps from its compare") {
    const auto in = lines({"LD A <- [c]", "CMP A, 48", "JC skip", "CMP A, 58", "JC yes", ":skip:", "LD A <- 0",
                           "JMP done", ":yes:", "LD A <- 1", ":done:", "LD [__t0+1] <- A", ":inl_isdig:",
                           "LD A <- [__t0+1]", "JZ else", "LD A <- 7", "LD [y] <- A", "RET", ":else:", "LD A <- 9",
                           "LD [y] <- A", "RET"});
    // The false arm reaches else, the true arm goes on in place. The
    // compare's jump to the false arm goes straight to else.
    CHECK(peephole(in) == lines({"LD A <- [c]", "CMP A, 48", "JC else", "CMP A, 58", "JNC else", ":skip:", ":yes:",
                                 ":done:", ":inl_isdig:", "LD A <- 7", "LD [y] <- A", "RET", ":else:", "LD A <- 9",
                                 "LD [y] <- A", "RET"}));
  }

  TEST_CASE("an answer tested by JNZ jumps from its compare") {
    const auto in = lines({"LD A <- [c]", "CMP A, 65", "JC skip", "CMP A, 71", "JC yes", ":skip:", "LD A <- 0",
                           "JMP done", ":yes:", "LD A <- 1", ":done:", "LD [__t0+1] <- A", "LD A <- [__t0+1]",
                           "JNZ hit", "LD A <- 1", "LD [y] <- A", "RET", ":hit:", "LD A <- 2", "LD [y] <- A", "RET"});
    CHECK(peephole(in) == lines({"LD A <- [c]", "CMP A, 65", "JC done", "CMP A, 71", "JC hit", ":skip:", ":yes:",
                                 ":done:", "LD A <- 1", "LD [y] <- A", "RET", ":hit:", "LD A <- 2", "LD [y] <- A", "RET"}));
  }

  TEST_CASE("an answer read again keeps its byte and its test") {
    // The byte is read after the test, so it must hold the answer there.
    const auto in = lines({"CMP A, 48", "JC yes", "LD A <- 0", "JMP done", ":yes:", "LD A <- 1", ":done:",
                           "LD [__t0+1] <- A", "LD A <- [__t0+1]", "JZ else", "LD A <- 5", "ADD A <- [__t0+1]",
                           "LD [y] <- A", "RET", ":else:", "RET"});
    CHECK(peephole(in) == in);
  }

  TEST_CASE("an answer another return also reaches keeps its test") {
    // A second return of the inlined function jumps to its end label with
    // its own answer in the temp.
    const auto in = lines({"LD [__t0+1] <- A", "LD A <- [x]", "JNZ inl_f", "CMP A, 48", "JC yes", "LD A <- 0",
                           "JMP done", ":yes:", "LD A <- 1", ":done:", "LD [__t0+1] <- A", ":inl_f:",
                           "LD A <- [__t0+1]", "JZ else", "LD A <- 7", "LD [y] <- A", "RET", ":else:", "RET"});
    const auto out = peephole(in);
    CHECK(std::find(out.begin(), out.end(), "        JZ else") != out.end());
  }

  TEST_CASE("an answer whose A a path still reads keeps its test") {
    const auto in = lines({"CMP A, 48", "JC yes", "LD A <- 0", "JMP done", ":yes:", "LD A <- 1", ":done:",
                           "LD [__t0+1] <- A", "LD A <- [__t0+1]", "JZ else", "LD [y] <- A", "RET", ":else:",
                           "LD [z] <- A", "RET"});
    CHECK(peephole(in) == in);
  }

  TEST_CASE("a jump to a JMP goes where the JMP goes") {
    // No jump reaches hop then, so what it held goes too.
    CHECK(peephole(lines({"LD A <- [c]", "JZ hop", "OUTA 1", "RET", ":hop:", "JMP far", "RET", ":far:", "OUTA 2",
                          "RET"})) == lines({"LD A <- [c]", "JZ far", "OUTA 1", "RET", ":hop:", ":far:", "OUTA 2", "RET"}));
  }

  TEST_CASE("a conditional jump over a JMP turns round") {
    CHECK(peephole(lines({"LD A <- [c]", "JZ on", "JMP away", ":on:", "OUTA 1", "RET", ":away:", "OUTA 2", "RET"})) ==
          lines({"LD A <- [c]", "JNZ away", ":on:", "OUTA 1", "RET", ":away:", "OUTA 2", "RET"}));
  }

  TEST_CASE("code after a RET that no jump reaches goes") {
    CHECK(peephole(lines({"OUTA 1", "RET", ":lost:", "OUTA 2", "RET"})) == lines({"OUTA 1", "RET", ":lost:"}));
  }

  TEST_CASE("code after a RET stays where a jump reaches it or a barrier stands") {
    const auto in = lines({"LD A <- [c]", "JZ found", "OUTA 1", "RET", ":found:", "OUTA 2", "RET"});
    CHECK(peephole(in) == in);
    std::vector<std::string> fenced = lines({"OUTA 1", "RET"});
    fenced.emplace_back(BARRIER_MARK);
    fenced.emplace_back("asm_entry: LD A <- 1");
    fenced.emplace_back("        RET");
    const auto out = peephole(fenced);
    CHECK(std::find(out.begin(), out.end(), "asm_entry: LD A <- 1") != out.end());
  }
}
