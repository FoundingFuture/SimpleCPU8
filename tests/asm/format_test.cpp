// The assembly layout: columns, long labels, comments, and that a laid out
// source assembles to exactly what the original did.

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "asm/asm.h"
#include "asm/format.h"

using namespace sc8;

namespace {

std::string readFile(const std::filesystem::path& p) {
  std::ifstream in(p);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

TEST_SUITE("assembly layout") {
  TEST_CASE("mnemonics and operands line up") {
    // PUSHB and PUSHW are the longest mnemonics, five letters.
    CHECK_EQ(operandColumn({}), 15);
    CHECK_EQ(formatAssembly("loop: SUB A <- 1\n  JZ done\nHLT"),
             "loop:   SUB    A <- 1\n"
             "        JZ     done\n"
             "        HLT");
  }

  TEST_CASE("a long code label goes on a line of its own") {
    CHECK_EQ(formatAssembly("attract: JSR drainkeys"), "attract:\n        JSR    drainkeys");
    CHECK_EQ(formatAssembly("gsdone:"), "gsdone:");
  }

  TEST_CASE("a label in .ram stays with its directive") {
    CHECK_EQ(formatAssembly(".ram\npalbuffer: .addr($2000)\nx: db 0"),
             "        .ram\n"
             "palbuffer: .addr($2000)\n"
             "x:      db     0");
  }

  TEST_CASE("comments") {
    // A comment line keeps its indentation; trailing comments go to
    // column 32 when the source has no column of its own.
    CHECK_EQ(formatAssembly("; header\n    ; indented\nLD A <- 1  ; one\nLD B <- 2   ; two\nNOP ; three\n"),
             "; header\n"
             "    ; indented\n"
             "        LD     A <- 1           ; one\n"
             "        LD     B <- 2           ; two\n"
             "        NOP                     ; three\n");
    // A source with a chosen column keeps it.
    CHECK_EQ(formatAssembly("x: LD A <- 1                ; one\nNOP                         ; two"),
             "x:      LD     A <- 1       ; one\n"
             "        NOP                 ; two");
  }

  TEST_CASE("laying out twice changes nothing more") {
    const std::string once = formatAssembly("a: LD A <- 1 ; x\nlonglabel: JMP a\n.ram\nv: db 1, 2\n");
    CHECK_EQ(formatAssembly(once), once);
  }

  TEST_CASE("every example assembles to the same bytes after layout") {
    int checked = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(SC8_EXAMPLES_DIR)) {
      if (e.path().extension() != ".asm") continue;
      const std::string source = readFile(e.path());
      const std::string laid = formatAssembly(source);
      const Assembled a = assemble(source);
      const Assembled b = assemble(laid);
      INFO(e.path().string());
      CHECK_EQ(a.errors.size(), b.errors.size());
      CHECK(a.program == b.program);
      CHECK(a.ram == b.ram);
      CHECK(a.cart == b.cart);
      CHECK(a.labels == b.labels);
      CHECK_EQ(formatAssembly(laid), laid);
      checked++;
    }
    CHECK_GT(checked, 25);
  }
}
