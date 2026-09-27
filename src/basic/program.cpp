#include "basic/program.h"

#include <algorithm>
#include <cctype>
#include <map>

#include "basic/keywords.h"

namespace sc8::basic {

const std::set<std::string, std::less<>>& basicKeywords() {
  static const std::set<std::string, std::less<>> words = [] {
    std::set<std::string, std::less<>> w;
    const std::string_view all = BASIC_KEYWORDS;
    size_t at = 0;
    while ((at = all.find_first_not_of(' ', at)) != std::string_view::npos) {
      const size_t end = all.find(' ', at);
      w.emplace(all.substr(at, end - at));
      at = end;
    }
    return w;
  }();
  return words;
}

namespace {

bool isNameStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool isNameChar(char c) { return isNameStart(c) || isDigit(c); }
bool isHexDigit(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

}  // namespace

// The walk is lexBasic's in src/ide/highlight.cpp. A number runs over
// name characters the way the highlighter's does, so `1to` stays as typed.
// keywords_up in edit.c is the machine's copy of this rule. The "small
// letters" tests type lines on the machine and hold the two to one result.
std::string canonicalLine(std::string_view body) {
  std::string out;
  out.reserve(body.size());
  size_t i = 0;
  while (i < body.size()) {
    const char c = body[i];
    size_t end = i + 1;
    if (c == '"') {
      end = body.find('"', i + 1);
      end = end == std::string_view::npos ? body.size() : end + 1;
    } else if (c == '$' && i + 1 < body.size() && isHexDigit(body[i + 1])) {
      while (end < body.size() && isHexDigit(body[end])) end++;
    } else if (isDigit(c)) {
      while (end < body.size() && (isNameChar(body[end]) || body[end] == '.')) end++;
    } else if (isNameStart(c)) {
      while (end < body.size() && isNameChar(body[end])) end++;
      if (end < body.size() && body[end] == '$') end++;
      std::string w(body.substr(i, end - i));
      for (char& ch : w) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      if (basicKeywords().count(w)) {
        out += w;
        i = end;
        if (w == "REM") {
          out += body.substr(i);
          i = body.size();
        }
        continue;
      }
    } else if (c == '!') {
      end = body.size();
    }
    out += body.substr(i, end - i);
    i = end;
  }
  return out;
}

std::string canonicalText(std::string_view text, int skip) {
  std::string out;
  out.reserve(text.size());
  int row = 0;
  size_t at = 0;
  while (at <= text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string_view::npos) end = text.size();
    const std::string_view line = text.substr(at, end - at);
    size_t i = 0;
    while (i < line.size() && line[i] == ' ') i++;
    const size_t digits = i;
    while (i < line.size() && isDigit(line[i])) i++;
    if (row == skip || i == digits) {
      out += line;
    } else {
      while (i < line.size() && line[i] == ' ') i++;
      out += line.substr(0, i);
      out += canonicalLine(line.substr(i));
    }
    if (end < text.size()) out += '\n';
    at = end + 1;
    row++;
  }
  return out;
}

std::vector<uint8_t> encodeProgram(const std::string& text) {
  // The map keeps the lines sorted and makes a repeated number a
  // replacement, which is what the interpreter does on entry.
  std::map<int, std::string> lines;
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(at, end - at);
    at = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t i = 0;
    while (i < line.size() && line[i] == ' ') i++;
    if (i >= line.size() || line[i] < '0' || line[i] > '9') continue;
    int n = 0;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
      n = n * 10 + (line[i] - '0');
      i++;
    }
    while (i < line.size() && line[i] == ' ') i++;
    std::string body = line.substr(i);
    if (body.size() > 250) body.resize(250);
    if (n <= 0 || n > 65535) continue;
    if (body.empty()) lines.erase(n);
    else lines[n] = canonicalLine(body);
  }
  std::vector<uint8_t> out;
  for (const auto& [n, body] : lines) {
    const size_t rec = body.size() + 4;
    if (out.size() + rec + 3 >= PROGRAM_MAX) break;
    out.push_back(static_cast<uint8_t>(n >> 8));
    out.push_back(static_cast<uint8_t>(n & 255));
    out.push_back(static_cast<uint8_t>(rec));
    out.insert(out.end(), body.begin(), body.end());
    out.push_back(0);
  }
  out.push_back(0);
  out.push_back(0);
  out.push_back(3);
  return out;
}

std::string decodeProgram(std::span<const uint8_t> bytes) {
  std::string out;
  size_t p = 0;
  while (p + 3 <= bytes.size()) {
    const int n = (bytes[p] << 8) | bytes[p + 1];
    if (n == 0) break;
    const size_t rec = bytes[p + 2];
    if (rec < 4 || p + rec > bytes.size()) break;
    out += std::to_string(n);
    out += ' ';
    for (size_t i = p + 3; i < p + rec - 1 && bytes[i]; i++) out += static_cast<char>(bytes[i]);
    out += '\n';
    p += rec;
  }
  return out;
}

std::map<int, std::string> programLines(std::span<const uint8_t> bytes) {
  std::map<int, std::string> lines;
  size_t p = 0;
  while (p + 3 <= bytes.size()) {
    const int n = (bytes[p] << 8) | bytes[p + 1];
    if (n == 0) break;
    const size_t rec = bytes[p + 2];
    if (rec < 4 || p + rec > bytes.size()) break;
    std::string body;
    for (size_t i = p + 3; i < p + rec - 1 && bytes[i]; i++) body += static_cast<char>(bytes[i]);
    lines[n] = std::move(body);
    p += rec;
  }
  return lines;
}

namespace {

std::string textOf(const std::map<int, std::string>& lines) {
  std::string text;
  for (const auto& [n, body] : lines) text += std::to_string(n) + " " + body + "\n";
  return text;
}

// A line's number and body the way encodeProgram reads them. The number
// is -1 for a line without one.
std::pair<int, std::string> parseLine(const std::string& line) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') i++;
  if (i >= line.size() || line[i] < '0' || line[i] > '9') return {-1, ""};
  int n = 0;
  while (i < line.size() && line[i] >= '0' && line[i] <= '9' && n <= 65535) n = n * 10 + (line[i++] - '0');
  while (i < line.size() && line[i] == ' ') i++;
  std::string body = line.substr(i);
  if (!body.empty() && body.back() == '\r') body.pop_back();
  if (body.size() > 250) body.resize(250);
  return {n, body};
}

}  // namespace

std::vector<uint8_t> mergePrograms(std::span<const uint8_t> base, std::span<const uint8_t> mine,
                                   std::span<const uint8_t> theirs) {
  const auto b = programLines(base);
  const auto m = programLines(mine);
  const auto t = programLines(theirs);
  std::map<int, std::string> out;
  auto numbers = [&] {
    std::map<int, bool> all;
    for (const auto* side : {&b, &m, &t}) {
      for (const auto& kv : *side) all[kv.first] = true;
    }
    return all;
  }();
  auto at = [](const std::map<int, std::string>& side, int n) -> const std::string* {
    const auto it = side.find(n);
    return it == side.end() ? nullptr : &it->second;
  };
  auto same = [](const std::string* x, const std::string* y) { return x == y || (x && y && *x == *y); };
  for (const auto& kv : numbers) {
    const int n = kv.first;
    const std::string* bl = at(b, n);
    const std::string* ml = at(m, n);
    const std::string* tl = at(t, n);
    const std::string* pick = same(ml, bl) ? tl : ml;
    if (pick) out[n] = *pick;
  }
  return encodeProgram(textOf(out));
}

std::string patchText(const std::string& text, std::span<const uint8_t> program) {
  const auto want = programLines(program);
  std::vector<std::string> rows;
  for (size_t at = 0; at <= text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    rows.push_back(text.substr(at, end - at));
    at = end + 1;
  }
  if (!rows.empty() && rows.back().empty()) rows.pop_back();

  // The last row with a number is the one encodeProgram keeps.
  std::map<int, size_t> lastRow;
  for (size_t r = 0; r < rows.size(); r++) {
    const int n = parseLine(rows[r]).first;
    if (n >= 0) lastRow[n] = r;
  }
  std::vector<std::pair<int, std::string>> kept;  // number, or -1, and the row
  for (size_t r = 0; r < rows.size(); r++) {
    const auto [n, body] = parseLine(rows[r]);
    if (n < 0) {
      kept.push_back({-1, rows[r]});
      continue;
    }
    if (lastRow[n] != r) continue;
    const auto it = want.find(n);
    if (it == want.end()) continue;
    kept.push_back({n, canonicalLine(body) == it->second ? rows[r] : std::to_string(n) + " " + it->second});
  }
  // The program's lines the document lacks, each before the first
  // numbered row above it.
  for (const auto& [n, body] : want) {
    bool present = false;
    for (const auto& k : kept) present = present || k.first == n;
    if (present) continue;
    size_t pos = kept.size();
    for (size_t k = 0; k < kept.size(); k++) {
      if (kept[k].first > n) {
        pos = k;
        break;
      }
    }
    kept.insert(kept.begin() + static_cast<std::ptrdiff_t>(pos), {n, std::to_string(n) + " " + body});
  }
  std::string out;
  for (const auto& k : kept) out += k.second + "\n";
  return out;
}

}  // namespace sc8::basic
