#include "basic/program.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <vector>

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

// The keywords in the order of keywords.h. A stored keyword is the byte
// 128 plus its place here.
const std::vector<std::string>& keywordOrder() {
  static const std::vector<std::string> words = [] {
    std::vector<std::string> w;
    const std::string_view all = BASIC_KEYWORDS;
    size_t at = 0;
    while ((at = all.find_first_not_of(' ', at)) != std::string_view::npos) {
      const size_t end = all.find(' ', at);
      w.emplace_back(all.substr(at, end - at));
      at = end;
    }
    return w;
  }();
  return words;
}

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

namespace {

// A keyword's byte, or 0 when the word in capitals is none.
uint8_t keywordByte(std::string_view word) {
  const auto& order = keywordOrder();
  for (size_t i = 0; i < order.size(); i++) {
    if (order[i] == word) return static_cast<uint8_t>(KW_FIRST + i);
  }
  return 0;
}

// data_line in edit.c: the first word, read the way the lexer reads a
// name, is DATA. A letter or a digit goes on, then one dollar.
bool isDataLine(std::string_view t) {
  size_t i = 0;
  while (i < t.size() && t[i] == ' ') i++;
  size_t end = i;
  while (end < t.size() && std::isalnum(static_cast<unsigned char>(t[end])) != 0) end++;
  if (end < t.size() && t[end] == '$') end++;
  std::string w(t.substr(i, end - i));
  for (char& ch : w) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return w == "DATA";
}

}  // namespace

namespace {

// lit_end in edit.c: the value of the number at the start of t, read the
// way the lexer reads one, wrapped at 16 bits.
uint16_t numberValue(std::string_view t) {
  uint16_t v = 0;
  size_t i = 0;
  auto at = [&](size_t k) { return k < t.size() ? t[k] : '\0'; };
  if (at(0) == '$') {
    for (i = 1; isHexDigit(at(i)); i++) {
      const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(at(i))));
      v = static_cast<uint16_t>((v << 4) | (c >= 'A' ? c - 'A' + 10 : c - '0'));
    }
  } else if (at(0) == '0' && (at(1) == 'b' || at(1) == 'B') && (at(2) == '0' || at(2) == '1')) {
    for (i = 2; at(i) == '0' || at(i) == '1'; i++) v = static_cast<uint16_t>((v << 1) | (at(i) - '0'));
  } else {
    for (i = 0; isDigit(at(i)); i++) v = static_cast<uint16_t>(v * 10 + (at(i) - '0'));
  }
  return v;
}

// One walk of crunch_pass in edit.c. The first `marks` numbers get their
// value behind KW_LITERAL. `numbers` counts the numbers met.
std::string crunchPass(std::string_view body, size_t marks, size_t& numbers) {
  numbers = 0;
  if (isDataLine(body)) return canonicalLine(body);
  std::string out;
  size_t i = 0;
  while (i < body.size()) {
    const char c = body[i];
    size_t end = i + 1;
    if (c == '"') {
      end = body.find('"', i + 1);
      end = end == std::string_view::npos ? body.size() : end + 1;
    } else if ((c == '$' && i + 1 < body.size() && isHexDigit(body[i + 1])) || isDigit(c)) {
      if (c == '$') {
        while (end < body.size() && isHexDigit(body[end])) end++;
      } else {
        while (end < body.size() && (isNameChar(body[end]) || body[end] == '.')) end++;
      }
      if (numbers < marks) {
        const uint16_t v = numberValue(body.substr(i));
        out += static_cast<char>(KW_LITERAL);
        out += static_cast<char>(v >> 8);
        out += static_cast<char>(v & 255);
      }
      numbers++;
    } else if (isNameStart(c)) {
      while (end < body.size() && isNameChar(body[end])) end++;
      if (end < body.size() && body[end] == '$') end++;
      std::string w(body.substr(i, end - i));
      for (char& ch : w) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      if (const uint8_t k = keywordByte(w)) {
        out += static_cast<char>(k);
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

}  // namespace

// crunch in edit.c is the machine's copy of this rule. A number's value
// costs 3 bytes, and a stored line holds 250. So the first numbers get
// their value while the line fits, and the rest stay digits.
std::string crunchLine(std::string_view body) {
  size_t numbers = 0;
  std::string out = crunchPass(body, 0, numbers);
  size_t marks = numbers;
  if (out.size() + 3 * marks > 250) marks = (250 - out.size()) / 3;
  if (marks == 0) return out;
  return crunchPass(body, marks, numbers);
}

// ed_expand in edit.c is the machine's copy of this walk.
std::string expandLine(std::string_view stored) {
  if (isDataLine(stored)) return std::string(stored);
  std::string out;
  size_t i = 0;
  while (i < stored.size()) {
    uint8_t c = static_cast<uint8_t>(stored[i++]);
    // A number's value, any two bytes. Its digits follow as text.
    if (c == KW_LITERAL) {
      i += 2;
      continue;
    }
    if (c >= KW_FIRST && c < KW_FIRST + keywordOrder().size()) {
      out += keywordOrder()[c - KW_FIRST];
      if (c == KW_REM) c = '!';
    } else {
      out += static_cast<char>(c);
    }
    if (c == '"') {
      while (i < stored.size() && stored[i] != '"') out += stored[i++];
      if (i < stored.size()) out += stored[i++];
    } else if (c == '!') {
      out += stored.substr(i);
      i = stored.size();
    }
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

void storeProgram(std::span<uint8_t> ram, std::span<const uint8_t> bytes) {
  const size_t prog = static_cast<size_t>((ram[SYS_PROG] << 8) | ram[SYS_PROG + 1]);
  if (prog + bytes.size() > ram.size()) return;
  std::copy(bytes.begin(), bytes.end(), ram.begin() + static_cast<std::ptrdiff_t>(prog));
  ram[SYS_PROG_LEN] = static_cast<uint8_t>(bytes.size() >> 8);
  ram[SYS_PROG_LEN + 1] = static_cast<uint8_t>(bytes.size() & 255);
  ram[SYS_READ] = 0;
  ram[SYS_READ + 1] = 0;
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
  for (const auto& [n, canonical] : lines) {
    const std::string body = crunchLine(canonical);
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

namespace {

// The stored text of the record at p, rec bytes long, without its zero.
// A number's two value bytes are taken with its marker, since either can
// be 0.
std::string storedText(std::span<const uint8_t> bytes, size_t p, size_t rec) {
  std::string t;
  for (size_t i = p + 3; i < p + rec - 1 && bytes[i]; i++) {
    t += static_cast<char>(bytes[i]);
    if (bytes[i] == KW_LITERAL) {
      for (size_t k = 1; k <= 2 && i + 1 < p + rec - 1; k++) t += static_cast<char>(bytes[++i]);
    }
  }
  return t;
}

}  // namespace

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
    out += expandLine(storedText(bytes, p, rec));
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
    lines[n] = expandLine(storedText(bytes, p, rec));
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
