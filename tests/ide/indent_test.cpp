// Tab and Backspace in the editor: a step is 4 columns, and an
// indentation of 8 columns is stored as one tab character.

#include <doctest.h>

#include <optional>
#include <string>

#include "ide/indent.h"

using sc8::backspaceKey;
using sc8::IndentLine;
using sc8::tabKey;

namespace {

// The line and the caret as one text, the caret shown as |.
std::string shown(const IndentLine& l) { return l.text.substr(0, l.caret) + "|" + l.text.substr(l.caret); }

std::string tab(const std::string& line, size_t caret, bool tabs = true) { return shown(tabKey(line, caret, tabs)); }

std::string back(const std::string& line, size_t caret, bool tabs = true) {
  const std::optional<IndentLine> l = backspaceKey(line, caret, tabs);
  return l ? shown(*l) : "untouched";
}

}  // namespace

TEST_SUITE("the editor's indentation keys") {
  TEST_CASE("Tab indents an empty line by 4 spaces") { CHECK_EQ(tab("", 0), "    |"); }

  TEST_CASE("a second Tab makes the 8 spaces one tab character") {
    CHECK_EQ(tab("    ", 4), "\t|");
    CHECK_EQ(tab("\t", 1), "\t    |");
    CHECK_EQ(tab("\t    ", 5), "\t\t|");
  }

  TEST_CASE("Tab goes to the next multiple of 4, not 4 further") {
    CHECK_EQ(tab("  ", 2), "    |");
    CHECK_EQ(tab("      x", 6), "\t|x");
  }

  TEST_CASE("Tab before a line's text indents the text") {
    CHECK_EQ(tab("LD A", 0), "    |LD A");
    CHECK_EQ(tab("    LD A", 4), "\t|LD A");
    CHECK_EQ(tab("\tLD A", 1), "\t    |LD A");
  }

  TEST_CASE("Tab inside the indentation indents the line, and the caret goes to its end") {
    CHECK_EQ(tab("        x", 2), "\t    |x");
    CHECK_EQ(tab("\tx", 0), "\t    |x");
  }

  TEST_CASE("Tab rewrites spaces before a tab, which a file may hold") {
    CHECK_EQ(tab("  \t x", 4), "\t    |x");
  }

  TEST_CASE("Tab after text adds spaces to the next multiple of 4, and never a tab") {
    CHECK_EQ(tab("LD", 2), "LD  |");
    CHECK_EQ(tab("abc", 3), "abc |");
    CHECK_EQ(tab("LD A", 4), "LD A    |");
    CHECK_EQ(tab("LD  ", 4), "LD      |");
    CHECK_EQ(tab("LDA", 2), "LD  |A");
  }

  TEST_CASE("Tab after text counts a tab at the line's start as 8 columns") {
    CHECK_EQ(tab("\tLD", 3), "\tLD  |");
    CHECK_EQ(tab("\tLD A", 5), "\tLD A    |");
  }

  TEST_CASE("an editor without tabs indents with spaces alone") {
    CHECK_EQ(tab("    ", 4, false), "        |");
    CHECK_EQ(tab("\t", 1, false), "            |");
  }

  TEST_CASE("Backspace in the indentation steps back to a multiple of 4") {
    CHECK_EQ(back("    ", 4), "|");
    CHECK_EQ(back("\t", 1), "    |");
    CHECK_EQ(back("\t    x", 5), "\t|x");
    CHECK_EQ(back("\t\tx", 2), "\t    |x");
    CHECK_EQ(back("      x", 6), "    |x");
    CHECK_EQ(back("     ", 5), "    |");
  }

  TEST_CASE("Backspace inside the indentation unindents the line") {
    CHECK_EQ(back("        x", 3), "    |x");
  }

  TEST_CASE("Backspace at the start of a line is left to join it to the line above") {
    CHECK_EQ(back("    x", 0), "untouched");
    CHECK_EQ(back("", 0), "untouched");
  }

  TEST_CASE("Backspace after text is left to delete one character") {
    CHECK_EQ(back("LD ", 3), "untouched");
    CHECK_EQ(back("    x", 5), "untouched");
    CHECK_EQ(back("\tLD  ", 5), "untouched");
  }

  TEST_CASE("Backspace in an editor without tabs leaves spaces") {
    CHECK_EQ(back("\t    x", 5, false), "        |x");
    CHECK_EQ(back("            ", 12, false), "        |");
  }
}
