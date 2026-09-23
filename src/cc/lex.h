// The C lexer. Tokens for the subset in docs/design/c-compiler-design.md.
//
// It keeps the line number on every token, because the whole point of the C
// layer is that a person can see which assembly came from which line, and
// set a breakpoint on one.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sc8::cc {

enum class TokKind { Num, Str, Id, Kw, Punct, Eof };

std::string_view tokKindName(TokKind k);

struct Tok {
  TokKind kind = TokKind::Eof;
  std::string text;
  // Num carries its value already folded, so the parser never re-parses.
  double value = 0;
  // Str carries its decoded bytes, escapes resolved.
  std::vector<uint8_t> bytes;
  int line = 1;
  std::string file;
};

// Every compiler error: the file and line it points at, and the message.
class CcError : public std::runtime_error {
 public:
  CcError(std::string inFile, int atLine, const std::string& message)
      : std::runtime_error(message), file(std::move(inFile)), line(atLine) {}
  std::string file;
  int line;
  std::string message() const { return what(); }
};

bool isKeyword(std::string_view word);

struct PpLineIn {
  std::string text;
  std::string file;
  int line;
};

// Lex a preprocessed file, one line at a time, so every token keeps the file
// and line it was written on rather than a position in the expansion.
std::vector<Tok> lexLines(const std::vector<PpLineIn>& lines);

std::vector<Tok> lex(std::string_view src, const std::string& file = "main.c");

}  // namespace sc8::cc
