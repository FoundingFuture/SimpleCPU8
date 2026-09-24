#include "asm/format.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <vector>

#include "core/isa.h"

namespace sc8 {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }
bool isNameStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool isNameChar(char c) { return isNameStart(c) || std::isdigit(static_cast<unsigned char>(c)) != 0; }

std::string_view trim(std::string_view s) {
  while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
  while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
  return s;
}

// The widest first word of an instruction's canonical name: PUSHB, PUSHW.
size_t longestMnemonic() {
  static const size_t longest = [] {
    size_t n = 0;
    for (const OpDef& op : ops()) {
      const size_t space = op.name.find(' ');
      n = std::max(n, space == std::string_view::npos ? op.name.size() : space);
    }
    return n;
  }();
  return longest;
}

// One source line in its parts. The assembler cuts the comment at the
// first semicolon and reads a label as a name before the first colon, so
// the split here is the same.
struct Parts {
  std::string_view label;    // without the colon
  std::string_view body;     // mnemonic and operands, trimmed
  std::string_view comment;  // from the semicolon on
  size_t commentAt = 0;      // the comment's column in the source
};

bool isIdent(std::string_view s) {
  if (s.empty() || !isNameStart(s.front())) return false;
  return std::all_of(s.begin(), s.end(), isNameChar);
}

Parts split(std::string_view line) {
  Parts p;
  std::string_view code = line;
  const size_t sc = line.find(';');
  if (sc != std::string_view::npos) {
    p.comment = line.substr(sc);
    while (!p.comment.empty() && isSpace(p.comment.back())) p.comment.remove_suffix(1);
    p.commentAt = sc;
    code = line.substr(0, sc);
  }
  code = trim(code);
  const size_t colon = code.find(':');
  if (colon != std::string_view::npos && isIdent(code.substr(0, colon))) {
    p.label = code.substr(0, colon);
    code = trim(code.substr(colon + 1));
  }
  p.body = code;
  return p;
}

// The column a text ends at, with tabs to the next multiple of 8.
size_t columnOf(std::string_view s) {
  size_t col = 0;
  for (char c : s) col = c == '\t' ? (col / 8 + 1) * 8 : col + 1;
  return col;
}

void padTo(std::string& out, size_t lineStart, size_t column) {
  const size_t at = out.size() - lineStart;
  out.append(column > at ? column - at : 1, ' ');
}

}  // namespace

int operandColumn(const AsmLayout& layout) {
  return layout.mnemonicColumn + static_cast<int>(longestMnemonic()) + 2;
}

std::string formatAssembly(std::string_view source, const AsmLayout& layout) {
  std::vector<std::string_view> lines;
  for (size_t pos = 0;;) {
    const size_t nl = source.find('\n', pos);
    lines.push_back(source.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos));
    if (nl == std::string_view::npos) break;
    pos = nl + 1;
  }

  // The comment column: the one most trailing comments already use, the
  // rightmost on a tie.
  size_t commentColumn = static_cast<size_t>(std::max(0, layout.commentColumn));
  if (commentColumn == 0) {
    std::map<size_t, int> seen;
    for (std::string_view line : lines) {
      const Parts p = split(line);
      if (!p.comment.empty() && (!p.body.empty() || !p.label.empty())) {
        seen[columnOf(line.substr(0, p.commentAt))]++;
      }
    }
    // A column left of the operands plus a short operand is a comment
    // squeezed after short code, not a column anyone chose.
    const size_t least = static_cast<size_t>(operandColumn(layout)) + 8;
    commentColumn = 32;
    int best = 0;
    for (const auto& [col, count] : seen) {
      if (col >= least && count >= best) {
        best = count;
        commentColumn = col;
      }
    }
  }

  const size_t mnemonic = static_cast<size_t>(layout.mnemonicColumn);
  const size_t operands = static_cast<size_t>(operandColumn(layout));
  std::string out;
  out.reserve(source.size() + source.size() / 4);
  // In .ram and .data a label binds to the directive on its line, and a
  // .addr needs one in front, so there a long label stays where it is.
  bool code = true;
  for (size_t i = 0; i < lines.size(); i++) {
    if (i) out += '\n';
    const std::string_view line = lines[i];
    const Parts p = split(line);
    // A blank line, or a line that is only a comment, stays as it was.
    if (p.label.empty() && p.body.empty()) {
      std::string_view kept = line;
      while (!kept.empty() && isSpace(kept.back())) kept.remove_suffix(1);
      out += kept;
      continue;
    }
    if (p.label.empty()) {
      if (p.body == ".code") code = true;
      else if (p.body == ".ram" || p.body == ".data") code = false;
    }
    size_t start = out.size();
    if (!p.label.empty()) {
      out += p.label;
      out += ':';
      // The label and one space must fit before the mnemonic column.
      if (code && !p.body.empty() && p.label.size() + 2 > mnemonic) {
        out += '\n';
        start = out.size();
      }
    }
    if (!p.body.empty()) {
      padTo(out, start, mnemonic);
      size_t word = 0;
      while (word < p.body.size() && !isSpace(p.body[word])) word++;
      out += p.body.substr(0, word);
      const std::string_view rest = trim(p.body.substr(word));
      if (!rest.empty()) {
        padTo(out, start, operands);
        out += rest;
      }
    }
    if (!p.comment.empty()) {
      padTo(out, start, commentColumn);
      out += p.comment;
    }
  }
  return out;
}

}  // namespace sc8
