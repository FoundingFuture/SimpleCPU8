// Conditions compile to branches: a comparison in an if or a loop jumps on
// the flags of its subtract, && and || jump past each other, and a pointer
// is tested by loading it. These pin the results over the edges where a
// wrong flag shows: zero, the sign boundary, and the ends of the range.

#include <doctest.h>

#include <string>
#include <vector>

#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

namespace {

// Bit k of each result is one comparison, in branch form and value form.
// Bits 6 and 7 must stay clear: they catch a value form that disagrees
// with its own inverse.
int expected(long a, long b) {
  int r = 0;
  if (a < b) r |= 1;
  if (a <= b) r |= 2;
  if (a > b) r |= 4;
  if (a >= b) r |= 8;
  if (a == b) r |= 16;
  if (a != b) r |= 32;
  return r;
}

std::string table(const std::string& type, const std::vector<long>& vals) {
  std::string init;
  for (size_t i = 0; i < vals.size(); i++) init += (i ? ", " : "") + std::to_string(vals[i]);
  const std::string n = std::to_string(vals.size());
  return type + " vals[" + n + "] = {" + init + "};\n" +
         "unsigned char res[" + std::to_string(vals.size() * vals.size()) + "];\n"
         "int main(void) {\n"
         "  int i; int j; int k = 0;\n"
         "  for (i = 0; i < " + n + "; i++) {\n"
         "    for (j = 0; j < " + n + "; j++) {\n"
         "      " + type + " a = vals[i];\n"
         "      " + type + " b = vals[j];\n"
         "      unsigned char r = 0;\n"
         "      if (a < b) r |= 1;\n"
         "      if (a <= b) r |= 2;\n"
         "      if (a > b) r |= 4;\n"
         "      if (a >= b) r |= 8;\n"
         "      if (a == b) r |= 16;\n"
         "      if (a != b) r |= 32;\n"
         "      if ((a < b) + (a >= b) != 1) r |= 64;\n"
         "      if ((a == b) + (a != b) != 1 || !(a > b) != (a <= b)) r |= 128;\n"
         "      res[k] = r;\n"
         "      k++;\n"
         "    }\n"
         "  }\n"
         "  return 0;\n"
         "}\n";
}

void checkTable(const std::string& type, const std::vector<long>& vals) {
  for (bool optimal : {false, true}) {
    const Ran r = run(table(type, vals), 3000000, optimal);
    REQUIRE(r.halted());
    const int base = r.addr("res");
    for (size_t i = 0; i < vals.size(); i++) {
      for (size_t j = 0; j < vals.size(); j++) {
        INFO(type, " ", vals[i], " vs ", vals[j]);
        CHECK_EQ(int(r.m->ram[static_cast<size_t>(base) + i * vals.size() + j]), expected(vals[i], vals[j]));
      }
    }
  }
}

}  // namespace

TEST_SUITE("conditions as branches") {
  TEST_CASE("signed int comparisons across the sign boundary") {
    checkTable("int", {-32768, -32767, -129, -128, -1, 0, 1, 127, 128, 255, 256, 32766, 32767});
  }

  TEST_CASE("unsigned int comparisons across the top bit") {
    checkTable("unsigned int", {0, 1, 127, 128, 255, 256, 32767, 32768, 65534, 65535});
  }

  TEST_CASE("signed and unsigned char comparisons") {
    checkTable("signed char", {-128, -127, -1, 0, 1, 126, 127});
    checkTable("unsigned char", {0, 1, 127, 128, 254, 255});
  }

  TEST_CASE("&& and || short circuit in branches and in values") {
    const Ran r = ran(
        "int calls; int t(int v) { calls++; return v; }\n"
        "int a; int b; int c; int d; int e; int f; int g; int h;\n"
        "int main(void) {\n"
        "  if (t(0) && t(1)) a = 1; else a = 2;\n"
        "  if (t(1) || t(0)) b = 1; else b = 2;\n"
        "  if (t(1) && t(0)) c = 1; else c = 2;\n"
        "  if (t(0) || t(0)) d = 1; else d = 2;\n"
        "  e = t(3) && t(4);\n"
        "  f = t(0) || t(5);\n"
        "  g = !(t(1) && !t(0));\n"
        "  if (!(t(0) || t(1)) || (t(2) && t(3))) h = 1; else h = 2;\n"
        "  return 0;\n"
        "}\n");
    CHECK_EQ(r.i16("a"), 2);
    CHECK_EQ(r.i16("b"), 1);
    CHECK_EQ(r.i16("c"), 2);
    CHECK_EQ(r.i16("d"), 2);
    CHECK_EQ(r.i16("e"), 1);
    CHECK_EQ(r.i16("f"), 1);
    CHECK_EQ(r.i16("g"), 0);
    CHECK_EQ(r.i16("h"), 1);
    // 1 + 1 + 2 + 2 + 2 + 2 + 2 + 4.
    CHECK_EQ(r.i16("calls"), 16);
  }

  TEST_CASE("a condition inside an expression keeps the operands before it") {
    const Ran r = ran(
        "int x; int y; int c;\n"
        "int main(void) { int a = 5; c = 1; x = a + (c ? 10 : 20); c = 0; y = a * 2 + (c > 0 ? 10 : 20); return 0; }");
    CHECK_EQ(r.i16("x"), 15);
    CHECK_EQ(r.i16("y"), 30);
  }

  TEST_CASE("loops test at the bottom and still honour continue and break") {
    const Ran r = ran(
        "int w; int d; int f; int z;\n"
        "int main(void) {\n"
        "  int i = 0;\n"
        "  while (i < 10) { i++; if (i == 3) continue; if (i == 8) break; w += i; }\n"
        "  i = 0;\n"
        "  do { i++; if (i & 1) continue; d += i; } while (i < 9);\n"
        "  for (i = 0; i < 6; i++) { if (i == 2) continue; f += i; }\n"
        "  for (i = 5; i < 3; i++) z = 99;\n"
        "  while (0) z = 98;\n"
        "  return 0;\n"
        "}\n");
    CHECK_EQ(r.i16("w"), 1 + 2 + 4 + 5 + 6 + 7);
    CHECK_EQ(r.i16("d"), 2 + 4 + 6 + 8);
    CHECK_EQ(r.i16("f"), 0 + 1 + 3 + 4 + 5);
    CHECK_EQ(r.i16("z"), 0);
  }

  TEST_CASE("a pointer is tested by loading it") {
    const std::string src =
        "int c1 = 1; int c2 = 2; int c3 = 4; int* next[4]; int sum; int nul; int set;\n"
        "int main(void) {\n"
        "  int i = 0; int* p;\n"
        "  next[0] = &c1; next[1] = &c2; next[2] = &c3; next[3] = 0;\n"
        "  for (p = next[0]; p; p = next[i]) { sum += *p; i++; }\n"
        "  if (!next[3]) nul = 1;\n"
        "  if (next[0] && next[2]) set = 1;\n"
        "  return 0;\n"
        "}\n";
    const Ran r = ran(src);
    CHECK_EQ(r.i16("sum"), 7);
    CHECK_EQ(r.i16("nul"), 1);
    CHECK_EQ(r.i16("set"), 1);
    const std::string text = compile(src);
    // The loop's test is the load itself: no OR of the two halves.
    CHECK(has(text, "JNZ"));
  }

  TEST_CASE("a counted loop ends in one inverted jump") {
    const std::string text = compile("unsigned char n; int main(void) { unsigned char i; for (i = 0; i != 10; i++) n++; return 0; }");
    CHECK(has(text, "JNZ"));
    CHECK_EQ(countOf(text, "JZ "), 0);
  }
}
