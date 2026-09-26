// Syntax colours for the three source languages.
//
// The editor stays Dear ImGui's multiline text box, so typing, selection,
// undo and the clipboard behave as before. Its own text is drawn fully
// transparent and the coloured text is drawn over it at the same place,
// line by line, for the lines in view. The colours come from a small lexer
// per language: keywords, comments, strings, numbers, and for assembly the
// labels, mnemonics, directives and registers. The lexers know nothing
// about meaning. A misspelt keyword is simply not coloured.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"

namespace sc8 {

enum class Syntax { Plain, C, Assembly, Basic };

// By file extension: .c and .h, .asm, .bas. Anything else is plain.
Syntax syntaxOf(std::string_view fileName);

enum class Token { Plain, Keyword, Comment, String, Number, Label, Directive, Register, Preprocessor };

struct Span {
  size_t begin = 0;
  size_t end = 0;
  Token token = Token::Plain;
};

// What carries from one line to the next: a C block comment left open.
struct LexState {
  bool inBlockComment = false;
};

// The coloured pieces of one line, in order and without gaps. The state
// is read for the line's start and left as it is at the line's end.
std::vector<Span> lexLine(Syntax syntax, std::string_view line, LexState& state);

// A multiline text box with the syntax drawn in colour over it. Takes the
// same arguments as panes::inputMultiline and returns what it returns.
bool codeEditor(const char* id, std::string& text, Syntax syntax, ImVec2 size, ImGuiInputTextFlags flags = 0,
                ImGuiInputTextCallback callback = nullptr, void* user = nullptr);

// One line of code in colour at the cursor, as TextUnformatted would put
// it. For the listing, where the line is not edited.
void codeLine(Syntax syntax, std::string_view line);

}  // namespace sc8
