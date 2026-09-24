#include "cc/pp.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace sc8::cc {

namespace {

constexpr size_t MAX_ROUNDS = 64;

bool isIdStart(char c) { return c == '_' || std::isalpha(static_cast<unsigned char>(c)) != 0; }
bool isIdChar(char c) { return c == '_' || std::isalnum(static_cast<unsigned char>(c)) != 0; }
bool isSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

std::string trim(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && isSpace(s[a])) a++;
  while (b > a && isSpace(s[b - 1])) b--;
  return std::string(s.substr(a, b - a));
}

// Split a macro's argument list on commas at depth zero.
std::vector<std::string> splitArgs(const std::string& s) {
  std::vector<std::string> out;
  int depth = 0;
  std::string cur;
  for (char c : s) {
    if (c == '(') depth++;
    if (c == ')') depth--;
    if (c == ',' && depth == 0) { out.push_back(trim(cur)); cur.clear(); continue; }
    cur += c;
  }
  if (!trim(cur).empty() || !out.empty()) out.push_back(trim(cur));
  return out;
}

// Find the matching ")" for the "(" at `from`. Returns npos when there is none.
size_t matchParen(const std::string& s, size_t from) {
  int depth = 0;
  for (size_t i = from; i < s.size(); i++) {
    if (s[i] == '(') depth++;
    else if (s[i] == ')') { depth--; if (depth == 0) return i; }
  }
  return std::string::npos;
}

// The identifier at s[i], or empty when there is none.
std::string identAt(const std::string& s, size_t i) {
  if (i >= s.size() || !isIdStart(s[i])) return "";
  size_t n = 1;
  while (i + n < s.size() && isIdChar(s[i + n])) n++;
  return s.substr(i, n);
}

// Replace every identifier in s by what `f` says, in one pass.
template <class F>
std::string mapIdents(const std::string& s, F f) {
  std::string out;
  size_t i = 0;
  while (i < s.size()) {
    const std::string id = identAt(s, i);
    if (id.empty()) { out += s[i]; i++; continue; }
    out += f(id);
    i += id.size();
  }
  return out;
}

// JavaScript's `Function` evaluated the folded #if text in the TypeScript.
// This is that evaluator: C precedence over doubles, with JavaScript's
// results for the operators the character set lets through. A syntax error
// throws, and the caller turns it into "#if cannot work out".
struct CondEval {
  const std::string& s;
  size_t i = 0;
  struct Bad {};

  void skip() { while (i < s.size() && isSpace(s[i])) i++; }
  bool take(std::string_view op) {
    skip();
    if (s.compare(i, op.size(), op) == 0) { i += op.size(); return true; }
    return false;
  }
  char peek() { skip(); return i < s.size() ? s[i] : '\0'; }

  double primary() {
    skip();
    if (take("(")) {
      const double v = orExpr();
      if (!take(")")) throw Bad{};
      return v;
    }
    if (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
      size_t n = 0;
      while (i + n < s.size() && std::isdigit(static_cast<unsigned char>(s[i + n]))) n++;
      // A leading zero is a legacy octal literal, which strict mode refuses.
      if (n > 1 && s[i] == '0') throw Bad{};
      const double v = std::strtod(s.substr(i, n).c_str(), nullptr);
      i += n;
      return v;
    }
    throw Bad{};
  }
  static int32_t i32(double v) {
    if (!std::isfinite(v)) return 0;
    double t = std::trunc(v);
    t = std::fmod(t, 4294967296.0);
    if (t < 0) t += 4294967296.0;
    return static_cast<int32_t>(static_cast<uint32_t>(t));
  }
  double unary() {
    if (take("-")) return -unary();
    if (take("+")) return unary();
    if (take("!")) {
      const double v = unary();
      return (v != 0 && !std::isnan(v)) ? 0 : 1;
    }
    if (take("~")) return static_cast<double>(~i32(unary()));
    return primary();
  }
  double mul() {
    double v = unary();
    for (;;) {
      if (take("*")) v = v * unary();
      else if (take("/")) v = v / unary();
      else if (take("%")) v = std::fmod(v, unary());
      else return v;
    }
  }
  double add() {
    double v = mul();
    for (;;) {
      if (take("+")) v = v + mul();
      else if (take("-")) v = v - mul();
      else return v;
    }
  }
  double shift() {
    double v = add();
    for (;;) {
      if (take(">>>")) v = static_cast<double>(static_cast<uint32_t>(i32(v)) >> (i32(add()) & 31));
      else if (take("<<")) v = static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(i32(v)) << (i32(add()) & 31)));
      else if (take(">>")) v = static_cast<double>(i32(v) >> (i32(add()) & 31));
      else return v;
    }
  }
  double rel() {
    double v = shift();
    for (;;) {
      if (take("<=")) v = v <= shift() ? 1 : 0;
      else if (take(">=")) v = v >= shift() ? 1 : 0;
      else if (peek() == '<' && s.compare(i, 2, "<<") != 0) { i++; v = v < shift() ? 1 : 0; }
      else if (peek() == '>' && s.compare(i, 2, ">>") != 0) { i++; v = v > shift() ? 1 : 0; }
      else return v;
    }
  }
  double eq() {
    double v = rel();
    for (;;) {
      if (take("===") || take("==")) v = v == rel() ? 1 : 0;
      else if (take("!==") || take("!=")) v = v != rel() ? 1 : 0;
      else return v;
    }
  }
  double band() {
    double v = eq();
    while (peek() == '&' && s.compare(i, 2, "&&") != 0) { i++; v = static_cast<double>(i32(v) & i32(eq())); }
    return v;
  }
  double bxor() {
    double v = band();
    while (take("^")) v = static_cast<double>(i32(v) ^ i32(band()));
    return v;
  }
  double bor() {
    double v = bxor();
    while (peek() == '|' && s.compare(i, 2, "||") != 0) { i++; v = static_cast<double>(i32(v) | i32(bxor())); }
    return v;
  }
  double andExpr() {
    double v = bor();
    while (take("&&")) {
      const double r = bor();
      v = (v != 0 && !std::isnan(v)) ? r : v;
    }
    return v;
  }
  double orExpr() {
    double v = andExpr();
    while (take("||")) {
      const double r = andExpr();
      v = (v != 0 && !std::isnan(v)) ? v : r;
    }
    return v;
  }
  double run() {
    const double v = orExpr();
    skip();
    if (i != s.size()) throw Bad{};
    return v;
  }
};

}  // namespace

std::string stripComments(std::string_view src) {
  std::string out;
  size_t i = 0;
  while (i < src.size()) {
    const char c = src[i];
    if (c == '"' || c == '\'') {
      const char q = c;
      size_t j = i + 1;
      while (j < src.size() && src[j] != q) j += src[j] == '\\' ? 2u : 1u;
      out += src.substr(i, std::min(j + 1, src.size()) - i);
      i = j + 1;
      continue;
    }
    if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
      while (i < src.size() && src[i] != '\n') { out += ' '; i++; }
      continue;
    }
    if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
      i += 2;
      out += "  ";
      while (i < src.size() && !(src[i] == '*' && i + 1 < src.size() && src[i + 1] == '/')) {
        out += src[i] == '\n' ? '\n' : ' ';
        i++;
      }
      if (i < src.size()) { out += "  "; i += 2; }
      continue;
    }
    out += c;
    i++;
  }
  return out;
}

Preprocessor::Preprocessor(PpOptions opts) : opts_(std::move(opts)) {
  for (const auto& [k, v] : opts_.defines) macros_[k] = Macro{std::nullopt, v};
}

PpResult Preprocessor::run(const std::string& src, const std::string& fileName) {
  file(src, fileName);
  return PpResult{out_, included_, macros_};
}

void Preprocessor::file(const std::string& src, const std::string& fileName) {
  if (open_.count(fileName)) throw CcError(fileName, 1, fileName + " includes itself");
  open_.insert(fileName);
  // A conditional stack: each entry says whether its branch is live, and
  // whether any branch of this if has already been taken.
  struct Cond { bool live; bool taken; };
  std::vector<Cond> cond;
  auto live = [&]() {
    for (const Cond& c : cond) if (!c.live) return false;
    return true;
  };

  // split("\n") keeps a trailing empty piece, and so does this.
  std::vector<std::string> raw;
  {
    const std::string stripped = stripComments(src);
    size_t start = 0;
    for (;;) {
      const size_t nl = stripped.find('\n', start);
      if (nl == std::string::npos) { raw.push_back(stripped.substr(start)); break; }
      raw.push_back(stripped.substr(start, nl - start));
      start = nl + 1;
    }
  }
  for (size_t i = 0; i < raw.size(); i++) {
    std::string text = raw[i];
    const int lineNo = static_cast<int>(i + 1);
    // A backslash at the end joins the next line, which is how a long
    // #define is written.
    while (!text.empty() && text.back() == '\\' && i + 1 < raw.size()) {
      text = text.substr(0, text.size() - 1) + " " + raw[++i];
    }
    const std::string trimmed = trim(text);

    if (!trimmed.starts_with("#")) {
      if (live()) out_.push_back({expand(trimmed, fileName, lineNo), fileName, lineNo});
      else out_.push_back({"", fileName, lineNo});
      continue;
    }

    // ^#\s*([a-z]+)\s*(.*)$
    size_t p = 1;
    while (p < trimmed.size() && isSpace(trimmed[p])) p++;
    size_t q = p;
    while (q < trimmed.size() && trimmed[q] >= 'a' && trimmed[q] <= 'z') q++;
    if (q == p) { out_.push_back({"", fileName, lineNo}); continue; }
    const std::string dir = trimmed.substr(p, q - p);
    while (q < trimmed.size() && isSpace(trimmed[q])) q++;
    const std::string rest = trimmed.substr(q);
    out_.push_back({"", fileName, lineNo});

    if (dir == "ifdef") {
      const bool has = macros_.count(trim(rest)) > 0;
      cond.push_back({has, has});
    } else if (dir == "ifndef") {
      const bool has = macros_.count(trim(rest)) > 0;
      cond.push_back({!has, !has});
    } else if (dir == "if") {
      const bool v = evalCond(rest, fileName, lineNo);
      cond.push_back({v, v});
    } else if (dir == "elif") {
      if (cond.empty()) throw CcError(fileName, lineNo, "#elif with no #if");
      Cond& top = cond.back();
      const bool v = !top.taken && evalCond(rest, fileName, lineNo);
      top.live = v;
      top.taken = top.taken || v;
    } else if (dir == "else") {
      if (cond.empty()) throw CcError(fileName, lineNo, "#else with no #if");
      Cond& top = cond.back();
      top.live = !top.taken;
      top.taken = true;
    } else if (dir == "endif") {
      if (cond.empty()) throw CcError(fileName, lineNo, "#endif with no #if");
      cond.pop_back();
    } else if (dir == "define") {
      if (live()) define(rest, fileName, lineNo);
    } else if (dir == "undef") {
      if (live()) macros_.erase(trim(rest));
    } else if (dir == "include") {
      if (!live()) continue;
      const std::string inc = trim(rest);
      const bool shaped = inc.size() >= 3 && (inc.front() == '<' || inc.front() == '"') &&
                          (inc.back() == '>' || inc.back() == '"') &&
                          inc.substr(1, inc.size() - 2).find_first_of("\">") == std::string::npos;
      if (!shaped) throw CcError(fileName, lineNo, "#include wants <name> or \"name\"");
      const bool angled = inc.front() == '<';
      const std::string name = inc.substr(1, inc.size() - 2);
      std::optional<std::string> text2;
      if (opts_.resolve) text2 = opts_.resolve(name, angled);
      if (!text2) {
        throw CcError(fileName, lineNo,
                      "cannot find " + (angled ? "<" + name + ">" : "\"" + name + "\"") +
                          ". The libraries are graphics.h, sound.h, keys.h, math.h, disk.h, and below them gpu.h, apu.h, io.h, acp.h, storage.h, sys.h and rom.h.");
      }
      bool seen = false;
      for (const std::string& n : included_) if (n == name) seen = true;
      if (!seen) included_.push_back(name);
      file(*text2, name);
    } else if (dir == "pragma") {
      // pragma once is unnecessary: a header guards itself
    } else if (dir == "error") {
      if (live()) throw CcError(fileName, lineNo, trim(rest).empty() ? "#error" : trim(rest));
    } else {
      throw CcError(fileName, lineNo, "unknown directive #" + dir);
    }
  }
  if (!cond.empty()) throw CcError(fileName, static_cast<int>(raw.size()), "#if with no #endif");
  open_.erase(fileName);
}

void Preprocessor::define(const std::string& rest, const std::string& fileName, int line) {
  const std::string name = identAt(rest, 0);
  if (name.empty()) throw CcError(fileName, line, "#define wants a name");
  // A function-like macro has its "(" right after the name, no space.
  if (name.size() < rest.size() && rest[name.size()] == '(') {
    const size_t close = rest.find(')', name.size() + 1);
    if (close != std::string::npos) {
      const std::string inside = rest.substr(name.size() + 1, close - name.size() - 1);
      std::vector<std::string> params;
      if (!trim(inside).empty()) {
        size_t start = 0;
        for (;;) {
          const size_t comma = inside.find(',', start);
          if (comma == std::string::npos) { params.push_back(trim(inside.substr(start))); break; }
          params.push_back(trim(inside.substr(start, comma - start)));
          start = comma + 1;
        }
      }
      macros_[name] = Macro{params, trim(rest.substr(close + 1))};
      return;
    }
  }
  macros_[name] = Macro{std::nullopt, trim(rest.substr(name.size()))};
}

// #if takes defined(X) and constant arithmetic, which is enough for a
// header guard and a build switch.
bool Preprocessor::evalCond(const std::string& rest, const std::string& fileName, int line) {
  // defined(X) and defined X become 1 or 0 before anything expands.
  std::string s;
  size_t i = 0;
  while (i < rest.size()) {
    const std::string id = identAt(rest, i);
    if (id == "defined") {
      size_t j = i + id.size();
      while (j < rest.size() && isSpace(rest[j])) j++;
      if (j < rest.size() && rest[j] == '(') {
        size_t k = j + 1;
        while (k < rest.size() && isSpace(rest[k])) k++;
        const std::string n = identAt(rest, k);
        size_t m = k + n.size();
        while (m < rest.size() && isSpace(rest[m])) m++;
        if (!n.empty() && m < rest.size() && rest[m] == ')') {
          s += macros_.count(n) ? "1" : "0";
          i = m + 1;
          continue;
        }
      } else if (j > i + id.size()) {
        const std::string n = identAt(rest, j);
        if (!n.empty()) {
          s += macros_.count(n) ? "1" : "0";
          i = j + n.size();
          continue;
        }
      }
    }
    if (!id.empty()) { s += id; i += id.size(); continue; }
    s += rest[i];
    i++;
  }
  s = expand(s, fileName, line);
  s = mapIdents(s, [](const std::string&) { return std::string("0"); });
  for (char c : s) {
    if (std::string_view("-+*/%()<>=!&|^~0123456789").find(c) == std::string_view::npos && !isSpace(c)) {
      throw CcError(fileName, line, "#if cannot work out " + trim(rest));
    }
  }
  try {
    // Only arithmetic reaches here: every name is gone and the character
    // set is checked above.
    const std::string text = trim(s).empty() ? "0" : s;
    CondEval ev{text};
    const double v = ev.run();
    return v != 0 && !std::isnan(v);
  } catch (const CondEval::Bad&) {
    throw CcError(fileName, line, "#if cannot work out " + trim(rest));
  }
}

// Expand macros in one line. ONE pass, because expandOnce already recurses
// into each body with that macro blocked. Re-scanning the whole line
// afterwards would undo the block: C paints a macro's own name in its own
// output so it is never expanded again, and `#define A A + 1` would
// otherwise grow a term per round until it hit a limit.
std::string Preprocessor::expand(const std::string& text, const std::string& fileName, int line,
                                 const std::set<std::string>& blocked) {
  if (blocked.size() > MAX_ROUNDS) throw CcError(fileName, line, "a macro expands forever here");
  return expandOnce(text, fileName, line, blocked);
}

std::string Preprocessor::expandOnce(const std::string& text, const std::string& fileName, int line,
                                     const std::set<std::string>& blocked) {
  std::string out;
  size_t i = 0;
  while (i < text.size()) {
    // Skip string and character literals whole: a macro name inside one is
    // text, not a macro.
    const char c = text[i];
    if (c == '"' || c == '\'') {
      const char q = c;
      size_t j = i + 1;
      while (j < text.size() && text[j] != q) j += text[j] == '\\' ? 2u : 1u;
      out += text.substr(i, std::min(j + 1, text.size()) - i);
      i = j + 1;
      continue;
    }
    if (!isIdStart(c)) { out += c; i++; continue; }

    const std::string name = identAt(text, i);
    auto it = macros_.find(name);
    if (it == macros_.end() || blocked.count(name)) { out += name; i += name.size(); continue; }
    const Macro& mac = it->second;

    std::set<std::string> inner = blocked;
    inner.insert(name);
    if (!mac.params) {
      out += expand(mac.body, fileName, line, inner);
      i += name.size();
      continue;
    }

    // A function-like macro without its parentheses is just a name.
    size_t k = i + name.size();
    while (k < text.size() && isSpace(text[k])) k++;
    if (k >= text.size() || text[k] != '(') { out += name; i += name.size(); continue; }
    const size_t close = matchParen(text, k);
    if (close == std::string::npos) throw CcError(fileName, line, name + "( is never closed");
    const std::vector<std::string> args = splitArgs(text.substr(k + 1, close - k - 1));
    const std::vector<std::string>& params = *mac.params;
    if (args.size() != params.size()) {
      throw CcError(fileName, line,
                    name + " takes " + std::to_string(params.size()) + " argument" +
                        (params.size() == 1 ? "" : "s") + " and " + std::to_string(args.size()) +
                        (args.size() == 1 ? " was" : " were") + " given");
    }
    // Every parameter is substituted in ONE pass. Replacing them one after
    // another lets a later parameter's name match text an earlier argument
    // brought in: poke(&v, 55) put 55 inside the &v, because the second
    // parameter was also called v.
    std::map<std::string, std::string> slots;
    for (size_t n = 0; n < params.size(); n++) slots[params[n]] = "(" + args[n] + ")";
    const std::string body = mapIdents(mac.body, [&](const std::string& w) {
      auto hit = slots.find(w);
      return hit == slots.end() ? w : hit->second;
    });
    out += expand(body, fileName, line, inner);
    i = close + 1;
  }
  return out;
}

PpResult preprocess(const std::string& src, const std::string& file, PpOptions opts) {
  return Preprocessor(std::move(opts)).run(src, file);
}

}  // namespace sc8::cc
