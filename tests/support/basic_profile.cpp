// basic_profile: where the BASIC interpreter spends its cycles.
//
// The measurements in docs/design/basic-speed.md come from this program.
// It boots basic.rom on the tests' headless Session with the optimal
// microcode and stores a program with basic::storeProgram. Then it types
// RUN and steps the machine one instruction at a time. It counts from the
// first instruction of rt_run to the RET that leaves it.
//
// Each instruction's cycles go to the C function whose code holds its slot,
// the function's own cycles. A shadow call stack, pushed on JSR and popped
// on RET, gives each function's cycles with its callees and splits a
// callee's cycles by caller. The labels come from assembling the build's
// basic.asm, whose program must equal the ROM's slot for slot.
//
//   basic_profile                  The statement table of the note.
//   basic_profile --detail         Each row's cycles by function as well.
//   basic_profile --tokens STMT    lx_next per token, STMT in the loop.
//   basic_profile --pair A B       Program file A over program file B.
//                                  Each loops 1000 times. Cycles a pass.
//   basic_profile --asm PATH       Another basic.asm, before any of those.
//
// A statement's cost is the difference between the program with it and the
// same program without it, divided by the 1000 passes of the loop.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "asm/asm.h"
#include "basic/program.h"
#include "core/isa.h"
#include "support/basic_session.h"

using namespace sc8;

namespace {

struct Fn {
  int slot;
  std::string name;
};

// The interpreter's functions by first slot, in slot order.
std::vector<Fn> fns;
// Slot to the C source line its block came from, as "run.c:512".
std::vector<int> slotSrc;
std::vector<std::string> srcNames;
// Slot tags inside lx_next. 1 is the 16 bit sum that forms &lx_text[i]. 2 is
// an inlined test's answer stored as 0 or 1 and tested again.
std::vector<int> slotTag;
int fnLxNext = -1;
int rtRun = -1;
// A RAM variable's address and width in bytes.
struct Var {
  int at = 0;
  int width = 2;
};
Var varLxText, varLxPos, varLxTokpos;

uint8_t opJsr, opJsrD1, opJsrD2, opRet;

int fnOf(int slot) {
  const auto it = std::upper_bound(fns.begin(), fns.end(), slot, [](int s, const Fn& f) { return s < f.slot; });
  return static_cast<int>(it - fns.begin()) - 1;
}

struct Prof {
  uint64_t total = 0;
  std::vector<uint64_t> excl, incl, calls;
  std::map<int, uint64_t> bySrc;
  // A callee's own cycles split by its caller.
  std::map<std::pair<int, int>, uint64_t> byCaller;
  bool ok = true;
  int err = 0;
};

// What --tokens gathers: per token text, the calls of lx_next and their
// cycles, and the cycles of the tagged patterns.
std::map<std::string, std::pair<uint64_t, uint64_t>> perToken;
uint64_t tagCycles[3], tagCount[3], allTag[3], allTotal;

int readVar(const Machine& m, const Var& v) {
  if (v.width == 1) return m.ram[static_cast<size_t>(v.at)];
  return (m.ram[static_cast<size_t>(v.at)] << 8) | m.ram[static_cast<size_t>(v.at) + 1];
}

// A stored line's bytes as a person reads them. A byte below 32 or above
// 127 shows as its number in angle brackets.
std::string shown(const Machine& m, int from, int to) {
  std::string t;
  for (int i = from; i < to && i < 65536; i++) {
    const uint8_t c = m.ram[static_cast<size_t>(i)];
    if (c >= 32 && c < 128) t += static_cast<char>(c);
    else t += "<" + std::to_string(c) + ">";
  }
  return t;
}

Prof runProgram(const std::string& text, bool record = false) {
  testing::Session s;
  s.load();
  s.runBudget(4000000);
  basic::storeProgram(std::span<uint8_t>(s.m->ram.data(), s.m->ram.size()), basic::encodeProgram(text));
  for (char c : std::string("RUN")) {
    s.pushKey(c, false);
    s.runBudget(120000);
  }
  s.pushKey(13, false);
  Prof p;
  p.excl.assign(fns.size(), 0);
  p.incl.assign(fns.size(), 0);
  p.calls.assign(fns.size(), 0);
  std::vector<int> stack;
  std::vector<uint64_t> startAt;
  std::vector<uint32_t> mark(fns.size(), 0);
  uint32_t epoch = 0;
  bool active = false;
  Machine& m = *s.m;
  for (uint64_t n = 0; n < 2000000000ull && m.status == Status::Running; n++) {
    const uint16_t pc = m.pc;
    const Instr in = m.program[pc];
    if (!active && pc == rtRun) {
      active = true;
      stack = {fnOf(pc)};
      startAt = {m.cycles};
      p.calls[static_cast<size_t>(stack[0])]++;
    }
    const uint64_t c0 = m.cycles;
    m.instructionStep();
    if (!active) continue;
    const uint64_t dc = m.cycles - c0;
    const int f0 = fnOf(pc);
    p.total += dc;
    p.excl[static_cast<size_t>(f0)] += dc;
    if (record) {
      allTag[slotTag[pc]] += dc;
      allTotal += dc;
      if (f0 == fnLxNext) {
        tagCycles[slotTag[pc]] += dc;
        tagCount[slotTag[pc]]++;
      }
    }
    p.bySrc[slotSrc[pc]] += dc;
    p.byCaller[{f0, stack.size() >= 2 ? stack[stack.size() - 2] : -1}] += dc;
    epoch++;
    for (int f : stack) {
      if (mark[static_cast<size_t>(f)] == epoch) continue;
      mark[static_cast<size_t>(f)] = epoch;
      p.incl[static_cast<size_t>(f)] += dc;
    }
    if (in.op == opJsr || in.op == opJsrD1 || in.op == opJsrD2) {
      const int f = fnOf(m.pc);
      stack.push_back(f);
      startAt.push_back(m.cycles);
      p.calls[static_cast<size_t>(f)]++;
    } else if (in.op == opRet) {
      const int f = stack.back();
      const uint64_t cost = m.cycles - startAt.back();
      stack.pop_back();
      startAt.pop_back();
      if (record && f == fnLxNext) {
        const int line = readVar(m, varLxText);
        std::string tok = shown(m, line + readVar(m, varLxTokpos), line + readVar(m, varLxPos));
        if (tok.empty()) tok = "<end>";
        perToken[tok].first++;
        perToken[tok].second += cost;
      }
      if (stack.empty()) break;
    }
  }
  p.err = m.ram[0x12];
  p.ok = m.status == Status::Running && p.err == 0;
  return p;
}

std::string readFile(const std::string& path) {
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::string trim(const std::string& t) {
  const size_t a0 = t.find_first_not_of(" \t");
  return a0 == std::string::npos ? std::string() : t.substr(a0);
}

// The labels, the source line of every slot and lx_next's tags, from the
// assembly. Returns false when it does not match the ROM.
bool readAssembly(const std::string& asmPath) {
  const std::string source = readFile(asmPath);
  const Assembled a = assemble(source);
  if (!a.errors.empty()) {
    std::printf("%s does not assemble\n", asmPath.c_str());
    return false;
  }
  {
    testing::Session s;
    if (s.cart.program != a.program) {
      std::printf("%s does not match basic.rom\n", asmPath.c_str());
      return false;
    }
  }
  std::vector<std::string> lines;
  std::stringstream ss(source);
  for (std::string l; std::getline(ss, l);) lines.push_back(l);
  // Assembly line to C line, from the file.c:N comment over each block.
  std::vector<int> lineSrc(lines.size() + 2, -1);
  std::map<std::string, int> ids;
  int cur = -1;
  for (size_t i = 0; i < lines.size(); i++) {
    const std::string& l = lines[i];
    if (l.size() > 2 && l[0] == ';' && l.find(".c:") != std::string::npos && l.find(' ', 2) == std::string::npos) {
      const std::string k = l.substr(2);
      if (!ids.count(k)) {
        ids[k] = static_cast<int>(srcNames.size());
        srcNames.push_back(k);
      }
      cur = ids[k];
    }
    lineSrc[i + 1] = cur;
  }
  srcNames.push_back("?");
  // The two patterns proposal 3 of the note names, inside lx_next.
  std::vector<int> lineTag(lines.size() + 2, 0);
  bool in = false;
  for (size_t i = 0; i + 1 < lines.size(); i++) {
    const std::string t = trim(lines[i]);
    const std::string t1 = trim(lines[i + 1]);
    if (t == "lx_next:") in = true;
    if (t == "lx_next__end:") in = false;
    if (in && t == "LD A <- [lx_text+1]" && i + 6 < lines.size()) {
      for (size_t k = 0; k < 7; k++) lineTag[i + k + 1] = 1;
    }
    // "LD A <- 0" then "JMP __Ln_tdone": the false arm of a test's answer.
    if (t == "LD A <- 0" && t1.find("_tdone") != std::string::npos && t1.rfind("JMP", 0) == 0) {
      lineTag[i + 1] = 2;
      lineTag[i + 2] = 2;
    }
    if (t.find("_true:") != std::string::npos && t1 == "LD A <- 1") lineTag[i + 2] = 2;
    if (t.find("_tdone:") != std::string::npos && t1 == "LD [__t0+1] <- A") lineTag[i + 2] = 2;
    if (t.find("_inl_") != std::string::npos && t1 == "LD A <- [__t0+1]" && i + 2 < lines.size()) {
      lineTag[i + 2] = 2;
      lineTag[i + 3] = 2;
    }
  }
  slotTag.assign(a.program.size(), 0);
  slotSrc.assign(a.program.size(), static_cast<int>(srcNames.size()) - 1);
  for (size_t i = 0; i < a.instrToLine.size() && i < a.program.size(); i++) {
    const int ln = a.instrToLine[i];
    if (ln >= 0 && ln < static_cast<int>(lineTag.size())) slotTag[i] = lineTag[static_cast<size_t>(ln)];
    if (ln >= 0 && ln < static_cast<int>(lineSrc.size()) && lineSrc[static_cast<size_t>(ln)] >= 0) {
      slotSrc[i] = lineSrc[static_cast<size_t>(ln)];
    }
  }
  std::map<std::string, int> code;
  std::vector<std::pair<int, std::string>> ram;
  for (const auto& [name, l] : a.labels) {
    if (l.kind == Label::Kind::Code) code[name] = l.value;
    if (l.kind == Label::Kind::Ram) ram.push_back({l.value, name});
  }
  for (const auto& [name, v] : code) {
    const bool fn = code.count(name + "__end") || name == "__mul16" || name == "__udiv16" || name == "__urem16" ||
                    name == "__sdiv16" || name == "__srem16" || name == "__start" || name == "__stack_overflow";
    if (fn) fns.push_back({v, name});
  }
  std::sort(fns.begin(), fns.end(), [](const Fn& x, const Fn& y) { return x.slot < y.slot; });
  rtRun = code.at("rt_run");
  for (size_t i = 0; i < fns.size(); i++) {
    if (fns[i].name == "lx_next") fnLxNext = static_cast<int>(i);
  }
  // A variable's width is the distance to the next RAM label.
  std::sort(ram.begin(), ram.end());
  auto var = [&](const std::string& name) {
    Var v;
    for (size_t i = 0; i < ram.size(); i++) {
      if (ram[i].second != name) continue;
      v.at = ram[i].first;
      v.width = i + 1 < ram.size() && ram[i + 1].first - ram[i].first == 1 ? 1 : 2;
    }
    return v;
  };
  varLxText = var("lx_text");
  varLxPos = var("lx_pos");
  varLxTokpos = var("lx_tokpos");
  return true;
}

struct Case {
  std::string name, prog, base;
};

// Lines numbered 1, 2, 3 and on from a list of bodies.
std::string numbered(const std::vector<std::string>& bodies) {
  std::string t;
  for (size_t i = 0; i < bodies.size(); i++) t += std::to_string(i + 1) + " " + bodies[i] + "\n";
  return t;
}

std::vector<std::string> fillTo(std::vector<std::string> b, size_t n) {
  while (b.size() < n) b.push_back("REM FILLER LINE");
  return b;
}

const std::string FOR = "FOR I=1 TO 1000";
const std::string NEXT = "NEXT I";

// The note's loop with one statement on line 3.
std::string body(const std::string& st, const std::string& setup = "B=7:C=9") {
  return numbered({setup, FOR, st, NEXT, "END"});
}

// The rows of the note's statement table, in its order.
std::vector<Case> cases() {
  const std::string small = numbered({"B=7:C=9", FOR, NEXT, "END"});
  // 300 lines: the FOR on line 2, the statement on 3, the NEXT on 202.
  auto far = [&](const std::string& st) {
    std::vector<std::string> b = fillTo({"B=7:C=9", FOR, st}, 201);
    b.push_back(NEXT);
    b.push_back("END");
    return numbered(fillTo(b, 300));
  };
  const std::string farBase = numbered(fillTo({"B=7:C=9", FOR, NEXT, "END"}, 300));
  // GOSUB: the routine on line 2 or on line 300 of 300.
  auto gosub = [&](int target, bool with) {
    std::vector<std::string> b = {"GOTO 3", "RETURN", FOR};
    if (with) b.push_back("GOSUB " + std::to_string(target));
    b.push_back(NEXT);
    b.push_back("END");
    b = fillTo(b, 299);
    b.push_back("RETURN");
    return numbered(b);
  };
  // A GOTO to the line below at the bottom of 300 lines.
  auto bottom = [&](bool with) {
    std::vector<std::string> b = fillTo({"GOTO 296"}, 295);
    b.push_back(FOR);
    if (with) b.push_back("GOTO 298");
    b.push_back(NEXT);
    b.push_back("END");
    return numbered(fillTo(b, 300));
  };
  return {
      {"A = A + 1", body("A = A + 1"), small},
      {"A=A+1", body("A=A+1"), small},
      {"A = B * 3 + C", body("A = B * 3 + C"), small},
      {"POKE 61440, A", body("POKE 61440, A"), small},
      {"IF A > 5 THEN B = 1, false", body("IF A > 5 THEN B = 1"), small},
      {"IF A > 5 THEN B = 1, true", body("IF A > 5 THEN B = 1", "A=9:B=7:C=9"),
       numbered({"A=9:B=7:C=9", FOR, NEXT, "END"})},
      {"GOTO the line below, 3 to 4 of 5", body("GOTO 4"), small},
      {"GOTO 200 lines down, 3 to 202 of 300", far("GOTO 202"), farBase},
      {"GOTO the line below, 297 to 298 of 300", bottom(true), bottom(false)},
      {"GOSUB and RETURN, routine on line 2", gosub(2, true), gosub(2, false)},
      {"GOSUB and RETURN, routine on line 300", gosub(300, true), gosub(300, false)},
      {"A = PEEK(61440)", body("A = PEEK(61440)"), small},
      {"FOR/NEXT on two lines, one pass", small, numbered({"B=7:C=9", "END"})},
      {"FOR I=1 TO 1000:NEXT, one pass", numbered({"B=7:C=9", "FOR I=1 TO 1000:NEXT", "END"}),
       numbered({"B=7:C=9", "END"})},
  };
}

double perPass(uint64_t with, uint64_t without) {
  return (static_cast<double>(with) - static_cast<double>(without)) / 1000.0;
}

void printRow(const std::string& name, const Prof& p, const Prof& b) {
  const double per = perPass(p.total, b.total);
  std::printf("%-44s %10.1f cycles  %6.2f in a frame%s\n", name.c_str(), per, 65536.0 / per,
              p.ok && b.ok ? "" : ("  ERROR " + std::to_string(p.ok ? b.err : p.err)).c_str());
}

void printDetail(const Prof& p, const Prof& b) {
  std::vector<size_t> idx(fns.size());
  for (size_t i = 0; i < fns.size(); i++) idx[i] = i;
  auto excl = [&](size_t i) { return perPass(p.excl[i], b.excl[i]); };
  std::sort(idx.begin(), idx.end(), [&](size_t x, size_t y) { return excl(x) > excl(y); });
  for (size_t i : idx) {
    const double ex = excl(i);
    const double inc = perPass(p.incl[i], b.incl[i]);
    const double calls = perPass(p.calls[i], b.calls[i]);
    if (ex == 0 && inc == 0 && calls == 0) continue;
    std::printf("    %-28s own %9.1f  with callees %9.1f  calls %7.2f\n", fns[i].name.c_str(), ex, inc, calls);
  }
  for (const auto& [k, v] : p.byCaller) {
    const auto it = b.byCaller.find(k);
    const double d = perPass(v, it == b.byCaller.end() ? 0 : it->second);
    const std::string& fn = fns[static_cast<size_t>(k.first)].name;
    if (d < 20 || (fn != "lx_is" && fn != "lx_next")) continue;
    std::printf("      %s from %s: %.1f\n", fn.c_str(), k.second < 0 ? "-" : fns[static_cast<size_t>(k.second)].name.c_str(), d);
  }
  std::vector<std::pair<double, int>> src;
  for (const auto& [k, v] : p.bySrc) {
    const auto it = b.bySrc.find(k);
    const double d = perPass(v, it == b.bySrc.end() ? 0 : it->second);
    if (d >= 30) src.push_back({d, k});
  }
  std::sort(src.rbegin(), src.rend());
  for (const auto& [d, k] : src) std::printf("        %-14s %8.1f\n", srcNames[static_cast<size_t>(k)].c_str(), d);
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  std::string asmPath = SC8_BASIC_ASM_PATH;
  if (args.size() >= 2 && args[0] == "--asm") {
    asmPath = args[1];
    args.erase(args.begin(), args.begin() + 2);
  }
  opJsr = opByName("JSR")->op;
  opJsrD1 = opByName("JSR D1")->op;
  opJsrD2 = opByName("JSR D2")->op;
  opRet = opByName("RET")->op;
  if (!readAssembly(asmPath)) return 1;

  if (args.size() == 2 && args[0] == "--tokens") {
    runProgram(body(args[1]), true);
    for (const auto& [t, v] : perToken) {
      if (v.first >= 900) {
        std::printf("  [%s] calls %llu  %.1f cycles each\n", t.c_str(), static_cast<unsigned long long>(v.first),
                    static_cast<double>(v.second) / static_cast<double>(v.first));
      }
    }
    std::printf("  lx_next: address sums %llu cycles in %llu instructions, stored tests %llu in %llu, rest %llu\n",
                static_cast<unsigned long long>(tagCycles[1]), static_cast<unsigned long long>(tagCount[1]),
                static_cast<unsigned long long>(tagCycles[2]), static_cast<unsigned long long>(tagCount[2]),
                static_cast<unsigned long long>(tagCycles[0]));
    std::printf("  whole run: %llu cycles, stored tests %llu, lexer address sums %llu\n",
                static_cast<unsigned long long>(allTotal), static_cast<unsigned long long>(allTag[2]),
                static_cast<unsigned long long>(allTag[1]));
    return 0;
  }
  if (args.size() >= 3 && args[0] == "--pair") {
    const Prof p = runProgram(readFile(args[1]));
    const Prof b = runProgram(readFile(args[2]));
    printRow(args[1], p, b);
    if (args.size() > 3 && args[3] == "--detail") printDetail(p, b);
    return 0;
  }
  const bool detail = !args.empty() && args[0] == "--detail";
  if (!args.empty() && !detail) {
    std::printf("usage: basic_profile [--asm PATH] [--detail | --tokens STATEMENT | --pair WITH WITHOUT [--detail]]\n");
    return 2;
  }
  for (const Case& c : cases()) {
    const Prof p = runProgram(c.prog);
    const Prof b = runProgram(c.base);
    printRow(c.name, p, b);
    if (detail) printDetail(p, b);
  }
  return 0;
}
