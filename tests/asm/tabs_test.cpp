// A tab character's width, and the white space that indents a line.

#include <doctest.h>

#include <string>

#include "asm/tabs.h"

using sc8::columnOf;
using sc8::indentTo;

TEST_SUITE("tabs") {
  TEST_CASE("a tab goes to the next multiple of 8") {
    CHECK_EQ(sc8::TAB_WIDTH, 8);
    CHECK_EQ(columnOf(""), 0);
    CHECK_EQ(columnOf("abc"), 3);
    CHECK_EQ(columnOf("\t"), 8);
    CHECK_EQ(columnOf("ab\t"), 8);
    CHECK_EQ(columnOf("    \t"), 8);
    CHECK_EQ(columnOf("\t\t"), 16);
    CHECK_EQ(columnOf("        \tx"), 17);
  }

  TEST_CASE("an indentation is tabs, then the spaces left over") {
    CHECK_EQ(indentTo(0), "");
    CHECK_EQ(indentTo(4), "    ");
    CHECK_EQ(indentTo(8), "\t");
    CHECK_EQ(indentTo(12), "\t    ");
    CHECK_EQ(indentTo(16), "\t\t");
    CHECK_EQ(indentTo(7), "       ");
  }

  TEST_CASE("an indentation ends at the column it was asked for") {
    for (size_t column = 0; column <= 40; column++) CHECK_EQ(columnOf(indentTo(column)), column);
  }
}
