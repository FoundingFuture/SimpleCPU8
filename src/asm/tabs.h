// A tab character's width, for the assembly layout and the editor.
//
// A tab goes to the next multiple of TAB_WIDTH. The editor draws a tab
// TAB_WIDTH columns wide wherever it stands, which is true only at the
// start of a line. So tabs are written into a line's indentation and
// nowhere else.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace sc8 {

constexpr size_t TAB_WIDTH = 8;

// The column a text ends at.
inline size_t columnOf(std::string_view s) {
  size_t column = 0;
  for (char c : s) column = c == '\t' ? (column / TAB_WIDTH + 1) * TAB_WIDTH : column + 1;
  return column;
}

// The white space that indents a line to a column: tabs, then spaces.
inline std::string indentTo(size_t column) {
  return std::string(column / TAB_WIDTH, '\t') + std::string(column % TAB_WIDTH, ' ');
}

}  // namespace sc8
