#include <doctest.h>

#include "core/isa.h"
#include "core/machine.h"
#include "core/microcode.h"

using namespace sc8;

namespace {

const Microcode& naive() {
  static const Microcode mc = buildNaive();
  return mc;
}

const Microcode& optimal() {
  static const Microcode mc = buildOptimal();
  return mc;
}

Machine bare() { return Machine({}, naive()); }

void row(Machine& m, std::initializer_list<std::string_view> names) { m.executeRow(rowOf(names)); }

}  // namespace

TEST_SUITE("row semantics") {
  TEST_CASE("reads see start-of-cycle values: post-increment read in one row") {
    Machine m = bare();
    m.d1 = 0x100;
    m.ram[0x100] = 42;
    row(m, {"RAM_TO_B", "ADDR_D1", "D1_INC"});
    CHECK_EQ(m.bLatch, 42);
    CHECK_EQ(m.d1, 0x101);
  }

  TEST_CASE("push reads the old PC while PC_LOAD commits the target") {
    Machine m = bare();
    m.pc = 0x1234;
    m.irOperand = 0x0777;
    row(m, {"STK_WRITE_PCL", "SP_DEC", "PC_LOAD"});
    CHECK_EQ(m.stack[STACK_TOP], 0x34);
    CHECK_EQ(m.pc, 0x0777);
    CHECK_EQ(m.sp, STACK_TOP - 1);
  }

  TEST_CASE("word transfer reaches the next address with EA_CIN") {
    Machine m = bare();
    m.d1 = 0x0300;
    m.ram[0x0300] = 0x12;
    m.ram[0x0301] = 0x34;
    row(m, {"RAM_TO_D2H", "ADDR_D1"});
    row(m, {"RAM_TO_D2L", "ADDR_D1", "EA_CIN"});
    CHECK_EQ(m.d2, 0x1234);
  }

  TEST_CASE("ACC wraps mod 256 through the step unit") {
    Machine m = bare();
    m.acc = 0xff;
    row(m, {"ACC_INC"});
    CHECK_EQ(m.acc, 0);
  }

  TEST_CASE("dereferences the whole 16 bit address space") {
    Machine m = bare();
    m.ram[0xffff] = 0x5a;
    m.d1 = 0xffff;
    row(m, {"RAM_TO_B", "ADDR_D1"});
    CHECK_EQ(m.status, Status::Running);
    CHECK_EQ(m.bLatch, 0x5a);
  }

  TEST_CASE("wraps an effective address past the top of RAM") {
    Machine m = bare();
    m.ram[0] = 0x77;
    m.d1 = 0xffff;
    row(m, {"RAM_TO_B", "ADDR_D1", "EA_CIN"});
    CHECK_EQ(m.status, Status::Running);
    CHECK_EQ(m.bLatch, 0x77);
  }

  TEST_CASE("gives the machine the full 64KB") {
    CHECK_EQ(RAM_SIZE, 65536);
    CHECK_EQ(bare().ram.size(), 65536u);
  }

  TEST_CASE("executing a conflicting row crashes with the rule") {
    Machine m = bare();
    row(m, {"D1_INC", "D2_INC"});
    CHECK_EQ(m.status, Status::Crashed);
    REQUIRE(m.crash);
    CHECK_EQ(m.crash->kind, CrashKind::SignalConflict);
    CHECK(m.crash->message.find("rule 7") != std::string::npos);
  }

  TEST_CASE("a row that passes every rule and writes nine registers runs") {
    // This row used to overflow the fixed write list, which held eight, and
    // the machine crashed for real. It passes the rules: one writer per
    // atom, one access per memory, one ALU select and one step.
    const Row r = rowOf({"PC_INC", "FETCH", "ACC_TO_A", "IMM_TO_B", "RAM_TO_D1H", "ADDR_OP8", "STK_TO_D1L",
                         "IO_READ_D2H", "ALU_PASS_B", "ACC_LOAD_ALU", "FLAGS_LOAD"});
    REQUIRE(!checkRow(r));
    Machine m = bare();
    m.pc = 0;
    m.acc = 0x21;
    m.irOperand = 0x0407;
    m.ram[7] = 0x5a;
    m.stack[m.sp] = 0xa5;
    CHECK(m.executeRow(r));
    CHECK_EQ(m.status, Status::Running);
    CHECK_EQ(m.pc, 1);
    CHECK_EQ(m.aLatch, 0x21);
    CHECK_EQ(m.bLatch, 0x07);
    CHECK_EQ(m.d1, 0x5aa5);
  }

  TEST_CASE("a row that writes ten atoms, the most the rules let through, runs") {
    // Every atom but D2L has a writer here that steps nothing and shares
    // no memory with another, and no signal writes D2L on its own without
    // one of those. So this is as wide as a legal row gets.
    const Row wide = rowOf({"PC_LOAD", "FETCH", "ACC_TO_A", "IMM_TO_B", "ALU_PASS_B", "ACC_LOAD_ALU", "FLAGS_LOAD",
                            "RAM_TO_D1H", "ADDR_OP8", "STK_TO_D1L", "IO_READ_D2H", "SP_DEC"});
    REQUIRE(!checkRow(wide));
    Machine m = bare();
    m.irOperand = 0x0102;
    CHECK(m.executeRow(wide));
    CHECK_EQ(m.status, Status::Running);
    CHECK_EQ(m.pc, 0x0102);
    CHECK_EQ(m.sp, STACK_TOP - 1);
  }
}

TEST_SUITE("ALU flags") {
  void aluRow(Machine& m, uint8_t a, uint8_t b, std::string_view op) {
    m.aLatch = a;
    m.bLatch = b;
    std::string sel = "ALU_" + std::string(op);
    row(m, {sel, "ACC_LOAD_ALU"});
    row(m, {sel, "FLAGS_LOAD"});
  }

  TEST_CASE("ADD sets carry and overflow") {
    Machine m = bare();
    aluRow(m, 0xff, 1, "ADD");
    CHECK_EQ(m.acc, 0);
    CHECK_EQ(m.flags, Flags{true, false, true, false});
    aluRow(m, 0x7f, 1, "ADD");
    CHECK_EQ(m.acc, 0x80);
    CHECK_EQ(m.flags, Flags{false, true, false, true});
  }

  TEST_CASE("SUB carry means borrow") {
    Machine m = bare();
    aluRow(m, 3, 5, "SUB");
    CHECK_EQ(m.acc, 0xfe);
    CHECK(m.flags.c);
    aluRow(m, 5, 3, "SUB");
    CHECK_EQ(m.acc, 2);
    CHECK(!m.flags.c);
  }

  TEST_CASE("ADC adds the carry in, SBC subtracts the borrow") {
    Machine m = bare();
    aluRow(m, 0xff, 1, "ADD");
    aluRow(m, 10, 20, "ADC");
    CHECK_EQ(m.acc, 31);
    CHECK(!m.flags.c);
    aluRow(m, 10, 20, "ADC");
    CHECK_EQ(m.acc, 30);

    Machine s = bare();
    aluRow(s, 3, 5, "SUB");
    aluRow(s, 10, 2, "SBC");
    CHECK_EQ(s.acc, 7);
    CHECK(!s.flags.c);
  }

  TEST_CASE("a 16-bit add chains through ADD then ADC") {
    Machine m = bare();
    aluRow(m, 0xff, 0x01, "ADD");
    CHECK_EQ(m.acc, 0x00);
    aluRow(m, 0x01, 0x03, "ADC");
    CHECK_EQ(m.acc, 0x05);
    CHECK(!m.flags.c);
  }

  TEST_CASE("logic operations leave C and V unchanged") {
    Machine m = bare();
    aluRow(m, 0xff, 1, "ADD");
    aluRow(m, 0x0f, 0xf0, "AND");
    CHECK_EQ(m.acc, 0);
    CHECK(m.flags.z);
    CHECK(m.flags.c);
  }
}

TEST_SUITE("conditional jumps") {
  TEST_CASE("each jump and its inverse take opposite paths on one flag") {
    struct Pair {
      uint8_t set, clear;
      bool Flags::*flag;
    };
    const Pair pairs[] = {{0x09, 0x79, &Flags::z}, {0x0a, 0x7a, &Flags::c}, {0x0b, 0x7b, &Flags::n},
                          {0x0c, 0x7c, &Flags::v}};
    for (const Pair& p : pairs) {
      for (bool on : {false, true}) {
        for (const Microcode* mc : {&naive(), &optimal()}) {
          for (uint8_t op : {p.set, p.clear}) {
            Machine m({{op, 3}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, *mc);
            m.flags.*p.flag = on;
            m.instructionStep();
            const bool taken = (op == p.set) == on;
            INFO("op ", int(op), " flag ", on);
            CHECK_EQ(m.pc, taken ? 3 : 1);
            CHECK_EQ(m.flags.*p.flag, on);
          }
        }
      }
    }
  }
}

namespace {

Instr ins(std::string_view name, uint16_t operand = 0) { return {opByName(name)->op, operand}; }

// Runs a program to its end under both sets and checks they agree.
Machine runBoth(std::vector<Instr> program) {
  Machine a(program, naive());
  Machine b(program, optimal());
  a.run(1000);
  b.run(1000);
  CHECK_EQ(a.status, Status::Halted);
  CHECK_EQ(a.acc, b.acc);
  CHECK_EQ(a.d1, b.d1);
  CHECK_EQ(a.d2, b.d2);
  CHECK_EQ(a.d3, b.d3);
  CHECK_EQ(a.flags, b.flags);
  return b;
}

}  // namespace

TEST_SUITE("compare, test, shift and address arithmetic") {
  TEST_CASE("CMP sets SUB's flags and keeps A") {
    Machine m = runBoth({ins("LD A <- imm8", 5), ins("CMP A, imm8", 7)});
    CHECK_EQ(m.acc, 5);
    CHECK(m.flags.c);
    CHECK(!m.flags.z);
    CHECK(m.flags.n);
    m = runBoth({ins("LD A <- imm8", 7), ins("CMP A, imm8", 7)});
    CHECK_EQ(m.acc, 7);
    CHECK(m.flags.z);
    CHECK(!m.flags.c);
  }

  TEST_CASE("TST sets AND's flags and keeps A") {
    Machine m = runBoth({ins("LD A <- imm8", 0x41), ins("TST A, imm8", 0x80)});
    CHECK_EQ(m.acc, 0x41);
    CHECK(m.flags.z);
  }

  TEST_CASE("the ALU reads a byte through a D register and a displacement") {
    Machine m = runBoth({ins("LD D1 <- imm16", 0x100), ins("LD A <- imm8", 9), ins("LD [D1+n] <- A", 3),
                         ins("LD A <- imm8", 1), ins("ADD A <- [D1+n]", 3), ins("CMP A, [D1+n]", 3)});
    CHECK_EQ(m.acc, 10);
    CHECK(!m.flags.z);
    CHECK(!m.flags.c);
  }

  TEST_CASE("shifts move one bit and put the bit that leaves in C") {
    Machine m = runBoth({ins("LD A <- imm8", 0x81), ins("SHL A")});
    CHECK_EQ(m.acc, 0x02);
    CHECK(m.flags.c);
    m = runBoth({ins("LD A <- imm8", 0x81), ins("SHR A")});
    CHECK_EQ(m.acc, 0x40);
    CHECK(m.flags.c);
    m = runBoth({ins("LD A <- imm8", 0x81), ins("ASR A")});
    CHECK_EQ(m.acc, 0xc0);
    CHECK(m.flags.n);
    // A 16 bit shift left of $80FF: SHL the low byte, ROL the high byte.
    m = runBoth({ins("LD A <- imm8", 0xff), ins("SHL A"), ins("LD [addr8] <- A", 1), ins("LD A <- imm8", 0x80),
                 ins("ROL A")});
    CHECK_EQ(m.acc, 0x01);
    CHECK(m.flags.c);
    CHECK_EQ(m.ram[1], 0xfe);
    m = runBoth({ins("LD A <- imm8", 0x01), ins("SUB A <- imm8", 2), ins("LD A <- imm8", 0x02), ins("ROR A")});
    CHECK_EQ(m.acc, 0x81);
    CHECK(!m.flags.c);
  }

  TEST_CASE("a shift leaves V alone") {
    Machine m = runBoth({ins("LD A <- imm8", 0x7f), ins("ADD A <- imm8", 1), ins("SHR A")});
    CHECK(m.flags.v);
  }

  TEST_CASE("a D register loads the adder's sum and tests it for zero") {
    Machine m = runBoth({ins("LD D1 <- imm16", 0x1000), ins("LD D2 <- D1+n", 0xfff8), ins("LD A <- imm8", 0x20),
                         ins("LD D1 <- D1+A")});
    CHECK_EQ(m.d2, 0x0ff8);
    CHECK_EQ(m.d1, 0x1020);
    CHECK(!m.flags.z);
    m = runBoth({ins("LD D1 <- imm16", 8), ins("LD D1 <- D1+n", 0xfff8)});
    CHECK_EQ(m.d1, 0);
    CHECK(m.flags.z);
    // The sum wraps at 16 bits, and C is the carry out of bit 15.
    m = runBoth({ins("LD D2 <- imm16", 0xffff), ins("LD D2 <- D2+n", 2)});
    CHECK_EQ(m.d2, 1);
    CHECK(m.flags.c);
  }

  TEST_CASE("an address add sets C from the carry out of bit 15") {
    // $0008 - 12 is $0008 + $FFF4, which does not carry: it went below zero.
    Machine m = runBoth({ins("LD A <- imm8", 0xff), ins("ADD A <- imm8", 1), ins("LD D1 <- imm16", 8),
                         ins("LD D2 <- D1+n", 0xfff4)});
    CHECK_EQ(m.d2, 0xfffc);
    CHECK(!m.flags.c);
    // $000C - 12 carries, and lands on zero.
    m = runBoth({ins("LD D1 <- imm16", 12), ins("LD D2 <- D1+n", 0xfff4)});
    CHECK_EQ(m.d2, 0);
    CHECK(m.flags.z);
    CHECK(m.flags.c);
    // N and V are left alone.
    m = runBoth({ins("LD A <- imm8", 0x7f), ins("ADD A <- imm8", 1), ins("LD D1 <- imm16", 1), ins("LD D1 <- D1+n", 1)});
    CHECK(m.flags.n);
    CHECK(m.flags.v);
  }

  TEST_CASE("the stack check: D3 minus the floor clears C when D3 is below it") {
    // Floors either side of $8000, where a sixteen bit offset turns negative.
    for (const uint16_t floor : {uint16_t{0x2000}, uint16_t{0xEAC0}}) {
      for (const int delta : {-1, 0, 1}) {
        const auto d3 = static_cast<uint16_t>(floor + delta);
        Machine m = runBoth({ins("LD D1 <- imm16", d3), ins("LD D3 <- D1+n", 0),
                             ins("LD D2 <- D3+n", static_cast<uint16_t>(0x10000 - floor))});
        CAPTURE(floor);
        CAPTURE(delta);
        CHECK_EQ(m.flags.c, delta >= 0);
      }
    }
  }
}

TEST_SUITE("the hardware stack's size") {
  TEST_CASE("the size sets SP's start and where a push overflows") {
    Machine m({{0x50, 0}, {0x08, 0}}, naive());  // PUSHB A, JMP 0
    CHECK_EQ(m.stackSize(), STACK_SIZE);
    m.setStackSize(256);
    CHECK_EQ(m.sp, 0xff);
    m.run(10000);
    REQUIRE(m.crash);
    CHECK_EQ(m.crash->kind, CrashKind::StackOverflow);
    // 255 pushes and their jumps finish. The 256th writes cell 0 and then
    // cannot step SP below it.
    CHECK_EQ(m.instructions, 255u * 2);
    m.setStackSize(MAX_STACK_SIZE);
    CHECK_EQ(m.sp, 0xffff);
  }

  TEST_CASE("a command line size reads as bytes, K or hex, inside the bounds") {
    CHECK_EQ(parseStackSize("2048"), 2048);
    CHECK_EQ(parseStackSize("16K"), 16384);
    CHECK_EQ(parseStackSize("0x800"), 2048);
    CHECK_EQ(parseStackSize("64k"), 65536);
    CHECK_FALSE(parseStackSize("65K"));
    CHECK_FALSE(parseStackSize("100"));
    CHECK_FALSE(parseStackSize("big"));
  }
}

TEST_SUITE("D3, the data stack pointer") {
  TEST_CASE("a frame at [D3+n]: bytes, words and the ALU") {
    Machine m = runBoth({ins("LD D1 <- imm16", 0x2000), ins("LD D3 <- D1+n", 0), ins("LD D3 <- D3+n", 0xfffa),
                         ins("LD A <- imm8", 7), ins("LD [D3+n] <- A", 1), ins("LD A <- imm8", 5),
                         ins("ADD A <- [D3+n]", 1), ins("LD [D3+n] <- A", 2), ins("LD D2 <- imm16", 0x1234),
                         ins("LD [D3+n] <- D2", 4), ins("LD D1 <- [D3+n]", 4), ins("CMP A, [D3+n]", 2)});
    CHECK_EQ(m.d3, 0x1ffa);
    CHECK_EQ(m.ram[0x1ffb], 7);
    CHECK_EQ(m.ram[0x1ffc], 12);
    CHECK_EQ(m.ram[0x1ffe], 0x12);
    CHECK_EQ(m.ram[0x1fff], 0x34);
    CHECK_EQ(m.d1, 0x1234);
    CHECK(m.flags.z);
  }

  TEST_CASE("the address of a local, and D3 saved and restored through memory") {
    Machine m = runBoth({ins("LD D1 <- imm16", 0x3000), ins("LD D3 <- D1+n", 0), ins("LD D2 <- D3+n", 3),
                         ins("LD [addr16] <- D3", 0x10), ins("LD D1 <- imm16", 0), ins("LD D3 <- D1+n", 0),
                         ins("LD D3 <- [addr16]", 0x10)});
    CHECK_EQ(m.d2, 0x3003);
    CHECK_EQ(m.d3, 0x3000);
    CHECK(!m.flags.z);
    CHECK_EQ(m.ram[0x10], 0x30);
  }

  TEST_CASE("TST A through D3") {
    Machine m = runBoth({ins("LD D1 <- imm16", 0x100), ins("LD D3 <- D1+n", 0), ins("LD A <- imm8", 0x0f),
                         ins("LD [D3+n] <- A", 0), ins("LD A <- imm8", 0xf0), ins("TST A, [D3+n]", 0)});
    CHECK(m.flags.z);
    CHECK_EQ(m.acc, 0xf0);
  }
}

TEST_SUITE("halt and crash boundaries") {
  TEST_CASE("PC at program length halts, past it crashes") {
    Machine m1({{0x00, 0}}, naive());
    m1.run(10);
    CHECK_EQ(m1.status, Status::Halted);

    Machine m2({{0x08, 5}}, naive());
    m2.run(10);
    CHECK_EQ(m2.status, Status::Crashed);
    CHECK_EQ(m2.crash->kind, CrashKind::IllegalProgramAddress);
  }

  TEST_CASE("JMP N is a graceful exit") {
    Machine m({{0x08, 1}}, naive());
    m.run(10);
    CHECK_EQ(m.status, Status::Halted);
  }

  TEST_CASE("missing microprogram crashes") {
    Microcode partial = naive();
    partial.erase("NOP");
    Machine m({{0x00, 0}}, partial);
    m.run(10);
    CHECK_EQ(m.status, Status::Crashed);
    CHECK_EQ(m.crash->kind, CrashKind::NoMicrocode);
  }

  TEST_CASE("stack overflow and underflow crash") {
    Machine m = bare();
    m.sp = 0;
    row(m, {"SP_DEC"});
    CHECK_EQ(m.crash->kind, CrashKind::StackOverflow);

    Machine m2 = bare();
    m2.sp = STACK_TOP;
    row(m2, {"SP_INC"});
    CHECK_EQ(m2.crash->kind, CrashKind::StackUnderflow);
  }

  TEST_CASE("heat arrays record access cycles") {
    Machine m = bare();
    m.d1 = 0x20;
    row(m, {"RAM_TO_B", "ADDR_D1"});
    CHECK_GT(m.ramReadAt[0x20], 0u);
    row(m, {"RAM_WRITE_ACC", "ADDR_OP8"});
    CHECK_GT(m.ramWriteAt[0], 0u);
  }

  TEST_CASE("reset clears heat access stamps along with the cycle counter") {
    Machine m = bare();
    m.d1 = 0x20;
    row(m, {"RAM_TO_B", "ADDR_D1"});
    row(m, {"RAM_WRITE_ACC", "ADDR_OP8"});
    CHECK_GT(m.ramReadAt[0x20], 0u);
    CHECK_GT(m.ramWriteAt[0], 0u);
    m.reset();
    CHECK_EQ(m.ramReadAt[0x20], 0u);
    CHECK_EQ(m.ramWriteAt[0], 0u);
  }

  TEST_CASE("access counts saturate and clear on demand") {
    Machine m = bare();
    m.ramReads[0x20] = 0xfffffffe;
    m.d1 = 0x20;
    row(m, {"RAM_TO_B", "ADDR_D1"});
    row(m, {"RAM_TO_B", "ADDR_D1"});
    row(m, {"RAM_TO_B", "ADDR_D1"});
    CHECK_EQ(m.ramReads[0x20], 0xffffffffu);
    m.clearAccessCounts();
    CHECK_EQ(m.ramReads[0x20], 0u);
    CHECK_EQ(m.status, Status::Running);
  }
}

TEST_SUITE("bus events") {
  TEST_CASE("starts with no bus event and records the fetch") {
    Machine m({{0x00, 0}}, naive());
    CHECK(!m.lastBus);
    m.microStep();
    REQUIRE(m.lastBus);
    CHECK_EQ(m.lastBus->kind, BusEvent::Kind::Fetch);
    CHECK_EQ(m.lastBus->addr, 0u);
    CHECK_EQ(m.lastBus->data, 0);
  }

  TEST_CASE("a zero-page store ends the instruction on a ram-write event") {
    Machine m({{0x12, 0x10}}, naive());
    m.acc = 0x2a;
    m.instructionStep();
    REQUIRE(m.lastBus);
    CHECK_EQ(m.lastBus->kind, BusEvent::Kind::RamWrite);
    CHECK_EQ(m.lastBus->addr, 0x10u);
    CHECK_EQ(m.lastBus->data, 0x2a);
  }

  TEST_CASE("JSR leaves a stack-write as the last bus event") {
    Machine m({{0x0e, 1}, {0x01, 0}}, naive());
    m.instructionStep();
    REQUIRE(m.lastBus);
    CHECK_EQ(m.lastBus->kind, BusEvent::Kind::StackWrite);
    CHECK_EQ(m.lastBus->addr, static_cast<uint32_t>(STACK_TOP - 1));
  }

  TEST_CASE("reset clears the bus event") {
    Machine m({{0x00, 0}}, naive());
    m.microStep();
    CHECK(m.lastBus);
    m.reset();
    CHECK(!m.lastBus);
  }

  TEST_CASE("tracing off records nothing and changes no result") {
    Machine m({{0x12, 0x10}}, naive());
    m.setTrace(false);
    m.acc = 0x2a;
    m.instructionStep();
    CHECK(!m.lastBus);
    CHECK(!m.lastMicro);
    CHECK_EQ(m.ram[0x10], 0x2a);
    CHECK_EQ(m.ramWriteAt[0x10], 0u);
    CHECK_EQ(m.ramWrites[0x10], 1u);
  }
  TEST_CASE("the sequencer position walks the fetch, then the instruction's rows") {
    // ADD A <- imm8 under the naive set: one fetch row, then five rows.
    Machine m({{0x48, 7}}, naive());
    const Rows* rows = naive().get("ADD A <- imm8");
    REQUIRE(rows);
    REQUIRE_EQ(naive().get("fetch")->size(), 1u);
    Machine::SequencerPos p = m.sequencer();
    CHECK(p.inFetch);
    CHECK_EQ(p.section, -1);
    CHECK_EQ(p.rowIndex, 0u);
    // After the fetch row the sequencer stands on the instruction's first row.
    m.microStep();
    p = m.sequencer();
    CHECK(!p.inFetch);
    CHECK_GE(p.section, 0);
    CHECK_EQ(p.rowIndex, 0u);
    CHECK_EQ(m.lastMicro->op, "fetch");
    for (size_t i = 0; i + 1 < rows->size(); i++) {
      m.microStep();
      p = m.sequencer();
      CHECK(!p.inFetch);
      CHECK_EQ(p.rowIndex, i + 1);
      CHECK_EQ(m.lastMicro->row, static_cast<int>(i));
    }
    // The last row finishes the instruction: back between instructions.
    m.microStep();
    p = m.sequencer();
    CHECK(p.inFetch);
    CHECK_EQ(p.section, -1);
    CHECK_EQ(m.instructions, 1u);
    CHECK_EQ(m.acc, 7);
  }
}
