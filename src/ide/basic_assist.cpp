#include "ide/basic_assist.h"

#include <algorithm>
#include <string_view>
#include <vector>

#include "basic/lines.h"
#include "basic/program.h"

namespace sc8 {

namespace {

bool blank(std::string_view s) {
  return std::all_of(s.begin(), s.end(), [](char c) { return c == ' ' || c == '\t' || c == '\r'; });
}

// The number of the line at `at`, or of the nearest numbered line above
// it. 0 when there is none, which puts a new line above every other.
int numberAtOrAbove(const std::vector<std::string>& lines, int at) {
  for (int i = at; i >= 0; i--) {
    if (const auto n = basic::lineNumber(lines[static_cast<size_t>(i)])) return *n;
  }
  return 0;
}

// Put the lines in the box and the caret at a line and column.
void replace(ImGuiInputTextCallbackData* data, const std::vector<std::string>& lines, int line, int column) {
  const std::string text = basic::joinLines(lines);
  data->DeleteChars(0, data->BufTextLen);
  data->InsertChars(0, text.data(), text.data() + text.size());
  int offset = 0;
  for (int i = 0; i < line; i++) offset += static_cast<int>(lines[static_cast<size_t>(i)].size()) + 1;
  offset += std::min(column, static_cast<int>(lines[static_cast<size_t>(line)].size()));
  data->CursorPos = data->SelectionStart = data->SelectionEnd = offset;
}

}  // namespace

int BasicAssist::callback(ImGuiInputTextCallbackData* data) {
  return static_cast<BasicAssist*>(data->UserData)->event(data);
}

bool BasicAssist::sortText(std::string& text) {
  std::string canonical = basic::canonicalText(text);
  bool changed = canonical != text;
  if (changed) text = std::move(canonical);
  auto lines = basic::splitLines(text);
  if (basic::linesInOrder(lines)) return changed;
  int caret = 0;
  basic::sortLines(lines, caret);
  text = basic::joinLines(lines);
  return true;
}

int BasicAssist::event(ImGuiInputTextCallbackData* data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) {
    if (data->EventChar != '\n') return 0;
    // Shift+Enter does what Cmd-I does, so its newline is dropped.
    if (ImGui::GetIO().KeyShift) {
      shiftEnter_ = true;
      return 1;
    }
    enter_ = true;
    return 0;
  }
  if (data->EventFlag != ImGuiInputTextFlags_CallbackAlways) return 0;

  const std::string_view text(data->Buf, static_cast<size_t>(data->BufTextLen));
  const std::string_view before = text.substr(0, static_cast<size_t>(data->CursorPos));
  int line = static_cast<int>(std::count(before.begin(), before.end(), '\n'));
  const size_t lineStart = before.rfind('\n') == std::string_view::npos ? 0 : before.rfind('\n') + 1;
  const int column = static_cast<int>(before.size() - lineStart);

  const ImGuiIO& io = ImGui::GetIO();
  const bool insert = shiftEnter_ || ((io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_I, false));
  shiftEnter_ = false;
  const bool selecting = data->SelectionStart != data->SelectionEnd;

  if (enter_) {
    enter_ = false;
    auto lines = basic::splitLines(text);
    // Only a line opened at the end of the one above: the new line holds
    // nothing yet. Enter in the middle of a line splits it as usual.
    if (line >= 1 && column == 0 && blank(lines[static_cast<size_t>(line)])) {
      const std::string& above = lines[static_cast<size_t>(line - 1)];
      if (basic::onlyNumber(above)) {
        // Enter on a bare number: the number goes and the caret stays.
        lines[static_cast<size_t>(line - 1)].clear();
        lines.erase(lines.begin() + line);
        line--;
        replace(data, lines, line, 0);
      } else if (basic::lineNumber(above)) {
        basic::sortLines(lines, line, true);
        std::optional<int> next = basic::numberAfter(lines, *basic::lineNumber(lines[static_cast<size_t>(line - 1)]));
        if (!next) {
          basic::renumberLines(lines);
          note_ = "no line number was free there, so the program is renumbered in tens";
          next = basic::numberAfter(lines, *basic::lineNumber(lines[static_cast<size_t>(line - 1)]));
        }
        if (next) {
          lines[static_cast<size_t>(line)] = std::to_string(*next) + " ";
          replace(data, lines, line, static_cast<int>(lines[static_cast<size_t>(line)].size()));
        }
      }
    }
  } else if (insert) {
    auto lines = basic::splitLines(text);
    const std::string& here = lines[static_cast<size_t>(line)];
    // An empty line takes the number itself. Any other line gets a new
    // line under it.
    const bool onHere = blank(here);
    if (!onHere) basic::sortLines(lines, line);
    auto numberFor = [&] { return basic::numberAfter(lines, numberAtOrAbove(lines, line)); };
    std::optional<int> next = numberFor();
    if (!next) {
      basic::renumberLines(lines);
      note_ = "no line number was free there, so the program is renumbered in tens";
      next = numberFor();
    }
    if (next) {
      if (!onHere) line++;
      const std::string opened = std::to_string(*next) + " ";
      if (onHere) lines[static_cast<size_t>(line)] = opened;
      else lines.insert(lines.begin() + line, opened);
      replace(data, lines, line, static_cast<int>(opened.size()));
    }
  } else if (lastLine_ >= 0 && line != lastLine_ && !selecting) {
    // The caret left a line: put the program in order if it is not.
    auto lines = basic::splitLines(text);
    if (!basic::linesInOrder(lines)) {
      basic::sortLines(lines, line);
      replace(data, lines, line, column);
    }
  }

  const std::string_view now(data->Buf, static_cast<size_t>(data->CursorPos));
  const int caretLine = static_cast<int>(std::count(now.begin(), now.end(), '\n'));
  if (caretLine != lastLine_) {
    // The caret left a line: the rows it is not on take the stored
    // spelling. That keeps their size, so the caret stays where it is.
    const std::string_view all(data->Buf, static_cast<size_t>(data->BufTextLen));
    const std::string canonical = basic::canonicalText(all, caretLine);
    if (canonical != all && canonical.size() == all.size()) {
      std::copy(canonical.begin(), canonical.end(), data->Buf);
      data->BufDirty = true;
    }
  }
  lastLine_ = caretLine;
  return 0;
}

}  // namespace sc8
