#include <doctest.h>

#include <map>
#include <string>

#include "cc/pp.h"

using namespace sc8::cc;

namespace {

// The lines joined, runs of blanks squeezed, trimmed.
std::string text(const std::string& src, const PpOptions& opts = {}) {
  std::string joined;
  for (const PpLine& l : preprocess(src, "main.c", opts).lines) joined += l.text + "\n";
  std::string out;
  bool blank = false;
  for (char c : joined) {
    if (c == ' ' || c == '\t') { blank = true; continue; }
    if (blank) out += ' ';
    blank = false;
    out += c;
  }
  size_t a = 0, b = out.size();
  while (a < b && (out[a] == ' ' || out[a] == '\n')) a++;
  while (b > a && (out[b - 1] == ' ' || out[b - 1] == '\n')) b--;
  return out.substr(a, b - a);
}

std::string boom(const std::string& src, const PpOptions& opts = {}) {
  try { preprocess(src, "main.c", opts); } catch (const CcError& e) { return e.message(); }
  return "no error";
}

PpOptions headers(std::map<std::string, std::string> map) {
  PpOptions o;
  o.resolve = [map](const std::string& n, bool) -> std::optional<std::string> {
    auto it = map.find(n);
    if (it == map.end()) return std::nullopt;
    return it->second;
  };
  return o;
}

bool has(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

int countOf(const std::string& s, const std::string& needle) {
  int n = 0;
  size_t at = 0;
  while ((at = s.find(needle, at)) != std::string::npos) { n++; at += needle.size(); }
  return n;
}

}  // namespace

TEST_SUITE("comments come out first, and the line count survives") {
  TEST_CASE("replaces a block comment that spans lines with blank space") {
    CHECK(countOf(stripComments("a\n/* x\ny */\nb"), "\n") == 3);
  }

  TEST_CASE("leaves a comment inside a string alone") {
    CHECK(stripComments("char *s = \"/* not a comment */\";") == "char *s = \"/* not a comment */\";");
  }

  TEST_CASE("keeps the line a token is on after a comment above it") {
    const PpResult r = preprocess("/* one\ntwo */\nint x;", "main.c");
    for (const PpLine& l : r.lines) if (has(l.text, "int")) CHECK(l.line == 3);
  }
}

TEST_SUITE("object macros") {
  TEST_CASE("expands one") { CHECK(has(text("#define N 5\nint a = N;"), "int a = 5;")); }

  TEST_CASE("expands one inside another") {
    CHECK(has(text("#define A 1\n#define B (A + 1)\nint x = B;"), "int x = (1 + 1);"));
  }

  TEST_CASE("does not expand a name inside a string") {
    CHECK(has(text("#define N 5\nchar *s = \"N\";"), "\"N\""));
  }

  TEST_CASE("does not loop on a macro that names itself") {
    CHECK(has(text("#define A A + 1\nint x = A;"), "A + 1"));
  }

  TEST_CASE("expands only whole names") { CHECK(has(text("#define N 5\nint NN;"), "int NN;")); }

  TEST_CASE("forgets one after #undef") {
    CHECK(has(text("#define N 5\n#undef N\nint a = N;"), "int a = N;"));
  }
}

TEST_SUITE("function macros") {
  TEST_CASE("substitutes its arguments") {
    CHECK(has(text("#define DBL(x) ((x) + (x))\nint a = DBL(3);"), "int a = (((3)) + ((3)));"));
  }

  TEST_CASE("parenthesises an argument, so precedence survives") {
    CHECK(has(text("#define SQ(x) (x * x)\nint a = SQ(1 + 2);"), "(1 + 2) * (1 + 2)"));
  }

  TEST_CASE("takes an argument with a comma inside parentheses") {
    CHECK(has(text("#define F(a) [a]\nint x = F(g(1, 2));"), "[(g(1, 2))]"));
  }

  TEST_CASE("is only a name when its parentheses are missing") {
    CHECK(has(text("#define F(a) 1\nint *p = F;"), "int *p = F;"));
  }

  // Sequential substitution let a later parameter match text an earlier
  // argument had just brought in. poke(&v, 55) put the 55 inside the &v.
  TEST_CASE("substitutes every parameter at once, not one after another") {
    CHECK(has(text("#define P(a, v) (*(a) = (v))\nint x = P(&v, 55);"), "(*((&v)) = ((55)))"));
  }

  TEST_CASE("counts its arguments and says so") {
    CHECK(has(boom("#define F(a, b) 1\nint x = F(1);"), "2 arguments"));
  }
}

TEST_SUITE("conditionals") {
  TEST_CASE("keeps the live branch and drops the other") {
    const std::string t = text("#define A\n#ifdef A\nint yes;\n#else\nint no;\n#endif");
    CHECK(has(t, "yes"));
    CHECK(!has(t, "no"));
  }

  TEST_CASE("does the same the other way with ifndef") {
    const std::string t = text("#ifndef A\nint yes;\n#else\nint no;\n#endif");
    CHECK(has(t, "yes"));
    CHECK(!has(t, "no"));
  }

  TEST_CASE("works out #if with arithmetic and defined()") {
    CHECK(has(text("#define N 3\n#if N > 2\nint big;\n#endif"), "big"));
    CHECK(!has(text("#if defined(NOPE)\nint no;\n#endif"), "no"));
  }

  TEST_CASE("takes the first true arm of an elif chain, and only that one") {
    const std::string t = text(
        "#define N 2\n#if N == 1\nint a;\n#elif N == 2\nint b;\n#elif N == 2\nint c;\n#else\nint d;\n#endif");
    CHECK(!has(t, "int a"));
    CHECK(has(t, "int b"));
    CHECK(!has(t, "int c"));
    CHECK(!has(t, "int d"));
  }

  TEST_CASE("nests") {
    const std::string t = text("#define A\n#ifdef A\n#ifdef B\nint ab;\n#else\nint aonly;\n#endif\n#endif");
    CHECK(!has(t, "ab"));
    CHECK(has(t, "aonly"));
  }

  TEST_CASE("does not define inside a dead branch") {
    CHECK(has(text("#ifdef NOPE\n#define X 9\n#endif\nint a = X;"), "int a = X;"));
  }

  TEST_CASE("names an unbalanced #endif") {
    CHECK(has(boom("#endif"), "#endif with no #if"));
    CHECK(has(boom("#ifdef A\nint x;"), "no #endif"));
  }
}

TEST_SUITE("include") {
  TEST_CASE("pulls a header in") {
    CHECK(has(text("#include <gpu.h>\nint x;", headers({{"gpu.h", "int fromheader;"}})), "int fromheader;"));
  }

  TEST_CASE("keeps the header's own file and line on its lines") {
    const PpResult r = preprocess("#include <a.h>\nint x;", "main.c", headers({{"a.h", "\nint inside;"}}));
    bool found = false;
    for (const PpLine& l : r.lines) {
      if (!has(l.text, "inside")) continue;
      found = true;
      CHECK(l.file == "a.h");
      CHECK(l.line == 2);
    }
    CHECK(found);
  }

  TEST_CASE("reports what it pulled in, in order") {
    const PpResult r = preprocess("#include <a.h>\n#include <b.h>", "main.c", headers({{"a.h", ""}, {"b.h", ""}}));
    CHECK(r.included == std::vector<std::string>{"a.h", "b.h"});
  }

  TEST_CASE("lets a guard stop a second copy") {
    const std::string h = "#ifndef A_H\n#define A_H\nint once;\n#endif";
    const std::string t = text("#include <a.h>\n#include <a.h>", headers({{"a.h", h}}));
    CHECK(countOf(t, "int once") == 1);
  }

  TEST_CASE("names the libraries when the header is not there") {
    CHECK(has(boom("#include <nope.h>", headers({})), "gpu.h"));
  }

  TEST_CASE("refuses a file that includes itself") {
    PpOptions r;
    r.resolve = [](const std::string& n, bool) -> std::optional<std::string> {
      if (n == "a.h") return std::string("#include <a.h>");
      return std::nullopt;
    };
    CHECK(has(boom("#include <a.h>", r), "includes itself"));
  }
}

TEST_SUITE("defines handed in from the build line") {
  TEST_CASE("are set before the file is read") {
    PpOptions o;
    o.defines["SOFT_MUL"] = "1";
    CHECK(has(text("#ifdef SOFT_MUL\nint yes;\n#endif", o), "yes"));
  }
}

TEST_SUITE("a macro's arguments are split the way C splits them") {
  TEST_CASE("keeps a comma inside a string literal in one argument") {
    CHECK(has(text("#define print(s) puts(s)\nprint(\"A, B\");"), "puts((\"A, B\"));"));
  }

  TEST_CASE("keeps a comma inside a character literal") {
    CHECK(has(text("#define put(c) out(c)\nput(',');"), "out((','));"));
  }

  TEST_CASE("keeps a parenthesis inside a string from counting") {
    CHECK(has(text("#define print(s) puts(s)\nprint(\"a)\");"), "puts((\"a)\"));"));
    CHECK(has(text("#define two(a, b) f(a, b)\ntwo(\"(\", 1);"), "f((\"(\"), (1));"));
  }

  TEST_CASE("still splits a comma outside the string, and inside parentheses not at all") {
    CHECK(has(text("#define two(a, b) f(a, b)\ntwo(\"x\", g(1, 2));"), "f((\"x\"), (g(1, 2)));"));
  }

  TEST_CASE("honours an escaped quote inside the string") {
    CHECK(has(text("#define print(s) puts(s)\nprint(\"say \\\"hi, there\\\"\");"),
              "puts((\"say \\\"hi, there\\\"\"));"));
  }
}

TEST_SUITE("a function cannot be named like a function-like macro") {
  const auto g = headers({{"graphics.h", "#define show(n) gpu_sprite_show(n)\n#define BLACK 0"}});

  TEST_CASE("names the macro and its header at the definition") {
    CHECK(boom("#include <graphics.h>\nvoid show(int n) { }", g) ==
          "show is a macro from graphics.h; a function of that name needs the header left out or another name");
  }

  TEST_CASE("catches a prototype and a pointer return too") {
    CHECK(has(boom("#include <graphics.h>\nstatic unsigned char *show(int n);", g), "show is a macro"));
  }

  TEST_CASE("leaves a call alone, and an object-like macro alone") {
    CHECK(has(text("#include <graphics.h>\nint main(void) { show(1); return 0; }", g), "gpu_sprite_show((1))"));
    CHECK(has(text("#include <graphics.h>\nint BLACK(void);", g), "int 0(void);"));
  }

  TEST_CASE("says nothing without the header") {
    CHECK(has(text("void show(int n) { }"), "void show(int n) { }"));
  }
}

// A file saved on Windows ends its lines with CR LF. A git checkout there
// makes the project's own sources so, and BASIC's keywords.h continues
// one #define over many lines.
TEST_SUITE("CR LF line ends") {
  TEST_CASE("a backslash before CR LF continues a #define") {
    CHECK(text("#define WORDS \"A \" \\\r\n  \"B\"\r\nchar *w = WORDS;\r\n") == "char *w = \"A \" \"B\";");
  }

  TEST_CASE("a CR before the line end is no part of the line") {
    CHECK(text("#define N 4\r\nint a = N;\r\n") == "int a = 4;");
  }
}
