// Arithmetic with a constant operand runs on the CPU's shifts: shifts by a
// constant, multiplies by a constant with few set bits, and divides and
// remainders by a power of two. Each answer is checked against C++ over
// values that reach the sign bit and the ends of the range.

#include <doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

namespace {

const std::vector<int> VALUES = {0, 1, 2, 3, 7, 100, 255, 256, 1000, 12345, 32767, -1, -2, -7, -100, -256, -32768};

// One program per operation: every value through it, answers in r[].
std::vector<int> runOp(const std::string& type, const std::string& op) {
  std::string init;
  for (size_t i = 0; i < VALUES.size(); i++) init += (i ? ", " : "") + std::to_string(VALUES[i]);
  const std::string n = std::to_string(VALUES.size());
  const std::string src = type + " v[" + n + "] = {" + init + "};\n" + type + " r[" + n +
                          "];\n"
                          "int main(void) {\n"
                          "  unsigned char i;\n"
                          "  for (i = 0; i < " + n + "; i++) { " + type + " x = v[i]; r[i] = " + op + "; }\n"
                          "  return 0;\n"
                          "}\n";
  const Ran run1 = run(src, 400000, false);
  const Ran run2 = run(src, 400000, true);
  REQUIRE(run1.halted());
  REQUIRE(run2.halted());
  std::vector<int> out;
  const int base = run1.addr("r");
  for (size_t i = 0; i < VALUES.size(); i++) {
    const size_t at = static_cast<size_t>(base) + 2 * i;
    const int v = run1.m->ram[at] << 8 | run1.m->ram[at + 1];
    const int w = run2.m->ram[at] << 8 | run2.m->ram[at + 1];
    CHECK_EQ(v, w);
    out.push_back(v);
  }
  return out;
}

int16_t s16(int v) { return static_cast<int16_t>(v); }
uint16_t u16(int v) { return static_cast<uint16_t>(v); }

}  // namespace

TEST_SUITE("constant arithmetic on the CPU") {
  TEST_CASE("signed int") {
    struct Case {
      const char* op;
      int (*f)(int16_t);
    };
    const Case cases[] = {
        {"x << 1", [](int16_t x) { return int(u16(x << 1)); }},
        {"x << 7", [](int16_t x) { return int(u16(x << 7)); }},
        {"x << 9", [](int16_t x) { return int(u16(x << 9)); }},
        {"x >> 1", [](int16_t x) { return int(u16(x >> 1)); }},
        {"x >> 5", [](int16_t x) { return int(u16(x >> 5)); }},
        {"x >> 8", [](int16_t x) { return int(u16(x >> 8)); }},
        {"x >> 15", [](int16_t x) { return int(u16(x >> 15)); }},
        {"x * 10", [](int16_t x) { return int(u16(x * 10)); }},
        {"x * 3", [](int16_t x) { return int(u16(x * 3)); }},
        {"x * 16", [](int16_t x) { return int(u16(x * 16)); }},
        {"x * 0", [](int16_t) { return 0; }},
        {"x * 1000", [](int16_t x) { return int(u16(x * 1000)); }},
        {"x / 4", [](int16_t x) { return int(u16(x / 4)); }},
        {"x / 256", [](int16_t x) { return int(u16(x / 256)); }},
        {"x / 1", [](int16_t x) { return int(u16(x)); }},
        {"x % 8", [](int16_t x) { return int(u16(x % 8)); }},
    };
    for (const Case& c : cases) {
      const std::vector<int> got = runOp("int", c.op);
      for (size_t i = 0; i < VALUES.size(); i++) {
        INFO(c.op, " with x = ", VALUES[i]);
        CHECK_EQ(got[i], c.f(s16(VALUES[i])));
      }
    }
  }

  TEST_CASE("unsigned int") {
    struct Case {
      const char* op;
      int (*f)(uint16_t);
    };
    const Case cases[] = {
        {"x >> 3", [](uint16_t x) { return int(x >> 3); }},
        {"x >> 12", [](uint16_t x) { return int(x >> 12); }},
        {"x << 4", [](uint16_t x) { return int(u16(x << 4)); }},
        {"x * 5", [](uint16_t x) { return int(u16(x * 5)); }},
        {"x / 8", [](uint16_t x) { return int(x / 8); }},
        {"x / 16384", [](uint16_t x) { return int(x / 16384); }},
        {"x % 256", [](uint16_t x) { return int(x % 256); }},
        {"x % 32", [](uint16_t x) { return int(x % 32); }},
    };
    for (const Case& c : cases) {
      const std::vector<int> got = runOp("unsigned int", c.op);
      for (size_t i = 0; i < VALUES.size(); i++) {
        INFO(c.op, " with x = ", VALUES[i]);
        CHECK_EQ(got[i], c.f(u16(VALUES[i])));
      }
    }
  }

  TEST_CASE("bytes shift in A") {
    const Ran r = ran(
        "unsigned char a; unsigned char b; unsigned char c; signed char d;\n"
        "int main(void) { unsigned char x = 201; a = x << 3; b = x >> 2; c = (x << 1) + 1; d = x << 1; return 0; }");
    CHECK_EQ(r.u8("a"), (201 << 3) & 255);
    CHECK_EQ(r.u8("b"), 201 >> 2);
    CHECK_EQ(r.u8("c"), ((201 << 1) + 1) & 255);
    CHECK_EQ(r.u8("d"), (201 << 1) & 255);
  }

  TEST_CASE("a multiply with many set bits goes to the coprocessor") {
    CHECK(has(compile("int a; int main(void) { a = a * 12345; return 0; }"), "__mul16"));
    CHECK_FALSE(has(compile("int a; int main(void) { a = a * 10; return 0; }"), "__mul16"));
  }
}
