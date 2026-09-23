// Constant expressions for the assembler. Assembly time only: every
// expression here folds to one number before a byte is emitted, so the CPU
// still has no multiply, no divide and no shift.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace sc8 {

using ExprLookup = std::function<std::optional<int64_t>(std::string_view name)>;

// A failure's kind decides who reports it. Name and Advice carry a message
// worth showing. Syntax means the text was never arithmetic, and the
// assembler keeps its own "undefined label" for that.
enum class ExprFailure { Name, Advice, Syntax };

struct ExprResult {
  bool ok;
  int64_t value;
  ExprFailure kind;
  std::string message;
};

// Whether the assembler should reach for the evaluator at all. A bare
// number or a bare label is left to the paths that already handled them.
// Anything carrying an operator or a round bracket is ours. A square
// bracket dereferences on this machine, so text carrying one is never
// arithmetic.
bool looksLikeExpression(std::string_view text);

// C's operators and C's precedence: ~ and unary -, then * / %, then + -,
// then << >>, then &, then ^, then |. Grouping is ( ). A name resolves
// through the lookup and contributes its ADDRESS, never its contents.
ExprResult evalExpr(std::string_view text, const ExprLookup& lookup);

// Parse one literal: decimal, $1C, 0x1C, 0b111. Empty when it is not one.
std::optional<int64_t> parseLiteral(std::string_view s);

}  // namespace sc8
