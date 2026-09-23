#include <doctest.h>

#include <cmath>
#include <string>

#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

// double, for real. Every operation is a coprocessor command: the CPU cannot
// add two of these bytes together, so `a * b + c` is three block writes,
// three commands and a read.
//
// A double lives in RAM, eight bytes, big-endian IEEE 754. These tests read
// those bytes back and decode them, because that is what the machine holds.
// The moves between slots are CMD_RAM_MOVE, which the harness's GPU stub
// answers.

namespace {

Ran one(const std::string& body, uint64_t budget = 2000000) {
  return ran("double r;\nint main(void) { " + body + " return 0; }", budget);
}

bool close(double a, double b) { return std::fabs(a - b) < 1e-9; }

}  // namespace

TEST_SUITE("a double is eight bytes in RAM") {
  TEST_CASE("takes a literal") { CHECK(one("r = 3.5;").f64("r") == 3.5); }

  TEST_CASE("keeps the whole of one that an int could not hold") { CHECK(one("r = 0.1;").f64("r") == 0.1); }

  TEST_CASE("takes a negative literal") { CHECK(one("r = -2.25;").f64("r") == -2.25); }

  TEST_CASE("spells float the same, because the coprocessor has one format") {
    CHECK(ran("float r;\nint main(void) { r = 1.5; return 0; }").f64("r") == 1.5);
  }
}

TEST_SUITE("arithmetic, which is a coprocessor command each") {
  TEST_CASE("adds") { CHECK(one("r = 2.5 + 4.0;").f64("r") == 6.5); }
  TEST_CASE("subtracts") { CHECK(one("r = 2.5 - 4.0;").f64("r") == -1.5); }
  TEST_CASE("multiplies") { CHECK(one("r = 2.5 * 4.0;").f64("r") == 10); }
  TEST_CASE("divides") { CHECK(close(one("r = 1.0 / 4.0;").f64("r"), 0.25)); }
  TEST_CASE("negates") { CHECK(one("r = -(2.5);").f64("r") == -2.5); }

  TEST_CASE("chains, left to right") { CHECK(one("r = 1.5 + 2.5 * 2.0;").f64("r") == 6.5); }

  TEST_CASE("keeps precision an int would lose") { CHECK(close(one("r = 1.0 / 3.0;").f64("r"), 1.0 / 3)); }

  TEST_CASE("works on variables") {
    CHECK(ran(R"(
      double r; double a; double b;
      int main(void) { a = 1.25; b = 0.75; r = a + b; return 0; }
    )").f64("r") == 2);
  }

  TEST_CASE("works on a local") {
    CHECK(ran(R"(
      double r;
      int main(void) { double t; t = 6.0; t = t / 4.0; r = t; return 0; }
    )").f64("r") == 1.5);
  }

  TEST_CASE("runs in a loop") {
    CHECK(close(ran(R"(
      double r;
      int main(void) { int i; r = 1.0; for (i = 0; i < 10; i++) r = r * 2.0; return 0; }
    )", 8000000).f64("r"), 1024));
  }
}

TEST_SUITE("between int and double") {
  TEST_CASE("widens an int to a double") {
    CHECK(ran(R"(
      double r; int n;
      int main(void) { n = 7; r = n; return 0; }
    )").f64("r") == 7);
  }

  TEST_CASE("widens a negative int") {
    CHECK(ran(R"(
      double r; int n;
      int main(void) { n = -5; r = n; return 0; }
    )").f64("r") == -5);
  }

  TEST_CASE("mixes an int into an expression") {
    CHECK(ran(R"(
      double r; int n;
      int main(void) { n = 3; r = 0.5 * n; return 0; }
    )").f64("r") == 1.5);
  }

  TEST_CASE("narrows a double to an int, truncating toward zero") {
    CHECK(ran(R"(
      int r; double d;
      int main(void) { d = -3.9; r = (int)d; return 0; }
    )").i16("r") == -3);
  }

  TEST_CASE("narrows a positive one the same way") {
    CHECK(ran(R"(
      int r; double d;
      int main(void) { d = 9.75; r = (int)d; return 0; }
    )").i16("r") == 9);
  }
}

TEST_SUITE("comparisons") {
  auto cmp = [](const std::string& e) {
    return ran(R"(
    unsigned char r; double a; double b;
    int main(void) { a = 1.5; b = 2.5; r = ()" + e + R"() ? 1 : 0; return 0; }
  )").u8("r");
  };

  TEST_CASE("compares both ways") {
    CHECK(cmp("a < b") == 1);
    CHECK(cmp("b < a") == 0);
    CHECK(cmp("a > b") == 0);
    CHECK(cmp("b > a") == 1);
  }

  TEST_CASE("does equality") {
    CHECK(cmp("a == a") == 1);
    CHECK(cmp("a == b") == 0);
    CHECK(cmp("a != b") == 1);
  }

  TEST_CASE("does the inclusive pair") {
    CHECK(cmp("a <= a") == 1);
    CHECK(cmp("b >= a") == 1);
    CHECK(cmp("b <= a") == 0);
  }

  TEST_CASE("compares a double against an int") {
    CHECK(ran(R"(
      unsigned char r; double d;
      int main(void) { d = 2.5; r = (d > 2) ? 1 : 0; return 0; }
    )").u8("r") == 1);
  }

  TEST_CASE("branches on one") {
    CHECK(ran(R"(
      double r;
      int main(void) { r = 0.0; if (1.5 < 2.5) r = 9.5; return 0; }
    )").f64("r") == 9.5);
  }
}

TEST_SUITE("functions") {
  TEST_CASE("returns a double") {
    CHECK(ran(R"(
      double r;
      double half(void) { return 0.5; }
      int main(void) { r = half(); return 0; }
    )").f64("r") == 0.5);
  }

  TEST_CASE("takes one as an argument") {
    CHECK(ran(R"(
      double r;
      double twice(double x) { return x + x; }
      int main(void) { r = twice(1.25); return 0; }
    )").f64("r") == 2.5);
  }

  TEST_CASE("takes an int and a double together") {
    CHECK(ran(R"(
      double r;
      double scale(int n, double x) { return x * n; }
      int main(void) { r = scale(3, 1.5); return 0; }
    )").f64("r") == 4.5);
  }

  TEST_CASE("recurses with doubles") {
    CHECK(close(ran(R"(
      double r;
      double pow2(int n) { if (n <= 0) return 1.0; return 2.0 * pow2(n - 1); }
      int main(void) { r = pow2(8); return 0; }
    )", 8000000).f64("r"), 256));
  }
}

TEST_SUITE("what a double still cannot do") {
  TEST_CASE("refuses the bitwise operators, which have no meaning on one") {
    CHECK(has(refuses("double a; double r; int main(void){ r = a & a; return 0; }"), "double"));
  }

  TEST_CASE("refuses a remainder") {
    CHECK(has(refuses("double a; double r; int main(void){ r = a % a; return 0; }"), "double"));
  }

  TEST_CASE("refuses a shift") {
    CHECK(has(refuses("double a; double r; int main(void){ r = a << 1; return 0; }"), "double"));
  }

  TEST_CASE("still refuses long, which is not built") {
    CHECK(has(refuses("long n; int main(void){ return 0; }"), "long"));
  }
}
