#include <doctest.h>

#include <string>

#include "cc/layout.h"
#include "core/machine.h"
#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

// main is a SUBROUTINE. __start calls it and halts when it comes back, so a
// program ends by returning, the way every other function does.
//
// A main that halts itself would make its own RET dead code, and would teach
// the opposite of the thing the two lines above it are there to show.

TEST_SUITE("the entry point") {
  TEST_CASE("calls main and halts when it returns") {
    const std::string a = compile("int main(void) { return 0; }");
    const std::string head = a.substr(a.find("__start:"), 300);
    CHECK(has(head, "JSR main"));
    // The HLT is AFTER the call, so it runs when main comes back.
    CHECK(head.find("HLT") > head.find("JSR main"));
  }

  TEST_CASE("gives main a RET, like any other function") {
    const std::string a = compile("int main(void) { return 0; }");
    // The return writes the epilogue in place, so the RET is in the body
    // and main__end has no second copy.
    const size_t from = a.find("\nmain:");
    const std::string body = a.substr(from, a.find("main__end:") - from);
    CHECK(has(body, "RET"));
  }

  TEST_CASE("halts a program that simply returns") {
    CHECK(ran("unsigned char v; int main(void) { v = 7; return 0; }").u8("v") == 7);
  }

  TEST_CASE("halts on a fall off the end, with no return at all") {
    CHECK(ran("unsigned char v; void main(void) { v = 3; }").u8("v") == 3);
  }

  TEST_CASE("unwinds main's frame before returning, so the stack is where it started") {
    const Ran r = ran(R"(
      unsigned int sp;
      int main(void) { int a; int b; a = 1; b = 2; sp = a + b; return 0; }
    )");
    // D3 is the heap stack pointer, and it is back at the top.
    CHECK(r.m->d3 == cc::C_STACK_TOP);
  }

  // halt() is still there, for stopping early from somewhere deep. It is not
  // how a program ENDS.
  TEST_CASE("still stops where halt() is called") {
    CHECK(ran(R"(
      #include <sys.h>
      unsigned char v;
      int main(void) { v = 1; halt(); v = 2; return 0; }
    )").u8("v") == 1);
  }
}

// The stack top is the one number the compiler and the GPU must agree on.
// They agree by derivation, rather than by both spelling 1344. A font change
// moves the grid, and a stack left at the old address would start inside the
// screen a program maps.
TEST_SUITE("the C stack clears the text screen") {
  TEST_CASE("starts at the top of RAM less the whole character grid") {
    CHECK(cc::C_STACK_TOP == RAM_SIZE - cc::TEXT_CELLS);
    CHECK(cc::TEXT_CELLS == 1344);
  }

  // The claim that matters is about the code the compiler EMITS, not about
  // the constant. A program that maps a screen at the convention address
  // must find its own stack pointer below the screen's first cell.
  TEST_CASE("starts a program's stack pointer below the first mapped cell") {
    const Ran r = ran("unsigned char v; int main(void) { v = 1; return 0; }");
    const int sp = (r.m->ram[0] << 8) | r.m->ram[1];
    CHECK(sp <= RAM_SIZE - cc::TEXT_COLS * cc::TEXT_ROWS);
  }
}

// --heap-stack-top moves the heap stack's first byte down, for a program
// that keeps a larger screen above it. BASIC's text buffer is 4 KB at
// $F000, so BASIC and a project with BASIC pass $F000. Every other program
// keeps $FAC0.
TEST_SUITE("the heap stack top") {
  TEST_CASE("is $FAC0 unless asked, the top of RAM less the power-on grid") {
    CHECK_EQ(cc::C_STACK_TOP, 0xFAC0);
    const std::string a = compile("int main(void) { return 0; }");
    CHECK(has(a, "LD D1 <- 64192\n        LD D3 <- D1"));
  }

  TEST_CASE("is the address given, where D3 starts") {
    CcOptions opts;
    opts.heapStackTop = 0xF000;
    const std::string a = compile("int f(int x) { return x + 1; } int main(void) { return f(1); }", opts);
    CHECK(has(a, "LD D1 <- 61440\n        LD D3 <- D1"));
    CHECK_FALSE(has(a, "64192"));
  }

  TEST_CASE("keeps a sized heap stack under the top given") {
    CcOptions opts;
    opts.heapStackTop = 0xF000;
    opts.heapStackSize = 0xF000;
    std::string why;
    try {
      compile("int main(void) { return 0; }", opts);
    } catch (const CcError& e) {
      why = e.message();
    }
    CHECK(has(why, "a heap stack of 61440 bytes reaches down to $0000"));
  }
}

