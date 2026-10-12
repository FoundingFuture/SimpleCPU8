#include "ide/indent_keys.h"

#include <cstring>
#include <optional>
#include <string>
#include <string_view>

#include "imgui_internal.h"

#include "asm/tabs.h"
#include "ide/indent.h"

namespace sc8 {

// Dear ImGui makes its tab IM_TABSIZE spaces wide, see wideTabs. The
// number is set in cmake/Dependencies.cmake, and the layout counts
// TAB_WIDTH columns for the same tab.
static_assert(IM_TABSIZE == TAB_WIDTH, "a tab is drawn as wide as asm/tabs.h counts it");

namespace {

// One box is typed in at a time, so one state serves every document.
struct State {
  ImGuiID box = 0;
  bool tab = false;  // Tab was pressed, and its character dropped
  // The box as the last frame left it, to see what Backspace took.
  int frame = -1;
  int length = 0;
  int caret = 0;
  bool selecting = false;
  char before = 0;  // the character left of the caret
};
State state;

// The line the caret is in, as offsets into the buffer.
struct Span {
  int begin;
  int end;
};
Span lineAt(const ImGuiInputTextCallbackData* data, int at) {
  Span s{at, at};
  while (s.begin > 0 && data->Buf[s.begin - 1] != '\n') s.begin--;
  while (s.end < data->BufTextLen && data->Buf[s.end] != '\n') s.end++;
  return s;
}

void replaceLine(ImGuiInputTextCallbackData* data, Span line, const IndentLine& with) {
  data->DeleteChars(line.begin, line.end - line.begin);
  data->InsertChars(line.begin, with.text.data(), with.text.data() + with.text.size());
  data->CursorPos = line.begin + static_cast<int>(with.caret);
  data->SelectionStart = data->SelectionEnd = data->CursorPos;
}

void pressTab(ImGuiInputTextCallbackData* data, bool tabs) {
  // Tab over a selection replaces it, as any typed character does.
  if (data->SelectionStart != data->SelectionEnd) {
    const int from = ImMin(data->SelectionStart, data->SelectionEnd);
    data->DeleteChars(from, ImMax(data->SelectionStart, data->SelectionEnd) - from);
    data->CursorPos = from;
  }
  const Span line = lineAt(data, data->CursorPos);
  const std::string_view text(data->Buf + line.begin, static_cast<size_t>(line.end - line.begin));
  replaceLine(data, line, tabKey(text, static_cast<size_t>(data->CursorPos - line.begin), tabs));
}

// The box has taken one blank left of the caret already. The line is
// put back as it was, and ide/indent.h decides what the key does to it.
void pressBackspace(ImGuiInputTextCallbackData* data, char taken, bool tabs) {
  const Span line = lineAt(data, data->CursorPos);
  const size_t caret = static_cast<size_t>(data->CursorPos - line.begin);
  std::string was(data->Buf + line.begin, static_cast<size_t>(line.end - line.begin));
  was.insert(caret, 1, taken);
  if (const std::optional<IndentLine> now = backspaceKey(was, caret + 1, tabs)) replaceLine(data, line, *now);
}

// What this frame's keys do to the box.
void keys(ImGuiInputTextCallbackData* data, const IndentKeys::Box& box) {
  const bool watched = state.box == box.id && state.frame == ImGui::GetFrameCount() - 1;
  if (state.tab && state.box == box.id) {
    pressTab(data, box.tabs);
  } else if (watched && !state.selecting && data->BufTextLen == state.length - 1 &&
             data->CursorPos == state.caret - 1 && (state.before == ' ' || state.before == '\t') &&
             ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) {
    pressBackspace(data, state.before, box.tabs);
  }
  state.tab = false;
}

// The box as this frame leaves it, for the next frame's Backspace.
void note(const ImGuiInputTextCallbackData* data, const IndentKeys::Box& box) {
  state.box = box.id;
  state.frame = ImGui::GetFrameCount();
  state.length = data->BufTextLen;
  state.caret = data->CursorPos;
  state.selecting = data->SelectionStart != data->SelectionEnd;
  state.before = data->CursorPos > 0 ? data->Buf[data->CursorPos - 1] : 0;
}

}  // namespace

// Dear ImGui takes a tab's width from the font's own tab glyph, which
// is one space wide in its default font. With that glyph left out of a
// font it makes the tab itself, IM_TABSIZE spaces wide.
void IndentKeys::wideTabs() {
  static const ImWchar TAB[] = {'\t', '\t', 0};
  for (ImFontConfig& font : ImGui::GetIO().Fonts->Sources) {
    if (font.GlyphExcludeRanges) continue;
    // The atlas frees the list with the font, so it comes from its heap.
    auto* list = static_cast<ImWchar*>(ImGui::MemAlloc(sizeof TAB));
    std::memcpy(list, TAB, sizeof TAB);
    font.GlyphExcludeRanges = list;
  }
}

int IndentKeys::callback(ImGuiInputTextCallbackData* data) {
  auto* box = static_cast<Box*>(data->UserData);
  if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter && data->EventChar == '\t') {
    // The character is dropped here. The edit waits for the Always
    // event of this frame, the one that may change the buffer.
    state.tab = true;
    state.box = box->id;
    return 1;
  }
  const bool always = data->EventFlag == ImGuiInputTextFlags_CallbackAlways;
  if (always) keys(data, *box);
  int result = 0;
  if (box->next && (box->nextFlags & data->EventFlag)) {
    data->UserData = box->nextUser;
    result = box->next(data);
    data->UserData = box;
  }
  // The box's own callback may edit the text, so the note comes last.
  if (always) note(data, *box);
  return result;
}

}  // namespace sc8
