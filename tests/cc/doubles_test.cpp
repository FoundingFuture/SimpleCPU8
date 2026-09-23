#include <doctest.h>

#include <algorithm>
#include <cctype>
#include <string>

#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

// double, float and long. Every one of them is a coprocessor type: the CPU
// can move those bytes and do nothing else with them.
//
// Until each context is BUILT it has to be refused. A compiler that accepts
// `a = 3.14 + 1` and quietly computes 4 is the machine lying, which is the
// one thing it may not do.

namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

}  // namespace

// double is built now, on the coprocessor. long is not, and every place it
// can appear still has to refuse rather than quietly use the low two bytes.
TEST_SUITE("nowhere is an unbuilt wide type silently narrowed") {
  TEST_CASE("refuses a global, a local, a parameter, a return type and a cast") {
    const std::pair<const char*, const char*> cases[] = {
        {"a global", "long n; int main(void){ return 0; }"},
        {"a local", "int main(void){ long n; n = 1; return 0; }"},
        {"a parameter", "int g(long x){ return 1; }\nint main(void){ return g(1); }"},
        {"a return type", "long f(void){ return 1; }\nint main(void){ f(); return 0; }"},
        {"a cast", "int a; long n; int main(void){ a = (long)n; return 0; }"},
    };
    for (const auto& [what, src] : cases) {
      const std::string m = refuses(src);
      CHECK_MESSAGE(m != "no error", what, " was accepted");
      CHECK_MESSAGE(has(lower(m), "long"), what);
    }
  }

  // The message has to say WHY, or somebody reads it as the compiler being
  // arbitrary rather than as the machine being honest.
  TEST_CASE("says a coprocessor type is bytes the CPU cannot work on") {
    CHECK(has(refuses("long n; int main(void){ return 0; }"), "coprocessor"));
  }
}

// Where a double meets an integer context, the narrowing is EXPLICIT or it
// is refused. C would convert silently; here a silent conversion of a type
// the CPU cannot touch is the thing worth being loud about.
TEST_SUITE("a double still narrows only when asked") {
  TEST_CASE("takes an explicit cast") {
    CHECK(refuses("int a; double d; int main(void){ a = (int)d; return 0; }") == "no error");
  }

  TEST_CASE("widens an int into a double context without a cast, which loses nothing") {
    CHECK(refuses("double r; int n; int main(void){ r = n + 1.0; return 0; }") == "no error");
  }
}

TEST_SUITE("what is still fine") {
  TEST_CASE("takes an integer literal that happens to be round") {
    CHECK(refuses("int a; int main(void){ a = 2; return 0; }") == "no error");
  }

  TEST_CASE("takes a double literal, which is what this whole file used to refuse") {
    CHECK(refuses("double d; int main(void){ d = 3.14; return 0; }") == "no error");
  }

  TEST_CASE("takes a cast between integer types") {
    CHECK(refuses("int a; unsigned char b; int main(void){ a = (int)b; return 0; }") == "no error");
  }
}
