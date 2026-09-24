#include <doctest.h>

#include <functional>

#include "asm/asm.h"
#include "core/machine.h"
#include "core/microcode.h"

using namespace sc8;

namespace {

const Microcode naive = buildNaive();
const Microcode optimal = buildOptimal();

Instr instr(std::string_view name, uint16_t operand = 0) {
  const OpDef* def = opByName(name);
  REQUIRE_MESSAGE(def, "no such op: ", name);
  return {def->op, operand};
}

using Prep = std::function<void(Machine&)>;

// Cycles for one instruction, fetch included, measured on a prepared machine.
uint64_t cyclesFor(const Microcode& mc, std::string_view name, uint16_t operand, const Prep& prep) {
  Machine m({instr(name, operand), instr("NOP"), instr("NOP")}, mc);
  if (prep) prep(m);
  m.instructionStep();
  CHECK_EQ(m.status, Status::Running);
  return m.cycles;
}

void stacked(Machine& m) {
  m.sp = STACK_TOP - 2;
  m.stack[STACK_TOP - 1] = 0x02;
  m.stack[STACK_TOP] = 0x00;
}

struct GoldenRow {
  std::string_view name;
  uint16_t operand;
  uint64_t naiveCycles;
  uint64_t optimalCycles;
  Prep prep = nullptr;
};

// The golden cycle table from spec v2 (naive / optimal), fetch included.
const GoldenRow TABLE[] = {
    {"NOP", 0, 2, 1},
    {"JMP", 1, 2, 2},
    {"JZ", 1, 3, 2},
    {"JC", 1, 3, 2},
    {"JN", 1, 3, 2},
    {"JV", 1, 3, 2},
    {"JNZ", 1, 3, 2},
    {"JNC", 1, 3, 2},
    {"JP", 1, 3, 2},
    {"JNV", 1, 3, 2},
    {"JSR", 1, 7, 3},
    {"RET", 0, 5, 3, stacked},
    {"LD A <- imm8", 7, 5, 3},
    {"LD A <- [addr8]", 3, 5, 3},
    {"LD [addr8] <- A", 3, 3, 2},
    {"LD A <- [A]", 0, 5, 3},
    {"LD A <- [D1]", 0, 5, 3},
    {"LD A <- [D1]+", 0, 6, 3},
    {"LD [D1] <- A", 0, 3, 2},
    {"LD [D1]+ <- A", 0, 4, 2},
    {"LD A <- [D1+n]", 2, 5, 3},
    {"LD A <- [D1+A]", 0, 5, 3},
    {"LD [D1+n] <- A", 2, 3, 2},
    {"LD D1 <- imm16", 0x123, 4, 3},
    {"LD D1 <- [addr16]", 0x10, 5, 4},
    {"LD [addr16] <- D1", 0x10, 4, 3},
    {"LD D2 <- [D1]", 0, 5, 4},
    {"LD D2 <- [D1]+", 0, 7, 4},
    {"LD D1 <- [A]", 0, 5, 4},
    {"LD D1 <- [A]+", 0, 7, 4},
    {"LD D2 <- [D1+n]", 2, 5, 4},
    {"LD D2 <- [D1+A]", 0, 5, 4},
    {"LD [D1] <- D2", 0, 4, 3},
    {"LD [D1+n] <- D2", 2, 4, 3},
    {"ADD A <- [addr8]", 3, 6, 3},
    {"ADD A <- imm8", 3, 6, 3},
    {"SUB A <- imm8", 3, 6, 3},
    {"ADC A <- [addr8]", 3, 6, 3},
    {"ADC A <- imm8", 3, 6, 3},
    {"SBC A <- imm8", 3, 6, 3},
    {"PUSHB A", 0, 4, 2},
    {"POPB A", 0, 6, 3, stacked},
    {"PUSHW D1", 0, 6, 3},
    {"POPW D1", 0, 7, 4, stacked},
    {"INC D1", 0, 3, 2},
    {"OUT", 0x0105, 3, 2},
    {"OUTA", 0x0100, 3, 2},
    {"INB", 0x0100, 3, 2},
    {"INW D1", 0x0100, 5, 4},
    {"INW D2", 0x0100, 5, 4},
    // The indirect forms cost what the direct ones cost.
    {"JMP D1", 0, 2, 2},
    {"JSR D1", 0, 7, 3},
    {"JSR D2", 0, 7, 3},
};

Machine runSource(std::string_view src, const Microcode& mc, uint64_t maxInstr = 100000) {
  Assembled a = assemble(src);
  for (const AsmError& e : a.errors) MESSAGE("line ", e.line, ": ", e.message);
  REQUIRE(a.errors.empty());
  Machine m(a.program, mc);
  std::copy(a.ram.begin(), a.ram.begin() + static_cast<long>(a.ramLength), m.ram.begin());
  m.run(maxInstr);
  return m;
}

}  // namespace

TEST_SUITE("golden cycle counts") {
  TEST_CASE("every instruction costs what the spec says") {
    for (const GoldenRow& r : TABLE) {
      CAPTURE(r.name);
      CHECK_EQ(cyclesFor(naive, r.name, r.operand, r.prep), r.naiveCycles);
      CHECK_EQ(cyclesFor(optimal, r.name, r.operand, r.prep), r.optimalCycles);
    }
  }

  TEST_CASE("HLT: naive 2, optimal 2") {
    for (const Microcode* mc : {&naive, &optimal}) {
      Machine m({instr("HLT")}, *mc);
      m.run(5);
      CHECK_EQ(m.status, Status::Halted);
      CHECK_EQ(m.cycles, 2u);
    }
  }

  TEST_CASE("Eddie's original ADD is 6 cycles naive, 3 optimal") {
    CHECK_EQ(cyclesFor(naive, "ADD A <- [addr8]", 3, nullptr), 6u);
    CHECK_EQ(cyclesFor(optimal, "ADD A <- [addr8]", 3, nullptr), 3u);
  }
}

TEST_SUITE("golden programs") {
  const char* COUNTDOWN = R"(
        LD A <- 5
loop:   SUB A <- 1
        JZ done
        JMP loop
done:   LD [result] <- A
        HLT
.ram
result: db 0xEE
)";

  const char* LIST_SUM = R"(
        LD D1 <- [head]
loopA:  JZ done
        LD A <- [D1]
        ADD A <- [sum]
        LD [sum] <- A
        LD D2 <- [D1+1]
loopB:  JZ done
        LD A <- [D2]
        ADD A <- [sum]
        LD [sum] <- A
        LD D1 <- [D2+1]
        JMP loopA
done:   HLT
.ram
sum:    db 0
head:   dw &node1
node1:  db 5, dw &node2
node2:  db 3, dw &node3
node3:  db 11, dw 0
)";

  const char* TABLE_LOOKUP = R"(
        LD A <- [index]
        ADD A <- table     ; base address as an immediate
        LD A <- [A]        ; the load-through table lookup
        LD [result] <- A
        HLT
.ram
index:  db 2
result: db 0
table:  db 10, db 20, db 30, db 40
)";

  const char* ARGBLOCK = R"(
        LD D1 <- args
        PUSHW D1
        JSR addtwo
        HLT
addtwo: POPW D1            ; return address, held as raw bits
        POPW D2            ; argument pointer
        PUSHW D1           ; return address back on top
        LD A <- [D2]+
        LD [tmp] <- A
        LD A <- [D2]
        ADD A <- [tmp]
        LD [result] <- A
        RET
.ram
tmp:    db 0
result: db 0
args:   db 12, db 30
)";

  const char* TREE = R"(
; Build a two-node tree at runtime, then read a child pointer back.
        LD D1 <- node1
        LD D2 <- node2
        LD [D1+1] <- D2    ; node1.left = &node2 at runtime
        LD D2 <- [D1+1]    ; follow it back
        JZ fail
        LD A <- [D2]
        LD [result] <- A
        HLT
fail:   HLT
.ram
result: db 0
node1:  db 1, dw 0, dw 0
node2:  db 99, dw 0, dw 0
)";

  const char* INDIRECT = R"(
        LD D1 <- [table]
        JSR D1
        LD [result] <- A
        HLT
target: LD A <- 77
        RET
.ram
result: db 0
table:  dw &target
)";

  TEST_CASE("both sets agree on every golden program") {
    for (const auto& [label, mc] : {std::pair{"naive", &naive}, std::pair{"optimal", &optimal}}) {
      CAPTURE(label);
      Machine a = runSource(COUNTDOWN, *mc);
      CHECK_EQ(a.status, Status::Halted);
      CHECK_EQ(a.ram[0], 0);

      Machine b = runSource(LIST_SUM, *mc);
      CHECK_EQ(b.status, Status::Halted);
      CHECK_EQ(b.ram[0], 19);

      Machine c = runSource(TABLE_LOOKUP, *mc);
      CHECK_EQ(c.status, Status::Halted);
      CHECK_EQ(c.ram[1], 30);

      Machine d = runSource(ARGBLOCK, *mc);
      CHECK_EQ(d.status, Status::Halted);
      CHECK_EQ(d.ram[1], 42);
      CHECK_EQ(d.sp, STACK_TOP);

      Machine e = runSource(TREE, *mc);
      CHECK_EQ(e.status, Status::Halted);
      CHECK_EQ(e.ram[0], 99);

      Machine f = runSource(INDIRECT, *mc);
      CHECK_EQ(f.status, Status::Halted);
      CHECK_EQ(f.ram[0], 77);
      CHECK_EQ(f.sp, STACK_TOP);
    }
  }

  TEST_CASE("naive and optimal agree on results, optimal is faster") {
    Machine a = runSource(LIST_SUM, naive);
    Machine b = runSource(LIST_SUM, optimal);
    CHECK(a.ram == b.ram);
    CHECK_LT(b.cycles, a.cycles);
  }
}
