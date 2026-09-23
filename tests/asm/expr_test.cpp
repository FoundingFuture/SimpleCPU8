#include <doctest.h>

#include "asm/expr.h"

using namespace sc8;

namespace {

const ExprLookup noLabels = [](std::string_view) { return std::nullopt; };

int64_t value(std::string_view text, const ExprLookup& lookup = noLabels) {
  ExprResult r = evalExpr(text, lookup);
  REQUIRE_MESSAGE(r.ok, text, " did not evaluate: ", r.message);
  return r.value;
}

std::string failure(std::string_view text, const ExprLookup& lookup = noLabels) {
  ExprResult r = evalExpr(text, lookup);
  REQUIRE_MESSAGE(!r.ok, text, " evaluated to ", r.value, " rather than failing");
  return r.message;
}

bool has(const std::string& s, std::string_view needle) { return s.find(needle) != std::string::npos; }

}  // namespace

TEST_SUITE("constant expressions") {
  TEST_CASE("evaluates a bare number in every base the assembler already took") {
    CHECK_EQ(value("42"), 42);
    CHECK_EQ(value("$1C"), 0x1c);
    CHECK_EQ(value("0x1C"), 0x1c);
    CHECK_EQ(value("0b111"), 7);
    CHECK_EQ(value("0"), 0);
  }

  TEST_CASE("combines flag masks with or, which is what a mask wants") {
    CHECK_EQ(value("2 | 8"), 10);
    CHECK_EQ(value("2 & 8"), 0);
  }

  TEST_CASE("applies C precedence rather than folding left to right") {
    CHECK_EQ(value("1 | 2 & 3"), 3);
    CHECK_EQ(value("1 ^ 2 & 2"), 3);
    CHECK_EQ(value("2 + 3 * 4"), 14);
    CHECK_EQ(value("1 << 2 + 1"), 8);
    CHECK_EQ(value("6 & 3 | 8"), 10);
  }

  TEST_CASE("groups with round brackets") {
    CHECK_EQ(value("(1 | 2) & 3"), 3);
    CHECK_EQ(value("(2 + 3) * 4"), 20);
    CHECK_EQ(value("((4))"), 4);
    CHECK_EQ(value("2 * (3 + (4 - 1))"), 12);
  }

  TEST_CASE("refuses square brackets, because they dereference on this machine") {
    std::string msg = failure("[1 + 2]");
    CHECK(has(msg, "square bracket"));
    CHECK(has(msg, "dereference"));
  }

  TEST_CASE("refuses && and || by name, and points at the single bar") {
    for (const char* text : {"2 && 8", "2 || 8"}) {
      std::string msg = failure(text);
      CHECK(has(msg, "|"));
      CHECK(has(msg, "mask"));
    }
  }

  TEST_CASE("shifts") {
    CHECK_EQ(value("1 << 3"), 8);
    CHECK_EQ(value("16 >> 2"), 4);
    CHECK_EQ(value("1 << 15"), 32768);
  }

  TEST_CASE("negates and complements") {
    CHECK_EQ(value("-1"), -1);
    CHECK_EQ(value("~0"), -1);
    CHECK_EQ(value("-(2 + 3)"), -5);
    CHECK_EQ(value("~0 & 255"), 255);
    CHECK_EQ(value("- -4"), 4);
  }

  TEST_CASE("takes a label's address through the lookup, never its contents") {
    ExprLookup labels = [](std::string_view n) -> std::optional<int64_t> {
      if (n == "BLOCK") return 0x0200;
      if (n == "SIZE") return 8;
      return std::nullopt;
    };
    CHECK_EQ(value("BLOCK", labels), 0x0200);
    CHECK_EQ(value("BLOCK + 8", labels), 0x0208);
    CHECK_EQ(value("BLOCK + SIZE * 2", labels), 0x0210);
    CHECK_EQ(value("BLOCK - 1", labels), 0x01ff);
  }

  TEST_CASE("lets an intermediate go negative or past a byte") {
    CHECK_EQ(value("10 - 20"), -10);
    CHECK_EQ(value("200 + 100"), 300);
    CHECK_EQ(value("(10 - 20) + 30"), 20);
  }

  TEST_CASE("divides toward zero, and refuses a divide by zero") {
    CHECK_EQ(value("7 / 2"), 3);
    CHECK_EQ(value("-7 / 2"), -3);
    CHECK_EQ(value("7 % 2"), 1);
    CHECK_EQ(value("-7 % 2"), -1);
    CHECK(has(failure("1 / 0"), "divide by zero"));
    CHECK(has(failure("1 % 0"), "divide by zero"));
  }

  TEST_CASE("refuses an unknown name, and says which one") {
    CHECK(has(failure("BLOCK + 1"), "BLOCK"));
    CHECK(has(failure("2 * nope"), "nope"));
  }

  TEST_CASE("refuses a malformed expression") {
    for (const char* text : {"1 +", "(1", "1)", "*2", "1 2", "", "()", "1 ** 2"}) {
      CAPTURE(text);
      CHECK(!evalExpr(text, noLabels).ok);
    }
  }

  TEST_CASE("takes whitespace or none, because the operand parser strips it") {
    CHECK_EQ(value("2|8"), 10);
    CHECK_EQ(value("  2  |  8  "), 10);
    ExprLookup four = [](std::string_view n) -> std::optional<int64_t> {
      if (n == "BLOCK") return 4;
      return std::nullopt;
    };
    CHECK_EQ(value("BLOCK+8", four), 12);
  }
}

TEST_SUITE("looksLikeExpression") {
  TEST_CASE("is false for anything the assembler already understood") {
    for (const char* text : {"42", "$1C", "0x1C", "0b111", "label", "some_label", "BTN_FIRE"}) {
      CAPTURE(text);
      CHECK(!looksLikeExpression(text));
    }
  }

  TEST_CASE("is true once an operator or a bracket appears") {
    for (const char* text : {"2 | 8", "BLOCK+8", "1<<3", "~0", "(1)", "2*3", "10-1", "-1"}) {
      CAPTURE(text);
      CHECK(looksLikeExpression(text));
    }
  }

  TEST_CASE("is false for anything carrying a square bracket, which never groups") {
    for (const char* text : {"[$1C]+", "[zp]x", "pre[zp]", "[1 + 2]"}) {
      CAPTURE(text);
      CHECK(!looksLikeExpression(text));
    }
  }
}
