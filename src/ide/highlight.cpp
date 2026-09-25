#include "ide/highlight.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "core/isa.h"
#include "ide/panes.h"
#include "imgui_internal.h"

namespace sc8 {

namespace {

bool isNameStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool isNameChar(char c) { return isNameStart(c) || isDigit(c); }
bool isHexDigit(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }
bool isBlank(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string upper(std::string_view s) {
  std::string u(s);
  for (char& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return u;
}

// Collects spans, merging a span into the previous one of the same kind,
// and fills a gap before a span with plain text.
class Spans {
 public:
  void add(size_t begin, size_t end, Token token) {
    if (end <= begin) return;
    if (begin > at_) push(at_, begin, Token::Plain);
    push(begin, end, token);
    at_ = end;
  }
  std::vector<Span> finish(size_t size) {
    if (size > at_) push(at_, size, Token::Plain);
    return std::move(out_);
  }
  size_t at() const { return at_; }

 private:
  void push(size_t begin, size_t end, Token token) {
    if (!out_.empty() && out_.back().token == token && out_.back().end == begin) out_.back().end = end;
    else out_.push_back({begin, end, token});
  }
  std::vector<Span> out_;
  size_t at_ = 0;
};

// A string or character literal from its opening quote, backslash escapes
// skipped. Returns the index past the closing quote, or the line's end.
size_t quoted(std::string_view s, size_t i, bool escapes) {
  const char q = s[i++];
  while (i < s.size() && s[i] != q) i += (escapes && s[i] == '\\' && i + 1 < s.size()) ? size_t{2} : size_t{1};
  return std::min(s.size(), i + 1);
}

// A number: decimal, 0x hex, 0b binary, $hex, with a C suffix or a
// fraction allowed. Returns the index past it.
size_t number(std::string_view s, size_t i) {
  if (s[i] == '$') {
    i++;
    while (i < s.size() && isHexDigit(s[i])) i++;
    return i;
  }
  if (s[i] == '0' && i + 1 < s.size() && (s[i + 1] == 'x' || s[i + 1] == 'X' || s[i + 1] == 'b' || s[i + 1] == 'B')) {
    i += 2;
    while (i < s.size() && isHexDigit(s[i])) i++;
    return i;
  }
  while (i < s.size() && (isNameChar(s[i]) || s[i] == '.')) i++;
  return i;
}

size_t word(std::string_view s, size_t i) {
  while (i < s.size() && isNameChar(s[i])) i++;
  return i;
}

// ---- C

const std::set<std::string, std::less<>>& cKeywords() {
  static const std::set<std::string, std::less<>> words = {
      "auto",     "break",   "case",    "char",   "const",    "continue", "default", "do",
      "double",   "else",    "enum",    "extern", "float",    "for",      "goto",    "if",
      "inline",   "int",     "long",    "register", "return", "short",    "signed",  "sizeof",
      "static",   "struct",  "switch",  "typedef", "union",   "unsigned", "void",    "volatile",
      "while",    "__ROM",   "__image", "__sprite", "__palette", "__sample", "__file", "NULL"};
  return words;
}

std::vector<Span> lexC(std::string_view s, LexState& st) {
  Spans out;
  size_t i = 0;
  if (st.inBlockComment) {
    const size_t end = s.find("*/");
    if (end == std::string_view::npos) {
      out.add(0, s.size(), Token::Comment);
      return out.finish(s.size());
    }
    out.add(0, end + 2, Token::Comment);
    st.inBlockComment = false;
    i = end + 2;
  }
  // A preprocessor line: the directive coloured, an include's name as a
  // string, the rest lexed as C.
  size_t first = i;
  while (first < s.size() && isBlank(s[first])) first++;
  if (i == 0 && first < s.size() && s[first] == '#') {
    size_t e = first + 1;
    while (e < s.size() && isBlank(s[e])) e++;
    e = word(s, e);
    out.add(first, e, Token::Preprocessor);
    i = e;
    while (i < s.size() && isBlank(s[i])) i++;
    if (i < s.size() && s[i] == '<') {
      const size_t close = s.find('>', i);
      const size_t end = close == std::string_view::npos ? s.size() : close + 1;
      out.add(i, end, Token::String);
      i = end;
    }
  }
  while (i < s.size()) {
    const char c = s[i];
    if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
      out.add(i, s.size(), Token::Comment);
      break;
    }
    if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
      const size_t end = s.find("*/", i + 2);
      if (end == std::string_view::npos) {
        out.add(i, s.size(), Token::Comment);
        st.inBlockComment = true;
        break;
      }
      out.add(i, end + 2, Token::Comment);
      i = end + 2;
      continue;
    }
    if (c == '"' || c == '\'') {
      const size_t end = quoted(s, i, true);
      out.add(i, end, Token::String);
      i = end;
      continue;
    }
    if (isDigit(c)) {
      const size_t end = number(s, i);
      out.add(i, end, Token::Number);
      i = end;
      continue;
    }
    if (isNameStart(c)) {
      const size_t end = word(s, i);
      if (cKeywords().count(s.substr(i, end - i))) out.add(i, end, Token::Keyword);
      i = end;
      continue;
    }
    i++;
  }
  return out.finish(s.size());
}

// ---- assembly

// The first words of the instruction set's canonical names, and IN and
// OUT, which the source spells without the width.
const std::set<std::string, std::less<>>& mnemonics() {
  static const std::set<std::string, std::less<>> words = [] {
    std::set<std::string, std::less<>> w = {"IN", "OUT"};
    for (const OpDef& op : ops()) w.insert(std::string(op.name.substr(0, op.name.find(' '))));
    return w;
  }();
  return words;
}

bool isDataWord(std::string_view w) {
  const std::string l = upper(w);
  return l == "DB" || l == "DW" || l == "DS" || l == "FILE" || l == "IMAGE" || l == "PALETTE" || l == "SAMPLE";
}

bool isRegister(std::string_view w) {
  const std::string u = upper(w);
  return u == "A" || u == "B" || u == "D1" || u == "D2" || u == "D3" || u == "SP" || u == "PC";
}

std::vector<Span> lexAssembly(std::string_view s) {
  Spans out;
  const size_t sc = s.find(';');
  const std::string_view code = s.substr(0, sc);
  size_t i = 0;
  while (i < code.size() && isBlank(code[i])) i++;
  // A leading label: a name and a colon.
  if (i < code.size() && isNameStart(code[i])) {
    const size_t end = word(code, i);
    if (end < code.size() && code[end] == ':') {
      out.add(i, end + 1, Token::Label);
      i = end + 1;
    }
  }
  while (i < code.size() && isBlank(code[i])) i++;
  // The mnemonic, directive or data keyword.
  if (i < code.size()) {
    size_t end = i;
    while (end < code.size() && !isBlank(code[end]) && code[end] != '(') end++;
    const std::string_view head = code.substr(i, end - i);
    if (head.front() == '.' || isDataWord(head)) out.add(i, end, Token::Directive);
    else if (mnemonics().count(upper(head))) out.add(i, end, Token::Keyword);
    i = end;
  }
  // The operands.
  while (i < code.size()) {
    const char c = code[i];
    if (c == '"' || c == '\'') {
      const size_t end = std::min(code.size(), quoted(code, i, false));
      out.add(i, end, Token::String);
      i = end;
    } else if (isDigit(c) || (c == '$' && i + 1 < code.size() && isHexDigit(code[i + 1]))) {
      const size_t end = number(code, i);
      out.add(i, end, Token::Number);
      i = end;
    } else if (isNameStart(c)) {
      const size_t end = word(code, i);
      if (isRegister(code.substr(i, end - i))) out.add(i, end, Token::Register);
      i = end;
    } else {
      i++;
    }
  }
  if (sc != std::string_view::npos) out.add(sc, s.size(), Token::Comment);
  return out.finish(s.size());
}

// ---- BASIC

const std::set<std::string, std::less<>>& basicKeywords() {
  static const std::set<std::string, std::less<>> words = {
      "ABS",  "AND",   "ASC",   "CALL",   "CATALOG", "CHR$",  "CIRCLE", "CLS",  "DEEK",  "DELETE",
      "DOKE", "DRAW",  "END",   "FOR",    "GOSUB",   "GOTO",  "IF",     "INK",  "INKEY$", "INPUT",
      "JMP",  "JSR",   "KEY",   "LEN",    "LET",     "LIST",  "LOAD",   "MID$", "MOD",   "MOVE",
      "NEW",  "NEXT",  "NOT",   "OR",     "PAD",     "PAPER", "PEEK",   "PIXEL", "PLOT", "POKE",
      "PRINT", "REM",  "RETURN", "RND",   "RUN",     "SAVE",  "STEP",   "STOP", "STR$",  "THEN",
      "TO",   "USR",   "VAL",   "WAIT"};
  return words;
}

std::vector<Span> lexBasic(std::string_view s) {
  Spans out;
  size_t i = 0;
  while (i < s.size() && isBlank(s[i])) i++;
  // The line number.
  if (i < s.size() && isDigit(s[i])) {
    size_t end = i;
    while (end < s.size() && isDigit(s[end])) end++;
    out.add(i, end, Token::Label);
    i = end;
  }
  while (i < s.size()) {
    const char c = s[i];
    if (c == '"') {
      const size_t end = quoted(s, i, false);
      out.add(i, end, Token::String);
      i = end;
    } else if (isDigit(c) || (c == '$' && i + 1 < s.size() && isHexDigit(s[i + 1]))) {
      const size_t end = number(s, i);
      out.add(i, end, Token::Number);
      i = end;
    } else if (isNameStart(c)) {
      size_t end = word(s, i);
      if (end < s.size() && s[end] == '$') end++;
      const std::string w = upper(s.substr(i, end - i));
      if (w == "REM") {
        out.add(i, end, Token::Keyword);
        out.add(end, s.size(), Token::Comment);
        break;
      }
      if (basicKeywords().count(w)) out.add(i, end, Token::Keyword);
      i = end;
    } else if (c == '!') {
      // The bang statement.
      out.add(i, i + 1, Token::Keyword);
      i++;
    } else {
      i++;
    }
  }
  return out.finish(s.size());
}

ImU32 colourOf(Token t, ImU32 plain) {
  switch (t) {
    case Token::Keyword: return IM_COL32(110, 170, 255, 255);
    case Token::Comment: return IM_COL32(115, 150, 115, 255);
    case Token::String: return IM_COL32(230, 175, 110, 255);
    case Token::Number: return IM_COL32(190, 150, 255, 255);
    case Token::Label: return IM_COL32(250, 215, 100, 255);
    case Token::Directive: return IM_COL32(215, 125, 215, 255);
    case Token::Register: return IM_COL32(120, 215, 200, 255);
    case Token::Preprocessor: return IM_COL32(215, 125, 215, 255);
    case Token::Plain: break;
  }
  return plain;
}

}  // namespace

Syntax syntaxOf(std::string_view name) {
  const size_t dot = name.rfind('.');
  if (dot == std::string_view::npos) return Syntax::Plain;
  const std::string ext = upper(name.substr(dot));
  if (ext == ".C" || ext == ".H") return Syntax::C;
  if (ext == ".ASM") return Syntax::Assembly;
  if (ext == ".BAS") return Syntax::Basic;
  return Syntax::Plain;
}

std::vector<Span> lexLine(Syntax syntax, std::string_view line, LexState& state) {
  switch (syntax) {
    case Syntax::C: return lexC(line, state);
    case Syntax::Assembly: return lexAssembly(line);
    case Syntax::Basic: return lexBasic(line);
    case Syntax::Plain: break;
  }
  return line.empty() ? std::vector<Span>{} : std::vector<Span>{{0, line.size(), Token::Plain}};
}

bool codeEditor(const char* id, std::string& text, Syntax syntax, ImVec2 size, ImGuiInputTextFlags flags) {
  if (syntax == Syntax::Plain) return panes::inputMultiline(id, text, size, flags);

  ImGuiWindow* parent = ImGui::GetCurrentWindow();
  const ImGuiID itemId = ImGui::GetID(id);
  const ImU32 plain = ImGui::GetColorU32(ImGuiCol_Text);
  // The box draws its text transparent; the caret and the selection keep
  // their own colours.
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
  const bool changed = panes::inputMultiline(id, text, size, flags);
  ImGui::PopStyleColor();

  // The box's scrolling child, named the way BeginChildEx names it.
  char name[512];
  ImFormatString(name, sizeof name, "%s/%s_%08X", parent->Name, id, itemId);
  ImGuiWindow* box = ImGui::FindWindowByName(name);
  ImGuiContext& g = *ImGui::GetCurrentContext();
  // A pane behind another tab still runs its body, but the box is not
  // submitted then. Its draw list is last frame's, and after a minute
  // unused Dear ImGui frees it, so drawing into it would crash.
  if (!box || box->Hidden || box->LastFrameActive != g.FrameCount) return changed;

  const float lineHeight = g.FontSize;
  float scrollX = 0.0f;
  if (const ImGuiInputTextState* state = ImGui::GetInputTextState(itemId)) scrollX = state->Scroll.x;
  const ImVec2 origin(box->Pos.x + g.Style.FramePadding.x - scrollX, box->Pos.y + g.Style.FramePadding.y - box->Scroll.y);
  const int firstVisible = std::max(0, static_cast<int>((box->Scroll.y - g.Style.FramePadding.y) / lineHeight));
  const int lastVisible = firstVisible + static_cast<int>(box->InnerRect.GetHeight() / lineHeight) + 2;

  // Walk the lines from the top, lexing only to carry a C comment's state
  // until the first visible line.
  ImDrawList* dl = box->DrawList;
  dl->PushClipRect(box->InnerClipRect.Min, box->InnerClipRect.Max, true);
  LexState st;
  const std::string_view all(text);
  size_t pos = 0;
  for (int n = 0; n <= lastVisible && pos <= all.size(); n++) {
    const size_t nl = all.find('\n', pos);
    const std::string_view line = all.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
    if (n < firstVisible) {
      if (syntax == Syntax::C) lexLine(syntax, line, st);
    } else {
      float x = origin.x;
      const float y = origin.y + static_cast<float>(n) * lineHeight;
      for (const Span& sp : lexLine(syntax, line, st)) {
        const char* b = line.data() + sp.begin;
        const char* e = line.data() + sp.end;
        dl->AddText(g.Font, g.FontSize, ImVec2(x, y), colourOf(sp.token, plain), b, e);
        x += ImGui::CalcTextSize(b, e).x;
      }
    }
    if (nl == std::string_view::npos) break;
    pos = nl + 1;
  }
  dl->PopClipRect();
  return changed;
}

void codeLine(Syntax syntax, std::string_view line) {
  LexState st;
  const ImU32 plain = ImGui::GetColorU32(ImGuiCol_Text);
  bool first = true;
  for (const Span& sp : lexLine(syntax, line, st)) {
    if (!first) ImGui::SameLine(0.0f, 0.0f);
    first = false;
    ImGui::PushStyleColor(ImGuiCol_Text, colourOf(sp.token, plain));
    ImGui::TextUnformatted(line.data() + sp.begin, line.data() + sp.end);
    ImGui::PopStyleColor();
  }
  if (first) ImGui::TextUnformatted(" ");
}

}  // namespace sc8
