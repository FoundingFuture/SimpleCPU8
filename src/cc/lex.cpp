#include "cc/lex.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>

namespace sc8::cc {

namespace {

const std::set<std::string_view> KEYWORDS = {
    "char", "short", "int", "long", "unsigned", "signed", "float", "double", "void",
    "const", "static", "extern", "struct", "return", "if", "else", "while", "for",
    "do", "switch", "case", "default", "break", "continue", "sizeof", "asm",
    "__ROM", "__zp", "inline", "typedef",
};

// Longest first, so ">>=" wins over ">>" and ">".
const std::string_view PUNCT[] = {
    ">>=", "<<=", "...",
    "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
    "{", "}", "(", ")", "[", "]", ";", ",", ".", "?", ":",
    "+", "-", "*", "/", "%", "&", "|", "^", "~", "!", "<", ">", "=",
};

[[noreturn]] void fail(const std::string& file, int line, const std::string& msg) {
  throw CcError(file, line, msg);
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isHex(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }
bool isIdStart(char c) { return c == '_' || std::isalpha(static_cast<unsigned char>(c)) != 0; }
bool isIdChar(char c) { return c == '_' || std::isalnum(static_cast<unsigned char>(c)) != 0; }

char at(std::string_view s, size_t i) { return i < s.size() ? s[i] : '\0'; }

// One escape, at s[i] which is the backslash. Returns the byte and the index
// after it. The set matches the assembler's own string escapes plus the ones
// C programs actually type.
std::pair<uint8_t, size_t> escape(std::string_view s, size_t i, const std::string& file, int line) {
  const char c = at(s, i + 1);
  switch (c) {
    case 'n': return {10, i + 2};
    case 't': return {9, i + 2};
    case 'r': return {13, i + 2};
    case '0': return {0, i + 2};
    case '\\': return {92, i + 2};
    case '\'': return {39, i + 2};
    case '"': return {34, i + 2};
    case 'x': {
      size_t n = 0;
      while (n < 2 && isHex(at(s, i + 2 + n))) n++;
      if (n == 0) fail(file, line, "\\x needs one or two hex digits");
      const int v = static_cast<int>(std::strtol(std::string(s.substr(i + 2, n)).c_str(), nullptr, 16));
      return {static_cast<uint8_t>(v), i + 2 + n};
    }
    default:
      fail(file, line, std::string("unknown escape \\") + (c ? std::string(1, c) : ""));
  }
}

// The number's body, as the TypeScript regex splits it: hex, binary, or a
// decimal with an optional fraction and exponent. Returns the length of
// the body, or zero when nothing matched.
size_t numberBody(std::string_view s) {
  if (s.size() > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    size_t n = 2;
    while (isHex(at(s, n))) n++;
    if (n > 2) return n;
  }
  if (s.size() > 1 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
    size_t n = 2;
    while (at(s, n) == '0' || at(s, n) == '1') n++;
    if (n > 2) return n;
  }
  size_t n = 0;
  while (isDigit(at(s, n))) n++;
  if (at(s, n) == '.' && isDigit(at(s, n + 1))) {
    n++;
    while (isDigit(at(s, n))) n++;
  } else if (n > 0 && at(s, n) == '.') {
    n++;
  } else if (n == 0) {
    return 0;
  }
  if (at(s, n) == 'e' || at(s, n) == 'E') {
    size_t m = n + 1;
    if (at(s, m) == '+' || at(s, m) == '-') m++;
    if (isDigit(at(s, m))) {
      while (isDigit(at(s, m))) m++;
      n = m;
    }
  }
  return n;
}

}  // namespace

std::string_view tokKindName(TokKind k) {
  switch (k) {
    case TokKind::Num: return "num";
    case TokKind::Str: return "str";
    case TokKind::Id: return "id";
    case TokKind::Kw: return "kw";
    case TokKind::Punct: return "punct";
    case TokKind::Eof: return "eof";
  }
  return "?";
}

bool isKeyword(std::string_view word) { return KEYWORDS.count(word) > 0; }

std::vector<Tok> lexLines(const std::vector<PpLineIn>& lines) {
  std::vector<Tok> out;
  std::string lastFile = "main.c";
  int lastLine = 1;
  for (const PpLineIn& l : lines) {
    bool blank = true;
    for (char c : l.text) {
      if (!std::isspace(static_cast<unsigned char>(c))) { blank = false; break; }
    }
    if (blank) continue;
    for (Tok t : lex(l.text, l.file)) {
      if (t.kind == TokKind::Eof) continue;
      t.line = l.line;
      t.file = l.file;
      out.push_back(std::move(t));
    }
    lastFile = l.file;
    lastLine = l.line;
  }
  out.push_back(Tok{TokKind::Eof, "", 0, {}, lastLine, lastFile});
  return out;
}

std::vector<Tok> lex(std::string_view src, const std::string& file) {
  std::vector<Tok> out;
  size_t i = 0;
  int line = 1;
  auto push = [&](TokKind kind, std::string text) {
    Tok t;
    t.kind = kind;
    t.text = std::move(text);
    t.line = line;
    t.file = file;
    out.push_back(std::move(t));
    return &out.back();
  };

  while (i < src.size()) {
    const char c = src[i];

    if (c == '\n') { line++; i++; continue; }
    if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }

    // Comments. A line comment starting with "build:" is the build line, and
    // the driver reads it off the raw text, so nothing special happens here.
    if (c == '/' && at(src, i + 1) == '/') {
      while (i < src.size() && src[i] != '\n') i++;
      continue;
    }
    if (c == '/' && at(src, i + 1) == '*') {
      const int start = line;
      i += 2;
      while (i < src.size() && !(src[i] == '*' && at(src, i + 1) == '/')) {
        if (src[i] == '\n') line++;
        i++;
      }
      if (i >= src.size()) fail(file, start, "unterminated /* comment");
      i += 2;
      continue;
    }

    // A preprocessor line survives to the parser's caller as its own token,
    // so the preprocessor can run over the token stream rather than the text.
    if (c == '#') {
      const size_t start = i;
      while (i < src.size() && src[i] != '\n') {
        // A backslash at end of line continues a #define.
        if (src[i] == '\\' && at(src, i + 1) == '\n') { line++; i += 2; continue; }
        i++;
      }
      push(TokKind::Punct, std::string(src.substr(start, i - start)));
      continue;
    }

    if (isDigit(c) || (c == '.' && isDigit(at(src, i + 1)))) {
      // The body and the suffix are captured apart. Stripping the suffix
      // afterwards ate the last digit of 0x1F, because F is both.
      const size_t n = numberBody(src.substr(i));
      if (n == 0) fail(file, line, "bad number");
      const std::string body(src.substr(i, n));
      size_t m = n;
      while (m < src.size() - i) {
        const char s = src[i + m];
        if (s == 'u' || s == 'U' || s == 'l' || s == 'L' || s == 'f' || s == 'F') m++;
        else break;
      }
      const std::string text(src.substr(i, m));
      double value;
      if (body.size() > 1 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X')) {
        value = static_cast<double>(std::strtoull(body.c_str() + 2, nullptr, 16));
      } else if (body.size() > 1 && body[0] == '0' && (body[1] == 'b' || body[1] == 'B')) {
        value = static_cast<double>(std::strtoull(body.c_str() + 2, nullptr, 2));
      } else {
        value = std::strtod(body.c_str(), nullptr);
      }
      if (!std::isfinite(value)) fail(file, line, "bad number " + text);
      push(TokKind::Num, text)->value = value;
      i += m;
      continue;
    }

    if (isIdStart(c)) {
      size_t n = 1;
      while (isIdChar(at(src, i + n))) n++;
      const std::string word(src.substr(i, n));
      push(isKeyword(word) ? TokKind::Kw : TokKind::Id, word);
      i += n;
      continue;
    }

    if (c == '"') {
      std::vector<uint8_t> bytes;
      i++;
      while (i < src.size() && src[i] != '"') {
        if (src[i] == '\n') fail(file, line, "unterminated string");
        if (src[i] == '\\') {
          auto [b, next] = escape(src, i, file, line);
          bytes.push_back(b);
          i = next;
        } else {
          bytes.push_back(static_cast<uint8_t>(src[i]));
          i++;
        }
      }
      if (i >= src.size()) fail(file, line, "unterminated string");
      i++;
      std::string text(bytes.begin(), bytes.end());
      push(TokKind::Str, text)->bytes = bytes;
      continue;
    }

    if (c == '\'') {
      i++;
      uint8_t value;
      if (at(src, i) == '\\') {
        auto [b, next] = escape(src, i, file, line);
        value = b;
        i = next;
      } else {
        value = static_cast<uint8_t>(at(src, i));
        i++;
      }
      if (at(src, i) != '\'') fail(file, line, "a character constant holds one character");
      i++;
      // A character constant IS an integer constant, so it is one here too.
      push(TokKind::Num, "'" + std::string(1, static_cast<char>(value)) + "'")->value = value;
      continue;
    }

    bool found = false;
    for (std::string_view p : PUNCT) {
      if (src.substr(i).starts_with(p)) {
        push(TokKind::Punct, std::string(p));
        i += p.size();
        found = true;
        break;
      }
    }
    if (!found) fail(file, line, std::string("stray \"") + c + "\"");
  }

  push(TokKind::Eof, "");
  return out;
}

}  // namespace sc8::cc
