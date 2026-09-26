// Line numbers in a BASIC program held as text: the editor's side of them.
//
// The IDE's editor keeps a program's lines in number order, numbers a new
// line for you, and renumbers when there is no room left between two
// lines. These are the text operations under that, with no editor in them.
//
// A line's number is the digits it starts with, after any spaces. A line
// without one belongs to the numbered line above it and moves with it, so
// a blank line under a line stays under it. Lines before the first
// numbered line stay at the top, and lines after the last one at the end.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sc8::basic {

// The text split at each newline. "a\nb\n" is three lines, the last one
// empty, so joinLines gives the same text back.
std::vector<std::string> splitLines(std::string_view text);
std::string joinLines(const std::vector<std::string>& lines);

// The number a line starts with, or nothing.
std::optional<int> lineNumber(std::string_view line);
// True for a line that is a number and nothing else, spaces aside.
bool onlyNumber(std::string_view line);

// True when the numbered lines already stand in number order.
bool linesInOrder(const std::vector<std::string>& lines);

// The lines in number order. Equal numbers keep their order. `caret` is a
// line index and follows its line to where it goes. With `glue`, the
// caret's line moves with the line above it even when it is at the end
// of the text, which is what a line just opened under another needs.
void sortLines(std::vector<std::string>& lines, int& caret, bool glue = false);

// A number for a new line under line `after`. Ten on when that leaves
// the next line alone, else halfway to it. Nothing when no number is free
// between them, or when the new one would pass 65535. `after` may be 0 for
// a line above every other.
std::optional<int> numberAfter(const std::vector<std::string>& lines, int after);

// Every numbered line numbered again from `start` in steps of `step`, in
// the order they stand. GOTO, GOSUB and THEN followed by a number that
// names a line are changed to match. A number in a string or after REM
// is left as it is, and so is one naming a line that does not exist.
void renumberLines(std::vector<std::string>& lines, int start = 10, int step = 10);

}  // namespace sc8::basic
