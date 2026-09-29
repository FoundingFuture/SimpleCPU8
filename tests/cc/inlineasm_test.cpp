// Inline assembly may name what the program defines. The compiler does not
// read that text, so a function it names is kept as reached, and a static
// name in it follows the static's private label.

#include <doctest.h>

#include <string>

#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

TEST_SUITE("names in inline assembly") {
  TEST_CASE("a function called only from inline assembly is kept, and runs") {
    const Ran r = ran(
        "int hits;\n"
        "void bump(void) { hits = hits + 1; }\n"
        "int main(void) { asm(\"JSR bump\"); asm(\"JSR bump\"); return 0; }");
    REQUIRE(r.halted());
    CHECK_EQ(r.i16("hits"), 2);
  }

  TEST_CASE("a function it reaches keeps what that function calls") {
    const Ran r = ran(
        "int hits;\n"
        "void add(int n) { hits = hits + n; }\n"
        "void bump(void) { add(3); }\n"
        "int main(void) { asm(\"JSR bump\"); return 0; }");
    REQUIRE(r.halted());
    CHECK_EQ(r.i16("hits"), 3);
  }

  TEST_CASE("a static function and a static global follow their private names into the text") {
    const Ran r = ran(
        "static int hits;\n"
        "static void bump(void) { hits = hits + 1; }\n"
        "int seen;\n"
        "int main(void) { asm(\"JSR bump\"); asm(\"LD A <- [hits+1]\"); asm(\"LD [seen+1] <- A\"); return 0; }");
    REQUIRE(r.halted());
    CHECK_EQ(r.i16("seen"), 1);
  }

  TEST_CASE("a word that only looks like a name is left alone") {
    const std::string text = compile(
        "int main(void) { asm(\"LD A <- 5\"); asm(\"OUT GPU_CMD, CMD_CLEAR\"); return 0; }");
    CHECK(has(text, "LD A <- 5"));
    CHECK(has(text, "OUT GPU_CMD, CMD_CLEAR"));
  }

  // docs/design/basic-speed.md, proposal 3: the peephole rules beside
  // hand written lines. An inlined function's body carries no statement
  // marks, so there the ;@barrier around an asm statement is all that
  // stands between a rule and the assembly.

  TEST_CASE("code after a return stays where only assembly jumps to a label inside a barrier") {
    // past: sits after tail's return, which no C jump reaches. The rule
    // that drops unreachable code stops at the barrier.
    const std::string src =
        "unsigned char r;\n"
        "static void tail(void) { r = 1; return; asm(\"past: LD A <- 42\"); asm(\"LD [r] <- A\"); }\n"
        "int main(void) { asm(\"JMP past\"); tail(); return 0; }\n";
    const std::string text = compile(src);
    CHECK(has(text, "past: LD A <- 42"));
    CHECK(has(text, "LD [r] <- A"));
    const Ran r = ran(src);
    REQUIRE(r.halted());
    CHECK_EQ(r.u8("r"), 42);
  }

  TEST_CASE("a jump is not threaded across a barrier to the JMP inside it") {
    // The else arm's first instruction is the asm JMP. Threading would
    // point the C test at out_asm, reading into the hand written block.
    const std::string src =
        "unsigned char r;\n"
        "static void pick(unsigned char a) { if (a) { r = 1; } else { asm(\"JMP out_asm\"); } r = 2; "
        "asm(\"out_asm:\"); r = r + 5; }\n"
        "int main(void) { pick(0); return 0; }\n";
    const std::string text = compile(src);
    CHECK_EQ(countOf(text, "out_asm"), 2);
    const Ran r = ran(src);
    REQUIRE(r.halted());
    CHECK_EQ(r.u8("r"), 5);
  }

  TEST_CASE("a function assembly calls stays when its only C call is unreachable") {
    // never's call to helper follows its return. Expanded into main, with
    // no statement mark between them, the peephole drops that JSR. helper
    // is reached from the syntax tree and from the asm text, so it stays,
    // and the asm JSR runs it. never's own copy keeps the call: there a
    // statement mark follows the return, and the rule stops at one.
    const std::string src =
        "unsigned char r;\n"
        "void helper(void) { r = r + 5; }\n"
        "static void never(void) { return; helper(); }\n"
        "int main(void) { asm(\"JSR helper\"); never(); return 0; }\n";
    const std::string text = compile(src);
    const size_t from = text.find("\nmain:");
    REQUIRE(from != std::string::npos);
    const std::string mainText = text.substr(from, text.find("main__end:", from) - from);
    CHECK_EQ(countOf(mainText, "JSR helper"), 1);
    CHECK(has(mainText, "_inl_"));
    CHECK(has(text, "helper:"));
    const Ran r = ran(src);
    REQUIRE(r.halted());
    CHECK_EQ(r.u8("r"), 5);
  }
}
