#include <doctest.h>

#include <stdexcept>
#include <string>

#include "core/microcode.h"
#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

// -msoft-mul. The same a * b, compiled the other way.
//
// The two runtimes have the same entry points, so the test that matters is
// that they AGREE. A software divide that is subtly wrong is worse than none:
// a student would compile both and be told the machine is inconsistent.

namespace {

uint64_t lastInstructions = 0;

cc::Program build(const std::string& src, bool soft) {
  CcOptions opts;
  if (soft) opts.defines["SOFT_MUL"] = "1";
  return cc::compileProgram({{"main.c", src}}, opts);
}

int runOne(const std::string& src, bool soft, uint64_t budget = 4000000) {
  const cc::Program p = build(src, soft);
  const Ran r = runAsm(p.text, budget, true);
  if (!r.halted()) {
    throw std::runtime_error(std::string(soft ? "soft" : "acp") + ": program is " +
                             std::string(statusName(r.m->status)));
  }
  lastInstructions = r.m->instructions;
  return r.u16("r");
}

int asSigned(int v) { return v >= 0x8000 ? v - 0x10000 : v; }

struct Both {
  int soft;
  int acp;
};

// Both ways, and they have to agree with each other AND with the answer.
Both both(const std::string& src) { return {runOne(src, true), runOne(src, false)}; }

std::string prog(const std::string& type, const std::string& expr) {
  return type + " r; int main(void) { r = " + expr + "; return 0; }";
}

}  // namespace

TEST_SUITE("the switch reaches the runtime") {
  TEST_CASE("leaves the coprocessor out entirely") {
    const cc::Program p = build(prog("unsigned int", "300 * 4"), true);
    CHECK(!has(p.text, "OUT ACP_CMD"));
    CHECK(!has(p.text, "__acp:"));
  }

  TEST_CASE("still uses it when the switch is off") {
    const cc::Program p = build(prog("unsigned int", "300 * 4"), false);
    CHECK(has(p.text, "OUT ACP_CMD, ACP_MUL"));
  }

  TEST_CASE("comes on from a build line, which is where a flag belongs") {
    const cc::Program p = cc::compileProgram({{
        "main.c", "// build: cc -o m main.c -msoft-mul\n" + prog("unsigned int", "6 * 7")}});
    CHECK(p.plan.softMul);
    CHECK(!has(p.text, "OUT ACP_CMD"));
  }

  // The lesson is what the machine RUNS, not what the compiler emitted. The
  // coprocessor runtime is more lines of setup and one command; the software
  // one is a loop that goes round sixteen times.
  TEST_CASE("runs far more instructions, which is the lesson") {
    runOne(prog("unsigned int", "300 * 4"), false);
    const uint64_t acp = lastInstructions;
    runOne(prog("unsigned int", "300 * 4"), true);
    const uint64_t soft = lastInstructions;
    CHECK_MESSAGE(soft > acp * 3, "soft ", soft, " acp ", acp);
  }

  TEST_CASE("is the divide that costs the most, because there is no shift at all") {
    runOne(prog("unsigned int", "60000 / 7"), false);
    const uint64_t acp = lastInstructions;
    runOne(prog("unsigned int", "60000 / 7"), true);
    CHECK(lastInstructions > acp * 3);
  }
}

TEST_SUITE("unsigned multiply agrees") {
  TEST_CASE("on every case") {
    for (const char* c : {"6 * 7", "300 * 4", "1000 * 60", "0 * 1234", "1 * 65535",
                          "255 * 257", "1000 * 1000", "12345 * 3"}) {
      const Both r = both(prog("unsigned int", c));
      CHECK_MESSAGE(r.soft == r.acp, c, ": soft ", r.soft, " acp ", r.acp);
    }
  }

  TEST_CASE("keeps only the low sixteen bits, like C") {
    CHECK(both(prog("unsigned int", "1000 * 1000")).soft == (1000000 & 0xffff));
  }
}

TEST_SUITE("signed multiply agrees") {
  TEST_CASE("on every case") {
    for (const char* c : {"-3 * 5", "-1 * -1", "1000 * -3", "-32768 * 1"}) {
      const Both r = both(prog("int", c));
      CHECK_MESSAGE(asSigned(r.soft) == asSigned(r.acp), c);
    }
  }
}

TEST_SUITE("unsigned divide and remainder agree") {
  TEST_CASE("on every case") {
    for (const char* c : {"100 / 7", "60000 / 3", "1 / 1", "0 / 5", "65535 / 255",
                          "7 / 8", "100 % 7", "65535 % 256", "5 % 5"}) {
      const Both r = both(prog("unsigned int", c));
      CHECK_MESSAGE(r.soft == r.acp, c, ": soft ", r.soft, " acp ", r.acp);
    }
  }

  TEST_CASE("divides by zero the way the machine's own divider does") {
    // div8 fills the quotient with ones and hands back the dividend as the
    // remainder. The software runtime matches it, which is what a student
    // stepping through both would expect.
    CHECK(runOne(prog("unsigned int", "1234 / 0"), true) == 0xffff);
    CHECK(runOne(prog("unsigned int", "1234 % 0"), true) == 1234);
  }
}

TEST_SUITE("signed divide and remainder agree") {
  TEST_CASE("on every case") {
    for (const char* c : {"-7 / 2", "7 / -2", "-7 / -2", "-7 % 2", "7 % -2", "-100 / 10"}) {
      const Both r = both(prog("int", c));
      CHECK_MESSAGE(asSigned(r.soft) == asSigned(r.acp), c);
    }
  }

  TEST_CASE("truncates toward zero, which is what C says") {
    CHECK(asSigned(runOne(prog("int", "-7 / 2"), true)) == -3);
    CHECK(asSigned(runOne(prog("int", "-7 % 2"), true)) == -1);
  }
}

TEST_SUITE("shifts agree") {
  TEST_CASE("on every case") {
    for (const char* c : {"1 << 3", "300 << 2", "1000 >> 3", "0x8000 >> 15", "65535 >> 8",
                          "1 >> 1", "1234 >> 0", "5 >> 16", "5 >> 20"}) {
      const Both r = both(prog("unsigned int", c));
      CHECK_MESSAGE(r.soft == r.acp, c, ": soft ", r.soft, " acp ", r.acp);
    }
  }

  TEST_CASE("shifts by a variable, which is the case a constant cannot cover") {
    const Both r = both(R"(
      unsigned int r; unsigned char n;
      int main(void) { n = 5; r = 3 << n; return 0; }
    )");
    CHECK(r.soft == 96);
    CHECK(r.acp == 96);
  }
}

TEST_SUITE("the arithmetic shift agrees, and floors") {
  TEST_CASE("on every case") {
    for (const char* c : {"-8 >> 1", "-1 >> 4", "-9 >> 1", "-32768 >> 15", "-3 >> 8"}) {
      const Both r = both(prog("int", c));
      CHECK_MESSAGE(asSigned(r.soft) == asSigned(r.acp), c);
    }
  }

  TEST_CASE("floors rather than truncating, which is the one thing divide cannot do") {
    CHECK(asSigned(runOne(prog("int", "-9 >> 1"), true)) == -5);
    CHECK(asSigned(runOne(prog("int", "-9 / 2"), true)) == -4);
  }
}

TEST_SUITE("a whole program runs the same either way") {
  TEST_CASE("computes a factorial with software multiply") {
    const Both b = both(R"(
      unsigned int r;
      unsigned int fact(unsigned int n) { if (n <= 1) return 1; return n * fact(n - 1); }
      int main(void) { r = fact(7); return 0; }
    )");
    CHECK(b.soft == 5040);
    CHECK(b.acp == 5040);
  }

  TEST_CASE("runs a loop of divides") {
    const Both b = both(R"(
      unsigned int r;
      int main(void) {
        unsigned int i;
        r = 0;
        for (i = 1; i < 20; i++) r = r + (1000 / i);
        return 0;
      }
    )");
    CHECK(b.soft == b.acp);
  }
}
