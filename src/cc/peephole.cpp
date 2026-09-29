#include "cc/peephole.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string_view>

namespace sc8::cc {

namespace {

enum class Kind { Skip, Label, Ins, Barrier };

// What one line does, as far as the rules below need to know.
struct Line {
  Kind kind = Kind::Skip;
  bool stmt = false;  // the statement marker: no temp is live here
  std::string mnem, dst, src;
  bool readsA = false, writesA = false;
  bool setsZ = false, setsN = false;
  bool flow = false;     // control may leave: any jump, call, return, halt
  bool devices = false;  // OUT or IN: a device may read or write RAM
  bool writesD1 = false, writesD2 = false, writesD3 = false;
  std::string byteStore;  // [X] when A is stored to X
  std::string wordStore;  // [X] when a D register is stored to X
};

std::string trim(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
  return std::string(s.substr(a, b - a));
}

bool mentionsA(const std::string& operand) {
  return operand.find("[A]") != std::string::npos || operand.find("+A") != std::string::npos;
}

bool isAlu(const std::string& m) {
  return m == "ADD" || m == "SUB" || m == "AND" || m == "OR" || m == "XOR" || m == "ADC" || m == "SBC";
}

bool isShift(const std::string& m) { return m == "SHL" || m == "SHR" || m == "ROL" || m == "ROR" || m == "ASR"; }

bool isJump(const std::string& m) {
  return m == "JMP" || m == "JZ" || m == "JNZ" || m == "JC" || m == "JNC" || m == "JN" || m == "JP" ||
         m == "JV" || m == "JNV";
}

Line parse(const std::string& raw) {
  Line l;
  std::string text = raw;
  const size_t semi = text.find(';');
  if (semi != std::string::npos) text = text.substr(0, semi);
  if (trim(raw) == STMT_MARK) {
    l.stmt = true;
    return l;
  }
  if (trim(raw) == BARRIER_MARK) {
    l.kind = Kind::Barrier;
    return l;
  }
  const std::string t = trim(text);
  if (t.empty()) return l;
  if (!std::isspace(static_cast<unsigned char>(raw[0]))) {
    // A label alone on its line is what the code generator writes. A label
    // with an instruction after it came from inline assembly.
    if (t.back() == ':' && t.find(' ') == std::string::npos) l.kind = Kind::Label;
    else l.kind = Kind::Barrier;
    return l;
  }
  const size_t sp = t.find(' ');
  std::string m = t.substr(0, sp);
  for (char& c : m) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  const std::string rest = sp == std::string::npos ? "" : trim(std::string_view(t).substr(sp + 1));
  l.kind = Kind::Ins;
  l.mnem = m;
  auto arrow = [&]() -> bool {
    const size_t p = rest.find("<-");
    if (p == std::string::npos) return false;
    l.dst = trim(std::string_view(rest).substr(0, p));
    l.src = trim(std::string_view(rest).substr(p + 2));
    return true;
  };

  if (m == "LD") {
    if (!arrow()) {
      l.kind = Kind::Barrier;
      return l;
    }
    const bool dstD = l.dst == "D1" || l.dst == "D2";
    // The heap stack pointer moving: an address add into D3. The frame
    // slots named through it change meaning, so the passes forget them.
    if (l.dst == "D3" && l.src.rfind("D3", 0) == 0) {
      l.writesD3 = true;
      l.setsZ = true;
      return l;
    }
    if (l.dst == "A") {
      l.writesA = true;
      l.readsA = mentionsA(l.src);
      l.setsZ = l.setsN = true;
      if (l.src.find("D1]+") != std::string::npos) l.writesD1 = true;
      if (l.src.find("D2]+") != std::string::npos) l.writesD2 = true;
    } else if (dstD) {
      l.setsZ = true;
      l.readsA = mentionsA(l.src);
      if (l.src.find("[A]+") != std::string::npos) l.writesA = true;
      (l.dst == "D1" ? l.writesD1 : l.writesD2) = true;
      if (l.src.find("D1]+") != std::string::npos) l.writesD1 = true;
      if (l.src.find("D2]+") != std::string::npos) l.writesD2 = true;
    } else if (!l.dst.empty() && l.dst.front() == '[') {
      if (l.dst.find("D1]+") != std::string::npos) l.writesD1 = true;
      if (l.dst.find("D2]+") != std::string::npos) l.writesD2 = true;
      if (l.src == "A") {
        l.readsA = true;
        l.byteStore = l.dst;
      } else if (l.src == "D1" || l.src == "D2") {
        l.readsA = mentionsA(l.dst);
        l.wordStore = l.dst;
      } else {
        l.kind = Kind::Barrier;
      }
      if (mentionsA(l.dst)) l.readsA = true;
    } else {
      l.kind = Kind::Barrier;
    }
    return l;
  }
  if (isAlu(m) || m == "INC" || m == "DEC") {
    if (m == "INC" || m == "DEC") {
      if (rest == "D1") l.writesD1 = true;
      else if (rest == "D2") l.writesD2 = true;
      else {
        l.readsA = l.writesA = true;
        l.setsZ = l.setsN = true;
      }
      return l;
    }
    if (!arrow() || l.dst != "A") l.kind = Kind::Barrier;
    l.readsA = l.writesA = true;
    l.setsZ = l.setsN = true;
    return l;
  }
  if (m == "CMP" || m == "TST") {
    const size_t c = rest.find(',');
    l.src = c == std::string::npos ? "" : trim(std::string_view(rest).substr(c + 1));
    l.readsA = true;
    l.setsZ = l.setsN = true;
    return l;
  }
  if (isShift(m)) {
    l.readsA = l.writesA = true;
    l.setsZ = l.setsN = true;
    return l;
  }
  if (isJump(m) || m == "JSR" || m == "RET" || m == "HLT") {
    l.flow = true;
    l.src = rest;
    return l;
  }
  if (m == "NOP") return l;
  if (m == "PUSHB") {
    l.readsA = true;
    return l;
  }
  if (m == "PUSHW") return l;
  if (m == "POPB") {
    l.writesA = true;
    l.setsZ = l.setsN = true;
    return l;
  }
  if (m == "POPW") {
    l.setsZ = true;
    (rest == "D1" ? l.writesD1 : l.writesD2) = true;
    return l;
  }
  if (m == "OUT" || m == "OUTA") {
    l.devices = true;
    l.readsA = m == "OUTA" || rest.find("<- A") != std::string::npos || rest.find("A ->") != std::string::npos;
    l.src = rest;
    return l;
  }
  if (m == "INB" || m == "IN" || m == "INW") {
    l.devices = true;
    // A word read lands in a D register and sets Z. A byte read sets
    // nothing, which is why this code knows the difference.
    if (rest.find("D1") != std::string::npos) {
      l.writesD1 = true;
      l.setsZ = true;
    } else if (rest.find("D2") != std::string::npos) {
      l.writesD2 = true;
      l.setsZ = true;
    } else {
      l.writesA = true;
    }
    return l;
  }
  l.kind = Kind::Barrier;
  return l;
}

// A memory operand or an immediate whose value can be remembered: a zero
// page label, a temp, or a frame slot [D3+n]. Not a pointer's target and
// not a post-increment, which move. D3 moves only in lines this pass treats
// as barriers, so a frame slot names one byte between them.
std::string keyOf(const std::string& operand) {
  if (operand.empty()) return "";
  if (operand.front() != '[') return "#" + operand;
  if (operand.back() != ']') return "";
  const std::string inner = operand.substr(1, operand.size() - 2);
  if (inner == "A" || inner.find("D1") != std::string::npos || inner.find("D2") != std::string::npos ||
      inner.find("+A") != std::string::npos) {
    return "";
  }
  return operand;
}

// The label a memory operand is based on: [__t3+1] is __t3.
std::string baseOf(const std::string& operand) {
  if (operand.size() < 3 || operand.front() != '[') return "";
  std::string inner = operand.substr(1, operand.find(']') - 1);
  const size_t plus = inner.find('+');
  return plus == std::string::npos ? inner : inner.substr(0, plus);
}

bool isTempBase(const std::string& base) {
  return base.size() > 3 && base.rfind("__t", 0) == 0 &&
         std::all_of(base.begin() + 3, base.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

// Does text name the temp? __t1 must not match __t12.
bool mentions(const std::string& text, const std::string& temp) {
  size_t at = 0;
  while ((at = text.find(temp, at)) != std::string::npos) {
    const size_t end = at + temp.size();
    if (end >= text.size() || !std::isdigit(static_cast<unsigned char>(text[end]))) return true;
    at = end;
  }
  return false;
}

class Pass {
 public:
  explicit Pass(const std::vector<std::string>& lines) : lines_(lines) {
    for (size_t i = 0; i < lines_.size(); i++) {
      info_.push_back(parse(lines_[i]));
      if (info_[i].kind == Kind::Label) labels_[trim(lines_[i]).substr(0, trim(lines_[i]).size() - 1)] = i;
    }
    alive_.assign(lines_.size(), true);
  }

  bool run() {
    // A rule that rewrites a line runs alone in its round, since the other
    // rules read what each line was when the round began.
    if (branchOnAnswer()) return true;
    if (threadJumps()) return true;
    if (invertJumps()) return true;
    bool changed = false;
    changed |= unreachable();
    changed |= jumpsToNext();
    changed |= knownValues();
    changed |= knownWords();
    changed |= deadTempStores();
    changed |= deadLoads();
    return changed;
  }

  std::vector<bool> alive() const { return alive_; }
  // The lines a rule rewrote, by index, with their new text.
  const std::map<size_t, std::string>& rewritten() const { return rewritten_; }

 private:
  const std::vector<std::string>& lines_;
  std::vector<Line> info_;
  std::vector<bool> alive_;
  std::map<std::string, size_t> labels_;
  std::map<size_t, std::string> rewritten_;

  void rewrite(size_t i, const std::string& ins) { rewritten_[i] = "        " + ins; }

  // The first alive instruction from i on, past labels, statement marks
  // and comments. lines_.size() when something else comes first.
  size_t insFrom(size_t i) const {
    for (size_t j = i; j < lines_.size(); j = next(j)) {
      if (!alive_[j]) continue;
      const Line& l = info_[j];
      if (l.kind == Kind::Label || l.kind == Kind::Skip) continue;
      return l.kind == Kind::Ins ? j : lines_.size();
    }
    return lines_.size();
  }

  // Is A written before it is read, on every path from line j? A call and
  // a return end A's life. The runtime routines write A before they read
  // it. A compiled function takes its arguments in its frame and answers
  // in __ret. A statement starts with nothing held in A.
  bool aDeadFrom(size_t j, int hops) const {
    for (; j < lines_.size(); j = next(j)) {
      const Line& l = info_[j];
      if (l.stmt) return true;
      if (l.kind == Kind::Label || l.kind == Kind::Skip) continue;
      if (l.kind != Kind::Ins) return false;
      if (l.flow) {
        if (l.mnem == "JSR" || l.mnem == "RET" || l.mnem == "HLT") return true;
        if (hops == 0) return false;
        const auto target = labels_.find(l.src);
        if (target == labels_.end() || !aDeadFrom(target->second, hops - 1)) return false;
        if (l.mnem == "JMP") return true;
        continue;
      }
      if (l.readsA) return false;
      if (l.writesA) return true;
    }
    return false;
  }

  // An answer built as 0 or 1 only to be tested. boolValue in codegen.cpp
  // writes the first six lines. The test after an inlined call writes the
  // rest.
  //     LD A <- 0 / JMP done / yes: / LD A <- 1 / done: / LD [t] <- A /
  //     (labels) / LD A <- [t] / JZ X or JNZ X.
  // The arm whose answer the test sends to X jumps to X. The other goes on
  // after the test, and the byte and its test go. The byte is a temp no
  // path reads again. A and the flags are dead on both paths. The labels
  // between the store and the load have no jump.
  bool branchOnAnswer() {
    bool changed = false;
    const std::map<std::string, int> refs = refCounts();
    auto refsOf = [&](const std::string& name) {
      const auto r = refs.find(name);
      return r == refs.end() ? 0 : r->second;
    };
    for (size_t i = 0; i < lines_.size(); i++) {
      if (!alive_[i] || info_[i].kind != Kind::Ins) continue;
      const Line& zero = info_[i];
      if (zero.mnem != "LD" || zero.dst != "A" || zero.src != "0") continue;
      const size_t jmp = insFrom(next(i));
      if (jmp != next(i) || info_[jmp].mnem != "JMP") continue;
      const std::string done = info_[jmp].src;
      const size_t yes = next(jmp);
      if (yes >= lines_.size() || info_[yes].kind != Kind::Label) continue;
      const size_t one = insFrom(yes);
      if (one >= lines_.size() || info_[one].mnem != "LD" || info_[one].dst != "A" || info_[one].src != "1") continue;
      const size_t doneAt = next(one);
      if (doneAt >= lines_.size() || info_[doneAt].kind != Kind::Label || labelName(doneAt) != done) continue;
      if (refsOf(done) != 1) continue;
      const size_t store = insFrom(doneAt);
      if (store >= lines_.size() || info_[store].byteStore.empty()) continue;
      const std::string t = info_[store].byteStore;
      if (!isTempBase(baseOf(t))) continue;
      size_t load = next(store);
      bool quiet = true;
      for (; load < lines_.size() && info_[load].kind == Kind::Label; load = next(load)) {
        if (refsOf(labelName(load)) != 0) quiet = false;
      }
      if (!quiet || load >= lines_.size() || info_[load].kind != Kind::Ins) continue;
      if (info_[load].mnem != "LD" || info_[load].dst != "A" || info_[load].src != t) continue;
      const size_t test = next(load);
      if (test >= lines_.size() || info_[test].kind != Kind::Ins) continue;
      const std::string cc = info_[test].mnem;
      if (cc != "JZ" && cc != "JNZ") continue;
      const std::string x = info_[test].src;
      const auto xAt = labels_.find(x);
      if (xAt == labels_.end()) continue;
      const size_t after = next(test);
      int budget = 64;
      const std::string base = baseOf(t);
      const int bytes = bytesOf(info_[store], base);
      if (!unread(after, base, bytes, budget) || !unread(xAt->second, base, bytes, budget)) continue;
      if (!aDeadFrom(after, 4) || !aDeadFrom(xAt->second, 4)) continue;
      if (!flagsDeadFrom(after, 4) || !flagsDeadFrom(xAt->second, 4)) continue;
      alive_[i] = false;
      alive_[store] = false;
      alive_[load] = false;
      alive_[test] = false;
      if (cc == "JZ") {
        // False goes to X, true goes on after the test.
        rewrite(jmp, "JMP " + x);
        alive_[one] = false;
      } else {
        // True goes to X, false goes on at done, now after the test.
        rewrite(one, "JMP " + x);
      }
      changed = true;
      i = test;
    }
    return changed;
  }

  // A jump to a label whose first instruction is JMP Y goes to Y.
  bool threadJumps() {
    bool changed = false;
    for (size_t i = 0; i < lines_.size(); i++) {
      if (!alive_[i] || info_[i].kind != Kind::Ins || !isJump(info_[i].mnem)) continue;
      const auto target = labels_.find(info_[i].src);
      if (target == labels_.end()) continue;
      const size_t j = insFrom(target->second);
      if (j >= lines_.size() || j == i || info_[j].mnem != "JMP" || info_[j].src == info_[i].src) continue;
      rewrite(i, info_[i].mnem + " " + info_[j].src);
      changed = true;
    }
    return changed;
  }

  static std::string inverse(const std::string& cc) {
    static const std::map<std::string, std::string> pairs = {{"JZ", "JNZ"}, {"JNZ", "JZ"}, {"JC", "JNC"},
                                                             {"JNC", "JC"}, {"JN", "JP"},  {"JP", "JN"},
                                                             {"JV", "JNV"}, {"JNV", "JV"}};
    const auto p = pairs.find(cc);
    return p == pairs.end() ? "" : p->second;
  }

  // Jcc L1 / JMP L2 / L1: is J(not cc) L2 / L1:. The labels between the
  // two jumps may have no jump to them, since the JMP goes.
  bool invertJumps() {
    bool changed = false;
    const std::map<std::string, int> refs = refCounts();
    for (size_t i = 0; i < lines_.size(); i++) {
      if (!alive_[i] || info_[i].kind != Kind::Ins || !isJump(info_[i].mnem)) continue;
      const std::string flip = inverse(info_[i].mnem);
      if (flip.empty()) continue;
      size_t j = next(i);
      bool quiet = true;
      for (; j < lines_.size() && info_[j].kind == Kind::Label; j = next(j)) {
        if (refs.count(labelName(j))) quiet = false;
      }
      if (!quiet || j >= lines_.size() || info_[j].kind != Kind::Ins || info_[j].mnem != "JMP") continue;
      bool lands = false;
      for (size_t k = next(j); k < lines_.size() && info_[k].kind == Kind::Label; k = next(k)) {
        if (labelName(k) == info_[i].src) lands = true;
      }
      if (!lands) continue;
      rewrite(i, flip + " " + info_[j].src);
      alive_[j] = false;
      changed = true;
      i = j;
    }
    return changed;
  }

  size_t next(size_t i) const {
    for (size_t j = i + 1; j < lines_.size(); j++) {
      if (alive_[j] && (info_[j].kind != Kind::Skip || info_[j].stmt)) return j;
    }
    return lines_.size();
  }

  // Would the Z and N a removed line set be read before something else
  // sets them? Only a line that sets both, reached in straight line, says
  // no.
  bool flagsDead(size_t i) const { return flagsDeadFrom(next(i), 4); }

  bool flagsDeadFrom(size_t from, int hops) const {
    bool z = true, n = true;
    for (size_t j = from; j < lines_.size(); j = next(j)) {
      const Line& l = info_[j];
      // A statement's code sets every flag it tests, so none is live where
      // one starts.
      if (l.stmt) return true;
      // The code generator's labels do not read the flags, except the one a
      // signed compare jumps to on overflow, where N is still to be read.
      if (l.kind == Kind::Label) {
        if (lines_[j].find("_ovf") != std::string::npos) return false;
        continue;
      }
      if (l.kind != Kind::Ins) return false;
      if (l.flow) {
        // A compiled function tests nothing it did not set, so a call or a
        // return leaves no flag live. A JMP goes on at its target.
        if (l.mnem == "JSR" || l.mnem == "RET") return true;
        if (l.mnem != "JMP" || hops == 0) return false;
        const auto target = labels_.find(l.src);
        if (target == labels_.end()) return false;
        return flagsDeadFrom(target->second, hops - 1);
      }
      if (l.setsZ) z = false;
      if (l.setsN) n = false;
      if (!z && !n) return true;
    }
    return false;
  }

  // An instruction after a JMP, a RET or a HLT that no jump reaches: the
  // labels between have no jump to them. Nothing past a barrier or a
  // statement mark is taken.
  bool unreachable() {
    bool changed = false;
    const std::map<std::string, int> refs = refCounts();
    for (size_t i = 0; i < lines_.size(); i++) {
      if (!alive_[i] || info_[i].kind != Kind::Ins) continue;
      const std::string& m = info_[i].mnem;
      if (m != "JMP" && m != "RET" && m != "HLT") continue;
      for (size_t j = next(i); j < lines_.size(); j = next(j)) {
        const Line& l = info_[j];
        if (l.stmt || l.kind == Kind::Barrier) break;
        if (l.kind == Kind::Label) {
          if (refs.count(labelName(j))) break;
          continue;
        }
        if (l.kind != Kind::Ins) break;
        alive_[j] = false;
        changed = true;
      }
    }
    return changed;
  }

  // JMP L, or a conditional jump to L, right before L.
  bool jumpsToNext() {
    bool changed = false;
    for (size_t i = 0; i < lines_.size(); i++) {
      if (!alive_[i] || info_[i].kind != Kind::Ins || !isJump(info_[i].mnem)) continue;
      for (size_t j = next(i); j < lines_.size() && (info_[j].kind == Kind::Label || info_[j].stmt); j = next(j)) {
        if (trim(lines_[j]) == info_[i].src + ":") {
          alive_[i] = false;
          changed = true;
          break;
        }
      }
    }
    return changed;
  }

  // A label reached only by one forward jump, with no fall-through into it,
  // starts with what was known at that jump.
  std::map<std::string, int> refCounts() const {
    std::map<std::string, int> refs;
    for (size_t i = 0; i < lines_.size(); i++) {
      if (alive_[i] && info_[i].kind == Kind::Ins && info_[i].flow) refs[info_[i].src]++;
    }
    return refs;
  }

  bool fallsInto(size_t label) const {
    for (size_t j = label; j-- > 0;) {
      if (!alive_[j]) continue;
      const Line& l = info_[j];
      if (l.kind == Kind::Skip && !l.stmt) continue;
      if (l.kind == Kind::Label || l.stmt) return true;
      if (l.kind != Kind::Ins) return true;
      return !(l.flow && (l.mnem == "JMP" || l.mnem == "RET" || l.mnem == "HLT"));
    }
    return true;
  }

  std::string labelName(size_t i) const {
    const std::string t = trim(lines_[i]);
    return t.substr(0, t.size() - 1);
  }

  // What A is known to equal: loads that load what A already holds, and
  // stores that store what the byte already holds.
  bool knownValues() {
    bool changed = false;
    std::vector<std::string> known;
    // Z and N describe the value in A: set by the last load into A or the
    // last ALU result. A reload then changes nothing, flags included.
    bool flagsMatchA = false;
    auto has = [&](const std::string& k) { return std::find(known.begin(), known.end(), k) != known.end(); };
    auto dropIf = [&](auto pred) { known.erase(std::remove_if(known.begin(), known.end(), pred), known.end()); };
    const std::map<std::string, int> refs = refCounts();
    std::map<std::string, std::pair<std::vector<std::string>, bool>> atLabel;
    for (size_t i = 0; i < lines_.size(); i++) {
      if (!alive_[i]) continue;
      const Line& l = info_[i];
      // A statement starts with no temp live, and the dead store rule
      // relies on that, so what A shares with a temp is forgotten there.
      if (l.stmt) dropIf([](const std::string& k) { return isTempBase(baseOf(k)); });
      if (l.kind == Kind::Skip) continue;
      if (l.kind == Kind::Label) {
        const auto carried = atLabel.find(labelName(i));
        if (carried != atLabel.end() && !fallsInto(i)) {
          known = carried->second.first;
          flagsMatchA = carried->second.second;
        } else {
          known.clear();
          flagsMatchA = false;
        }
        continue;
      }
      if (l.kind != Kind::Ins) {
        known.clear();
        flagsMatchA = false;
        continue;
      }
      if (l.flow && isJump(l.mnem)) {
        const auto r = refs.find(l.src);
        if (r != refs.end() && r->second == 1) atLabel[l.src] = {known, flagsMatchA};
      }
      if (l.mnem == "LD" && l.dst == "A") {
        const std::string k = keyOf(l.src);
        if (!k.empty() && has(k) && (flagsMatchA || flagsDead(i))) {
          alive_[i] = false;
          changed = true;
          continue;
        }
        known.clear();
        if (!k.empty()) known.push_back(k);
        flagsMatchA = true;
      } else if (!l.byteStore.empty()) {
        const std::string k = keyOf(l.byteStore);
        if (!k.empty() && has(k)) {
          alive_[i] = false;
          changed = true;
          continue;
        }
        if (!k.empty()) known.push_back(k);
      } else {
        // A conditional jump not taken changes nothing. Any other change of
        // flow leaves nothing known.
        const bool conditional = l.flow && isJump(l.mnem) && l.mnem != "JMP";
        if (l.flow && !conditional) {
          known.clear();
          flagsMatchA = false;
        }
        if (l.writesA) known.clear();
        if (l.writesA || l.setsZ || l.setsN) flagsMatchA = l.writesA && l.setsZ && l.setsN;
        if (!l.wordStore.empty()) {
          const std::string base = baseOf(l.wordStore);
          if (isTempBase(base) || (keyOf(l.wordStore) == l.wordStore && base.rfind("D3", 0) != 0)) {
            dropIf([&](const std::string& k) { return baseOf(k) == base; });
          } else {
            dropIf([](const std::string& k) { return k[0] == '[' && !isTempBase(baseOf(k)); });
          }
        }
        if (l.devices) dropIf([](const std::string& k) { return k[0] == '['; });
      }
      if (l.writesD1) dropIf([](const std::string& k) { return k.find("D1") != std::string::npos; });
      if (l.writesD3) dropIf([](const std::string& k) { return k.find("D3") != std::string::npos; });
    }
    return changed;
  }

  // Z alone: a load into D2 sets nothing else.
  bool zDead(size_t i, int hops = 4) const {
    for (size_t j = next(i); j < lines_.size(); j = next(j)) {
      const Line& l = info_[j];
      if (l.stmt) return true;
      if (l.kind == Kind::Label) {
        if (lines_[j].find("_ovf") != std::string::npos) return false;
        continue;
      }
      if (l.kind != Kind::Ins) return false;
      if (l.flow) {
        if (l.mnem == "JSR" || l.mnem == "RET") return true;
        if (l.mnem != "JMP" || hops == 0) return false;
        const auto target = labels_.find(l.src);
        return target != labels_.end() && target->second > 0 && zDead(target->second - 1, hops - 1);
      }
      if (l.setsZ) return true;
    }
    return false;
  }

  // The same for D2 and the words it was loaded from or stored to.
  bool knownWords() {
    bool changed = false;
    std::vector<std::string> known;
    auto has = [&](const std::string& k) { return std::find(known.begin(), known.end(), k) != known.end(); };
    auto dropIf = [&](auto pred) { known.erase(std::remove_if(known.begin(), known.end(), pred), known.end()); };
    auto wordKey = [](const std::string& operand) -> std::string {
      if (operand.empty()) return "";
      if (operand.front() != '[') {
        // An address add sets C as well as Z, and a check reads it, so it is
        // never taken for a known value.
        if (operand.find("D1") != std::string::npos || operand.find("D2") != std::string::npos ||
            operand.find("D3") != std::string::npos) {
          return "";
        }
        return "#" + operand;
      }
      return keyOf(operand);
    };
    // A write to memory at spelling x: the words it may overlap go.
    auto clobber = [&](const std::string& x) {
      const std::string base = baseOf(x);
      if (x.find("D2") != std::string::npos || x.find("[A]") != std::string::npos) {
        dropIf([](const std::string& k) { return k[0] == '[' && !isTempBase(baseOf(k)); });
      } else if (base.rfind("D1", 0) == 0) {
        // Through a pointer: any byte but a temp may be the one.
        dropIf([](const std::string& k) { return k[0] == '[' && !isTempBase(baseOf(k)); });
      } else if (base.rfind("D3", 0) == 0) {
        dropIf([](const std::string& k) { return k.find("D3") != std::string::npos; });
      } else {
        dropIf([&](const std::string& k) { return baseOf(k) == base; });
      }
    };
    const std::map<std::string, int> refs = refCounts();
    std::map<std::string, std::vector<std::string>> atLabel;
    for (size_t i = 0; i < lines_.size(); i++) {
      if (!alive_[i]) continue;
      const Line& l = info_[i];
      if (l.stmt) dropIf([](const std::string& k) { return isTempBase(baseOf(k)); });
      if (l.kind == Kind::Skip) continue;
      if (l.kind == Kind::Label) {
        const auto carried = atLabel.find(labelName(i));
        if (carried != atLabel.end() && !fallsInto(i)) known = carried->second;
        else known.clear();
        continue;
      }
      if (l.kind != Kind::Ins) {
        known.clear();
        continue;
      }
      if (l.flow && isJump(l.mnem)) {
        const auto r = refs.find(l.src);
        if (r != refs.end() && r->second == 1) atLabel[l.src] = known;
      }
      if (l.mnem == "LD" && l.dst == "D2") {
        const std::string k = wordKey(l.src);
        if (!k.empty() && has(k) && zDead(i)) {
          alive_[i] = false;
          changed = true;
          continue;
        }
        known.clear();
        if (!k.empty()) known.push_back(k);
        continue;
      }
      if (!l.wordStore.empty() && l.src == "D2") {
        const std::string k = keyOf(l.wordStore);
        if (!k.empty() && has(k)) {
          alive_[i] = false;
          changed = true;
          continue;
        }
        clobber(l.wordStore);
        if (!k.empty()) known.push_back(k);
        continue;
      }
      const bool conditional = l.flow && isJump(l.mnem) && l.mnem != "JMP";
      if ((l.flow && !conditional) || l.writesD2) known.clear();
      if (!l.byteStore.empty()) clobber(l.byteStore);
      if (!l.wordStore.empty()) clobber(l.wordStore);
      if (l.devices) dropIf([](const std::string& k) { return k[0] == '['; });
      if (l.writesD1) dropIf([](const std::string& k) { return k.find("D1") != std::string::npos; });
      if (l.writesD3) dropIf([](const std::string& k) { return k.find("D3") != std::string::npos; });
    }
    return changed;
  }

  // A store to a temp that no path reads: every path stores to it again,
  // reaches the start of a statement, where no temp is live, or returns.
  bool deadTempStores() {
    bool changed = false;
    for (size_t i = 0; i < lines_.size(); i++) {
      if (!alive_[i] || info_[i].kind != Kind::Ins) continue;
      const Line& l = info_[i];
      const std::string target = !l.byteStore.empty() ? l.byteStore : l.wordStore;
      const std::string base = baseOf(target);
      if (target.empty() || !isTempBase(base)) continue;
      int budget = 64;
      if (unread(next(i), base, bytesOf(l, base), budget)) {
        alive_[i] = false;
        changed = true;
      }
    }
    return changed;
  }

  // Which bytes of the temp a line stores: 1 the high, 2 the low, 3 both.
  static int bytesOf(const Line& x, const std::string& base) {
    const std::string t = !x.byteStore.empty() ? x.byteStore : x.wordStore;
    if (t.empty() || baseOf(t) != base) return 0;
    if (!x.wordStore.empty()) return 3;
    return t.find("+1]") != std::string::npos ? 2 : 1;
  }

  // True when, from line j on, the pending bytes are written or dead on
  // every path before any read. The budget bounds the paths followed.
  bool unread(size_t j, const std::string& base, int pending, int& budget) const {
    for (; j < lines_.size(); j = next(j)) {
      if (--budget < 0) return false;
      const Line& x = info_[j];
      if (x.stmt) return true;
      if (x.kind == Kind::Label || x.kind == Kind::Skip) continue;
      if (x.kind != Kind::Ins || x.devices) return false;
      if (x.flow) {
        if (x.mnem == "RET") return true;
        if (x.mnem == "HLT") return false;
        // A callee writes temps before it reads them, and a caller's live
        // temps are saved in its frame before the call, which reads them.
        if (x.mnem == "JSR") continue;
        // The overflow stop reads no temp: it prints and halts.
        if (x.src == "__stack_overflow") continue;
        const auto target = labels_.find(x.src);
        if (target == labels_.end() || !unread(target->second, base, pending, budget)) return false;
        if (x.mnem == "JMP") return true;
        continue;
      }
      if (mentions(x.src, base)) return false;
      if (x.byteStore.empty() && x.wordStore.empty() && mentions(x.dst, base)) return false;
      pending &= ~bytesOf(x, base);
      if (pending == 0) return true;
    }
    return false;
  }

  // A load into A that nothing reads before A is loaded again.
  bool deadLoads() {
    bool changed = false;
    for (size_t i = 0; i < lines_.size(); i++) {
      if (!alive_[i] || info_[i].kind != Kind::Ins) continue;
      const Line& l = info_[i];
      if (l.mnem != "LD" || l.dst != "A" || l.readsA || l.writesD1 || l.writesD2) continue;
      for (size_t j = next(i); j < lines_.size(); j = next(j)) {
        const Line& x = info_[j];
        if (x.kind != Kind::Ins || x.flow || x.readsA) break;
        if (x.writesA) {
          if (flagsDead(i)) {
            alive_[i] = false;
            changed = true;
          }
          break;
        }
      }
    }
    return changed;
  }
};

}  // namespace

std::vector<std::string> peephole(const std::vector<std::string>& lines, std::vector<int>* remap) {
  std::vector<std::string> cur = lines;
  std::vector<int> origin(lines.size());
  for (size_t i = 0; i < lines.size(); i++) origin[i] = static_cast<int>(i);
  // The end of the text ends every statement.
  cur.push_back(STMT_MARK);
  origin.push_back(static_cast<int>(lines.size()));
  for (int round = 0; round < 16; round++) {
    Pass pass(cur);
    if (!pass.run()) break;
    const std::vector<bool> alive = pass.alive();
    const std::map<size_t, std::string>& rewritten = pass.rewritten();
    std::vector<std::string> kept;
    std::vector<int> keptOrigin;
    for (size_t i = 0; i < cur.size(); i++) {
      if (!alive[i]) continue;
      const auto r = rewritten.find(i);
      kept.push_back(r == rewritten.end() ? cur[i] : r->second);
      keptOrigin.push_back(origin[i]);
    }
    cur = std::move(kept);
    origin = std::move(keptOrigin);
  }
  // The markers were for this pass alone.
  {
    std::vector<std::string> kept;
    std::vector<int> keptOrigin;
    for (size_t i = 0; i < cur.size(); i++) {
      if (trim(cur[i]) == STMT_MARK || trim(cur[i]) == BARRIER_MARK) continue;
      kept.push_back(cur[i]);
      keptOrigin.push_back(origin[i]);
    }
    cur = std::move(kept);
    origin = std::move(keptOrigin);
  }
  if (remap) {
    remap->assign(lines.size() + 1, static_cast<int>(cur.size()));
    size_t k = cur.size();
    for (size_t i = lines.size() + 1; i-- > 0;) {
      while (k > 0 && origin[k - 1] >= static_cast<int>(i)) k--;
      (*remap)[i] = static_cast<int>(k);
    }
  }
  return cur;
}

}  // namespace sc8::cc
