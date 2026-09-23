#include <doctest.h>

#include <string>
#include <vector>

#include "cc/lex.h"

using namespace sc8::cc;

namespace {

std::vector<std::string> kinds(const std::string& s) {
  std::vector<std::string> out;
  const std::vector<Tok> toks = lex(s);
  for (size_t i = 0; i + 1 < toks.size(); i++) {
    out.push_back(std::string(tokKindName(toks[i].kind)) + ":" + toks[i].text);
  }
  return out;
}

std::vector<double> nums(const std::string& s) {
  std::vector<double> out;
  for (const Tok& t : lex(s)) if (t.kind == TokKind::Num) out.push_back(t.value);
  return out;
}

const Tok* findText(const std::vector<Tok>& toks, const std::string& text) {
  for (const Tok& t : toks) if (t.text == text) return &t;
  return nullptr;
}

std::string boom(const std::string& s) {
  try { lex(s); } catch (const CcError& e) { return e.message(); }
  return "no error";
}

}  // namespace

TEST_SUITE("the lexer") {
  TEST_CASE("splits an ordinary declaration") {
    CHECK(kinds("int x = 3;") == std::vector<std::string>{"kw:int", "id:x", "punct:=", "num:3", "punct:;"});
  }

  TEST_CASE("keeps the line number on every token, because breakpoints need it") {
    std::vector<int> lines;
    for (const Tok& t : lex("int a;\n\nint b;")) if (t.kind == TokKind::Id) lines.push_back(t.line);
    CHECK(lines == std::vector<int>{1, 3});
  }

  TEST_CASE("counts lines through a block comment") {
    const std::vector<Tok> t = lex("/* one\ntwo\n */ x");
    CHECK(findText(t, "x")->line == 3);
  }

  TEST_CASE("reads hex, binary and decimal") {
    CHECK(nums("0x1F 0b1010 42") == std::vector<double>{31, 10, 42});
  }

  TEST_CASE("reads a floating literal, because double is a real type here") {
    CHECK(nums("3.14 1e3 .5") == std::vector<double>{3.14, 1000, 0.5});
  }

  TEST_CASE("drops the integer suffixes rather than choking on them") {
    CHECK(nums("10u 20L 30UL 1.5f") == std::vector<double>{10, 20, 30, 1.5});
  }

  TEST_CASE("takes the longest punctuator, so >>= is one token") {
    CHECK(kinds("a >>= b >> c > d") == std::vector<std::string>{
        "id:a", "punct:>>=", "id:b", "punct:>>", "id:c", "punct:>", "id:d"});
  }

  TEST_CASE("decodes a string to bytes, escapes and all") {
    CHECK(lex("\"a\\n\\x41\\0\"")[0].bytes == std::vector<uint8_t>{97, 10, 65, 0});
  }

  TEST_CASE("decodes a character constant to its value") {
    CHECK(nums("'A' '\\n' '\\\\'") == std::vector<double>{65, 10, 92});
  }

  TEST_CASE("knows the storage classes this machine added") {
    CHECK(kinds("__ROM __zp") == std::vector<std::string>{"kw:__ROM", "kw:__zp"});
  }

  TEST_CASE("hands a preprocessor line over whole") {
    const std::vector<Tok> t = lex("#include <gpu.h>\nint x;");
    CHECK(t[0].text == "#include <gpu.h>");
    CHECK(t[1].text == "int");
  }

  TEST_CASE("joins a continued #define into one line") {
    const std::vector<Tok> t = lex("#define A 1 \\\n  + 2\nint x;");
    std::string squashed;
    bool space = false;
    for (char c : t[0].text) {
      if (c == ' ' || c == '\t' || c == '\n') { space = true; continue; }
      if (space && !squashed.empty()) squashed += ' ';
      space = false;
      squashed += c;
    }
    CHECK(squashed == "#define A 1 \\ + 2");
    CHECK(t[1].line == 3);
  }

  TEST_CASE("skips a line comment without eating the newline") {
    CHECK(findText(lex("// hi\nint x;"), "int")->line == 2);
  }
}

TEST_SUITE("what the lexer refuses") {
  TEST_CASE("names an unterminated string") {
    CHECK(boom("\"abc").find("unterminated string") != std::string::npos);
  }

  TEST_CASE("names an unterminated comment") {
    CHECK(boom("/* abc").find("unterminated") != std::string::npos);
  }

  TEST_CASE("refuses a multi character constant rather than guessing") {
    CHECK(boom("'ab'").find("one character") != std::string::npos);
  }

  TEST_CASE("names an unknown escape") {
    CHECK(boom("\"\\q\"").find("unknown escape") != std::string::npos);
  }

  TEST_CASE("reports the line the error is on") {
    try {
      lex("int a;\nint b;\n'ab'");
      FAIL("no error");
    } catch (const CcError& e) {
      CHECK(e.line == 3);
    }
  }
}
