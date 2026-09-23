#include <doctest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "asm/asm.h"
#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

// A breakpoint on a line of C. The compiler says which assembly line each C
// line produced first, and the assembler says which instruction each
// assembly line became. The two together are a C line to a program address.

namespace {

struct Built {
  cc::Program p;
  Assembled a;

  // The first instruction at or after the assembly line a C line produced.
  int pcOf(const std::string& file, int line) const {
    auto it = p.lineOf.find(file + ":" + std::to_string(line));
    REQUIRE_MESSAGE(it != p.lineOf.end(), "no code for ", file, ":", line);
    for (size_t i = 0; i < a.instrToLine.size(); i++) {
      if (a.instrToLine[i] >= it->second) return static_cast<int>(i);
    }
    throw std::runtime_error("no instruction for " + file + ":" + std::to_string(line));
  }
};

Built build(const std::vector<SourceFile>& files) {
  Built b{cc::compileProgram(files), {}};
  b.a = assemble(b.p.text);
  CHECK(b.a.errors.empty());
  return b;
}

const char* SRC = R"(unsigned char a;
int main(void)
{
    a = 1;
    a = 2;
    a = 3;
    return 0;
}
)";

int lineCount(const std::string& text) {
  int n = 1;
  for (char c : text) if (c == '\n') n++;
  return n;
}

}  // namespace

TEST_SUITE("the C line map") {
  TEST_CASE("names a line for every statement") {
    const Built b = build({{"main.c", SRC}});
    for (int line : {4, 5, 6, 7}) {
      CHECK_MESSAGE(b.p.lineOf.count("main.c:" + std::to_string(line)) == 1, "line ", line);
    }
  }

  TEST_CASE("gives later lines later addresses") {
    const Built b = build({{"main.c", SRC}});
    const int p4 = b.pcOf("main.c", 4), p5 = b.pcOf("main.c", 5), p6 = b.pcOf("main.c", 6);
    CHECK(p5 > p4);
    CHECK(p6 > p5);
  }

  TEST_CASE("points at the line's FIRST instruction, so a stop is before it runs") {
    const Built b = build({{"main.c", SRC}});
    // a = 2 is a load of the constant, so the instruction at that address
    // has not yet written anything.
    const size_t pc = static_cast<size_t>(b.pcOf("main.c", 5));
    CHECK(b.a.instrToLine[pc - 1] < b.a.instrToLine[pc]);
  }

  TEST_CASE("keeps two files apart, because both have a line 4") {
    const Built b = build({
        {"main.c", "int other(void);\nint r;\nint main(void)\n{\n r = other();\n return 0;\n}\n"},
        {"two.c", "int r2;\nint other(void)\n{\n r2 = 9;\n return 1;\n}\n"},
    });
    CHECK(b.p.lineOf.at("main.c:5") != b.p.lineOf.at("two.c:4"));
    CHECK(b.p.lineOf.count("two.c:4") == 1);
  }

  TEST_CASE("counts assembly lines from one, against the finished text") {
    const Built b = build({{"main.c", SRC}});
    const int at = b.p.lineOf.at("main.c:4");
    // The line it names really is inside main, not in the header block.
    std::string before;
    int n = 0;
    for (char c : b.p.text) {
      if (n >= at) break;
      before += c;
      if (c == '\n') n++;
    }
    CHECK(has(before, "main:"));
    CHECK(at > 1);
  }

  TEST_CASE("has no line that points past the end of the text") {
    const Built b = build({{"main.c", SRC}});
    const int n = lineCount(b.p.text);
    for (const auto& [k, v] : b.p.lineOf) CHECK_MESSAGE(v <= n, k, " points at line ", v, " of ", n);
  }
}
