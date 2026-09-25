// Small static functions are expanded where they are called. What the
// program computes must not change, whatever the body does.

#include <doctest.h>

#include <string>

#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

TEST_SUITE("inlining") {
  TEST_CASE("a small static function leaves no call behind") {
    const std::string src =
        "static unsigned char twice(unsigned char c) { return c + c; }\n"
        "unsigned char r;\n"
        "int main(void) { r = twice(21); return 0; }\n";
    CHECK_FALSE(has(compile(src), "JSR twice"));
    CHECK_EQ(ran(src).u8("r"), 42);
  }

  TEST_CASE("a function that is not static stays a call") {
    CHECK(has(compile("unsigned char twice(unsigned char c) { return c + c; }\n"
                      "unsigned char r; int main(void) { r = twice(21); return 0; }\n"),
              "JSR twice"));
  }

  TEST_CASE("the caller's half-done expression survives the body") {
    const Ran r = ran(
        "static int sq(int x) { int y; y = x * x; return y; }\n"
        "int a; int b;\n"
        "int main(void) { int k = 3; a = k + sq(k + 1) * 2 + sq(2); b = sq(sq(2)) - k; return 0; }\n");
    CHECK_EQ(r.i16("a"), 3 + 16 * 2 + 4);
    CHECK_EQ(r.i16("b"), 16 - 3);
  }

  TEST_CASE("returns from inside loops and branches, and a parameter written") {
    const Ran r = ran(
        "static unsigned char first(unsigned char *s, unsigned char c) {\n"
        "  unsigned char i;\n"
        "  for (i = 0; s[i]; i++) { if (s[i] == c) return i; }\n"
        "  c = 255;\n"
        "  return c;\n"
        "}\n"
        "static unsigned char upper(unsigned char c) { if (c >= 97 && c <= 122) return c - 32; return c; }\n"
        "static unsigned char isup(unsigned char c) { c = upper(c); return c >= 65 && c <= 90; }\n"
        "unsigned char a; unsigned char b; unsigned char c; unsigned char d; unsigned char e;\n"
        "int main(void) {\n"
        "  a = first(\"HELLO\", 'L'); b = first(\"HELLO\", 'Z');\n"
        "  c = upper('q'); d = isup('q') + isup('!') * 2; e = upper(upper('a'));\n"
        "  return 0;\n"
        "}\n");
    CHECK_EQ(r.u8("a"), 2);
    CHECK_EQ(r.u8("b"), 255);
    CHECK_EQ(r.u8("c"), 'Q');
    CHECK_EQ(r.u8("d"), 1);
    CHECK_EQ(r.u8("e"), 'A');
  }

  TEST_CASE("recursion stays a call") {
    const Ran r = ran(
        "static int fact(int n) { if (n < 2) return 1; return n * fact(n - 1); }\n"
        "int r; int main(void) { r = fact(6); return 0; }\n");
    CHECK_EQ(r.i16("r"), 720);
  }

  TEST_CASE("a static local keeps its value across expansions") {
    const Ran r = ran(
        "static int next(void) { static int n; n = n + 1; return n; }\n"
        "int a; int b; int main(void) { next(); next(); a = next(); b = next() + next(); return 0; }\n");
    CHECK_EQ(r.i16("a"), 3);
    CHECK_EQ(r.i16("b"), 4 + 5);
  }

  TEST_CASE("a call inside an inlined body saves the caller's temps") {
    const Ran r = ran(
        "int g(int x) { return x + 100; }\n"
        "static int h(int x) { return g(x) * 2; }\n"
        "int a; int main(void) { int k = 5; a = k * 3 + h(k) + k; return 0; }\n");
    CHECK_EQ(r.i16("a"), 15 + 210 + 5);
  }
}
