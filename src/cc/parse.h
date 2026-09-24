// Recursive descent over the token stream, with C's precedence.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "cc/ast.h"
#include "cc/lex.h"
#include "cc/pp.h"

namespace sc8::cc {

class Parser {
 public:
  Parser(std::vector<Tok> toks, std::string file);

  ExprPtr parseExpr();
  StmtPtr statement();
  Unit parseUnit();

 private:
  struct Spec {
    BaseType base;
    Storage storage;
    bool zp, rom, isConst, isInline, isTypedef;
  };

  const Tok& t() const { return toks_[i_]; }
  Pos pos() const { return Pos{t().file, t().line}; }
  [[noreturn]] void err(const std::string& msg) const;
  [[noreturn]] void err(const std::string& msg, const Tok& tok) const;
  bool at(std::string_view text) const;
  bool atKind(TokKind k) const { return t().kind == k; }
  bool eat(std::string_view text);
  Tok want(std::string_view text);
  std::string wantId();

  bool startsType() const;
  Spec specifiers();
  int stars();
  bool isTypeAhead(size_t n) const;
  CType typeName();

  ExprPtr assign();
  ExprPtr conditional();
  ExprPtr binary(size_t level);
  ExprPtr unary();
  ExprPtr postfix();
  ExprPtr primary();

  StmtPtr block();
  double constExpr();
  StmtPtr localDecl();
  VarDecl declarator(const Spec& spec, const Pos& pos);
  void declaratorRest(VarDecl& decl);
  Initializer initializer();
  FuncDecl functionRest(const std::string& name, const CType& ret, const Spec& spec, const Pos& pos);

  std::vector<Tok> toks_;
  std::string file_;
  size_t i_ = 0;
  // Names introduced by typedef. Without them `foo * bar;` cannot be told
  // from a multiply, which is C's oldest ambiguity.
  std::set<std::string> typedefs_{"rom_t"};
};

// Fold what can be folded now. No answer when it cannot be. Numbers are
// doubles with the JavaScript operators, as the TypeScript folds them.
std::optional<double> foldConst(const Expr& e);
std::optional<double> foldConst(const ExprPtr& e);

// JavaScript's ToInt32, which every bitwise operator in the reference
// applied before it worked.
int32_t toInt32(double v);

// A number as JavaScript prints it: no decimals on an integer, the
// shortest digits that read back exactly otherwise.
std::string numText(double v);

// The `// build:` lines of a raw source text, in order.
std::vector<std::string> buildLines(const std::string& src);

Unit parse(const std::string& src, const std::string& file = "main.c", const PpOptions* opts = nullptr);

}  // namespace sc8::cc
