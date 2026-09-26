// The editor's line numbering: sorting, the number for a new line, and
// renumbering with the references that follow the lines.

#include <doctest.h>

#include "basic/lines.h"

using namespace sc8::basic;

namespace {

std::string sorted(const std::string& text, int caret = 0, bool glue = false, int* caretOut = nullptr) {
  auto lines = splitLines(text);
  sortLines(lines, caret, glue);
  if (caretOut) *caretOut = caret;
  return joinLines(lines);
}

std::string renumbered(const std::string& text) {
  auto lines = splitLines(text);
  renumberLines(lines);
  return joinLines(lines);
}

}  // namespace

TEST_SUITE("the editor keeps BASIC lines in number order") {
  TEST_CASE("split and join give the text back, a final newline too") {
    CHECK_EQ(joinLines(splitLines("10 A\n20 B\n")), "10 A\n20 B\n");
    CHECK_EQ(splitLines("10 A\n20 B\n").size(), 3u);
    CHECK_EQ(joinLines(splitLines("")), "");
  }

  TEST_CASE("a line's number is the digits it starts with") {
    CHECK_EQ(lineNumber("  30 PRINT"), 30);
    CHECK_FALSE(lineNumber("PRINT 30"));
    CHECK_FALSE(lineNumber(""));
    CHECK(onlyNumber("40 "));
    CHECK_FALSE(onlyNumber("40 X"));
  }

  TEST_CASE("a line typed out of place moves to its number") {
    CHECK_EQ(sorted("10 A\n30 C\n20 B\n"), "10 A\n20 B\n30 C\n");
    CHECK_EQ(sorted("10 A\n20 B\n5 Z\n"), "5 Z\n10 A\n20 B\n");
    CHECK(linesInOrder(splitLines("10 A\n\n20 B")));
    CHECK_FALSE(linesInOrder(splitLines("20 B\n10 A")));
  }

  TEST_CASE("unnumbered lines move with the line above, and the ends stay put") {
    CHECK_EQ(sorted("REM TOP\n30 C\n\n10 A\n20 B\n"), "REM TOP\n10 A\n20 B\n30 C\n\n");
    // The final empty line is the tail and stays at the end.
    CHECK_EQ(sorted("30 C\n10 A\n"), "10 A\n30 C\n");
  }

  TEST_CASE("the caret follows its line") {
    int caret = -1;
    CHECK_EQ(sorted("10 A\n30 C\n20 B", 1, false, &caret), "10 A\n20 B\n30 C");
    CHECK_EQ(caret, 2);
  }

  TEST_CASE("a glued caret line at the end goes with the line above") {
    int caret = -1;
    // Enter pressed after "5 Z" at the end of the text: the new line
    // follows 5 to the top instead of staying at the end.
    CHECK_EQ(sorted("10 A\n20 B\n5 Z\n", 3, true, &caret), "5 Z\n\n10 A\n20 B");
    CHECK_EQ(caret, 1);
  }

  TEST_CASE("a program typed in any order comes out low to high") {
    CHECK_EQ(sorted("10 for i = 1 to 10\n25 circle 40\n6 paper 0\n7 ink 80\n30 next i\n5 x = 80\n"
                    "20 move x,128\n26 x=x+10\n\n\n"),
             "5 x = 80\n6 paper 0\n7 ink 80\n10 for i = 1 to 10\n20 move x,128\n25 circle 40\n"
             "26 x=x+10\n30 next i\n\n\n");
  }

  TEST_CASE("equal numbers keep their order") {
    CHECK_EQ(sorted("20 X\n10 A\n20 Y"), "10 A\n20 X\n20 Y");
  }
}

TEST_SUITE("the editor numbers a new line") {
  TEST_CASE("ten on, or halfway to the next line, or nothing when there is no room") {
    const auto lines = splitLines("10 A\n20 B\n21 C\n100 D");
    CHECK_EQ(numberAfter(lines, 100), 110);
    CHECK_EQ(numberAfter(lines, 10), 15);
    CHECK_FALSE(numberAfter(lines, 20));
    CHECK_EQ(numberAfter(lines, 21), 31);
    CHECK_EQ(numberAfter(lines, 0), 5);
    CHECK_EQ(numberAfter(splitLines(""), 0), 10);
    CHECK_FALSE(numberAfter(splitLines("65530 A"), 65530));
  }

  TEST_CASE("renumbering moves GOTO, GOSUB and THEN along") {
    CHECK_EQ(renumbered("1 PRINT \"GOTO 2\"\n2 IF A THEN 5\n3 GOSUB 5: GOTO 1\n5 REM GOTO 1\n"),
             "10 PRINT \"GOTO 2\"\n20 IF A THEN 40\n30 GOSUB 40: GOTO 10\n40 REM GOTO 1\n");
  }

  TEST_CASE("a reference to a line that does not exist is left as it is") {
    CHECK_EQ(renumbered("1 GOTO 99\n2 goto 1"), "10 GOTO 99\n20 goto 10");
  }
}
