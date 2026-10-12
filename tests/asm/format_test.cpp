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
             "\tJZ     done\n"
             "\tHLT");
  }

  // A tab is 8 columns, and the editor draws one truly only at the start
  // of a line. So the indentation is tabs, and the space after a label
  // or before a comment stays spaces.
  TEST_CASE("a line without a label is indented with tabs") {
    CHECK_EQ(formatAssembly("NOP"), "\tNOP");
    CHECK_EQ(formatAssembly("a: NOP"), "a:      NOP");
    AsmLayout wide;
    wide.mnemonicColumn = 12;
    CHECK_EQ(formatAssembly("NOP\na: NOP", wide), "\t    NOP\na:          NOP");
  }

  TEST_CASE("a source indented with tabs lays out as one indented with spaces") {
    CHECK_EQ(formatAssembly("\tLD A <- 1 ; one\nx:\tNOP\t; two"), formatAssembly("    LD A <- 1 ; one\nx: NOP ; two"));
  }

  TEST_CASE("a long code label goes on a line of its own") {
    CHECK_EQ(formatAssembly("attract: JSR drainkeys"), "attract:\n\tJSR    drainkeys");
    CHECK_EQ(formatAssembly("gsdone:"), "gsdone:");
  }

  TEST_CASE("a label in .ram stays with its directive") {
    CHECK_EQ(formatAssembly(".ram\npalbuffer: .addr($2000)\nx: db 0"),
             "\t.ram\n"
             "palbuffer: .addr($2000)\n"
             "x:      db     0");
  }

  TEST_CASE("comments") {
    // A comment line keeps its indentation; trailing comments go to
    // column 32 when the source has no column of its own.
    CHECK_EQ(formatAssembly("; header\n    ; indented\nLD A <- 1  ; one\nLD B <- 2   ; two\nNOP ; three\n"),
             "; header\n"
             "    ; indented\n"
             "\tLD     A <- 1           ; one\n"
             "\tLD     B <- 2           ; two\n"
             "\tNOP                     ; three\n");
    // A source with a chosen column keeps it.
    CHECK_EQ(formatAssembly("x: LD A <- 1                ; one\nNOP                         ; two"),
             "x:      LD     A <- 1       ; one\n"
             "\tNOP                 ; two");
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
