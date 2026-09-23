#include <doctest.h>

#include "core/machine.h"
#include "core/microcode.h"

using namespace sc8;

namespace {

const Microcode& naive() {
  static const Microcode mc = buildNaive();
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
}
