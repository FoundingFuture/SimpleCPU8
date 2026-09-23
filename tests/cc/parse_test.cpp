#include <doctest.h>

#include <string>
#include <vector>

#include "cc/ast.h"
#include "cc/lex.h"
#include "cc/parse.h"

using namespace sc8::cc;

namespace {

std::string boom(const std::string& src) {
  try { parse(src); } catch (const CcError& e) { return e.message(); }
  return "no error";
}

// The expression of `return <src>;` inside a function.
struct Held {
  Unit unit;
  ExprPtr e;
};

Held expr(const std::string& src) {
  Held h;
  h.unit = parse("int f(void) { return " + src + "; }");
  h.e = h.unit.funcs[0].body->body[0]->e;
  return h;
}

std::string shape(const ExprPtr& e) {
  switch (e->k) {
    case ExprKind::Num: return numText(e->value);
    case ExprKind::Id: return e->name;
    case ExprKind::Bin: return "(" + shape(e->l) + " " + e->op + " " + shape(e->r) + ")";
    case ExprKind::Un: return e->op + shape(e->e);
    case ExprKind::Assign: return "(" + shape(e->l) + " " + e->op + " " + shape(e->r) + ")";
    case ExprKind::Call: {
      std::string args;
      for (size_t i = 0; i < e->args.size(); i++) args += (i ? "," : "") + shape(e->args[i]);
      return shape(e->fn) + "(" + args + ")";
    }
    case ExprKind::Index: return shape(e->a) + "[" + shape(e->i) + "]";
    case ExprKind::Cond: return "(" + shape(e->c) + " ? " + shape(e->t) + " : " + shape(e->f) + ")";
    case ExprKind::Cast: return "(" + typeName(e->type) + ")" + shape(e->e);
    case ExprKind::Post: return shape(e->e) + e->op;
    case ExprKind::Sizeof: return "sizeof";
    case ExprKind::Str: return "str";
    case ExprKind::Comma: return "(" + shape(e->l) + " , " + shape(e->r) + ")";
  }
  return "?";
}

std::string shapeOf(const std::string& src) { return shape(expr(src).e); }

bool has(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

std::vector<StmtKind> kinds(const std::string& src) {
  const Unit u = parse("void f(void) { " + src + " }");
  std::vector<StmtKind> out;
  for (const StmtPtr& s : u.funcs[0].body->body) out.push_back(s->k);
  return out;
}

}  // namespace

TEST_SUITE("declarations") {
  TEST_CASE("reads a global with an initializer") {
    const Unit u = parse("int score = 7;");
    CHECK(u.vars[0].name == "score");
    CHECK(typeName(u.vars[0].type) == "int");
    CHECK(u.vars[0].init->one->k == ExprKind::Num);
  }

  TEST_CASE("reads several names on one line") {
    const Unit u = parse("unsigned char a, b, c;");
    std::vector<std::string> names;
    for (const VarDecl& v : u.vars) names.push_back(v.name);
    CHECK(names == std::vector<std::string>{"a", "b", "c"});
    for (const VarDecl& v : u.vars) CHECK(v.type.base == BaseType::UChar);
  }

  TEST_CASE("gives each star to the name it is written on, the way C does") {
    const Unit u = parse("char *p, q;");
    CHECK(typeName(u.vars[0].type) == "char*");
    CHECK(typeName(u.vars[1].type) == "char");
  }

  TEST_CASE("takes an array length from the initializer when it is left open") {
    CHECK(parse("int t[] = { 1, 2, 3 };").vars[0].type.arrayLen == 3);
  }

  TEST_CASE("counts the terminator when a string fills a char array") {
    CHECK(parse("char m[] = \"hi\";").vars[0].type.arrayLen == 3);
  }

  TEST_CASE("folds a constant expression in an array bound") {
    CHECK(parse("char b[4 * 8];").vars[0].type.arrayLen == 32);
  }

  TEST_CASE("carries __ROM and makes it const") {
    const VarDecl v = parse("__ROM const unsigned char ship[] = { 1, 2 };").vars[0];
    CHECK(v.rom);
    CHECK(v.isConst);
  }

  TEST_CASE("carries __zp") { CHECK(parse("__zp unsigned char frame;").vars[0].zp); }

  TEST_CASE("spells float and double the same, because the ACP has one format") {
    const Unit u = parse("float a; double b;");
    CHECK(u.vars[0].type.base == BaseType::Double);
    CHECK(u.vars[1].type.base == BaseType::Double);
  }

  TEST_CASE("makes long and long long the same 64 bit integer") {
    const Unit u = parse("long a; long long b; unsigned long c;");
    CHECK(u.vars[0].type.base == BaseType::Long);
    CHECK(u.vars[1].type.base == BaseType::Long);
    CHECK(u.vars[2].type.base == BaseType::ULong);
  }
}

TEST_SUITE("sizes") {
  TEST_CASE("are the machine's") {
    CHECK(sizeOf(T(BaseType::Char)) == 1);
    CHECK(sizeOf(T(BaseType::Int)) == 2);
    CHECK(sizeOf(T(BaseType::Int, 1)) == 2);
    CHECK(sizeOf(T(BaseType::Double)) == 8);
    CHECK(sizeOf(T(BaseType::RomT)) == 3);
    CHECK(sizeOf(CType{BaseType::Int, 0, 10}) == 20);
  }
}

TEST_SUITE("functions") {
  TEST_CASE("reads a definition with parameters") {
    const FuncDecl f = parse("int add(int a, char b) { return a; }").funcs[0];
    CHECK(f.name == "add");
    REQUIRE(f.params.size() == 2);
    CHECK(f.params[0].name + ":" + typeName(f.params[0].type) == "a:int");
    CHECK(f.params[1].name + ":" + typeName(f.params[1].type) == "b:char");
    CHECK(f.body != nullptr);
  }

  TEST_CASE("reads a prototype, which has no body") {
    CHECK(parse("int f(void);").funcs[0].body == nullptr);
  }

  TEST_CASE("takes void as no parameters at all") {
    CHECK(parse("int f(void) { return 0; }").funcs[0].params.empty());
  }

  TEST_CASE("marks static, which is what makes a label private later") {
    CHECK(parse("static int f(void) { return 0; }").funcs[0].isStatic);
  }

  TEST_CASE("marks variadic, for printf") {
    CHECK(parse("int p(const char *f, ...);").funcs[0].variadic);
  }

  TEST_CASE("turns an array parameter into a pointer") {
    const FuncDecl f = parse("int f(char buf[16]) { return 0; }").funcs[0];
    CHECK(typeName(f.params[0].type) == "char*");
  }
}

TEST_SUITE("expressions and their precedence") {
  TEST_CASE("multiplies before it adds") { CHECK(shapeOf("1 + 2 * 3") == "(1 + (2 * 3))"); }

  TEST_CASE("shifts after it adds, which is C and trips people up") {
    CHECK(shapeOf("1 + 2 << 3") == "((1 + 2) << 3)");
  }

  TEST_CASE("puts & below == , which is the other one") { CHECK(shapeOf("a & b == c") == "(a & (b == c))"); }

  TEST_CASE("associates subtraction to the left") { CHECK(shapeOf("9 - 4 - 3") == "((9 - 4) - 3)"); }

  TEST_CASE("associates assignment to the right") { CHECK(shapeOf("a = b = 1") == "(a = (b = 1))"); }

  TEST_CASE("binds a unary minus tighter than a multiply") { CHECK(shapeOf("-a * b") == "(-a * b)"); }

  TEST_CASE("reads a call with arguments") { CHECK(shapeOf("f(1, g(2))") == "f(1,g(2))"); }

  TEST_CASE("reads an index and a postfix step") { CHECK(shapeOf("t[i++]") == "t[i++]"); }

  TEST_CASE("tells a cast from a parenthesised expression") {
    CHECK(shapeOf("(char)x") == "(char)x");
    CHECK(shapeOf("(x)+1") == "(x + 1)");
  }

  TEST_CASE("reads the conditional, right associatively") {
    CHECK(shapeOf("a ? b : c ? d : e") == "(a ? b : (c ? d : e))");
  }

  // A string's text is its content, so a string holding a bracket once
  // matched the bracket itself and the parser ate the argument.
  TEST_CASE("does not mistake a string for the punctuation it contains") {
    CHECK(shapeOf("f(\")\")") == "f(str)");
    CHECK(shapeOf("g(\"(\", 1)") == "g(str,1)");
    CHECK(shapeOf("h(\"]\")") == "h(str)");
    CHECK(shapeOf("k(\",\")") == "k(str)");
  }

  TEST_CASE("joins adjacent string literals, which printf formats want") {
    const Unit u = parse("char *s = \"ab\" \"cd\";");
    CHECK(u.vars[0].init->one->bytes.size() == 4);
  }
}

TEST_SUITE("statements") {
  TEST_CASE("reads the whole statement set") {
    CHECK(kinds("if (a) b(); while (a) b(); do b(); while (a); for (;;) b();") ==
          std::vector<StmtKind>{StmtKind::If, StmtKind::While, StmtKind::Do, StmtKind::For});
    CHECK(kinds("return 1; break; continue; ;") ==
          std::vector<StmtKind>{StmtKind::Return, StmtKind::Break, StmtKind::Continue, StmtKind::Empty});
  }

  TEST_CASE("attaches else to the nearest if") {
    const Unit u = parse("void f(void){ if (a) if (b) c(); else d(); }");
    const StmtPtr outer = u.funcs[0].body->body[0];
    CHECK(outer->f == nullptr);
    CHECK(outer->t->f != nullptr);
  }

  TEST_CASE("reads a for with all three parts, and a declaration in the first") {
    const Unit u = parse("void f(void){ for (int i = 0; i < 3; i++) g(); }");
    const StmtPtr f = u.funcs[0].body->body[0];
    CHECK(f->init->k == StmtKind::Var);
    CHECK(f->c != nullptr);
    CHECK(f->step != nullptr);
  }

  TEST_CASE("reads a switch with cases and a default") {
    const Unit u = parse("void f(void){ switch (x) { case 1: a(); break; case 2: default: b(); } }");
    const StmtPtr s = u.funcs[0].body->body[0];
    REQUIRE(s->cases.size() == 3);
    CHECK(s->cases[0].value == 1);
    CHECK(s->cases[1].value == 2);
    CHECK(!s->cases[2].value);
  }

  TEST_CASE("reads inline asm as its text") {
    const Unit u = parse("void f(void){ asm(\"HLT\"); }");
    CHECK(u.funcs[0].body->body[0]->text == "HLT");
  }
}

TEST_SUITE("the build line") {
  TEST_CASE("is read out of a comment, in order") {
    const Unit u = parse("// build: cc -c main.c\n// build: ld -o g main.o -lgpu\nint x;");
    CHECK(u.builds == std::vector<std::string>{"cc -c main.c", "ld -o g main.o -lgpu"});
  }

  TEST_CASE("is not confused by the word build inside code") {
    CHECK(parse("char *s = \"build: no\";").builds.empty());
  }
}

TEST_SUITE("constant folding") {
  TEST_CASE("folds what the assembler would have folded") {
    CHECK(foldConst(expr("2 + 3 * 4").e) == 14);
    CHECK(foldConst(expr("1 << 4").e) == 16);
    CHECK(foldConst(expr("7 & 3").e) == 3);
  }

  TEST_CASE("gives up on anything with a name in it") { CHECK(!foldConst(expr("x + 1").e)); }

  TEST_CASE("refuses to divide by zero at compile time") { CHECK(!foldConst(expr("1 / 0").e)); }
}

TEST_SUITE("what the parser refuses") {
  TEST_CASE("names the token it did not expect") {
    CHECK(has(boom("int f(void) { return 1 }"), "expected \";\""));
  }

  TEST_CASE("catches an unclosed brace") { CHECK(has(boom("int f(void) { return 1;"), "unclosed {")); }

  TEST_CASE("refuses a declaration with no type") { CHECK(has(boom("x = 1;"), "needs a type")); }

  TEST_CASE("refuses an array with neither length nor initializer") {
    CHECK(has(boom("int t[];"), "needs a length or an initializer"));
  }

  TEST_CASE("refuses a case label that is not constant") {
    CHECK(has(boom("void f(void){ switch (x) { case y: break; } }"), "constant"));
  }

  TEST_CASE("reports the line, so the editor can point at it") {
    try {
      parse("int a;\nint b;\nint f(void) { return 1 }");
      FAIL("no error");
    } catch (const CcError& e) {
      CHECK(e.line == 3);
    }
  }
}
