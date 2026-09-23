#include <doctest.h>

#include <string>
#include <vector>

#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

// The first behaviour tests. A snippet runs and leaves the right bytes.
//
// Every one of these ends in a HLT, because a program that ran out of budget
// and a program that computed the wrong answer must not look the same.

namespace {

int unsignedAs16(int v) { return v >= 0x8000 ? v - 0x10000 : v; }

}  // namespace

TEST_SUITE("a program at all") {
  TEST_CASE("runs main and halts") { CHECK(ran("int main(void) { return 0; }").halted()); }

  TEST_CASE("writes a byte global") {
    CHECK(ran("unsigned char x; int main(void) { x = 7; return 0; }").u8("x") == 7);
  }

  TEST_CASE("writes a word global, high byte first like the rest of the machine") {
    const Ran r = ran("unsigned int w; int main(void) { w = 0x1234; return 0; }");
    CHECK(r.m->ram[static_cast<size_t>(r.addr("w"))] == 0x12);
    CHECK(r.m->ram[static_cast<size_t>(r.addr("w")) + 1] == 0x34);
  }

  TEST_CASE("keeps an initializer") {
    CHECK(ran("unsigned char x = 9; int main(void) { return 0; }").u8("x") == 9);
  }
}

TEST_SUITE("integer arithmetic the CPU can do itself") {
  auto v = [](const std::string& expr, const std::string& type = "int") {
    return ran(type + " r; int main(void) { r = " + expr + "; return 0; }").i16("r");
  };

  TEST_CASE("adds and subtracts words") {
    CHECK(v("300 + 45") == 345);
    CHECK(v("300 - 45") == 255);
    CHECK(v("5 - 9") == -4);
  }

  TEST_CASE("carries between the two bytes of a word") { CHECK(v("0x00FF + 1") == 0x0100); }

  TEST_CASE("borrows between them too") { CHECK(v("0x0100 - 1") == 0x00FF); }

  TEST_CASE("does the bitwise operators") {
    CHECK(v("0xF0F0 & 0x0FF0") == 0x00F0);
    CHECK(v("0xF000 | 0x000F") == unsignedAs16(0xF00F));
    CHECK(v("0xFF00 ^ 0x0F0F") == unsignedAs16(0xF00F));
  }

  TEST_CASE("negates and complements") {
    CHECK(v("-7") == -7);
    CHECK(v("~0") == -1);
  }

  TEST_CASE("works on variables, not just folded constants") {
    CHECK(ran(R"(
      int a, b, r;
      int main(void) { a = 1000; b = 24; r = a - b; return 0; }
    )").i16("r") == 976);
  }

  TEST_CASE("keeps byte arithmetic in a byte") {
    CHECK(ran(R"(
      unsigned char r;
      int main(void) { unsigned char a; a = 200; r = a + 100; return 0; }
    )").u8("r") == 44);
  }
}

TEST_SUITE("locals live on the software stack") {
  TEST_CASE("holds a local across a statement") {
    CHECK(ran(R"(
      int r;
      int main(void) { int a; a = 5; a = a + 3; r = a; return 0; }
    )").i16("r") == 8);
  }

  TEST_CASE("gives each function its own frame") {
    CHECK(ran(R"(
      int r;
      int inner(void) { int a; a = 99; return a; }
      int main(void) { int a; a = 1; inner(); r = a; return 0; }
    )").i16("r") == 1);
  }
}

TEST_SUITE("control flow") {
  TEST_CASE("takes an if") {
    CHECK(ran("int r; int main(void) { r = 0; if (1) r = 5; return 0; }").i16("r") == 5);
  }

  TEST_CASE("skips an if that is false, and takes its else") {
    CHECK(ran("int r; int main(void) { if (0) r = 5; else r = 6; return 0; }").i16("r") == 6);
  }

  TEST_CASE("runs a while loop the right number of times") {
    CHECK(ran(R"(
      int r; int main(void) { int i; r = 0; i = 0; while (i < 10) { r = r + i; i = i + 1; } return 0; }
    )").i16("r") == 45);
  }

  TEST_CASE("runs a for loop") {
    CHECK(ran(R"(
      int r; int main(void) { int i; r = 0; for (i = 1; i <= 5; i++) r = r + i; return 0; }
    )").i16("r") == 15);
  }

  TEST_CASE("runs a do while at least once") {
    CHECK(ran("int r; int main(void) { r = 0; do { r = r + 1; } while (0); return 0; }").i16("r") == 1);
  }

  TEST_CASE("breaks and continues") {
    CHECK(ran(R"(
      int r; int main(void) {
        int i; r = 0;
        for (i = 0; i < 10; i++) { if (i == 3) continue; if (i == 6) break; r = r + i; }
        return 0;
      }
    )").i16("r") == 0 + 1 + 2 + 4 + 5);
  }
}

TEST_SUITE("comparisons, which this machine has no CMP for") {
  auto cmp = [](const std::string& e) {
    return ran("unsigned char r; int main(void) { r = (" + e + ") ? 1 : 0; return 0; }").u8("r");
  };

  TEST_CASE("compares unsigned words both ways") {
    CHECK(cmp("300 < 400") == 1);
    CHECK(cmp("400 < 300") == 0);
    CHECK(cmp("300 < 300") == 0);
  }

  TEST_CASE("does equality and its negation") {
    CHECK(cmp("7 == 7") == 1);
    CHECK(cmp("7 == 8") == 0);
    CHECK(cmp("7 != 8") == 1);
  }

  TEST_CASE("does the other two, which are the first two with the sides swapped") {
    CHECK(cmp("5 > 3") == 1);
    CHECK(cmp("3 >= 3") == 1);
    CHECK(cmp("4 <= 3") == 0);
  }

  TEST_CASE("compares signed values, where N xor V is the answer") {
    CHECK(ran(R"(
      unsigned char r; int a, b;
      int main(void) { a = -5; b = 3; r = (a < b) ? 1 : 0; return 0; }
    )").u8("r") == 1);
  }

  TEST_CASE("compares across the sign boundary, which is where a byte compare fails") {
    CHECK(ran(R"(
      unsigned char r; int a, b;
      int main(void) { a = -1; b = 1; r = (a < b) ? 1 : 0; return 0; }
    )").u8("r") == 1);
  }
}

TEST_SUITE("functions") {
  TEST_CASE("calls one and takes its answer") {
    CHECK(ran(R"(
      int r;
      int five(void) { return 5; }
      int main(void) { r = five(); return 0; }
    )").i16("r") == 5);
  }

  TEST_CASE("passes arguments") {
    CHECK(ran(R"(
      int r;
      int add(int a, int b) { return a + b; }
      int main(void) { r = add(300, 45); return 0; }
    )").i16("r") == 345);
  }

  TEST_CASE("passes bytes and words together") {
    CHECK(ran(R"(
      int r;
      int f(unsigned char a, int b) { return b - a; }
      int main(void) { r = f(5, 100); return 0; }
    )").i16("r") == 95);
  }

  TEST_CASE("recurses, which is what the software stack is for") {
    CHECK(ran(R"(
      int r;
      int fact(int n) { if (n <= 1) return 1; return n * fact(n - 1); }
      int main(void) { r = fact(6); return 0; }
    )", 2000000).i16("r") == 720);
  }

  // The frame's temp save area is sized at the PROLOGUE, before the body
  // that decides how many temps the program needs. The two passes used to
  // reset that count between them, so the FIRST function generated got a
  // save area of nothing and its spill wrote over its own first argument.
  // This one returned 3 instead of 7 and said nothing at all.
  TEST_CASE("keeps the first function's argument through a nested call") {
    CHECK(ran(R"(
      int r;
      int first(int a) { return one() + one() + a; }
      int one(void) { return 1; }
      int main(void) { r = first(5); return 0; }
    )").i16("r") == 7);
  }

  TEST_CASE("keeps a deep first function's locals too") {
    CHECK(ran(R"(
      int r;
      int first(void) { int k; k = 100; return id(1) + (id(2) + (id(3) + k)); }
      int id(int x) { return x; }
      int main(void) { r = first(); return 0; }
    )").i16("r") == 106);
  }

  TEST_CASE("nests calls in one expression") {
    CHECK(ran(R"(
      int r;
      int dbl(int x) { return x + x; }
      int main(void) { r = dbl(dbl(3)) + dbl(1); return 0; }
    )").i16("r") == 14);
  }
}

TEST_SUITE("both microcode sets agree") {
  TEST_CASE("on a program with a loop and a call in it") {
    CHECK(agree(R"(
      int r;
      int f(int x) { return x + 1; }
      int main(void) { int i; r = 0; for (i = 0; i < 8; i++) r = r + f(i); return 0; }
    )", [](const Ran& x) { return x.i16("r"); }) == 8 + 28);
  }
}

TEST_SUITE("what the compiler refuses") {
  TEST_CASE("names an unknown variable") {
    CHECK(has(refuses("int main(void) { x = 1; return 0; }"), "x"));
  }

  TEST_CASE("names an unknown function") {
    CHECK(has(refuses("int main(void) { nope(); return 0; }"), "nope"));
  }

  TEST_CASE("refuses a program with no main") {
    CHECK(has(refuses("int f(void) { return 0; }"), "main"));
  }

  TEST_CASE("counts the arguments at a call") {
    CHECK(has(refuses(R"(
      int f(int a) { return a; }
      int main(void) { f(1, 2); return 0; }
    )"), "argument"));
  }
}
