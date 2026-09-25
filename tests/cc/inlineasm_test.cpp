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
}
