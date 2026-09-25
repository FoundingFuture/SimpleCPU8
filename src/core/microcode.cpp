#include "core/microcode.h"

#include <algorithm>

namespace sc8 {

void Microcode::set(std::string name, Rows rows) {
  for (Section& s : sections_) {
    if (s.name == name) {
      s.rows = std::move(rows);
      return;
    }
  }
  sections_.push_back({std::move(name), std::move(rows)});
}

bool Microcode::erase(std::string_view name) {
  auto it = std::find_if(sections_.begin(), sections_.end(),
                         [&](const Section& s) { return s.name == name; });
  if (it == sections_.end()) return false;
  sections_.erase(it);
  return true;
}

const Microcode::Section* Microcode::find(std::string_view name) const {
  for (const Section& s : sections_) {
    if (s.name == name) return &s;
  }
  return nullptr;
}

const Rows* Microcode::get(std::string_view name) const {
  const Section* s = find(name);
  return s ? &s->rows : nullptr;
}

// The two shipped sets are generated from family templates because the D1
// and D2 variants mirror each other.
namespace {

using S = Signal;

Row cat(Row a, std::initializer_list<Signal> more) {
  a.insert(a.end(), more);
  return a;
}

Row aluPassLoad() { return {S::ALU_PASS_B, S::ACC_LOAD_ALU}; }
Row aluPassFlags() { return {S::ALU_PASS_B, S::FLAGS_LOAD}; }

struct Regs {
  Signal ramToH, ramToL, ramWriteH, ramWriteL, tstz, inc, loadOp16;
  Signal addr, stkWriteH, stkWriteL, stkToH, stkToL, ioReadH, ioReadL;
};

constexpr Regs D1{S::RAM_TO_D1H,   S::RAM_TO_D1L,   S::RAM_WRITE_D1H, S::RAM_WRITE_D1L,
                  S::D1_TSTZ,      S::D1_INC,       S::D1_LOAD_OP16,  S::ADDR_D1,
                  S::STK_WRITE_D1H, S::STK_WRITE_D1L, S::STK_TO_D1H,  S::STK_TO_D1L,
                  S::IO_READ_D1H,  S::IO_READ_D1L};
constexpr Regs D2{S::RAM_TO_D2H,   S::RAM_TO_D2L,   S::RAM_WRITE_D2H, S::RAM_WRITE_D2L,
                  S::D2_TSTZ,      S::D2_INC,       S::D2_LOAD_OP16,  S::ADDR_D2,
                  S::STK_WRITE_D2H, S::STK_WRITE_D2L, S::STK_TO_D2H,  S::STK_TO_D2L,
                  S::IO_READ_D2H,  S::IO_READ_D2L};
// D3 has no step, stack or port forms. Those fields repeat D3_TSTZ and are
// never read for it.
constexpr Regs D3{S::RAM_TO_D3H,  S::RAM_TO_D3L,  S::RAM_WRITE_D3H, S::RAM_WRITE_D3L,
                  S::D3_TSTZ,     S::D3_TSTZ,     S::D3_TSTZ,       S::ADDR_D3,
                  S::D3_TSTZ,     S::D3_TSTZ,     S::D3_TSTZ,       S::D3_TSTZ,
                  S::D3_TSTZ,     S::D3_TSTZ};

// JSR's own rows with the target signal swapped in for PC_LOAD. The optimal
// fetch has already stepped PC, so only the naive form has to step it first
// to push PC+1.
Rows indirectCall(Signal load, bool fetchStepsPc) {
  if (fetchStepsPc) {
    return {{S::STK_WRITE_PCH, S::SP_DEC}, {S::STK_WRITE_PCL, S::SP_DEC, load}};
  }
  return {{S::PC_INC}, {S::STK_WRITE_PCH}, {S::SP_DEC}, {S::STK_WRITE_PCL}, {S::SP_DEC}, {load}};
}

Rows naiveByteLoad(Row source, std::optional<Signal> step = std::nullopt) {
  Rows rows{source};
  if (step) rows.push_back({*step});
  rows.push_back(aluPassLoad());
  rows.push_back(aluPassFlags());
  rows.push_back({S::PC_INC});
  return rows;
}

Rows optimalByteLoad(Row source, std::optional<Signal> step = std::nullopt) {
  if (step) source.push_back(*step);
  return {source, {S::ALU_PASS_B, S::ACC_LOAD_ALU, S::FLAGS_LOAD}};
}

// Two byte reads, big-endian. The stepped variant advances the pointer
// between reads. The plain variant reaches the next byte with EA_CIN.
Rows naiveWordLoad(const Regs& x, Row base, std::optional<Signal> step = std::nullopt) {
  Row hi = cat({x.ramToH}, {});
  hi.insert(hi.end(), base.begin(), base.end());
  Row lo = {x.ramToL};
  lo.insert(lo.end(), base.begin(), base.end());
  if (!step) lo.push_back(S::EA_CIN);
  Rows rows{hi};
  if (step) rows.push_back({*step});
  rows.push_back(lo);
  if (step) rows.push_back({*step});
  rows.push_back({x.tstz});
  rows.push_back({S::PC_INC});
  return rows;
}

Rows optimalWordLoad(const Regs& x, Row base, std::optional<Signal> step = std::nullopt) {
  Row hi = {x.ramToH};
  hi.insert(hi.end(), base.begin(), base.end());
  Row lo = {x.ramToL};
  lo.insert(lo.end(), base.begin(), base.end());
  if (step) {
    hi.push_back(*step);
    lo.push_back(*step);
  } else {
    lo.push_back(S::EA_CIN);
  }
  return {hi, lo, {x.tstz}};
}

Rows wordStore(const Regs& x, Row base, bool naive) {
  Row hi = {x.ramWriteH};
  hi.insert(hi.end(), base.begin(), base.end());
  Row lo = {x.ramWriteL};
  lo.insert(lo.end(), base.begin(), base.end());
  lo.push_back(S::EA_CIN);
  Rows rows{hi, lo};
  if (naive) rows.push_back({S::PC_INC});
  return rows;
}

Rows naiveAlu(Signal op, Row source) {
  return {source, {S::ACC_TO_A}, {op, S::ACC_LOAD_ALU}, {op, S::FLAGS_LOAD}, {S::PC_INC}};
}

Rows optimalAlu(Signal op, Row source) {
  source.push_back(S::ACC_TO_A);
  return {source, {op, S::ACC_LOAD_ALU, S::FLAGS_LOAD}};
}

// CMP and TST: the operation's flags without its result.
Rows naiveTest(Signal op, Row source) {
  return {source, {S::ACC_TO_A}, {op, S::FLAGS_LOAD}, {S::PC_INC}};
}

Rows optimalTest(Signal op, Row source) {
  source.push_back(S::ACC_TO_A);
  return {source, {op, S::FLAGS_LOAD}};
}

struct Shift {
  std::string_view name;
  Signal select;
};

constexpr Shift SHIFTS[] = {
    {"SHL", S::ALU_SHL}, {"SHR", S::ALU_SHR}, {"ROL", S::ALU_ROL}, {"ROR", S::ALU_ROR}, {"ASR", S::ALU_ASR},
};

// The address arithmetic. dst gets base + n or base + A from the EA
// adder, and EA_FLAGS sets Z from the sum and C when it wrapped, in the
// same row.
Signal loadEa(int d) { return d == 1 ? S::D1_LOAD_EA : d == 2 ? S::D2_LOAD_EA : S::D3_LOAD_EA; }
Signal addrOf(int d) { return d == 1 ? S::ADDR_D1 : d == 2 ? S::ADDR_D2 : S::ADDR_D3; }

// The address sums that exist: every pair of D1 and D2, and D3 with each.
// Only D1 and D2 take +A.
struct EaForm {
  int dst, base;
  bool plusA;
};
std::vector<EaForm> eaForms() {
  std::vector<EaForm> v;
  for (int d = 1; d <= 2; d++) {
    for (int b = 1; b <= 2; b++) {
      v.push_back({d, b, false});
      v.push_back({d, b, true});
    }
  }
  v.push_back({3, 3, false});
  v.push_back({1, 3, false});
  v.push_back({2, 3, false});
  v.push_back({3, 1, false});
  v.push_back({3, 2, false});
  return v;
}

Row eaRow(const EaForm& f) {
  return {loadEa(f.dst), addrOf(f.base), f.plusA ? S::EA_OFF_A : S::EA_OFF_OP16, S::EA_FLAGS};
}
std::string eaName(const EaForm& f) {
  return "LD D" + std::to_string(f.dst) + " <- D" + std::to_string(f.base) + (f.plusA ? "+A" : "+n");
}

struct AluFamily {
  std::string_view name;
  Signal select;
};

constexpr AluFamily ALU_FAMILIES[] = {
    {"ADD", S::ALU_ADD}, {"SUB", S::ALU_SUB}, {"ADC", S::ALU_ADC}, {"SBC", S::ALU_SBC},
    {"AND", S::ALU_AND}, {"OR", S::ALU_OR},   {"XOR", S::ALU_XOR},
};

std::string dn(int x) { return "D" + std::to_string(x); }

// The frame forms of D3: bytes, words and the ALU at [D3+n], and D3 itself
// loaded and stored as a word.
void addD3(Microcode& m, bool naive) {
  const Row at = {S::ADDR_D3, S::EA_OFF_OP8};
  const Row byteSrc = cat(Row{S::RAM_TO_B}, {S::ADDR_D3, S::EA_OFF_OP8});
  m.set("LD A <- [D3+n]", naive ? naiveByteLoad(byteSrc) : optimalByteLoad(byteSrc));
  const Row store = cat(Row{S::RAM_WRITE_ACC}, {S::ADDR_D3, S::EA_OFF_OP8});
  m.set("LD [D3+n] <- A", naive ? Rows{store, {S::PC_INC}} : Rows{store});
  for (int xi = 1; xi <= 2; xi++) {
    const Regs& x = xi == 1 ? D1 : D2;
    m.set("LD " + dn(xi) + " <- [D3+n]", naive ? naiveWordLoad(x, at) : optimalWordLoad(x, at));
    m.set("LD [D3+n] <- " + dn(xi), wordStore(x, at, naive));
  }
  m.set("LD D3 <- [addr16]", naive ? naiveWordLoad(D3, {S::ADDR_OP16}) : optimalWordLoad(D3, {S::ADDR_OP16}));
  m.set("LD [addr16] <- D3", wordStore(D3, {S::ADDR_OP16}, naive));
  for (const AluFamily& f : ALU_FAMILIES) {
    m.set(std::string(f.name) + " A <- [D3+n]", naive ? naiveAlu(f.select, byteSrc) : optimalAlu(f.select, byteSrc));
  }
  m.set("CMP A, [D3+n]", naive ? naiveTest(S::ALU_SUB, byteSrc) : optimalTest(S::ALU_SUB, byteSrc));
  m.set("TST A, [D3+n]", naive ? naiveTest(S::ALU_AND, byteSrc) : optimalTest(S::ALU_AND, byteSrc));
}

}  // namespace

Microcode buildNaive() {
  Microcode m;
  m.set("fetch", {{S::FETCH}});
  m.set("NOP", {{S::PC_INC}});
  m.set("HLT", {{S::HALT}});

  m.set("JMP", {{S::PC_LOAD}});
  m.set("JZ", {{S::PC_INC}, {S::PC_LOAD_Z}});
  m.set("JC", {{S::PC_INC}, {S::PC_LOAD_C}});
  m.set("JN", {{S::PC_INC}, {S::PC_LOAD_N}});
  m.set("JV", {{S::PC_INC}, {S::PC_LOAD_V}});
  m.set("JNZ", {{S::PC_INC}, {S::PC_LOAD_NZ}});
  m.set("JNC", {{S::PC_INC}, {S::PC_LOAD_NC}});
  m.set("JP", {{S::PC_INC}, {S::PC_LOAD_NN}});
  m.set("JNV", {{S::PC_INC}, {S::PC_LOAD_NV}});
  // The naive fetch does not step PC, so JSR steps it first to push PC+1.
  m.set("JSR", {{S::PC_INC},
                {S::STK_WRITE_PCH},
                {S::SP_DEC},
                {S::STK_WRITE_PCL},
                {S::SP_DEC},
                {S::PC_LOAD}});
  m.set("JMP D1", {{S::PC_FROM_D1}});
  m.set("JMP D2", {{S::PC_FROM_D2}});
  m.set("JSR D1", indirectCall(S::PC_FROM_D1, false));
  m.set("JSR D2", indirectCall(S::PC_FROM_D2, false));
  m.set("RET", {{S::SP_INC}, {S::STK_TO_PCL}, {S::SP_INC}, {S::STK_TO_PCH}});

  m.set("LD A <- imm8", naiveByteLoad({S::IMM_TO_B}));
  m.set("LD A <- [addr8]", naiveByteLoad({S::RAM_TO_B, S::ADDR_OP8}));
  m.set("LD [addr8] <- A", {{S::RAM_WRITE_ACC, S::ADDR_OP8}, {S::PC_INC}});
  m.set("LD A <- [A]", naiveByteLoad({S::RAM_TO_B, S::ADDR_A}));

  for (int xi = 1; xi <= 2; xi++) {
    const Regs& x = xi == 1 ? D1 : D2;
    const Regs& y = xi == 1 ? D2 : D1;
    const std::string X = dn(xi), Y = dn(3 - xi);
    m.set("LD A <- [" + X + "]", naiveByteLoad({S::RAM_TO_B, x.addr}));
    m.set("LD A <- [" + X + "]+", naiveByteLoad({S::RAM_TO_B, x.addr}, x.inc));
    m.set("LD [" + X + "] <- A", {{S::RAM_WRITE_ACC, x.addr}, {S::PC_INC}});
    m.set("LD [" + X + "]+ <- A", {{S::RAM_WRITE_ACC, x.addr}, {x.inc}, {S::PC_INC}});
    m.set("LD A <- [" + X + "+n]", naiveByteLoad({S::RAM_TO_B, x.addr, S::EA_OFF_OP8}));
    m.set("LD A <- [" + X + "+A]", naiveByteLoad({S::RAM_TO_B, x.addr, S::EA_OFF_A}));
    m.set("LD [" + X + "+n] <- A", {{S::RAM_WRITE_ACC, x.addr, S::EA_OFF_OP8}, {S::PC_INC}});

    m.set("LD " + X + " <- imm16", {{x.loadOp16}, {x.tstz}, {S::PC_INC}});
    m.set("LD " + X + " <- [addr16]", naiveWordLoad(x, {S::ADDR_OP16}));
    m.set("LD [addr16] <- " + X, wordStore(x, {S::ADDR_OP16}, true));
    m.set("LD " + Y + " <- [" + X + "]", naiveWordLoad(y, {x.addr}));
    m.set("LD " + Y + " <- [" + X + "]+", naiveWordLoad(y, {x.addr}, x.inc));
    m.set("LD " + X + " <- [A]", naiveWordLoad(x, {S::ADDR_A}));
    m.set("LD " + X + " <- [A]+", naiveWordLoad(x, {S::ADDR_A}, S::ACC_INC));
    m.set("LD " + Y + " <- [" + X + "+n]", naiveWordLoad(y, {x.addr, S::EA_OFF_OP8}));
    m.set("LD " + Y + " <- [" + X + "+A]", naiveWordLoad(y, {x.addr, S::EA_OFF_A}));
    m.set("LD [" + X + "] <- " + Y, wordStore(y, {x.addr}, true));
    m.set("LD [" + X + "+n] <- " + Y, wordStore(y, {x.addr, S::EA_OFF_OP8}, true));

    m.set("PUSHW " + X, {{x.stkWriteH}, {S::SP_DEC}, {x.stkWriteL}, {S::SP_DEC}, {S::PC_INC}});
    m.set("POPW " + X,
          {{S::SP_INC}, {x.stkToL}, {S::SP_INC}, {x.stkToH}, {x.tstz}, {S::PC_INC}});
    m.set("INC " + X, {{x.inc}, {S::PC_INC}});
  }

  for (const AluFamily& f : ALU_FAMILIES) {
    m.set(std::string(f.name) + " A <- [addr8]", naiveAlu(f.select, {S::RAM_TO_B, S::ADDR_OP8}));
    m.set(std::string(f.name) + " A <- imm8", naiveAlu(f.select, {S::IMM_TO_B}));
    for (int xi = 1; xi <= 2; xi++) {
      m.set(std::string(f.name) + " A <- [" + dn(xi) + "+n]",
            naiveAlu(f.select, {S::RAM_TO_B, addrOf(xi), S::EA_OFF_OP8}));
    }
  }
  for (const auto& [name, op] : {std::pair<std::string, Signal>{"CMP", S::ALU_SUB}, {"TST", S::ALU_AND}}) {
    m.set(name + " A, [addr8]", naiveTest(op, {S::RAM_TO_B, S::ADDR_OP8}));
    m.set(name + " A, imm8", naiveTest(op, {S::IMM_TO_B}));
    for (int xi = 1; xi <= 2; xi++) {
      m.set(name + " A, [" + dn(xi) + "+n]", naiveTest(op, {S::RAM_TO_B, addrOf(xi), S::EA_OFF_OP8}));
    }
  }
  for (const Shift& sh : SHIFTS) {
    m.set(std::string(sh.name) + " A", {{S::ACC_TO_A}, {sh.select, S::ACC_LOAD_ALU}, {sh.select, S::FLAGS_LOAD}, {S::PC_INC}});
  }
  for (const EaForm& f : eaForms()) m.set(eaName(f), {eaRow(f), {S::PC_INC}});
  addD3(m, true);

  m.set("PUSHB A", {{S::STK_WRITE_ACC}, {S::SP_DEC}, {S::PC_INC}});
  m.set("POPB A", {{S::SP_INC}, {S::STK_TO_B}, aluPassLoad(), aluPassFlags(), {S::PC_INC}});

  m.set("OUT", {{S::IO_WRITE_IMM}, {S::PC_INC}});
  m.set("OUTA", {{S::IO_WRITE_ACC}, {S::PC_INC}});
  m.set("INB", {{S::IO_READ}, {S::PC_INC}});
  for (int xi = 1; xi <= 2; xi++) {
    const Regs& x = xi == 1 ? D1 : D2;
    m.set("INW " + dn(xi), {{x.ioReadH}, {x.ioReadL}, {x.tstz}, {S::PC_INC}});
  }
  return m;
}

Microcode buildOptimal() {
  Microcode m;
  m.set("fetch", {{S::FETCH, S::PC_INC}});
  m.set("NOP", {});
  m.set("HLT", {{S::HALT}});

  m.set("JMP", {{S::PC_LOAD}});
  m.set("JZ", {{S::PC_LOAD_Z}});
  m.set("JC", {{S::PC_LOAD_C}});
  m.set("JN", {{S::PC_LOAD_N}});
  m.set("JV", {{S::PC_LOAD_V}});
  m.set("JNZ", {{S::PC_LOAD_NZ}});
  m.set("JNC", {{S::PC_LOAD_NC}});
  m.set("JP", {{S::PC_LOAD_NN}});
  m.set("JNV", {{S::PC_LOAD_NV}});
  // PC already stepped in fetch: the pushes read PC+1. The last push reads
  // the old PC while PC_LOAD commits the target at end of cycle.
  m.set("JSR", {{S::STK_WRITE_PCH, S::SP_DEC}, {S::STK_WRITE_PCL, S::SP_DEC, S::PC_LOAD}});
  m.set("JMP D1", {{S::PC_FROM_D1}});
  m.set("JMP D2", {{S::PC_FROM_D2}});
  m.set("JSR D1", indirectCall(S::PC_FROM_D1, true));
  m.set("JSR D2", indirectCall(S::PC_FROM_D2, true));
  m.set("RET", {{S::STK_TO_PCL, S::STK_CIN, S::SP_INC}, {S::STK_TO_PCH, S::STK_CIN, S::SP_INC}});

  m.set("LD A <- imm8", optimalByteLoad({S::IMM_TO_B}));
  m.set("LD A <- [addr8]", optimalByteLoad({S::RAM_TO_B, S::ADDR_OP8}));
  m.set("LD [addr8] <- A", {{S::RAM_WRITE_ACC, S::ADDR_OP8}});
  m.set("LD A <- [A]", optimalByteLoad({S::RAM_TO_B, S::ADDR_A}));

  for (int xi = 1; xi <= 2; xi++) {
    const Regs& x = xi == 1 ? D1 : D2;
    const Regs& y = xi == 1 ? D2 : D1;
    const std::string X = dn(xi), Y = dn(3 - xi);
    m.set("LD A <- [" + X + "]", optimalByteLoad({S::RAM_TO_B, x.addr}));
    m.set("LD A <- [" + X + "]+", optimalByteLoad({S::RAM_TO_B, x.addr}, x.inc));
    m.set("LD [" + X + "] <- A", {{S::RAM_WRITE_ACC, x.addr}});
    m.set("LD [" + X + "]+ <- A", {{S::RAM_WRITE_ACC, x.addr, x.inc}});
    m.set("LD A <- [" + X + "+n]", optimalByteLoad({S::RAM_TO_B, x.addr, S::EA_OFF_OP8}));
    m.set("LD A <- [" + X + "+A]", optimalByteLoad({S::RAM_TO_B, x.addr, S::EA_OFF_A}));
    m.set("LD [" + X + "+n] <- A", {{S::RAM_WRITE_ACC, x.addr, S::EA_OFF_OP8}});

    m.set("LD " + X + " <- imm16", {{x.loadOp16}, {x.tstz}});
    m.set("LD " + X + " <- [addr16]", optimalWordLoad(x, {S::ADDR_OP16}));
    m.set("LD [addr16] <- " + X, wordStore(x, {S::ADDR_OP16}, false));
    m.set("LD " + Y + " <- [" + X + "]", optimalWordLoad(y, {x.addr}));
    m.set("LD " + Y + " <- [" + X + "]+", optimalWordLoad(y, {x.addr}, x.inc));
    m.set("LD " + X + " <- [A]", optimalWordLoad(x, {S::ADDR_A}));
    m.set("LD " + X + " <- [A]+", optimalWordLoad(x, {S::ADDR_A}, S::ACC_INC));
    m.set("LD " + Y + " <- [" + X + "+n]", optimalWordLoad(y, {x.addr, S::EA_OFF_OP8}));
    m.set("LD " + Y + " <- [" + X + "+A]", optimalWordLoad(y, {x.addr, S::EA_OFF_A}));
    m.set("LD [" + X + "] <- " + Y, wordStore(y, {x.addr}, false));
    m.set("LD [" + X + "+n] <- " + Y, wordStore(y, {x.addr, S::EA_OFF_OP8}, false));

    m.set("PUSHW " + X, {{x.stkWriteH, S::SP_DEC}, {x.stkWriteL, S::SP_DEC}});
    m.set("POPW " + X,
          {{x.stkToL, S::STK_CIN, S::SP_INC}, {x.stkToH, S::STK_CIN, S::SP_INC}, {x.tstz}});
    m.set("INC " + X, {{x.inc}});
  }

  for (const AluFamily& f : ALU_FAMILIES) {
    m.set(std::string(f.name) + " A <- [addr8]",
          optimalAlu(f.select, {S::RAM_TO_B, S::ADDR_OP8}));
    m.set(std::string(f.name) + " A <- imm8", optimalAlu(f.select, {S::IMM_TO_B}));
    for (int xi = 1; xi <= 2; xi++) {
      m.set(std::string(f.name) + " A <- [" + dn(xi) + "+n]",
            optimalAlu(f.select, {S::RAM_TO_B, addrOf(xi), S::EA_OFF_OP8}));
    }
  }
  for (const auto& [name, op] : {std::pair<std::string, Signal>{"CMP", S::ALU_SUB}, {"TST", S::ALU_AND}}) {
    m.set(name + " A, [addr8]", optimalTest(op, {S::RAM_TO_B, S::ADDR_OP8}));
    m.set(name + " A, imm8", optimalTest(op, {S::IMM_TO_B}));
    for (int xi = 1; xi <= 2; xi++) {
      m.set(name + " A, [" + dn(xi) + "+n]", optimalTest(op, {S::RAM_TO_B, addrOf(xi), S::EA_OFF_OP8}));
    }
  }
  for (const Shift& sh : SHIFTS) {
    m.set(std::string(sh.name) + " A", {{S::ACC_TO_A}, {sh.select, S::ACC_LOAD_ALU, S::FLAGS_LOAD}});
  }
  for (const EaForm& f : eaForms()) m.set(eaName(f), {eaRow(f)});
  addD3(m, false);

  m.set("PUSHB A", {{S::STK_WRITE_ACC, S::SP_DEC}});
  m.set("POPB A", {{S::STK_TO_B, S::STK_CIN, S::SP_INC},
                   {S::ALU_PASS_B, S::ACC_LOAD_ALU, S::FLAGS_LOAD}});

  m.set("OUT", {{S::IO_WRITE_IMM}});
  m.set("OUTA", {{S::IO_WRITE_ACC}});
  m.set("INB", {{S::IO_READ}});
  for (int xi = 1; xi <= 2; xi++) {
    const Regs& x = xi == 1 ? D1 : D2;
    m.set("INW " + dn(xi), {{x.ioReadH}, {x.ioReadL}, {x.tstz}});
  }
  return m;
}

}  // namespace sc8
