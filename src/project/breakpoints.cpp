#include "project/breakpoints.h"

#include <algorithm>
#include <climits>

namespace sc8::project {

namespace {

int newlines(std::string_view text) { return static_cast<int>(std::count(text.begin(), text.end(), '\n')); }

// The first slot an assembly line became. Assembled::lineToInstr keeps the
// last one, and a line can make more than one. A .org gap's slots hold
// line 0, which no source line is.
std::optional<int> firstSlot(const Assembled& a, int asmLine) {
  for (size_t i = 0; i < a.instrToLine.size(); i++) {
    if (a.instrToLine[i] == asmLine) return static_cast<int>(i);
  }
  return std::nullopt;
}

// The first assembly line in [from, end) that holds an instruction.
std::optional<int> firstCode(const Assembled& a, int from, int end) {
  auto it = a.lineToInstr.lower_bound(from);
  if (it == a.lineToInstr.end() || it->first >= end) return std::nullopt;
  return it->first;
}

}  // namespace

std::optional<Stop> stopAt(const LineMap& lines, const Assembled& assembled, const std::string& file, int line) {
  if (line < 1) return std::nullopt;
  // A file's lines run from its span's .code line to the next span's.
  auto spanOf = [&](const std::string& name) -> std::optional<std::pair<int, int>> {
    for (size_t i = 0; i < lines.spans.size(); i++) {
      if (lines.spans[i].file != name) continue;
      const int end = i + 1 < lines.spans.size() ? lines.spans[i + 1].firstLine : INT_MAX;
      return std::pair{lines.spans[i].firstLine, end};
    }
    return std::nullopt;
  };

  if (auto c = lines.cLines.find(file); c != lines.cLines.end()) {
    const auto span = spanOf("generated.asm");
    if (!span) return std::nullopt;
    // A line whose first instruction is also the next marked line's made
    // no code of its own, as a declaration does. The later line holds it.
    std::optional<Stop> found;
    for (auto it = c->second.lower_bound(line); it != c->second.end(); ++it) {
      const auto code = firstCode(assembled, span->first + it->second, span->second);
      if (!code) break;
      const auto pc = firstSlot(assembled, *code);
      if (!pc) break;
      if (found && found->pc != *pc) break;
      found = Stop{it->first, *pc};
    }
    return found;
  }

  const auto span = spanOf(file);
  if (!span) return std::nullopt;
  const auto code = firstCode(assembled, span->first + line, span->second);
  if (!code) return std::nullopt;
  const auto pc = firstSlot(assembled, *code);
  if (!pc) return std::nullopt;
  return Stop{*code - span->first, *pc};
}

Edit::Edit(std::string_view before, std::string_view after) {
  const size_t shorter = std::min(before.size(), after.size());
  size_t p = 0;
  while (p < shorter && before[p] == after[p]) p++;
  if (p == before.size() && p == after.size()) return;
  same_ = false;
  size_t s = 0;
  while (s < shorter - p && before[before.size() - 1 - s] == after[after.size() - 1 - s]) s++;
  const size_t oldEnd = before.size() - s;
  const size_t newEnd = after.size() - s;
  first_ = 1 + newlines(before.substr(0, p));
  firstAtStart_ = p == 0 || before[p - 1] == '\n';
  last_ = 1 + newlines(before.substr(0, oldEnd));
  lastAtStart_ = oldEnd == 0 || before[oldEnd - 1] == '\n';
  delta_ = newlines(after.substr(p, newEnd - p)) - newlines(before.substr(p, oldEnd - p));
}

std::optional<int> Edit::follow(int line) const {
  if (same_ || line < first_) return line;
  if (line > last_ || (line == last_ && lastAtStart_)) return line + delta_;
  if ((line > first_ || firstAtStart_) && line < last_) return std::nullopt;
  return first_;
}

}  // namespace sc8::project
