#include "cc/parse.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace sc8::cc {

namespace {

// Binary precedence, loosest first. Each row binds tighter than the one above.
const std::vector<std::vector<std::string_view>> LEVELS = {
    {"||"}, {"&&"}, {"|"}, {"^"}, {"&"},
    {"==", "!="}, {"<", ">", "<=", ">="},
    {"<<", ">>"}, {"+", "-"}, {"*", "/", "%"},
};

const std::set<std::string_view> ASSIGN_OPS = {
    "=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=",
};

const std::set<std::string_view> TYPE_WORDS = {
    "char", "short", "int", "long", "unsigned", "signed", "float",
    "double", "void", "const", "static", "extern", "__ROM", "__zp",
    "inline", "typedef",
};

// The asset initializers, by the form each spells. `__ROM const unsigned
// char ship[] = __image("ship.png");` takes its bytes from the file.
const std::map<std::string_view, AssetForm> ASSET_FORMS = {
    {"__image", AssetForm::Image},   {"__sprite", AssetForm::Sprite}, {"__palette", AssetForm::Palette},
    {"__sample", AssetForm::Sample}, {"__file", AssetForm::File},     {"__font", AssetForm::Font},
};

const std::set<std::string_view> TYPE_AHEAD_WORDS = {
    "char", "short", "int", "long", "unsigned", "signed", "float",
    "double", "void", "const",
};

// JSON.stringify on a string, which is how the reference quotes a token.
std::string quote(std::string_view s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c == '\t') out += "\\t";
    else out += c;
  }
  return out + "\"";
}

ExprPtr mk(ExprKind k, Pos pos) {
  auto e = std::make_shared<Expr>();
  e->k = k;
  e->pos = std::move(pos);
  return e;
}

StmtPtr mkStmt(StmtKind k, Pos pos) {
  auto s = std::make_shared<Stmt>();
  s->k = k;
  s->pos = std::move(pos);
  return s;
}

bool isFloatText(const std::string& text) {
  if (text.size() > 1 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) return false;
  return text.find_first_of(".eE") != std::string::npos;
}

}  // namespace

int32_t toInt32(double v) {
  if (!std::isfinite(v)) return 0;
  double t = std::fmod(std::trunc(v), 4294967296.0);
  if (t < 0) t += 4294967296.0;
  return static_cast<int32_t>(static_cast<uint32_t>(t));
}

std::string numText(double v) {
  if (std::isfinite(v) && v == std::trunc(v) && std::fabs(v) < 1e15) {
    return std::to_string(static_cast<long long>(v));
  }
  char buf[64];
  for (int prec = 1; prec <= 17; prec++) {
    std::snprintf(buf, sizeof buf, "%.*g", prec, v);
    if (std::strtod(buf, nullptr) == v) break;
  }
  return buf;
}

Parser::Parser(std::vector<Tok> toks, std::string file) : toks_(std::move(toks)), file_(std::move(file)) {}

void Parser::err(const std::string& msg) const { err(msg, t()); }

void Parser::err(const std::string& msg, const Tok& tok) const { throw CcError(tok.file, tok.line, msg); }

// A token matches a spelling only when it is a WORD or a PUNCTUATOR. A
// string's text is its content, so `f(")")` once matched the closing paren
// against the string and swallowed it: the argument vanished and the real
// paren became a stray expression.
bool Parser::at(std::string_view text) const {
  const TokKind k = t().kind;
  if (k == TokKind::Str || k == TokKind::Num) return false;
  return t().text == text;
}

bool Parser::eat(std::string_view text) {
  if (at(text)) { i_++; return true; }
  return false;
}

Tok Parser::want(std::string_view text) {
  if (!at(text)) {
    err("expected " + quote(text) + ", found " + quote(t().text.empty() ? "end of file" : t().text));
  }
  return toks_[i_++];
}

std::string Parser::wantId() {
  if (t().kind != TokKind::Id) err("expected a name, found " + quote(t().text));
  return toks_[i_++].text;
}

// ---- types -----------------------------------------------------------------

bool Parser::startsType() const {
  const std::string& x = t().text;
  if (t().kind == TokKind::Kw) return TYPE_WORDS.count(x) > 0;
  return t().kind == TokKind::Id && typedefs_.count(x) > 0;
}

// Reads the declaration specifiers: storage, qualifiers and the base type.
Parser::Spec Parser::specifiers() {
  Storage storage = Storage::Auto;
  bool zp = false, rom = false, isConst = false, isInline = false, isTypedef = false;
  int sign = 0;  // 0 none, 1 signed, 2 unsigned
  std::optional<std::string> core;
  int longs = 0;

  for (;;) {
    const std::string& x = t().text;
    if (x == "static") { storage = Storage::Static; i_++; continue; }
    if (x == "extern") { storage = Storage::Extern; i_++; continue; }
    if (x == "typedef") { isTypedef = true; i_++; continue; }
    if (x == "const") { isConst = true; i_++; continue; }
    if (x == "inline") { isInline = true; i_++; continue; }
    if (x == "__zp") { zp = true; i_++; continue; }
    if (x == "__ROM") { rom = true; isConst = true; i_++; continue; }
    if (x == "unsigned") { sign = 2; i_++; continue; }
    if (x == "signed") { sign = 1; i_++; continue; }
    if (x == "long") { longs++; i_++; continue; }
    if (x == "short") { if (!core) core = "int"; i_++; continue; }
    if (x == "char" || x == "int" || x == "void" || x == "double" || x == "float") {
      core = x; i_++; continue;
    }
    if (t().kind == TokKind::Id && typedefs_.count(x) && !core && longs == 0) {
      core = x; i_++; continue;
    }
    break;
  }

  BaseType base;
  const bool u = sign == 2;
  if (core == "void") base = BaseType::Void;
  else if (core == "double" || core == "float") base = BaseType::Double;
  else if (core == "rom_t") base = BaseType::RomT;
  else if (longs > 0) base = u ? BaseType::ULong : BaseType::Long;
  else if (core == "char") base = u ? BaseType::UChar : BaseType::Char;
  else base = u ? BaseType::UInt : BaseType::Int;

  if (!core && longs == 0 && sign == 0 && !isTypedef) {
    // Nothing named a type at all. `x = 1;` at file scope, say.
    err("a declaration needs a type");
  }
  return Spec{base, storage, zp, rom, isConst, isInline, isTypedef};
}

int Parser::stars() {
  int n = 0;
  while (eat("*")) {
    n++;
    // `char * const p` is accepted and the qualifier ignored: nothing here
    // can write through a pointer differently for it.
    while (at("const")) i_++;
  }
  return n;
}

// ---- expressions -----------------------------------------------------------

ExprPtr Parser::parseExpr() {
  ExprPtr e = assign();
  while (at(",")) {
    Pos p = pos();
    i_++;
    ExprPtr c = mk(ExprKind::Comma, p);
    c->l = e;
    c->r = assign();
    e = c;
  }
  return e;
}

ExprPtr Parser::assign() {
  ExprPtr l = conditional();
  if (t().kind == TokKind::Punct && ASSIGN_OPS.count(t().text)) {
    const std::string op = t().text;
    Pos p = pos();
    i_++;
    ExprPtr e = mk(ExprKind::Assign, p);
    e->op = op;
    e->l = l;
    e->r = assign();
    return e;
  }
  return l;
}

ExprPtr Parser::conditional() {
  ExprPtr c = binary(0);
  if (!at("?")) return c;
  Pos p = pos();
  i_++;
  ExprPtr tr = parseExpr();
  want(":");
  ExprPtr e = mk(ExprKind::Cond, p);
  e->c = c;
  e->t = tr;
  e->f = conditional();
  return e;
}

ExprPtr Parser::binary(size_t level) {
  if (level >= LEVELS.size()) return unary();
  ExprPtr l = binary(level + 1);
  for (;;) {
    const std::string& op = t().text;
    bool inLevel = false;
    for (std::string_view o : LEVELS[level]) if (o == op) inLevel = true;
    if (t().kind != TokKind::Punct || !inLevel) return l;
    Pos p = pos();
    i_++;
    ExprPtr e = mk(ExprKind::Bin, p);
    e->op = op;
    e->l = l;
    e->r = binary(level + 1);
    l = e;
  }
}

ExprPtr Parser::unary() {
  Pos p = pos();
  const std::string x = t().text;

  if (x == "sizeof" && t().kind == TokKind::Kw) {
    i_++;
    if (at("(") && isTypeAhead(1)) {
      want("(");
      CType type = typeName();
      want(")");
      ExprPtr e = mk(ExprKind::Sizeof, p);
      e->ofType = type;
      return e;
    }
    ExprPtr e = mk(ExprKind::Sizeof, p);
    e->e = unary();
    return e;
  }

  static const std::set<std::string_view> UN = {"-", "+", "!", "~", "*", "&", "++", "--"};
  if (UN.count(x) && t().kind == TokKind::Punct) {
    i_++;
    ExprPtr e = mk(ExprKind::Un, p);
    e->op = x;
    e->e = unary();
    return e;
  }

  // A cast, told from a parenthesised expression by what follows the paren.
  if (x == "(" && t().kind == TokKind::Punct && isTypeAhead(1)) {
    want("(");
    CType type = typeName();
    want(")");
    ExprPtr e = mk(ExprKind::Cast, p);
    e->type = type;
    e->e = unary();
    return e;
  }

  return postfix();
}

// Is the token at offset n the start of a type name? Used for casts and
// sizeof, which are the two places C needs the lookahead.
bool Parser::isTypeAhead(size_t n) const {
  if (i_ + n >= toks_.size()) return false;
  const Tok& tk = toks_[i_ + n];
  if (tk.kind == TokKind::Kw) return TYPE_AHEAD_WORDS.count(tk.text) > 0;
  return tk.kind == TokKind::Id && typedefs_.count(tk.text) > 0;
}

CType Parser::typeName() {
  const Spec spec = specifiers();
  return CType{spec.base, stars(), std::nullopt};
}

ExprPtr Parser::postfix() {
  ExprPtr e = primary();
  for (;;) {
    Pos p = pos();
    if (eat("[")) {
      ExprPtr idx = parseExpr();
      want("]");
      ExprPtr n = mk(ExprKind::Index, p);
      n->a = e;
      n->i = idx;
      e = n;
      continue;
    }
    if (eat("(")) {
      ExprPtr n = mk(ExprKind::Call, p);
      n->fn = e;
      if (!at(")")) {
        do { n->args.push_back(assign()); } while (eat(","));
      }
      want(")");
      e = n;
      continue;
    }
    if (at("++") || at("--")) {
      ExprPtr n = mk(ExprKind::Post, p);
      n->op = t().text;
      i_++;
      n->e = e;
      e = n;
      continue;
    }
    return e;
  }
}

ExprPtr Parser::primary() {
  Pos p = pos();
  const Tok& tk = t();
  if (tk.kind == TokKind::Num) {
    i_++;
    ExprPtr e = mk(ExprKind::Num, p);
    e->value = tk.value;
    // An integer literal takes the first type it fits, which is what C
    // does. 60000 is unsigned, and getting that wrong made 60000 / 3 a
    // signed divide of -5536.
    if (isFloatText(tk.text)) e->type = T(BaseType::Double);
    else if (tk.value >= -32768 && tk.value <= 32767) e->type = T(BaseType::Int);
    else if (tk.value <= 65535) e->type = T(BaseType::UInt);
    else e->type = T(BaseType::Long);
    return e;
  }
  if (tk.kind == TokKind::Str) {
    i_++;
    ExprPtr e = mk(ExprKind::Str, p);
    e->bytes = tk.bytes;
    // Adjacent string literals concatenate, which printf formats rely on.
    while (t().kind == TokKind::Str) {
      e->bytes.insert(e->bytes.end(), t().bytes.begin(), t().bytes.end());
      i_++;
    }
    return e;
  }
  if (tk.kind == TokKind::Id) {
    i_++;
    ExprPtr e = mk(ExprKind::Id, p);
    e->name = tk.text;
    return e;
  }
  if (eat("(")) {
    ExprPtr e = parseExpr();
    want(")");
    return e;
  }
  err("expected an expression, found " + quote(tk.text.empty() ? "end of file" : tk.text));
}

// ---- statements ------------------------------------------------------------

StmtPtr Parser::block() {
  Pos p = pos();
  want("{");
  StmtPtr s = mkStmt(StmtKind::Block, p);
  while (!at("}")) {
    if (atKind(TokKind::Eof)) err("unclosed {");
    s->body.push_back(statement());
  }
  want("}");
  return s;
}

StmtPtr Parser::statement() {
  Pos p = pos();
  const std::string x = t().text;

  if (at("{")) return block();
  if (eat(";")) return mkStmt(StmtKind::Empty, p);

  if (at("asm")) {
    i_++;
    want("(");
    if (t().kind != TokKind::Str) err("asm takes a string");
    const Tok& s = toks_[i_++];
    StmtPtr st = mkStmt(StmtKind::Asm, p);
    st->text = std::string(s.bytes.begin(), s.bytes.end());
    want(")");
    want(";");
    return st;
  }

  if (at("if")) {
    i_++;
    want("(");
    StmtPtr s = mkStmt(StmtKind::If, p);
    s->c = parseExpr();
    want(")");
    s->t = statement();
    if (eat("else")) s->f = statement();
    return s;
  }

  if (at("while")) {
    i_++;
    want("(");
    StmtPtr s = mkStmt(StmtKind::While, p);
    s->c = parseExpr();
    want(")");
    s->loopBody = statement();
    return s;
  }

  if (at("do")) {
    i_++;
    StmtPtr s = mkStmt(StmtKind::Do, p);
    s->loopBody = statement();
    want("while");
    want("(");
    s->c = parseExpr();
    want(")");
    want(";");
    return s;
  }

  if (at("for")) {
    i_++;
    want("(");
    StmtPtr s = mkStmt(StmtKind::For, p);
    if (!at(";")) {
      if (startsType()) {
        s->init = localDecl();
      } else {
        s->init = mkStmt(StmtKind::Expr, p);
        s->init->e = parseExpr();
        want(";");
      }
    } else {
      want(";");
    }
    if (!at(";")) s->c = parseExpr();
    want(";");
    if (!at(")")) s->step = parseExpr();
    want(")");
    s->loopBody = statement();
    return s;
  }

  if (at("switch")) {
    i_++;
    want("(");
    StmtPtr s = mkStmt(StmtKind::Switch, p);
    s->e = parseExpr();
    want(")");
    want("{");
    while (!at("}")) {
      if (atKind(TokKind::Eof)) err("unclosed switch");
      SwitchCase c;
      c.pos = pos();
      if (eat("case")) {
        c.value = constExpr();
        want(":");
      } else if (eat("default")) {
        want(":");
      } else {
        err("a switch body holds case and default labels only");
      }
      while (!at("case") && !at("default") && !at("}")) c.body.push_back(statement());
      s->cases.push_back(std::move(c));
    }
    want("}");
    return s;
  }

  if (at("return")) {
    i_++;
    StmtPtr s = mkStmt(StmtKind::Return, p);
    if (eat(";")) return s;
    s->e = parseExpr();
    want(";");
    return s;
  }

  if (at("break")) { i_++; want(";"); return mkStmt(StmtKind::Break, p); }
  if (at("continue")) { i_++; want(";"); return mkStmt(StmtKind::Continue, p); }

  if (startsType()) return localDecl();

  StmtPtr s = mkStmt(StmtKind::Expr, p);
  s->e = parseExpr();
  want(";");
  return s;
}

// A constant expression, folded now. Used by case labels and array bounds.
double Parser::constExpr() {
  ExprPtr e = conditional();
  const std::optional<double> v = foldConst(e);
  if (!v) err("this has to be a constant the compiler can work out");
  return *v;
}

// One local declaration line, which may declare several names.
StmtPtr Parser::localDecl() {
  Pos p = pos();
  const Spec spec = specifiers();
  if (spec.isTypedef) err("typedef inside a function is not in the subset");
  std::vector<VarDecl> decls;
  do {
    decls.push_back(declarator(spec, p));
  } while (eat(","));
  want(";");
  auto var = [&](VarDecl d) {
    StmtPtr s = mkStmt(StmtKind::Var, p);
    s->decl = std::move(d);
    return s;
  };
  if (decls.size() == 1) return var(decls[0]);
  StmtPtr b = mkStmt(StmtKind::Block, p);
  for (VarDecl& d : decls) b->body.push_back(var(std::move(d)));
  return b;
}

// One name in a declaration, with its stars, array bound and initializer.
VarDecl Parser::declarator(const Spec& spec, const Pos& p) {
  const int ptr = stars();
  const std::string name = wantId();
  VarDecl decl;
  decl.name = name;
  decl.type = CType{spec.base, ptr, std::nullopt};
  decl.storage = spec.storage;
  decl.zp = spec.zp;
  decl.rom = spec.rom;
  decl.isConst = spec.isConst;
  decl.pos = p;
  declaratorRest(decl);
  return decl;
}

// The array bound and the initializer of one declarator, after its name. An
// open bound is filled from the initializer. An asset form leaves it open:
// its length is a file's, known only when the cartridge is laid out.
void Parser::declaratorRest(VarDecl& decl) {
  bool open = false;
  if (eat("[")) {
    if (at("]")) open = true;  // filled from the initializer
    else decl.type.arrayLen = static_cast<int>(constExpr());
    want("]");
  }
  const Tok& initTok = t();
  if (eat("=")) decl.init = initializer();
  if (decl.init && decl.init->asset) {
    const std::string form = assetFormName(decl.init->asset->form);
    if (!decl.rom) err(form + " fills a __ROM object. Declare " + decl.name + " __ROM.", initTok);
    if (decl.type.ptr != 0 || decl.type.base != BaseType::UChar || (!open && !decl.type.arrayLen)) {
      err(form + " fills an unsigned char array. Declare " + decl.name + " as unsigned char " + decl.name + "[].",
          initTok);
    }
    if (!open) {
      err(form + " sets the length itself. Leave the brackets of " + decl.name + " empty.", initTok);
    }
    return;
  }
  if (open) {
    if (decl.init && decl.init->isList) decl.type.arrayLen = static_cast<int>(decl.init->list.size());
    else if (decl.init && decl.init->one->k == ExprKind::Str) {
      decl.type.arrayLen = static_cast<int>(decl.init->one->bytes.size()) + 1;
    } else {
      err(decl.name + " needs a length or an initializer");
    }
  }
}

Initializer Parser::initializer() {
  Initializer init;
  if (t().kind == TokKind::Id) {
    auto form = ASSET_FORMS.find(t().text);
    if (form != ASSET_FORMS.end()) {
      const std::string word = t().text;
      i_++;
      want("(");
      if (t().kind != TokKind::Str) err(word + " takes a file name in quotes");
      AssetInit a;
      a.form = form->second;
      a.name = std::string(t().bytes.begin(), t().bytes.end());
      i_++;
      if (eat(",")) {
        if (a.form != AssetForm::Sprite) err(word + " takes one argument, the file name");
        a.frames = static_cast<int>(constExpr());
        a.framesGiven = true;
      }
      want(")");
      init.asset = a;
      return init;
    }
  }
  if (!eat("{")) { init.one = assign(); return init; }
  init.isList = true;
  if (!at("}")) {
    do {
      if (at("}")) break;  // a trailing comma
      init.list.push_back(assign());
    } while (eat(","));
  }
  want("}");
  return init;
}

// ---- the unit --------------------------------------------------------------

Unit Parser::parseUnit() {
  Unit unit;
  unit.file = file_;
  while (!atKind(TokKind::Eof)) {
    if (eat(";")) continue;
    Pos p = pos();
    const Spec spec = specifiers();

    if (spec.isTypedef) {
      stars();
      const std::string name = wantId();
      typedefs_.insert(name);
      want(";");
      continue;
    }

    // A function or a list of variables, told apart by the "(" after the
    // first name.
    const int ptr = stars();
    const std::string name = wantId();

    if (at("(")) {
      unit.funcs.push_back(functionRest(name, CType{spec.base, ptr, std::nullopt}, spec, p));
      continue;
    }

    // Variables. The first name is already read, so its declarator is
    // finished by hand and the rest go through the normal path.
    VarDecl first;
    first.name = name;
    first.type = CType{spec.base, ptr, std::nullopt};
    first.storage = spec.storage;
    first.zp = spec.zp;
    first.rom = spec.rom;
    first.isConst = spec.isConst;
    first.pos = p;
    declaratorRest(first);
    unit.vars.push_back(std::move(first));
    while (eat(",")) unit.vars.push_back(declarator(spec, p));
    want(";");
  }
  return unit;
}

FuncDecl Parser::functionRest(const std::string& name, const CType& ret, const Spec& spec, const Pos& p) {
  want("(");
  FuncDecl fn;
  fn.name = name;
  fn.ret = ret;
  fn.isStatic = spec.storage == Storage::Static;
  fn.isInline = spec.isInline;
  fn.pos = p;
  if (!at(")")) {
    if (at("void") && i_ + 1 < toks_.size() && toks_[i_ + 1].text == ")") {
      i_++;
    } else {
      do {
        if (eat("...")) { fn.variadic = true; break; }
        const Spec ps = specifiers();
        const int pptr = stars();
        std::string pname;
        if (t().kind == TokKind::Id) pname = toks_[i_++].text;
        CType ptype{ps.base, pptr, std::nullopt};
        // An array parameter is a pointer, the way C says.
        if (eat("[")) {
          if (!at("]")) constExpr();
          want("]");
          ptype.ptr++;
        }
        fn.params.push_back(Param{pname, ptype});
      } while (eat(","));
    }
  }
  want(")");

  if (eat(";")) return fn;
  fn.body = block();
  return fn;
}

std::optional<double> foldConst(const ExprPtr& e) {
  if (!e) return std::nullopt;
  return foldConst(*e);
}

std::optional<double> foldConst(const Expr& e) {
  switch (e.k) {
    case ExprKind::Num: return e.value;
    case ExprKind::Un: {
      const auto v = foldConst(e.e);
      if (!v) return std::nullopt;
      if (e.op == "-") return -*v;
      if (e.op == "+") return *v;
      if (e.op == "!") return *v == 0 ? 1 : 0;
      if (e.op == "~") return static_cast<double>(~toInt32(*v));
      return std::nullopt;
    }
    case ExprKind::Cast: return foldConst(e.e);
    case ExprKind::Bin: {
      const auto a = foldConst(e.l);
      const auto b = foldConst(e.r);
      if (!a || !b) return std::nullopt;
      const std::string& op = e.op;
      if (op == "+") return *a + *b;
      if (op == "-") return *a - *b;
      if (op == "*") return *a * *b;
      if (op == "/") return *b == 0 ? std::nullopt : std::optional<double>(std::trunc(*a / *b));
      if (op == "%") return *b == 0 ? std::nullopt : std::optional<double>(std::fmod(*a, *b));
      if (op == "<<") return static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(toInt32(*a)) << (toInt32(*b) & 31)));
      if (op == ">>") return static_cast<double>(toInt32(*a) >> (toInt32(*b) & 31));
      if (op == "&") return static_cast<double>(toInt32(*a) & toInt32(*b));
      if (op == "|") return static_cast<double>(toInt32(*a) | toInt32(*b));
      if (op == "^") return static_cast<double>(toInt32(*a) ^ toInt32(*b));
      if (op == "<") return *a < *b ? 1 : 0;
      if (op == ">") return *a > *b ? 1 : 0;
      if (op == "<=") return *a <= *b ? 1 : 0;
      if (op == ">=") return *a >= *b ? 1 : 0;
      if (op == "==") return *a == *b ? 1 : 0;
      if (op == "!=") return *a != *b ? 1 : 0;
      if (op == "&&") return (*a != 0 && *b != 0) ? 1 : 0;
      if (op == "||") return (*a != 0 || *b != 0) ? 1 : 0;
      return std::nullopt;
    }
    case ExprKind::Cond: {
      const auto c = foldConst(e.c);
      if (!c) return std::nullopt;
      return *c != 0 ? foldConst(e.t) : foldConst(e.f);
    }
    default: return std::nullopt;
  }
}

std::vector<std::string> buildLines(const std::string& src) {
  std::vector<std::string> builds;
  size_t start = 0;
  while (start <= src.size()) {
    size_t nl = src.find('\n', start);
    if (nl == std::string::npos) nl = src.size();
    std::string_view line(src.data() + start, nl - start);
    size_t p = 0;
    while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) p++;
    if (line.substr(p).starts_with("//")) {
      p += 2;
      while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) p++;
      if (line.substr(p).starts_with("build:")) {
        p += 6;
        while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) p++;
        std::string rest(line.substr(p));
        while (!rest.empty() && (rest.back() == '\r' || rest.back() == ' ' || rest.back() == '\t')) rest.pop_back();
        if (!rest.empty()) builds.push_back(rest);
      }
    }
    start = nl + 1;
  }
  return builds;
}

Unit parse(const std::string& src, const std::string& file, const PpOptions* opts) {
  // Build lines live in comments, so they are read off the RAW text: the
  // preprocessor strips comments before anything else sees them.
  const std::vector<std::string> builds = buildLines(src);
  PpResult pp = preprocess(src, file, opts ? *opts : PpOptions{});
  Unit unit = Parser(lexLines(pp.lines), file).parseUnit();
  unit.builds = builds;
  unit.included = pp.included;
  return unit;
}

}  // namespace sc8::cc
