#include <doctest.h>

#include <cstdint>

#include "core/machine.h"
#include "core/microcode.h"

using namespace sc8;

// Differential check: naive and optimal must produce identical architectural
// results on random straight-line programs.

namespace {

// mulberry32, the same generator the TypeScript suite used, so the 250
// programs are the ones that suite ran.
struct Rand {
  uint32_t a;
  double next() {
    a += 0x6d2b79f5u;
    uint32_t t = a;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  }
  uint32_t below(uint32_t n) { return static_cast<uint32_t>(next() * n); }
};

const char* GEN_OPS[] = {
    "NOP",
    "LD A <- imm8",
    "LD A <- [addr8]",
    "LD [addr8] <- A",
    "LD A <- [A]",
    "LD A <- [D1]+",
    "LD [D2]+ <- A",
    "LD A <- [D1+n]",
    "LD A <- [D2+A]",
    "LD [D1+n] <- A",
    "LD D1 <- imm16",
    "LD D2 <- imm16",
    "LD D1 <- [addr16]",
    "LD [addr16] <- D2",
    "LD D2 <- [D1]",
    "LD D2 <- [D1]+",
    "LD D1 <- [A]+",
    "LD [D1] <- D2",
    "LD [D2+n] <- D1",
    "ADD A <- imm8",
    "SUB A <- [addr8]",
    "AND A <- imm8",
    "OR A <- [addr8]",
    "XOR A <- imm8",
    "INC D1",
    "INC D2",
    "PUSHB A",
    "PUSHW D1",
    "OUT",
    "OUTA",
    "ADD A <- [D1+n]",
    "SBC A <- [D2+n]",
    "CMP A, imm8",
    "CMP A, [D1+n]",
    "TST A, [addr8]",
    "SHL A",
    "SHR A",
    "ROL A",
    "ROR A",
    "ASR A",
    "LD D1 <- D2+n",
    "LD D2 <- D1+A",
    "LD D3 <- D1+n",
    "LD D3 <- D3+n",
    "LD D2 <- D3+n",
    "LD A <- [D3+n]",
    "LD [D3+n] <- A",
    "LD [D3+n] <- D1",
    "LD D2 <- [D3+n]",
    "ADC A <- [D3+n]",
    "CMP A, [D3+n]",
    "LD [addr16] <- D3",
};

std::vector<Instr> randomProgram(Rand& rand, int length) {
  std::vector<Instr> program;
  for (int i = 0; i < length; i++) {
    const OpDef* def = opByName(GEN_OPS[rand.below(std::size(GEN_OPS))]);
    REQUIRE(def);
    uint16_t operand = 0;
    switch (def->operand) {
      case OperandKind::Imm8:
      case OperandKind::Disp8:
      case OperandKind::Addr8: operand = static_cast<uint16_t>(rand.below(256)); break;
      // Keep pointers inside RAM so dereferences stay legal.
      case OperandKind::Imm16: operand = static_cast<uint16_t>(rand.below(512)); break;
      case OperandKind::Off16: operand = static_cast<uint16_t>(rand.below(512)); break;
      case OperandKind::Addr16: operand = static_cast<uint16_t>(rand.below(1022)); break;
      case OperandKind::PortImm:
        operand = static_cast<uint16_t>((rand.below(4) << 8) | rand.below(256));
        break;
      case OperandKind::Port: operand = static_cast<uint16_t>(rand.below(4) << 8); break;
      case OperandKind::None:
      case OperandKind::Target: break;
    }
    program.push_back({def->op, operand});
  }
  return program;
}

struct Snapshot {
  Status status;
  std::optional<CrashKind> crash;
  uint8_t acc;
  uint16_t d1, d2, d3, sp;
  Flags flags;
  std::array<uint8_t, RAM_SIZE> ram;
  std::vector<uint8_t> stack;
  std::vector<LogIoBus::Entry> io;
  bool operator==(const Snapshot&) const = default;
};

Snapshot snapshot(const Machine& m, const LogIoBus& io) {
  return {m.status, m.crash ? std::optional(m.crash->kind) : std::nullopt,
          m.acc, m.d1, m.d2, m.d3, m.sp, m.flags, m.ram, m.stack, io.log};
}

}  // namespace

TEST_CASE("naive vs optimal agree on 250 random straight-line programs") {
  const Microcode naive = buildNaive();
  const Microcode optimal = buildOptimal();
  Rand rand{0xc0ffee};
  for (int i = 0; i < 250; i++) {
    CAPTURE(i);
    std::vector<Instr> program = randomProgram(rand, 25);
    LogIoBus ioA, ioB;
    Machine a(program, naive, &ioA);
    Machine b(program, optimal, &ioB);
    a.run(100);
    b.run(100);
    if (a.status == Status::Crashed || b.status == Status::Crashed) {
      // A crash freezes mid-instruction, where microarchitectural detail
      // legitimately differs between sets. The fault itself must match.
      CHECK_EQ(b.status, a.status);
      REQUIRE(a.crash);
      REQUIRE(b.crash);
      CHECK_EQ(b.crash->kind, a.crash->kind);
      CHECK_EQ(b.instructions, a.instructions);
    } else {
      CHECK(snapshot(b, ioB) == snapshot(a, ioA));
    }
  }
}
