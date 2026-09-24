#include <doctest.h>

#include <optional>
#include <string>
#include <vector>

#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

// The zero page allocator. See docs/design/c-compiler-design.md.
//
// The zero page is not an optimisation on this machine: it is the only place
// a byte can be reached without spending a pointer register. So who gets it
// matters, and the rule is a count that weights a loop.

namespace {

cc::Program build(const std::string& src) { return cc::compileProgram({{"main.c", src}}); }

std::vector<cc::ZpEntry> zp(const std::string& src) { return build(src).zeroPage; }

std::optional<int> addrOf(const std::string& src, const std::string& name) {
  for (const cc::ZpEntry& z : zp(src)) if (z.name == name) return z.addr;
  return std::nullopt;
}

const cc::ZpEntry* entry(const std::vector<cc::ZpEntry>& map, const std::string& name) {
  for (const cc::ZpEntry& z : map) if (z.name == name) return &z;
  return nullptr;
}

int temps(const std::string& src) {
  int n = 0;
  for (const cc::ZpEntry& z : zp(src)) if (z.name.starts_with("__t")) n++;
  return n;
}

}  // namespace

TEST_SUITE("the compiler's own reservations come first") {
  TEST_CASE("puts the stack pointer at the very bottom") {
    const std::vector<cc::ZpEntry> map = zp("int main(void){return 0;}");
    CHECK(map[0].name == "__sp");
    CHECK(map[0].addr == 0);
  }

  TEST_CASE("reserves the return word, the compare byte and the runtime operands") {
    const std::vector<cc::ZpEntry> map = zp("int main(void){return 0;}");
    for (const char* n : {"__sp", "__ret", "__cmp", "__ra", "__rb"}) CHECK_MESSAGE(entry(map, n) != nullptr, n);
  }

  TEST_CASE("reserves one temp slot per level of the deepest expression") {
    const int shallow = temps("int a, b; int main(void){ a = b; return 0; }");
    // A variable is named in the instruction and takes no temp, so the
    // depth comes from subtractions of sums, which cannot swap sides.
    const int deep = temps("int a, b, c, d; int main(void){ a = (b + c) - ((c + d) - (d + b)); return 0; }");
    CHECK(deep > shallow);
  }
}

TEST_SUITE("__zp comes next, in declaration order") {
  TEST_CASE("beats a variable that is used far more often") {
    const std::string src = R"(
      unsigned char cold;
      __zp unsigned char pinned;
      int main(void) {
        int i;
        for (i = 0; i < 100; i++) { cold = cold + 1; }
        pinned = 1;
        return 0;
      }
    )";
    CHECK(*addrOf(src, "pinned") < *addrOf(src, "cold"));
  }

  TEST_CASE("keeps two of them in the order they were written") {
    const std::string src = R"(
      __zp unsigned char first;
      __zp unsigned char second;
      int main(void) { first = 1; second = 2; return 0; }
    )";
    CHECK(*addrOf(src, "first") < *addrOf(src, "second"));
  }

  TEST_CASE("says so, so the map explains itself") {
    const std::string src = "__zp unsigned char v; int main(void){ v = 1; return 0; }";
    CHECK(has(entry(zp(src), "v")->why, "__zp"));
  }

  // The error belongs here and nowhere else. It can only ever say that the
  // __zp set does not fit, never that an expression got deep and a variable
  // quietly moved.
  TEST_CASE("refuses when the __zp set does not fit, and says what took the room") {
    const std::string m = refuses(R"(
      __zp unsigned char big[300];
      int main(void) { big[0] = 1; return 0; }
    )");
    CHECK(has(m, "__zp"));
    CHECK(has(m, "300"));
    CHECK(has(m, "reserv"));
  }
}

TEST_SUITE("the rest goes by weighted count") {
  TEST_CASE("gives the zero page to the variable a loop touches") {
    const std::string src = R"(
      unsigned char rare;
      unsigned char hot;
      int main(void) {
        int i;
        rare = 1; rare = 2; rare = 3; rare = 4; rare = 5;
        for (i = 0; i < 10; i++) hot = hot + 1;
        return 0;
      }
    )";
    // Five mentions outside a loop lose to one inside it. Being in a loop at
    // all outranks not being in one, which is the answer that matters.
    CHECK(*addrOf(src, "hot") < *addrOf(src, "rare"));
  }

  TEST_CASE("weights a nested loop more than a single one") {
    const std::string src = R"(
      unsigned char one;
      unsigned char two;
      int main(void) {
        int i; int j;
        for (i = 0; i < 3; i++) one = one + 1;
        for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) two = two + 1;
        return 0;
      }
    )";
    CHECK(*addrOf(src, "two") < *addrOf(src, "one"));
  }

  TEST_CASE("reads a literal bound and uses it") {
    const std::string src = R"(
      unsigned char small;
      unsigned char large;
      int main(void) {
        int i;
        for (i = 0; i < 3; i++) small = small + 1;
        for (i = 0; i < 200; i++) large = large + 1;
        return 0;
      }
    )";
    CHECK(*addrOf(src, "large") < *addrOf(src, "small"));
  }

  TEST_CASE("caps a bound, so one long loop cannot swamp the program") {
    const std::string src = R"(
      unsigned char a;
      unsigned char b;
      int main(void) {
        int i;
        for (i = 0; i < 30000; i++) a = a + 1;
        for (i = 0; i < 100; i++) { b = b + 1; b = b + 1; b = b + 1; b = b + 1;
                                    b = b + 1; b = b + 1; b = b + 1; b = b + 1; }
        return 0;
      }
    )";
    // Uncapped, one mention times 30000 beats eight times 100 by far.
    CHECK(*addrOf(src, "b") < *addrOf(src, "a"));
  }

  TEST_CASE("counts a while loop too, where there is no bound to read") {
    const std::string src = R"(
      unsigned char idle;
      unsigned char busy;
      int main(void) {
        idle = 1; idle = 2;
        while (busy) busy = busy - 1;
        return 0;
      }
    )";
    CHECK(*addrOf(src, "busy") < *addrOf(src, "idle"));
  }
}

TEST_SUITE("the ranking is per byte, not per variable") {
  // The page is 256 bytes. A big array touched a fair number of times asks
  // for eighty times the room a counter does, and gets it if the totals are
  // compared rather than the work each byte buys.
  TEST_CASE("gives a hot byte the page ahead of a lukewarm array") {
    const std::string src = R"(
      unsigned char buf[80];
      unsigned char n;
      int main(void) {
        int i;
        for (i = 0; i < 4; i++) buf[i] = i;
        for (i = 0; i < 40; i++) n = n + 1;
        return 0;
      }
    )";
    CHECK(*addrOf(src, "n") < *addrOf(src, "buf"));
  }

  TEST_CASE("still lets a genuinely hot array in") {
    const std::string src = R"(
      unsigned char cold;
      unsigned char t[4];
      int main(void) {
        int i;
        cold = 1;
        for (i = 0; i < 50; i++) t[0] = t[0] + 1;
        return 0;
      }
    )";
    CHECK(*addrOf(src, "t") < *addrOf(src, "cold"));
  }

  TEST_CASE("says the work each byte bought, on anything bigger than one") {
    const std::string src = R"(
      unsigned int w;
      int main(void) { int i; for (i = 0; i < 5; i++) w = w + 1; return 0; }
    )";
    CHECK(has(entry(zp(src), "w")->why, "a byte"));
  }
}

TEST_SUITE("what does not fit goes above, and nothing is lost") {
  TEST_CASE("puts a big array above the zero page rather than filling it") {
    const std::string src = R"(
      unsigned char big[400];
      unsigned char small;
      int main(void) { big[0] = 1; small = 2; return 0; }
    )";
    const std::vector<cc::ZpEntry> map = zp(src);
    CHECK(entry(map, "big") == nullptr);
    CHECK(entry(map, "small") != nullptr);
  }

  TEST_CASE("still compiles and runs a program with more variables than fit") {
    std::string src;
    for (int i = 0; i < 200; i++) src += "unsigned int v" + std::to_string(i) + ";\n";
    src += "int main(void) { v0 = 1; v199 = 2; return 0; }";
    CHECK(has(build(src).text, "v199:"));
  }

  TEST_CASE("never reports a zero page entry past 255") {
    std::string src;
    for (int i = 0; i < 200; i++) src += "unsigned int v" + std::to_string(i) + ";\n";
    src += "int main(void) { v0 = 1; return 0; }";
    for (const cc::ZpEntry& z : zp(src)) CHECK(z.addr + z.size <= 256);
  }
}

TEST_SUITE("the map explains itself") {
  TEST_CASE("gives every entry an address, a size and a reason") {
    for (const cc::ZpEntry& z : zp("unsigned char v; int main(void){ v = 1; return 0; }")) {
      CHECK(z.addr >= 0);
      CHECK(z.size > 0);
      CHECK(!z.why.empty());
    }
  }

  TEST_CASE("lists entries in address order") {
    const std::vector<cc::ZpEntry> map = zp(R"(
      unsigned char a; unsigned char b; unsigned int c;
      int main(void) { a = 1; b = 2; c = 3; return 0; }
    )");
    for (size_t i = 1; i < map.size(); i++) CHECK(map[i].addr >= map[i - 1].addr);
  }
}

TEST_SUITE("a system page ahead of the compiler") {
  TEST_CASE("zpReserve leaves the first bytes to the program and shifts everything up") {
    CcOptions opts;
    opts.zpReserve = 32;
    const cc::Program p = cc::compileProgram({{"main.c", "int a; int main(void){ a = 1; return 0; }"}}, opts);
    REQUIRE(!p.zeroPage.empty());
    CHECK(p.zeroPage[0].name == "__sys");
    CHECK(p.zeroPage[0].addr == 0);
    CHECK(p.zeroPage[0].size == 32);
    const cc::ZpEntry* sp = entry(p.zeroPage, "__sp");
    REQUIRE(sp);
    CHECK(sp->addr == 32);
  }
}
