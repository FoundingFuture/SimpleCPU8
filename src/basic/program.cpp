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

bool isLetter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool isNameStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool isNameChar(char c) { return isNameStart(c) || isDigit(c); }
bool isHexDigit(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

std::string upperOf(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

// The words whose line number is a jump target, and the words after
// which the rest of the line is not BASIC.
bool takesTarget(std::string_view word) {
  return word == "GOTO" || word == "GOSUB" || word == "THEN" || word == "RESTORE";
}
bool endsBasic(std::string_view word) { return word == "REM" || word == "INCLUDE" || word == "FILE"; }

// crunch_target in edit.c reads a target this way. `at` stands after
// GOTO, GOSUB, THEN or RESTORE.
struct Target {
  bool found = false;
  bool named = false;
  size_t name = 0;    // where the file's name starts, or the number
  size_t colon = 0;   // the colon after the name
  size_t end = 0;     // past the digits
  uint16_t line = 0;
};

Target targetAt(std::string_view t, size_t at, bool then) {
  auto ch = [&](size_t k) { return k < t.size() ? t[k] : '\0'; };
  Target g;
  size_t q = at;
  while (ch(q) == ' ') q++;
  g.name = q;
  if (isLetter(ch(q))) {
    while (isLetter(ch(q)) || isDigit(ch(q))) q++;
    if (ch(q) != ':' || !isDigit(ch(q + 1)) || q - g.name > FILE_NAME - 1) return g;
    g.named = true;
    g.colon = q;
    q++;
  }
  if (!isDigit(ch(q))) return g;
  size_t e = q;
  while (isDigit(ch(e))) {
    g.line = static_cast<uint16_t>(g.line * 10 + (ch(e) - '0'));
    e++;
  }
  if (!g.named && !then) {
    size_t r = e;
    while (ch(r) == ' ') r++;
    if (r < t.size() && ch(r) != ':') return g;
  }
  g.end = e;
  g.found = true;
  return g;
}

}  // namespace

int FileTable::find(std::string_view name) const {
  const std::string want = upperOf(name);
  for (size_t i = 0; i < names.size(); i++) {
    if (names[i] == want) return static_cast<int>(i);
  }
  return -1;
}

int FileTable::add(std::string_view name) {
  const int have = find(name);
  if (have >= 0) return have;
  if (names.size() >= FILE_MAX || name.empty() || name.size() > FILE_NAME - 1) return -1;
  names.push_back(upperOf(name));
  return static_cast<int>(names.size()) - 1;
}

bool isFileName(std::string_view name) {
  if (name.empty() || name.size() > FILE_NAME - 1) return false;
  return std::all_of(name.begin(), name.end(), [](char c) { return isLetter(c) || (c >= '0' && c <= '9'); });
}

// file_of in store.c is the machine's copy of this rule.
std::string fileNameOf(std::string_view path) {
  const size_t slash = path.find_last_of("/\\");
  std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
  name = name.substr(0, name.find('.'));
  if (name.size() > FILE_NAME - 1) name = name.substr(0, FILE_NAME - 1);
  return upperOf(name);
}

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
        if (endsBasic(w)) {
          out += body.substr(i);
          i = body.size();
        } else if (takesTarget(w)) {
          // A file's name before a target's colon, as LIST prints it.
          const Target g = targetAt(body, i, w == "THEN");
          if (g.found && g.named) {
            out += body.substr(i, g.name - i);
            out += upperOf(body.substr(g.name, g.colon - g.name));
            i = g.colon;
          }
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

// One walk of crunch_pass in edit.c. The first `marks` numbers and jump
// targets get their value behind KW_LITERAL or KW_TARGET. `numbers`
// counts the two together.
std::string crunchPass(std::string_view body, size_t marks, size_t& numbers, FileTable& files, int own) {
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
        if (endsBasic(w)) {
          out += body.substr(i);
          i = body.size();
        } else if (takesTarget(w)) {
          const Target g = targetAt(body, i, w == "THEN");
          if (g.found) {
            numbers++;
            // A full table leaves the target as text, as edit.c does.
            int file = own;
            if (numbers <= marks && g.named) {
              const std::string_view name = body.substr(g.name, g.colon - g.name);
              file = files.find(name) >= 0 || files.names.size() < FILE_MAX ? files.add(name) : -1;
            }
            if (numbers > marks || file < 0) {
              out += body.substr(i, g.end - i);
            } else {
              out.append(g.name - i, ' ');
              out += static_cast<char>(KW_TARGET);
              out += static_cast<char>(file);
              out += static_cast<char>(g.line >> 8);
              out += static_cast<char>(g.line & 255);
            }
            i = g.end;
          }
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

namespace {

// The walk of crunchPass, looking for a byte of 128 or more where the
// lexer reads the line. A DATA line is read whole, outside its quotes.
bool readsHighByte(std::string_view body) {
  const bool data = isDataLine(body);
  size_t i = 0;
  while (i < body.size()) {
    const char c = body[i];
    size_t end = i + 1;
    if (c == '"') {
      end = body.find('"', i + 1);
      end = end == std::string_view::npos ? body.size() : end + 1;
    } else if (!data && isNameStart(c)) {
      while (end < body.size() && isNameChar(body[end])) end++;
      if (end < body.size() && body[end] == '$') end++;
      std::string w(body.substr(i, end - i));
      for (char& ch : w) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      if (endsBasic(w)) return false;
    } else if (!data && c == '!') {
      return false;
    } else if (static_cast<uint8_t>(c) >= KW_FIRST) {
      return true;
    }
    i = end;
  }
  return false;
}

}  // namespace

// crunch in edit.c is the machine's copy of this rule. A number's value
// costs 3 bytes, a target at most 3 more than its digits, and a stored
// line holds 250. So the first numbers and targets get their marker
// while the line fits, and the rest stay text.
std::string crunchLine(std::string_view body, FileTable& files, int own) {
  size_t numbers = 0;
  std::string out = crunchPass(body, 0, numbers, files, own);
  size_t marks = numbers;
  if (out.size() + 3 * marks > 250) marks = (250 - out.size()) / 3;
  if (marks == 0) return out;
  return crunchPass(body, marks, numbers, files, own);
}

std::string crunchLine(std::string_view body) {
  FileTable files{{"MAIN"}};
  return crunchLine(body, files, 0);
}

// ed_expand in edit.c is the machine's copy of this walk.
std::string expandLine(std::string_view stored, const FileTable& files, int own) {
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
    // A jump target: the file's name when it is another file, the line.
    if (c == KW_TARGET) {
      if (i + 3 > stored.size()) break;
      const auto file = static_cast<size_t>(static_cast<uint8_t>(stored[i]));
      const int line = (static_cast<uint8_t>(stored[i + 1]) << 8) | static_cast<uint8_t>(stored[i + 2]);
      if (static_cast<int>(file) != own) out += (file < files.names.size() ? files.names[file] : "?") + ":";
      out += std::to_string(line);
      i += 3;
      continue;
    }
    if (c >= KW_FIRST && c < KW_FIRST + keywordOrder().size()) {
      out += keywordOrder()[c - KW_FIRST];
      if (c == KW_REM || c == KW_INCLUDE || c == KW_FILE) c = '!';
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

std::string expandLine(std::string_view stored) {
  const FileTable files{{"MAIN"}};
  return expandLine(stored, files, 0);
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

bool atPrompt(std::span<const uint8_t> ram) { return ram.size() > SYS_PROMPT && ram[SYS_PROMPT] == 1; }

void storeProgram(std::span<uint8_t> ram, std::span<const uint8_t> bytes) {
  const size_t prog = static_cast<size_t>((ram[SYS_PROG] << 8) | ram[SYS_PROG + 1]);
  if (prog + bytes.size() > ram.size()) return;
  std::copy(bytes.begin(), bytes.end(), ram.begin() + static_cast<std::ptrdiff_t>(prog));
  ram[SYS_PROG_LEN] = static_cast<uint8_t>(bytes.size() >> 8);
  ram[SYS_PROG_LEN + 1] = static_cast<uint8_t>(bytes.size() & 255);
  ram[SYS_READ] = 0;
  ram[SYS_READ + 1] = 0;
}

namespace {

// A line's number and body the way encodeProgram reads them, or -1 for a
// line without a number. The body keeps its spaces after the number.
std::pair<int, std::string_view> numberedBody(std::string_view line) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') i++;
  if (i >= line.size() || line[i] < '0' || line[i] > '9') return {-1, {}};
  int n = 0;
  while (i < line.size() && line[i] >= '0' && line[i] <= '9' && n <= 65535) n = n * 10 + (line[i++] - '0');
  return {n, line.substr(i)};
}

}  // namespace

int refusedLine(std::string_view text) {
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string_view::npos) end = text.size();
    const auto [n, body] = numberedBody(text.substr(at, end - at));
    if (n > 0 && readsHighByte(body)) return n;
    at = end + 1;
  }
  return -1;
}

std::string refusal(std::string_view text) {
  const int n = refusedLine(text);
  if (n < 0) return "";
  return "Line " + std::to_string(n) +
         " holds a byte of 128 or more outside a string. BASIC would read it as a keyword, so the program "
         "goes to the computer once the line is changed.";
}

std::vector<uint8_t> encodeLines(const std::string& text, FileTable& files, int own, size_t room) {
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
    if (readsHighByte(body)) continue;
    if (body.empty()) lines.erase(n);
    else lines[n] = canonicalLine(body);
  }
  std::vector<uint8_t> out;
  for (const auto& [n, canonical] : lines) {
    const std::string body = crunchLine(canonical, files, own);
    const size_t rec = body.size() + RECORD_TEXT + 1;
    if (out.size() + rec >= room) break;
    out.push_back(static_cast<uint8_t>(own));
    out.push_back(static_cast<uint8_t>(n >> 8));
    out.push_back(static_cast<uint8_t>(n & 255));
    out.push_back(static_cast<uint8_t>(rec));
    out.insert(out.end(), body.begin(), body.end());
    out.push_back(0);
  }
  return out;
}

FileTable Store::table() const {
  FileTable t;
  for (const StoredFile& f : files) t.names.push_back(f.name);
  return t;
}

void Store::adopt(const FileTable& names) {
  for (size_t i = files.size(); i < names.names.size() && i < FILE_MAX; i++) files.push_back({names.names[i], false, {}});
}

Store readStore(std::span<const uint8_t> bytes) {
  Store store;
  if (bytes.size() < EMPTY_PROGRAM || bytes[0] == 0 || bytes[0] > FILE_MAX) return store;
  for (size_t f = 0; f < bytes[0]; f++) {
    const size_t entry = 2 + f * FILE_ENTRY;
    StoredFile file;
    for (size_t i = 0; i < FILE_NAME - 1 && bytes[entry + i]; i++) file.name += static_cast<char>(bytes[entry + i]);
    size_t p = static_cast<size_t>((bytes[entry + FILE_NAME] << 8) | bytes[entry + FILE_NAME + 1]);
    file.loaded = p != 0;
    // The block's records up to the one numbered zero.
    while (file.loaded && p + END_RECORD <= bytes.size()) {
      const size_t rec = bytes[p + 3];
      if ((bytes[p + 1] == 0 && bytes[p + 2] == 0) || rec < RECORD_TEXT + 1 || p + rec > bytes.size()) break;
      file.lines.insert(file.lines.end(), bytes.begin() + static_cast<std::ptrdiff_t>(p),
                        bytes.begin() + static_cast<std::ptrdiff_t>(p + rec));
      p += rec;
    }
    store.files.push_back(std::move(file));
  }
  return store;
}

std::vector<uint8_t> writeStore(const Store& store) {
  std::vector<uint8_t> out(TABLE_SIZE, 0);
  const size_t count = std::min(store.files.size(), FILE_MAX);
  out[0] = static_cast<uint8_t>(count);
  for (size_t f = 0; f < count; f++) {
    const StoredFile& file = store.files[f];
    const size_t entry = 2 + f * FILE_ENTRY;
    for (size_t i = 0; i < file.name.size() && i < FILE_NAME - 1; i++) out[entry + i] = static_cast<uint8_t>(file.name[i]);
    if (!file.loaded) continue;
    const size_t start = out.size();
    out[entry + FILE_NAME] = static_cast<uint8_t>(start >> 8);
    out[entry + FILE_NAME + 1] = static_cast<uint8_t>(start & 255);
    out.insert(out.end(), file.lines.begin(), file.lines.end());
    out.insert(out.end(), {static_cast<uint8_t>(f), 0, 0, static_cast<uint8_t>(END_RECORD)});
  }
  return out;
}

std::vector<uint8_t> encodeProgram(const std::string& text) {
  FileTable files{{"MAIN"}};
  Store store;
  store.files.push_back({"MAIN", true, encodeLines(text, files, 0)});
  store.adopt(files);
  return writeStore(store);
}

namespace {

// The stored text of a record: what lies between its four byte head and
// its closing zero. A value byte inside it may be zero too, so the
// record's length says where it ends.
std::string storedText(std::span<const uint8_t> records, size_t p, size_t rec) {
  return std::string(records.begin() + static_cast<std::ptrdiff_t>(p + RECORD_TEXT),
                     records.begin() + static_cast<std::ptrdiff_t>(p + rec - 1));
}

// The main file of a store, or nothing when the store does not read.
StoredFile mainOf(std::span<const uint8_t> bytes, FileTable& files) {
  Store store = readStore(bytes);
  files = store.table();
  if (files.names.empty()) files.names.push_back("MAIN");
  return store.files.empty() ? StoredFile{} : std::move(store.files.front());
}

}  // namespace

std::map<int, std::string> linesOf(std::span<const uint8_t> records, const FileTable& files, int own) {
  std::map<int, std::string> lines;
  size_t p = 0;
  while (p + RECORD_TEXT < records.size()) {
    const int n = (records[p + 1] << 8) | records[p + 2];
    const size_t rec = records[p + 3];
    if (n == 0 || rec < RECORD_TEXT + 1 || p + rec > records.size()) break;
    lines[n] = expandLine(storedText(records, p, rec), files, own);
    p += rec;
  }
  return lines;
}

std::string decodeLines(std::span<const uint8_t> records, const FileTable& files, int own) {
  std::string out;
  for (const auto& [n, body] : linesOf(records, files, own)) out += std::to_string(n) + " " + body + "\n";
  return out;
}

std::string decodeProgram(std::span<const uint8_t> bytes) {
  FileTable files;
  const StoredFile file = mainOf(bytes, files);
  return decodeLines(file.lines, files, 0);
}

std::map<int, std::string> programLines(std::span<const uint8_t> bytes) {
  FileTable files;
  const StoredFile file = mainOf(bytes, files);
  return linesOf(file.lines, files, 0);
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

std::vector<uint8_t> mergeLines(std::span<const uint8_t> base, std::span<const uint8_t> mine,
                                std::span<const uint8_t> theirs, FileTable& files, int own) {
  const auto b = linesOf(base, files, own);
  const auto m = linesOf(mine, files, own);
  const auto t = linesOf(theirs, files, own);
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
  return encodeLines(textOf(out), files, own);
}

std::vector<uint8_t> mergePrograms(std::span<const uint8_t> base, std::span<const uint8_t> mine,
                                   std::span<const uint8_t> theirs) {
  // Each store has its own table. The lines meet as text, which names a
  // target's file, and the answer gets a table of its own.
  auto text = [](std::span<const uint8_t> bytes) { return decodeProgram(bytes); };
  FileTable files{{"MAIN"}};
  const std::vector<uint8_t> b = encodeLines(text(base), files, 0);
  const std::vector<uint8_t> m = encodeLines(text(mine), files, 0);
  const std::vector<uint8_t> t = encodeLines(text(theirs), files, 0);
  Store store;
  store.files.push_back({"MAIN", true, mergeLines(b, m, t, files, 0)});
  store.adopt(files);
  return writeStore(store);
}

Synced syncLines(std::span<const uint8_t> base, const std::string& text, std::span<const uint8_t> stored,
                 FileTable& files, int own) {
  Synced out;
  out.text = text;
  const std::vector<uint8_t> doc = encodeLines(text, files, own);
  const bool same = std::equal(doc.begin(), doc.end(), stored.begin(), stored.end());
  const bool docMoved = !std::equal(doc.begin(), doc.end(), base.begin(), base.end());
  const bool storeMoved = !std::equal(stored.begin(), stored.end(), base.begin(), base.end());
  if (same) out.lines = doc;
  else if (!storeMoved) out.lines = doc;
  else if (!docMoved) out.lines.assign(stored.begin(), stored.end());
  else out.lines = mergeLines(base, doc, stored, files, own);
  out.storeChanged = !std::equal(out.lines.begin(), out.lines.end(), stored.begin(), stored.end());
  if (out.lines != doc) {
    out.text = patchLines(text, out.lines, files, own);
    out.textChanged = out.text != text;
  }
  return out;
}

std::string patchText(const std::string& text, std::span<const uint8_t> program) {
  FileTable files;
  const StoredFile file = mainOf(program, files);
  return patchLines(text, file.lines, files, 0);
}

std::string patchLines(const std::string& text, std::span<const uint8_t> records, const FileTable& files, int own) {
  const auto want = linesOf(records, files, own);
  // A document's line as LIST would print it once stored.
  auto listed = [&](const std::string& body) {
    FileTable names = files;
    return expandLine(crunchLine(canonicalLine(body), names, own), names, own);
  };
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
    kept.push_back({n, listed(body) == it->second ? rows[r] : std::to_string(n) + " " + it->second});
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
