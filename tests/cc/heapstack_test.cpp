// The heap stack: parameters and locals in RAM under D3. A function that
// can recurse, or that assembly calls, checks for room at entry. A fixed
// call chain is summed when it compiles, and main's is refused there when
// it does not fit.

#include <doctest.h>

#include <string>

#include "cc/layout.h"
#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

namespace {

int count(const std::string& text, const std::string& what) { return countOf(text, what); }

}  // namespace

TEST_SUITE("the heap stack") {
  TEST_CASE("a fixed call chain carries no check at run time") {
    const std::string text = compile(
        "int sq(int x) { return x * x; }\n"
        "int r;\n"
        "int main(void) { r = sq(3); return 0; }");
    CHECK_EQ(count(text, "JNC __stack_overflow"), 0);
    CHECK_FALSE(has(text, "__stack_overflow:"));
  }

  TEST_CASE("a recursive function checks, and the rest of its chain is counted in") {
    const std::string text = compile(
        "int leaf(int x) { int a; int b; a = x; b = a + 1; return b; }\n"
        "int down(int n) { if (n == 0) return leaf(1); return down(n - 1); }\n"
        "int r;\n"
        "int main(void) { r = down(3); return 0; }");
    CHECK_EQ(count(text, "JNC __stack_overflow"), 1);
    CHECK(has(text, ".equ __hs_down,"));
    CHECK_FALSE(has(text, ".equ __hs_leaf,"));
    CHECK_FALSE(has(text, ".equ __hs_main,"));
  }

  TEST_CASE("recursion that stays inside the heap stack runs to the end") {
    const Ran r = ran(
        "#pragma heap_stack_size 200\n"
        "int down(int n) { if (n == 0) return 7; return down(n - 1); }\n"
        "int r;\n"
        "int main(void) { r = down(10); return 0; }");
    REQUIRE(r.halted());
    CHECK_EQ(r.i16("r"), 7);
  }

  TEST_CASE("recursion past the heap stack stops at the overflow, before it writes below the floor") {
    const Ran r = ran(
        "#pragma heap_stack_size 64\n"
        "int down(int n) { int pad[4]; pad[0] = n; if (n == 0) return 7; return down(n - 1); }\n"
        "int r;\n"
        "int main(void) { r = down(100); return 0; }");
    REQUIRE(r.halted());
    CHECK_EQ(r.i16("r"), 0);
    // The check runs before the body, so nothing was written below the floor.
    for (int a = cc::C_STACK_TOP - 64 - 32; a < cc::C_STACK_TOP - 64; a++) {
      CHECK_EQ(r.m->ram[static_cast<size_t>(a)], 0);
    }
  }

  TEST_CASE("a function called from assembly checks, since the compiler cannot see from how deep") {
    const std::string text = compile(
        "int helper(int x) { return x + 1; }\n"
        "int r;\n"
        "int main(void) { r = helper(1); asm(\"JSR helper\"); return 0; }");
    CHECK(has(text, ".equ __hs_helper,"));
  }

  TEST_CASE("main's chain is refused when it compiles if it cannot fit") {
    CHECK(has(refuses("#pragma heap_stack_size 4\n"
                      "int f(int a, int b, int c) { int x; int y; x = a + b; y = x + c; return y; }\n"
                      "int r;\n"
                      "int main(void) { r = f(1, 2, 3); return 0; }"),
              "bytes of heap stack"));
  }

  TEST_CASE("a heap stack that would reach into the program's data is refused") {
    CHECK(has(refuses("#pragma heap_stack_size 65000\n"
                      "int r;\n"
                      "int main(void) { r = 1; return 0; }"),
              "the program's data ends at"));
  }

  TEST_CASE("the pragma takes a number") {
    CHECK(has(refuses("#pragma heap_stack_size lots\nint main(void) { return 0; }"), "takes a number of bytes"));
  }
}
