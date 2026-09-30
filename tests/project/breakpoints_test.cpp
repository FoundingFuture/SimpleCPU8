// Breakpoints on source lines: the address a file's line stops at, and
// where a breakpoint's line goes when the text around it is edited.

#include <doctest.h>

#include <optional>
#include <string>
#include <vector>

#include "project/breakpoints.h"
#include "project/project.h"

using namespace sc8;
using namespace sc8::project;

namespace {

Built buildOf(const std::vector<Source>& sources) {
  Built b = buildSources(sources, Assets{}, Options{}, "test", "");
  REQUIRE_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
  return b;
}

std::optional<Stop> stopOf(const Built& b, const std::string& file, int line) {
  return stopAt(b.lines, b.assembled, file, line);
}

const char* C_SRC = R"(unsigned char a;
int main(void)
{
    a = 1;

    a = 2;
    a = 3;
    return 0;
}
)";

}  // namespace

TEST_SUITE("a breakpoint's address") {
  TEST_CASE("an instruction line stops at its instruction") {
    const Built b = buildOf({{"main.asm", "start:  LD A <- 1\n        INC A\n        HLT\n"}});
    const auto s = stopOf(b, "main.asm", 2);
    REQUIRE(s);
    CHECK(s->line == 2);
    CHECK(s->pc == 1);
  }

  TEST_CASE("a line without code stops at the next line that has some") {
    const Built b = buildOf({{"main.asm", "        LD A <- 1\n; a comment\n\n        HLT\n"}});
    const auto s = stopOf(b, "main.asm", 2);
    REQUIRE(s);
    CHECK(s->line == 4);
    CHECK(s->pc == 1);
  }

  TEST_CASE("nothing after the file's last instruction stops anywhere") {
    const Built b = buildOf({{"a.asm", "        HLT\n.data\nx: db 1\n"}, {"b.asm", "        HLT\n"}});
    CHECK_FALSE(stopOf(b, "a.asm", 2));
    CHECK_FALSE(stopOf(b, "a.asm", 40));
  }

  TEST_CASE("each file counts its own lines, in the order the build joins them") {
    const Built b = buildOf({{"b.asm", "bee:    INC A\n        RET\n"}, {"a.asm", "        JSR bee\n        HLT\n"}});
    const auto a1 = stopOf(b, "a.asm", 1);
    const auto b1 = stopOf(b, "b.asm", 1);
    REQUIRE(a1);
    REQUIRE(b1);
    CHECK(a1->pc == 0);
    CHECK(b1->pc == 2);
  }

  TEST_CASE("a file the build never saw stops nowhere") {
    const Built b = buildOf({{"main.asm", "        HLT\n"}});
    CHECK_FALSE(stopOf(b, "other.asm", 1));
  }

  TEST_CASE("a C statement stops at its first instruction, later lines later") {
    const Built b = buildOf({{"main.c", C_SRC}});
    const auto s4 = stopOf(b, "main.c", 4);
    const auto s6 = stopOf(b, "main.c", 6);
    const auto s7 = stopOf(b, "main.c", 7);
    REQUIRE(s4);
    REQUIRE(s6);
    REQUIRE(s7);
    CHECK(s4->line == 4);
    CHECK(s6->pc > s4->pc);
    CHECK(s7->pc > s6->pc);
    // The instruction before the stop belongs to an earlier line, so the
    // machine stops before the statement runs.
    const auto pc = static_cast<size_t>(s6->pc);
    CHECK(b.assembled.instrToLine[pc - 1] < b.assembled.instrToLine[pc]);
  }

  TEST_CASE("a C line without a statement stops at the next statement") {
    const Built b = buildOf({{"main.c", C_SRC}});
    const auto blank = stopOf(b, "main.c", 5);
    const auto six = stopOf(b, "main.c", 6);
    REQUIRE(blank);
    REQUIRE(six);
    CHECK(blank->line == 6);
    CHECK(blank->pc == six->pc);
  }

  TEST_CASE("a declaration that makes no code stops at the statement after it") {
    const Built b = buildOf({{"main.c", "unsigned char a;\nint main(void)\n{\n    int i;\n    a = 1;\n    return 0;\n}\n"}});
    const auto decl = stopOf(b, "main.c", 4);
    const auto five = stopOf(b, "main.c", 5);
    REQUIRE(decl);
    REQUIRE(five);
    CHECK(decl->line == 5);
    CHECK(decl->pc == five->pc);
  }

  TEST_CASE("an assembly file beside C counts from its own first line") {
    const Built b = buildOf({{"main.c", "void tick(void);\nint main(void)\n{\n    tick();\n    return 0;\n}\n"},
                             {"tick.asm", "tick:   INC A\n        RET\n"}});
    const auto s = stopOf(b, "tick.asm", 1);
    REQUIRE(s);
    CHECK(s->line == 1);
    CHECK(b.assembled.labels.at("tick").value == s->pc);
  }
}

TEST_SUITE("a breakpoint follows an edit") {
  TEST_CASE("an unchanged text moves nothing") {
    const Edit e("a\nb\nc\n", "a\nb\nc\n");
    CHECK(e.follow(1) == 1);
    CHECK(e.follow(3) == 3);
  }

  TEST_CASE("typing inside a line moves nothing") {
    const Edit e("a\nb\nc\n", "a\nbxy\nc\n");
    CHECK(e.follow(1) == 1);
    CHECK(e.follow(2) == 2);
    CHECK(e.follow(3) == 3);
  }

  TEST_CASE("Enter at the end of a line moves the lines below it") {
    const Edit e("a\nb\nc\n", "a\n\nb\nc\n");
    CHECK(e.follow(1) == 1);
    CHECK(e.follow(2) == 3);
    CHECK(e.follow(3) == 4);
  }

  TEST_CASE("Enter at the start of a line moves that line down") {
    const Edit e("a\nb\nc\n", "a\nx\nb\nc\n");
    CHECK(e.follow(1) == 1);
    CHECK(e.follow(2) == 3);
  }

  TEST_CASE("a pasted block moves everything below it by its lines") {
    const Edit e("a\nb\n", "p\nq\nr\na\nb\n");
    CHECK(e.follow(1) == 4);
    CHECK(e.follow(2) == 5);
  }

  TEST_CASE("a deleted line loses its breakpoint and the lines below move up") {
    const Edit e("a\nb\nc\nd\n", "a\nc\nd\n");
    CHECK(e.follow(1) == 1);
    CHECK_FALSE(e.follow(2));
    CHECK(e.follow(3) == 2);
    CHECK(e.follow(4) == 3);
  }

  TEST_CASE("a line joined onto the one above keeps its breakpoint there") {
    const Edit e("a\nb\nc\n", "ab\nc\n");
    CHECK(e.follow(1) == 1);
    CHECK(e.follow(2) == 1);
    CHECK(e.follow(3) == 2);
  }

  TEST_CASE("a selection deleted across lines leaves the lines it cut into on one line") {
    const Edit e("one\ntwo\nthree\nfour\n", "onree\nfour\n");
    CHECK(e.follow(1) == 1);
    CHECK_FALSE(e.follow(2));
    CHECK(e.follow(3) == 1);
    CHECK(e.follow(4) == 2);
  }

  TEST_CASE("the last line without a newline moves too") {
    const Edit e("a\nb", "x\na\nb");
    CHECK(e.follow(2) == 3);
  }
}
