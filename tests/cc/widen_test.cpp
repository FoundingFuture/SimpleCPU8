// A byte widened to an int: a signed char fills the high byte with its sign,
// an unsigned one with zero. The sign fill is SHL, a load and SBC, so these
// pin it over the values either side of bit 7. Negative initializers and a
// ?: that picks a byte are covered here as well.

#include <doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

TEST_SUITE("widening a byte") {
  TEST_CASE("a signed char keeps its negative starting value") {
    const Ran r = ran(
        "signed char g = -5; signed char h = -128; int wg; int wh; int wl;\n"
        "int main(void) { signed char l = -1; wg = g; wh = h; wl = l; return 0; }");
    CHECK_EQ(r.i16("wg"), -5);
    CHECK_EQ(r.i16("wh"), -128);
    CHECK_EQ(r.i16("wl"), -1);
  }

  TEST_CASE("an int function returns a char with the right high byte") {
    const Ran r = ran(
        "signed char s[4] = {0, 127, -128, -1};\n"
        "unsigned char u[4] = {0, 127, 128, 255};\n"
        "int rs[4]; int ru[4];\n"
        "int fs(unsigned char i) { return s[i]; }\n"
        "int fu(unsigned char i) { return u[i]; }\n"
        "int main(void) { unsigned char i; for (i = 0; i < 4; i++) { rs[i] = fs(i); ru[i] = fu(i); } return 0; }");
    const size_t bs = static_cast<size_t>(r.addr("rs"));
    const size_t bu = static_cast<size_t>(r.addr("ru"));
    const int want_s[4] = {0, 127, -128, -1};
    const int want_u[4] = {0, 127, 128, 255};
    for (size_t i = 0; i < 4; i++) {
      const int vs = static_cast<int16_t>(r.m->ram[bs + 2 * i] << 8 | r.m->ram[bs + 2 * i + 1]);
      const int vu = r.m->ram[bu + 2 * i] << 8 | r.m->ram[bu + 2 * i + 1];
      CHECK_EQ(vs, want_s[i]);
      CHECK_EQ(vu, want_u[i]);
    }
  }

  TEST_CASE("a ?: between two signed chars widens the one it picks") {
    const Ran r = ran(
        "signed char a = -3; signed char b = 4; int x; int y;\n"
        "int main(void) { int c = 1; x = c ? a : b; c = 0; y = c ? a : b; return 0; }");
    CHECK_EQ(r.i16("x"), -3);
    CHECK_EQ(r.i16("y"), 4);
  }

  TEST_CASE("the sign fill takes no branch") {
    const std::string text = compile("signed char g; int w; int main(void) { w = g; return 0; }");
    CHECK(has(text, "SBC A <- 0"));
    CHECK_EQ(countOf(text, "JN "), 0);
  }
}
