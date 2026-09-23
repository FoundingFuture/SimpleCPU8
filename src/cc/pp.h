// The preprocessor. Line oriented, which is what C's is, and it keeps every
// line's real file and number so an error and a breakpoint still point at
// the line somebody wrote rather than at the expansion.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "cc/lex.h"

namespace sc8::cc {

using PpLine = PpLineIn;

struct Macro {
  std::optional<std::vector<std::string>> params;
  std::string body;
};

struct PpResult {
  std::vector<PpLine> lines;
  // Every file the source pulled in, in the order it was first reached.
  std::vector<std::string> included;
  std::map<std::string, Macro> macros;
};

struct PpOptions {
  // A header the compiler ships, or another file in the project. No answer
  // is "no such file", which is an error naming the include.
  std::function<std::optional<std::string>(const std::string& name, bool angled)> resolve;
  std::map<std::string, std::string> defines;
};

// Comments go before anything else, the way C removes them, and they are
// replaced by spaces so every line keeps its number.
std::string stripComments(std::string_view src);

class Preprocessor {
 public:
  explicit Preprocessor(PpOptions opts = {});
  PpResult run(const std::string& src, const std::string& file);

 private:
  void file(const std::string& src, const std::string& file);
  void define(const std::string& rest, const std::string& file, int line);
  bool evalCond(const std::string& rest, const std::string& file, int line);
  std::string expand(const std::string& text, const std::string& file, int line,
                     const std::set<std::string>& blocked = {});
  std::string expandOnce(const std::string& text, const std::string& file, int line,
                         const std::set<std::string>& blocked);

  PpOptions opts_;
  std::map<std::string, Macro> macros_;
  std::vector<PpLine> out_;
  std::vector<std::string> included_;
  std::set<std::string> open_;
};

PpResult preprocess(const std::string& src, const std::string& file, PpOptions opts = {});

}  // namespace sc8::cc
