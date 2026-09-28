#include "assets/font.h"

#include <cstdio>

namespace sc8::font {

namespace {

constexpr int ROWS = gpu::GLYPH_BYTES;
constexpr int COLS = 8;
constexpr int LAST_CODE = gpu::GLYPHS - 1;

// A glyph code as the file writes it: $ and two hex digits in capitals.
std::string codeText(int code) {
  char b[4];
  std::snprintf(b, sizeof b, "$%02X", code);
  return b;
}

std::string at(int line, const std::string& what) { return "line " + std::to_string(line) + ": " + what; }

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// "$41" or "$41 A": the code, or -1 when the line is not one.
int codeOf(std::string_view line) {
  if (line.size() != 3 && line.size() != 5) return -1;
  if (line[0] != '$') return -1;
  const int hi = hexDigit(line[1]);
  const int lo = hexDigit(line[2]);
  if (hi < 0 || lo < 0) return -1;
  if (line.size() == 5 && line[3] != ' ') return -1;
  return hi * 16 + lo;
}

// "cell 6 8": true, with the two sizes, when the line is one and both fit.
bool cellOf(std::string_view line, int& w, int& h) {
  if (!line.starts_with("cell ")) return false;
  line.remove_prefix(5);
  if (line.size() != 3 || line[1] != ' ') return false;
  w = line[0] - '0';
  h = line[2] - '0';
  const auto fits = [](int n) { return n >= gpu::TEXT_CELL_MIN && n <= gpu::TEXT_CELL_MAX; };
  return fits(w) && fits(h);
}

// "X......." as a row byte, bit 0 the left pixel, or -1.
int rowOf(std::string_view line) {
  if (line.size() != COLS) return -1;
  int bits = 0;
  for (int c = 0; c < COLS; c++) {
    const char ch = line[static_cast<size_t>(c)];
    if (ch == 'X') bits |= 1 << c;
    else if (ch != '.') return -1;
  }
  return bits;
}

}  // namespace

ReadResult read(std::string_view text) {
  // A file checked out with Windows line ends still reads.
  std::vector<std::string_view> lines;
  for (size_t start = 0; start < text.size();) {
    size_t nl = text.find('\n', start);
    if (nl == std::string_view::npos) nl = text.size();
    std::string_view line = text.substr(start, nl - start);
    if (line.ends_with('\r')) line.remove_suffix(1);
    lines.push_back(line);
    start = nl + 1;
  }
  const auto refuse = [](int line, const std::string& what) { return ReadResult{std::nullopt, at(line, what)}; };

  Font f;
  if (lines.empty() || lines[0] != MAGIC) {
    return refuse(1, "a font starts with the line " + std::string(MAGIC));
  }
  if (lines.size() < 2 || !cellOf(lines[1], f.width, f.height)) {
    return refuse(2, "the cell is a width and a height, each 4 to 8, as cell 6 8");
  }

  int next = 0;   // the code the next glyph must have
  int rows = -1;  // rows read of the glyph being read, -1 between glyphs
  for (size_t i = 2; i < lines.size(); i++) {
    const int n = static_cast<int>(i) + 1;
    const std::string_view line = lines[i];
    if (rows >= 0) {
      if (line.empty() || line.starts_with("$")) {
        return refuse(n, "glyph " + codeText(next) + " has " + std::to_string(rows) + " rows, and needs 8");
      }
      const int bits = rowOf(line);
      if (bits < 0) return refuse(n, "a row is eight characters of X and ., the left one bit 0");
      f.glyphs[static_cast<size_t>(next * ROWS + rows)] = static_cast<uint8_t>(bits);
      if (++rows == ROWS) {
        rows = -1;
        next++;
      }
      continue;
    }
    if (line.empty()) continue;
    if (next > LAST_CODE) return refuse(n, "nothing may follow glyph " + codeText(LAST_CODE));
    const int code = codeOf(line);
    if (code < 0) return refuse(n, "a glyph starts with $ and its code in two hex digits, as $41 A");
    if (code != next) return refuse(n, "glyph " + codeText(next) + " comes next, not " + codeText(code));
    rows = 0;
  }
  const int end = static_cast<int>(lines.size()) + 1;
  if (rows >= 0) {
    return refuse(end, "glyph " + codeText(next) + " has " + std::to_string(rows) + " rows, and needs 8");
  }
  if (next <= LAST_CODE) return refuse(end, "the font ends before glyph " + codeText(next));
  return {f, ""};
}

std::string write(const Font& f) {
  std::string out(MAGIC);
  out += "\ncell " + std::to_string(f.width) + " " + std::to_string(f.height) + "\n";
  for (int code = 0; code <= LAST_CODE; code++) {
    out += "\n" + codeText(code);
    // The printable codes carry their character, which the reader skips.
    if (code > ' ' && code < 0x7f) {
      out += ' ';
      out += static_cast<char>(code);
    }
    out += '\n';
    for (int r = 0; r < ROWS; r++) {
      const uint8_t bits = f.glyphs[static_cast<size_t>(code * ROWS + r)];
      for (int c = 0; c < COLS; c++) out += ((bits >> c) & 1) ? 'X' : '.';
      out += '\n';
    }
  }
  return out;
}

Font builtIn() {
  Font f;
  for (int code = FONT_FIRST; code <= FONT_LAST; code++) {
    for (int r = 0; r < ROWS; r++) {
      uint8_t bits = glyphRow(code, r);
      // The cursor block carries its rightmost column on to the eighth, so
      // it fills a cell of any width.
      if (code == FONT_LAST && (bits & (1u << (FONT_W - 1)))) bits = 0xff;
      f.glyphs[static_cast<size_t>(code * ROWS + r)] = bits;
    }
  }
  return f;
}

std::vector<uint8_t> blob(const Font& f) {
  std::vector<uint8_t> out;
  out.reserve(2 + f.glyphs.size());
  out.push_back(static_cast<uint8_t>(f.width));
  out.push_back(static_cast<uint8_t>(f.height));
  out.insert(out.end(), f.glyphs.begin(), f.glyphs.end());
  return out;
}

}  // namespace sc8::font
