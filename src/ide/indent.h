// Tab and Backspace in a document's text box, as edits of one line.
//
// Tab moves to the next multiple of INDENT_STEP. In a line's indentation
// it indents the line, and 8 columns of indentation are one tab
// character. After text it adds spaces, since asm/tabs.h keeps tabs to
// the indentation. Backspace in the indentation steps back the same way.
// Only the standard library is used here, so the tests need no window.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace sc8 {

constexpr size_t INDENT_STEP = 4;

// A line after a key, and the caret as an offset into its text.
struct IndentLine {
  std::string text;
  size_t caret;
};

// Tab with the caret at an offset into a line. An editor whose language
// does not read a tab as white space passes tabs false.
IndentLine tabKey(std::string_view line, size_t caret, bool tabs);

// Backspace the same way. Nothing where the text box's own Backspace is
// right: at the start of a line and after text.
std::optional<IndentLine> backspaceKey(std::string_view line, size_t caret, bool tabs);

}  // namespace sc8
