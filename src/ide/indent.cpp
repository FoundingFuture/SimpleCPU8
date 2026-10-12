#include "ide/indent.h"

#include <algorithm>
#include <utility>

#include "asm/tabs.h"

namespace sc8 {

namespace {

bool isBlank(char c) { return c == ' ' || c == '\t'; }

// Where a line's indentation ends.
size_t indentEnd(std::string_view line) {
  size_t end = 0;
  while (end < line.size() && isBlank(line[end])) end++;
  return end;
}

// A line with its indentation set to a column, the caret at its end.
// The whole indentation is rewritten, so no tab is left behind a space.
IndentLine indented(std::string_view line, size_t column, bool tabs) {
  std::string text = tabs ? indentTo(column) : std::string(column, ' ');
  const size_t caret = text.size();
  text += line.substr(indentEnd(line));
  return {std::move(text), caret};
}

}  // namespace

IndentLine tabKey(std::string_view line, size_t caret, bool tabs) {
  caret = std::min(caret, line.size());
  const size_t end = indentEnd(line);
  if (caret <= end) {
    const size_t column = columnOf(line.substr(0, end));
    return indented(line, (column / INDENT_STEP + 1) * INDENT_STEP, tabs);
  }
  const size_t column = columnOf(line.substr(0, caret));
  const size_t add = INDENT_STEP - column % INDENT_STEP;
  std::string text(line.substr(0, caret));
  text.append(add, ' ');
  text += line.substr(caret);
  return {std::move(text), caret + add};
}

std::optional<IndentLine> backspaceKey(std::string_view line, size_t caret, bool tabs) {
  const size_t end = indentEnd(line);
  if (caret == 0 || caret > end) return std::nullopt;
  const size_t column = columnOf(line.substr(0, end));
  return indented(line, (column - 1) / INDENT_STEP * INDENT_STEP, tabs);
}

}  // namespace sc8
