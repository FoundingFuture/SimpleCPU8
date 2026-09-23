#include "asm/expr.h"

#include <cctype>
#include <stdexcept>
#include <vector>

namespace sc8 {

namespace {

bool isHex(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }
bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool isNameStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isNameChar(char c) { return isNameStart(c) || isDigit(c); }

}  // namespace

std::optional<int64_t> parseLiteral(std::string_view s) {
  if (s.empty()) return std::nullopt;
  int base = 10;
  std::string_view digits = s;
  if (s[0] == '$') {
    base = 16;
    digits = s.substr(1);
  } else if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    base = 16;
    digits = s.substr(2);
  } else if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
    base = 2;
    digits = s.substr(2);
  }
  if (digits.empty()) return std::nullopt;
  int64_t v = 0;
  for (char c : digits) {
    int d;
    if (isDigit(c)) d = c - '0';
    else if (base == 16 && isHex(c)) d = 10 + (std::tolower(static_cast<unsigned char>(c)) - 'a');
    else return std::nullopt;
    if (d >= base) return std::nullopt;
    v = v * base + d;
  }
  return v;
}

bool looksLikeExpression(std::string_view text) {
  for (char c : text) {
    if (c == '[' || c == ']') return false;
  }
  for (char c : text) {
    if (std::string_view("+-*/%&^|~<>()").find(c) != std::string_view::npos) return true;
  }
  return false;
}

namespace {

struct Tok {
  enum Kind { Num, Name, Op } kind;
  int64_t num = 0;
  std::string text;
};

struct ExprError : std::runtime_error {
  ExprFailure kind;
  ExprError(const std::string& m, ExprFailure k = ExprFailure::Syntax)
      : std::runtime_error(m), kind(k) {}
};

// `&` is bitwise AND here and never address-of. The operand parser strips a
// leading `&` before an expression reaches this file.
std::vector<Tok> tokenize(std::string_view text) {
  std::vector<Tok> out;
  size_t i = 0;
  while (i < text.size()) {
    const char c = text[i];
    if (std::isspace(static_cast<unsigned char>(c))) {
      i++;
      continue;
    }
    // Numbers first, so $1C is one token rather than an operator and a name.
    if (c == '$' || isDigit(c)) {
      size_t j = i + 1;
      if (c == '0' && j < text.size() && (text[j] == 'x' || text[j] == 'b')) j++;
      while (j < text.size() && (isHex(text[j]))) j++;
      auto v = parseLiteral(text.substr(i, j - i));
      if (!v) throw ExprError("bad number " + std::string(text.substr(i, j - i)));
      out.push_back({Tok::Num, *v, {}});
      i = j;
      continue;
    }
    if (isNameStart(c)) {
      size_t j = i + 1;
      while (j < text.size() && isNameChar(text[j])) j++;
      out.push_back({Tok::Name, 0, std::string(text.substr(i, j - i))});
      i = j;
      continue;
    }
    std::string_view two = text.substr(i, 2);
    if (two == "&&" || two == "||") {
      // The English is misleading and the mistake assembles cleanly, so
      // this is refused by name.
      throw ExprError(std::string(two) + " is not an operator here. Use a single | to mask bits together.",
                      ExprFailure::Advice);
    }
    if (two == "<<" || two == ">>") {
      out.push_back({Tok::Op, 0, std::string(two)});
      i += 2;
      continue;
    }
    if (c == '[' || c == ']') {
      throw ExprError("square brackets dereference on this machine, so they cannot group. Use ( ).",
                      ExprFailure::Advice);
    }
    if (std::string_view("+-*/%&^|~()").find(c) != std::string_view::npos) {
      out.push_back({Tok::Op, 0, std::string(1, c)});
      i++;
      continue;
    }
    throw ExprError(std::string("unexpected character ") + c);
  }
  return out;
}

// C's order, tightest last in this list because the parser walks it outward.
const std::vector<std::vector<std::string>> LEVELS = {
    {"|"}, {"^"}, {"&"}, {"<<", ">>"}, {"+", "-"}, {"*", "/", "%"},
};

int64_t apply(const std::string& op, int64_t a, int64_t b) {
  if (op == "+") return a + b;
  if (op == "-") return a - b;
  if (op == "*") return a * b;
  // Division truncates toward zero, which is what C does.
  if (op == "/") {
    if (b == 0) throw ExprError("divide by zero", ExprFailure::Advice);
    return a / b;
  }
  if (op == "%") {
    if (b == 0) throw ExprError("divide by zero", ExprFailure::Advice);
    return a % b;
  }
  if (op == "<<") return b < 0 || b > 62 ? 0 : static_cast<int64_t>(static_cast<uint64_t>(a) << b);
  if (op == ">>") return b < 0 || b > 62 ? (a < 0 ? -1 : 0) : a >> b;
  if (op == "&") return a & b;
  if (op == "^") return a ^ b;
  if (op == "|") return a | b;
  throw ExprError("unknown operator " + op);
}

class Parser {
 public:
  Parser(std::vector<Tok> toks, const ExprLookup& lookup) : toks_(std::move(toks)), lookup_(lookup) {}

  int64_t parse() {
    if (toks_.empty()) throw ExprError("empty expression");
    int64_t v = binary(0);
    if (pos_ < toks_.size()) throw ExprError("unexpected " + describe(toks_[pos_]));
    return v;
  }

 private:
  static std::string describe(const Tok& t) {
    return t.kind == Tok::Num ? std::to_string(t.num) : t.text;
  }

  int64_t binary(size_t level) {
    if (level == LEVELS.size()) return unary();
    int64_t left = binary(level + 1);
    for (;;) {
      if (pos_ >= toks_.size()) return left;
      const Tok& t = toks_[pos_];
      if (t.kind != Tok::Op) return left;
      bool mine = false;
      for (const auto& op : LEVELS[level]) mine = mine || op == t.text;
      if (!mine) return left;
      const std::string op = t.text;
      pos_++;
      int64_t right = binary(level + 1);
      left = apply(op, left, right);
    }
  }

  int64_t unary() {
    if (pos_ < toks_.size() && toks_[pos_].kind == Tok::Op &&
        (toks_[pos_].text == "-" || toks_[pos_].text == "~")) {
      const bool neg = toks_[pos_].text == "-";
      pos_++;
      int64_t v = unary();
      return neg ? -v : ~v;
    }
    return primary();
  }

  int64_t primary() {
    if (pos_ >= toks_.size()) throw ExprError("expression ends early");
    const Tok& t = toks_[pos_];
    if (t.kind == Tok::Num) {
      pos_++;
      return t.num;
    }
    if (t.kind == Tok::Name) {
      pos_++;
      // A label contributes its ADDRESS. The byte stored there is a runtime
      // value the assembler cannot see.
      auto v = lookup_(t.text);
      if (!v) throw ExprError("undefined label: " + t.text, ExprFailure::Name);
      return *v;
    }
    if (t.text == "(") {
      pos_++;
      int64_t v = binary(0);
      if (pos_ >= toks_.size() || toks_[pos_].kind != Tok::Op || toks_[pos_].text != ")") {
        throw ExprError("missing )");
      }
      pos_++;
      return v;
    }
    throw ExprError("unexpected " + t.text);
  }

  std::vector<Tok> toks_;
  const ExprLookup& lookup_;
  size_t pos_ = 0;
};

}  // namespace

ExprResult evalExpr(std::string_view text, const ExprLookup& lookup) {
  try {
    int64_t v = Parser(tokenize(text), lookup).parse();
    return {true, v, ExprFailure::Syntax, ""};
  } catch (const ExprError& e) {
    return {false, 0, e.kind, e.what()};
  }
}

}  // namespace sc8
