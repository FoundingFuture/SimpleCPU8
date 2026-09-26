#include "basic/lines.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace sc8::basic {

std::vector<std::string> splitLines(std::string_view text) {
  std::vector<std::string> out;
  size_t at = 0;
  for (;;) {
    const size_t nl = text.find('\n', at);
    if (nl == std::string_view::npos) {
      out.emplace_back(text.substr(at));
      return out;
    }
    out.emplace_back(text.substr(at, nl - at));
    at = nl + 1;
  }
}

std::string joinLines(const std::vector<std::string>& lines) {
  std::string out;
  for (size_t i = 0; i < lines.size(); i++) {
    if (i) out += '\n';
    out += lines[i];
  }
  return out;
}

std::optional<int> lineNumber(std::string_view line) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') i++;
  if (i >= line.size() || !std::isdigit(static_cast<unsigned char>(line[i]))) return std::nullopt;
  long n = 0;
  while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
    n = n * 10 + (line[i] - '0');
    if (n > 99999) n = 99999;  // far past 65535, and no overflow
    i++;
  }
  return static_cast<int>(n);
}

bool onlyNumber(std::string_view line) {
  if (!lineNumber(line)) return false;
  for (char c : line) {
    if (c != ' ' && !std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

bool linesInOrder(const std::vector<std::string>& lines) {
  int last = -1;
  for (const std::string& l : lines) {
    if (const auto n = lineNumber(l)) {
      if (*n < last) return false;
      last = *n;
    }
  }
  return true;
}

void sortLines(std::vector<std::string>& lines, int& caret, bool glue) {
  const int count = static_cast<int>(lines.size());
  int first = -1;
  int last = -1;
  for (int i = 0; i < count; i++) {
    if (lineNumber(lines[static_cast<size_t>(i)])) {
      if (first < 0) first = i;
      last = i;
    }
  }
  if (first < 0) return;
  // The tail: lines after the last numbered one, which stay at the end.
  // A glued caret there belongs to the block above it instead.
  int tail = last + 1;
  if (glue && caret >= tail && caret < count) tail = caret + 1;

  struct Block {
    int number;
    int begin;
    int end;
  };
  std::vector<Block> blocks;
  for (int i = first; i < tail; i++) {
    if (const auto n = lineNumber(lines[static_cast<size_t>(i)])) blocks.push_back({*n, i, i + 1});
    else blocks.back().end = i + 1;
  }
  std::stable_sort(blocks.begin(), blocks.end(), [](const Block& a, const Block& b) { return a.number < b.number; });

  std::vector<std::string> out;
  out.reserve(lines.size());
  int newCaret = caret;
  auto take = [&](int i) {
    if (i == caret) newCaret = static_cast<int>(out.size());
    out.push_back(std::move(lines[static_cast<size_t>(i)]));
  };
  for (int i = 0; i < first; i++) take(i);
  for (const Block& b : blocks) {
    for (int i = b.begin; i < b.end; i++) take(i);
  }
  for (int i = tail; i < count; i++) take(i);
  lines = std::move(out);
  caret = newCaret;
}

std::optional<int> numberAfter(const std::vector<std::string>& lines, int after) {
  std::optional<int> next;
  for (const std::string& l : lines) {
    if (const auto n = lineNumber(l); n && *n > after && (!next || *n < *next)) next = n;
  }
  int pick = after + 10;
  if (next && *next - after <= 10) {
    if (*next - after < 2) return std::nullopt;
    pick = after + (*next - after) / 2;
  }
  if (pick > 65535) return std::nullopt;
  return pick;
}

namespace {

// The body's references rewritten through the map: a number after GOTO,
// GOSUB or THEN. Strings are skipped, and REM and ! end the scan, since
// the rest of their line is not BASIC.
std::string rewriteReferences(const std::string& body, const std::map<int, int>& renamed) {
  std::string out;
  size_t i = 0;
  while (i < body.size()) {
    const char c = body[i];
    if (c == '"') {
      const size_t close = body.find('"', i + 1);
      const size_t end = close == std::string::npos ? body.size() : close + 1;
      out.append(body, i, end - i);
      i = end;
      continue;
    }
    if (c == '!') {
      out.append(body, i, std::string::npos);
      break;
    }
    if (!std::isalpha(static_cast<unsigned char>(c))) {
      out += c;
      i++;
      continue;
    }
    size_t j = i;
    while (j < body.size() && (std::isalnum(static_cast<unsigned char>(body[j])) || body[j] == '$')) j++;
    std::string word = body.substr(i, j - i);
    out += word;
    for (char& w : word) w = static_cast<char>(std::toupper(static_cast<unsigned char>(w)));
    i = j;
    if (word == "REM") {
      out.append(body, i, std::string::npos);
      break;
    }
    if (word != "GOTO" && word != "GOSUB" && word != "THEN") continue;
    while (i < body.size() && body[i] == ' ') out += body[i++];
    size_t k = i;
    while (k < body.size() && std::isdigit(static_cast<unsigned char>(body[k]))) k++;
    if (k == i) continue;
    const auto n = lineNumber(std::string_view(body).substr(i, k - i));
    const auto it = n ? renamed.find(*n) : renamed.end();
    if (it != renamed.end()) out += std::to_string(it->second);
    else out.append(body, i, k - i);
    i = k;
  }
  return out;
}

}  // namespace

void renumberLines(std::vector<std::string>& lines, int start, int step) {
  std::map<int, int> renamed;
  int next = start;
  for (const std::string& l : lines) {
    if (const auto n = lineNumber(l)) {
      renamed.emplace(*n, next);  // a repeated number keeps its first place
      next += step;
    }
  }
  next = start;
  for (std::string& l : lines) {
    if (!lineNumber(l)) continue;
    size_t i = 0;
    while (i < l.size() && l[i] == ' ') i++;
    while (i < l.size() && std::isdigit(static_cast<unsigned char>(l[i]))) i++;
    l = std::to_string(next) + rewriteReferences(l.substr(i), renamed);
    next += step;
  }
}

}  // namespace sc8::basic
