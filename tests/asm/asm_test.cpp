#include <doctest.h>

#include <regex>

#include "asm/asm.h"
#include "core/cartridge.h"
#include "core/machine.h"
#include "core/mcparse.h"
#include "core/microcode.h"
#include "devices/constants.h"

using namespace sc8;

namespace {

uint8_t opOf(std::string_view name) {
  const OpDef* def = opByName(name);
  REQUIRE_MESSAGE(def, "no such op: ", name);
  return def->op;
}

std::string firstError(std::string_view src) {
  Assembled a = assemble(src);
  REQUIRE_MESSAGE(!a.errors.empty(), src, " assembled cleanly");
  return a.errors[0].message;
}

Assembled ok(std::string_view src) {
  Assembled a = assemble(src);
  for (const AsmError& e : a.errors) MESSAGE("line ", e.line, ": ", e.message);
  REQUIRE_MESSAGE(a.errors.empty(), src);
  return a;
}

bool has(const std::string& s, std::string_view needle) { return s.find(needle) != std::string::npos; }

std::vector<uint8_t> slice(const std::vector<uint8_t>& v, size_t n) {
  return {v.begin(), v.begin() + static_cast<long>(n)};
}

std::vector<uint8_t> opcodes(std::string_view src) {
  std::vector<uint8_t> out;
  for (const Instr& i : ok(src).program) out.push_back(i.op);
  return out;
}

std::string repeat(std::string_view s, size_t n) {
  std::string out;
  for (size_t i = 0; i < n; i++) out += s;
  return out;
}

}  // namespace

TEST_SUITE("assembler syntax") {
  TEST_CASE("the inverted jumps take a label") {
    Assembled a = ok("top: JNZ top\nJNC top\nJP end\nJNV end\nend: HLT");
    REQUIRE_EQ(a.program.size(), 5u);
    CHECK(a.program[0] == Instr{0x79, 0});
    CHECK(a.program[1] == Instr{0x7a, 0});
    CHECK(a.program[2] == Instr{0x7b, 4});
    CHECK(a.program[3] == Instr{0x7c, 4});
  }

  TEST_CASE("arrows work in both directions") {
    Assembled a = ok("LD A <- 5");
    Assembled b = ok("LD 5 -> A");
    CHECK(a.program == b.program);
    CHECK(a.program[0] == Instr{opOf("LD A <- imm8"), 5});
  }

  TEST_CASE("OUT and IN take the arrow form as OUTA and INB") {
    Assembled outa = ok("OUTA GPU_DATA0");
    for (const char* src : {"OUT A -> GPU_DATA0", "OUT GPU_DATA0 <- A"}) {
      CAPTURE(src);
      CHECK(ok(src).program == outa.program);
    }
    Assembled inp = ok("IN GPU_FRAME");
    for (const char* src : {"IN GPU_FRAME -> A", "IN A <- GPU_FRAME", "INB GPU_FRAME"}) {
      CAPTURE(src);
      CHECK(ok(src).program == inp.program);
    }
    CHECK_EQ(inp.program[0].op, opOf("INB"));
    CHECK(has(firstError("OUT 5 -> GPU_DATA0"), "sends A"));
  }

  TEST_CASE("IN into a D register is the word opcode INW") {
    Assembled a = ok("IN GPU_RAND -> D2");
    CHECK_EQ(a.program[0].op, opOf("INW D2"));
    for (const char* src : {"IN GPU_RAND -> D2", "IN D2 <- GPU_RAND", "INW GPU_RAND -> D2"}) {
      CAPTURE(src);
      CHECK(ok(src).program == a.program);
    }
    CHECK_EQ(ok("IN GPU_RAND -> D1").program[0].op, opOf("INW D1"));
    CHECK(has(firstError("INB GPU_RAND -> D2"), "byte into A"));
    CHECK(has(firstError("INW GPU_RAND -> A"), "word into D1 or D2"));
  }

  TEST_CASE("brackets mean contents-of, bare means the value") {
    Assembled a = ok("LD D1 <- spot\nLD A <- [spot]\n.ram\nspot: db 7");
    CHECK(a.program[0] == Instr{opOf("LD D1 <- imm16"), 0});
    CHECK(a.program[1] == Instr{opOf("LD A <- [addr8]"), 0});
  }

  TEST_CASE("the + suffix selects the post-increment opcode") {
    Assembled a = ok("LD A <- [D1]\nLD A <- [D1]+");
    CHECK_EQ(a.program[0].op, opOf("LD A <- [D1]"));
    CHECK_EQ(a.program[1].op, opOf("LD A <- [D1]+"));
  }

  TEST_CASE("displacement and indexed shapes resolve") {
    Assembled a = ok("LD D2 <- [D1+3]\nLD A <- [D2+A]\nLD [D1+1] <- D2");
    CHECK(a.program[0] == Instr{opOf("LD D2 <- [D1+n]"), 3});
    CHECK_EQ(a.program[1].op, opOf("LD A <- [D2+A]"));
    CHECK(a.program[2] == Instr{opOf("LD [D1+n] <- D2"), 1});
  }

  TEST_CASE("INC A is a one-to-one alias of ADD A <- 1") {
    Assembled a = ok("INC A\nDEC A\nINC D1");
    CHECK(a.program[0] == Instr{opOf("ADD A <- imm8"), 1});
    CHECK(a.program[1] == Instr{opOf("SUB A <- imm8"), 1});
    CHECK_EQ(a.program[2].op, opOf("INC D1"));
  }

  TEST_CASE("emits a source map in both directions") {
    Assembled a = ok("; comment\nNOP\n\nstart: HLT");
    CHECK(a.instrToLine == std::vector<int>{2, 4});
    CHECK_EQ(a.lineToInstr.at(4), 1);
    CHECK(a.labels.at("start") == Label{Label::Kind::Code, 1});
  }

  TEST_CASE("mnemonics are case insensitive") {
    Assembled a = ok("ld A <- 5\njmp D1\nhlt");
    CHECK(a.program[0] == Instr{opOf("LD A <- imm8"), 5});
    CHECK_EQ(a.program[1].op, opOf("JMP D1"));
  }
}

TEST_SUITE("assembler .ram and .data") {
  TEST_CASE("db and dw emit big-endian with address-of labels") {
    Assembled a = ok("NOP\n.ram\nnode1: db 5, dw &node2, dw 0\nnode2: db 4");
    CHECK(slice(a.ram, 6) == std::vector<uint8_t>{5, 0, 5, 0, 0, 4});
    CHECK(a.labels.at("node2") == Label{Label::Kind::Ram, 5});
  }

  TEST_CASE("stores a string literal as its bytes in .data") {
    Assembled a = ok("NOP\n.data\nmsg: db \"Hi\", 0");
    CHECK(slice(a.cart, 3) == std::vector<uint8_t>{72, 105, 0});
  }

  TEST_CASE("decodes escapes in a string") {
    Assembled a = ok("NOP\n.data\nt: db \"a\\nb\\t\\x41\\\\\"");
    CHECK(slice(a.cart, 6) == std::vector<uint8_t>{97, 10, 98, 9, 0x41, 92});
  }

  TEST_CASE("keeps a comma inside a string") {
    Assembled a = ok("NOP\n.data\nt: db \"a,b\", 5");
    CHECK(slice(a.cart, 4) == std::vector<uint8_t>{97, 44, 98, 5});
  }

  TEST_CASE("stores a string into .ram too") {
    Assembled a = ok("NOP\n.ram\nt: db \"Yo\"");
    CHECK(slice(a.ram, 2) == std::vector<uint8_t>{89, 111});
  }

  TEST_CASE("fills all of RAM from .ram without complaining") {
    std::string full = ".ram\n        db " + repeat("0,", RAM_SIZE - 1) + "0\n";
    CHECK(assemble(full).errors.empty());
  }

  TEST_CASE("names the real RAM size when .ram overflows") {
    std::string over = ".ram\n        db " + repeat("0,", RAM_SIZE) + "0\n";
    Assembled a = assemble(over);
    bool named = false, stale = false;
    for (const AsmError& e : a.errors) {
      named = named || has(e.message, std::to_string(RAM_SIZE));
      stale = stale || has(e.message, "2048");
    }
    CHECK(named);
    CHECK(!stale);
  }

  TEST_CASE("ds reserves zero bytes in both sections") {
    Assembled a = ok("NOP\n.ram\na: db 1\nbuf: ds 4\nb: db 2\n.data\nx: db 9\npad: ds 3\ny: db 8");
    CHECK(a.labels.at("b") == Label{Label::Kind::Ram, 5});
    CHECK(slice(a.ram, 6) == std::vector<uint8_t>{1, 0, 0, 0, 0, 2});
    CHECK(a.labels.at("y") == Label{Label::Kind::Data, 4});
    CHECK(a.cart == std::vector<uint8_t>{9, 0, 0, 0, 8});
    CHECK(has(firstError(".ram\nbuf: ds later\nlater: db 1"), "not a label"));
  }

  TEST_CASE("a .file blob lands in the cartridge, deduplicated, with a size the macros read") {
    Assets assets;
    assets.files["a.bin"] = {1, 2, 3};
    assets.files["b.bin"] = {1, 2, 3};
    Assembled a = assemble("NOP\n.data\none: .file('a.bin')\ntwo: .file(\"b.bin\")\nthree: db 4",
                           &assets);
    REQUIRE(a.errors.empty());
    CHECK(a.labels.at("one") == Label{Label::Kind::Data, 0});
    CHECK(a.labels.at("two") == Label{Label::Kind::Data, 0});
    CHECK(a.labels.at("three") == Label{Label::Kind::Data, 3});
    CHECK(a.cart == std::vector<uint8_t>{1, 2, 3, 4});
    CHECK(a.assets.size() == 2);
    CHECK(a.assets[0] == RomAsset{"file", "one", 0, 3});
  }

  TEST_CASE("a missing asset is asked of the loader before it is an error") {
    Assets assets;
    assets.loadFile = [](std::string_view name) -> std::optional<std::vector<uint8_t>> {
      if (name == "disk.bin") return std::vector<uint8_t>{7, 7};
      return std::nullopt;
    };
    Assembled a = assemble("NOP\n.data\nd: .file('disk.bin')", &assets);
    CHECK(a.errors.empty());
    CHECK(a.cart == std::vector<uint8_t>{7, 7});
    CHECK(has(firstError("NOP\n.data\nd: .file('nope.bin')"), "unknown file: nope.bin"));
  }
}

TEST_SUITE("assembler errors") {
  TEST_CASE("rejects [A]+ into A") {
    CHECK(has(firstError("LD [A]+ -> A"), "overwrites the register being stepped"));
  }

  TEST_CASE("rejects same-register word loads in every shape") {
    CHECK(has(firstError("LD D1 <- [D1]"), "corrupts its own address"));
    CHECK(has(firstError("LD D2 <- [D2+3]"), "corrupts its own address"));
    CHECK(has(firstError("LD D1 <- [D1+A]"), "corrupts its own address"));
  }

  TEST_CASE("rejects indexed stores") {
    CHECK(has(firstError("LD [D1+A] <- A"), "indexed stores do not exist"));
  }

  TEST_CASE("rejects byte direct addressing outside the zero page") {
    CHECK(has(firstError("LD A <- [far]\n.ram\npad: dw 0\n" + repeat("db 0\n", 300) + "far: db 1"),
              "zero page only"));
  }

  TEST_CASE("rejects register-to-register moves") {
    CHECK(has(firstError("LD A <- D2"), "register to register"));
    CHECK(has(firstError("LD D1 <- A"), "through RAM"));
  }

  TEST_CASE("TST D1 and TST D2 are the address add with nothing added") {
    Assembled a = ok("TST D1\nTST D2\nLD D1 <- D1+0\nLD D1 <- D1");
    REQUIRE_EQ(a.program.size(), 4u);
    CHECK_EQ(a.program[3].op, a.program[0].op);
    CHECK_EQ(a.program[3].operand, 0);
    CHECK_EQ(a.program[0].op, opByName("LD D1 <- D1+n")->op);
    CHECK_EQ(a.program[0].operand, 0);
    CHECK_EQ(a.program[1].op, opByName("LD D2 <- D2+n")->op);
    CHECK_EQ(a.program[2].op, a.program[0].op);
  }

  TEST_CASE("a D register takes a sum: the address arithmetic") {
    Assembled a = ok("LD D1 <- D1+8\nLD D1 <- D1-8\nLD D2 <- D1+A\nLD D1 <- D2\nLD D2 <- D2+count\n.ram\ncount: db 0");
    REQUIRE_EQ(a.program.size(), 5u);
    CHECK(a.program[0] == Instr{opOf("LD D1 <- D1+n"), 8});
    CHECK(a.program[1] == Instr{opOf("LD D1 <- D1+n"), 0xfff8});
    CHECK(a.program[2] == Instr{opOf("LD D2 <- D1+A"), 0});
    CHECK(a.program[3] == Instr{opOf("LD D1 <- D2+n"), 0});
    CHECK(a.program[4] == Instr{opOf("LD D2 <- D2+n"), 0});
  }

  TEST_CASE("CMP and TST take a comma, the shifts take A") {
    Assembled a = ok("CMP A, 5\nCMP A, [x]\nTST A, [D2+3]\nADD A <- [D1+4]\nSHL A\nASR A\n.ram\nx: db 0");
    CHECK(a.program[0] == Instr{opOf("CMP A, imm8"), 5});
    CHECK(a.program[1] == Instr{opOf("CMP A, [addr8]"), 0});
    CHECK(a.program[2] == Instr{opOf("TST A, [D2+n]"), 3});
    CHECK(a.program[3] == Instr{opOf("ADD A <- [D1+n]"), 4});
    CHECK(a.program[4] == Instr{opOf("SHL A"), 0});
    CHECK(a.program[5] == Instr{opOf("ASR A"), 0});
    CHECK(has(firstError("CMP A <- 5"), "takes no arrow"));
    CHECK(has(firstError("SHL D1"), "shifts A"));
  }

  TEST_CASE("rejects undefined and duplicate labels") {
    CHECK(has(firstError("JMP nowhere"), "undefined label"));
    CHECK(has(firstError("x: NOP\nx: NOP"), "duplicate label"));
  }

  TEST_CASE("rejects jumps to RAM labels") {
    CHECK(has(firstError("JMP spot\n.ram\nspot: db 1"), "not a code label"));
  }

  TEST_CASE("names the indirect jump spelling") {
    CHECK(has(firstError("JMP [D1]"), "Use JMP D1"));
    CHECK(has(firstError("JSR SP"), "D1 and D2 only"));
  }
}

TEST_SUITE("stack sugar: PUSH, POP, and the [SP] forms") {
  TEST_CASE("PUSH and POP pick the byte or word opcode from the register") {
    CHECK(opcodes("PUSH A") == std::vector<uint8_t>{opOf("PUSHB A")});
    CHECK(opcodes("PUSH D1") == std::vector<uint8_t>{opOf("PUSHW D1")});
    CHECK(opcodes("PUSH D2") == std::vector<uint8_t>{opOf("PUSHW D2")});
    CHECK(opcodes("POP A") == std::vector<uint8_t>{opOf("POPB A")});
    CHECK(opcodes("POP D1") == std::vector<uint8_t>{opOf("POPW D1")});
    CHECK(opcodes("POP D2") == std::vector<uint8_t>{opOf("POPW D2")});
  }

  TEST_CASE("LD to [SP]- is a push in both arrow directions") {
    CHECK(opcodes("LD [SP]- <- A") == std::vector<uint8_t>{opOf("PUSHB A")});
    CHECK(opcodes("LD A -> [SP]-") == std::vector<uint8_t>{opOf("PUSHB A")});
    CHECK(opcodes("LD [SP]- <- D1") == std::vector<uint8_t>{opOf("PUSHW D1")});
    CHECK(opcodes("LD D2 -> [SP]-") == std::vector<uint8_t>{opOf("PUSHW D2")});
  }

  TEST_CASE("LD from [SP]+ is a pop in both arrow directions") {
    CHECK(opcodes("LD A <- [SP]+") == std::vector<uint8_t>{opOf("POPB A")});
    CHECK(opcodes("LD [SP]+ -> A") == std::vector<uint8_t>{opOf("POPB A")});
    CHECK(opcodes("LD D1 <- [SP]+") == std::vector<uint8_t>{opOf("POPW D1")});
    CHECK(opcodes("LD [SP]+ -> D2") == std::vector<uint8_t>{opOf("POPW D2")});
  }

  TEST_CASE("rejects the wrong stack direction and non-register operands") {
    CHECK(has(firstError("LD [SP]+ <- A"), "[SP]-"));
    CHECK(has(firstError("LD A <- [SP]-"), "[SP]+"));
    CHECK(has(firstError("LD [SP]- <- 5"), "register"));
    CHECK(has(firstError("PUSH B"), "A, D1, or D2"));
  }
}

TEST_SUITE("assembler bracket dereferencing") {
  const std::string RAM = "\n.ram\nzp: db 7\nfour: db 4";

  Instr one(const std::string& src) {
    Assembled a = ok(src + RAM);
    REQUIRE_EQ(a.program.size(), 1u);
    return a.program[0];
  }

  struct Mode {
    const char* brackets;
    const char* parens;
    const char* name;
    uint16_t operand;
  };

  // bracket source, the round-bracket source it replaced, the ISA name of
  // the opcode it must pick, and the operand that opcode must carry.
  const Mode MODES[] = {
      {"LD A <- [D1]", "LD A <- (D1)", "LD A <- [D1]", 0},
      {"LD A <- [D2]", "LD A <- (D2)", "LD A <- [D2]", 0},
      {"LD A <- [D1]+", "LD A <- (D1)+", "LD A <- [D1]+", 0},
      {"LD A <- [D2]+", "LD A <- (D2)+", "LD A <- [D2]+", 0},
      {"LD A <- [A]", "LD A <- (A)", "LD A <- [A]", 0},
      {"LD D1 <- [A]", "LD D1 <- (A)", "LD D1 <- [A]", 0},
      {"LD D2 <- [A]+", "LD D2 <- (A)+", "LD D2 <- [A]+", 0},
      {"LD A <- [D1+A]", "LD A <- (D1+A)", "LD A <- [D1+A]", 0},
      {"LD A <- [D2+A]", "LD A <- (D2+A)", "LD A <- [D2+A]", 0},
      {"LD D2 <- [D1+A]", "LD D2 <- (D1+A)", "LD D2 <- [D1+A]", 0},
      {"LD A <- [D1+4]", "LD A <- (D1+4)", "LD A <- [D1+n]", 4},
      {"LD A <- [D2+four]", "LD A <- (D2+four)", "LD A <- [D2+n]", 1},
      {"LD D2 <- [D1+3]", "LD D2 <- (D1+3)", "LD D2 <- [D1+n]", 3},
      {"LD A <- [zp]", "LD A <- (zp)", "LD A <- [addr8]", 0},
      {"LD A <- [$1C]", "LD A <- ($1C)", "LD A <- [addr8]", 0x1c},
      {"LD D1 <- [four]", "LD D1 <- (four)", "LD D1 <- [addr16]", 1},
      {"LD D2 <- [D1]", "LD D2 <- (D1)", "LD D2 <- [D1]", 0},
      {"LD D1 <- [D2]+", "LD D1 <- (D2)+", "LD D1 <- [D2]+", 0},
      {"LD [D1] <- A", "LD (D1) <- A", "LD [D1] <- A", 0},
      {"LD [D2]+ <- A", "LD (D2)+ <- A", "LD [D2]+ <- A", 0},
      {"LD [D2] <- D1", "LD (D2) <- D1", "LD [D2] <- D1", 0},
      {"LD [D1+2] <- A", "LD (D1+2) <- A", "LD [D1+n] <- A", 2},
      {"LD [D1+2] <- D2", "LD (D1+2) <- D2", "LD [D1+n] <- D2", 2},
      {"LD [zp] <- A", "LD (zp) <- A", "LD [addr8] <- A", 0},
      {"LD [four] <- D1", "LD (four) <- D1", "LD [addr16] <- D1", 1},
      {"LD [SP]- <- A", "LD (SP)- <- A", "PUSHB A", 0},
      {"LD [SP]- <- D1", "LD (SP)- <- D1", "PUSHW D1", 0},
      {"LD A <- [SP]+", "LD A <- (SP)+", "POPB A", 0},
      {"LD D2 <- [SP]+", "LD D2 <- (SP)+", "POPW D2", 0},
      {"LD [SP]+ -> A", "LD (SP)+ -> A", "POPB A", 0},
      {"ADD A <- [zp]", "ADD A <- (zp)", "ADD A <- [addr8]", 0},
      {"AND A <- [$1C]", "AND A <- ($1C)", "AND A <- [addr8]", 0x1c},
      {"LD A <- [ D1 ]", "LD A <- ( D1 )", "LD A <- [D1]", 0},
  };

  TEST_CASE("takes every addressing mode in brackets") {
    for (const Mode& m : MODES) {
      CAPTURE(m.brackets);
      CHECK(one(m.brackets) == Instr{opOf(m.name), m.operand});
    }
  }

  TEST_CASE("refuses every addressing mode in round brackets") {
    for (const Mode& m : MODES) {
      CAPTURE(m.parens);
      Assembled a = assemble(std::string(m.parens) + RAM);
      REQUIRE_EQ(a.errors.size(), 1u);
      CHECK(has(a.errors[0].message, "round brackets no longer dereference"));
      CHECK(a.program.empty());
    }
  }

  TEST_CASE("names the square spelling of the operand it refused") {
    const std::pair<const char*, const char*> REFUSED[] = {
        {"LD A <- (D1)", "Use [D1] rather than (D1)."},
        {"LD A <- (D1)+", "Use [D1]+ rather than (D1)+."},
        {"LD D1 <- (A)+", "Use [A]+ rather than (A)+."},
        {"LD A <- (D1+A)", "Use [D1+A] rather than (D1+A)."},
        {"LD A <- (D1+4)", "Use [D1+4] rather than (D1+4)."},
        {"LD A <- (zp)", "Use [zp] rather than (zp)."},
        {"ADD A <- ($1C)", "Use [$1C] rather than ($1C)."},
        {"LD (SP)- <- A", "Use [SP]- rather than (SP)-."},
        {"LD A <- (SP)+", "Use [SP]+ rather than (SP)+."},
        {"LD (D1)+ <- A", "Use [D1]+ rather than (D1)+."},
        {"LD (D1+2) <- D2", "Use [D1+2] rather than (D1+2)."},
    };
    for (const auto& [src, advice] : REFUSED) {
      CAPTURE(src);
      CHECK_EQ(firstError(std::string(src) + RAM), std::string("round brackets no longer dereference. ") + advice);
    }
  }

  TEST_CASE("parses a call in round brackets") {
    Assembled t = ok("NOP\n.ram\nq: db sin(0.25), cos(0.25), sin(0), cos(0)");
    CHECK(slice(t.ram, 4) == std::vector<uint8_t>{255, 128, 128, 255});

    const std::string cart = "NOP\n        OUT GPU_DATA2, get_lowbyte(blob)\n        OUT GPU_DATA1, get_highbyte(blob)\n"
                             "        OUT GPU_DATA0, get_bankbyte(70000)\n.data\npad:    db " +
                             repeat("0,", 299) + "0\nblob:   db 1, 2, 3";
    Assembled c = ok(cart);
    CHECK(c.labels.at("blob") == Label{Label::Kind::Data, 300});
    CHECK_EQ(c.program[1].operand & 0xff, 44);
    CHECK_EQ(c.program[2].operand & 0xff, 1);
    CHECK_EQ(c.program[3].operand & 0xff, 1);

    std::string asOperand = std::regex_replace(cart, std::regex("OUT GPU_DATA2, get_lowbyte\\(blob\\)"),
                                               "LD A <- get_lowbyte(blob)");
    Assembled o = ok(asOperand);
    CHECK(o.program[1] == Instr{opOf("LD A <- imm8"), 44});

    Assets assets;
    assets.files["a.bin"] = std::vector<uint8_t>(300);
    Assembled sized = assemble(
        "NOP\n        OUT APU_ARG, get_sizehi(snd)\n        OUT APU_ARG2, get_sizelo(snd)\n.data\nsnd: .file('a.bin')",
        &assets);
    REQUIRE(sized.errors.empty());
    CHECK_EQ(sized.program[1].operand & 0xff, 1);
    CHECK_EQ(sized.program[2].operand & 0xff, 44);

    Assembled ad = ok("LD A <- [slot]\n.ram\nslot: .addr($10)");
    CHECK(ad.labels.at("slot") == Label{Label::Kind::Ram, 0x10});
    CHECK(ad.program[0] == Instr{opOf("LD A <- [addr8]"), 0x10});
  }

  TEST_CASE("assembles a whole program of bracket operands") {
    const std::string program = R"(        LD D1 <- list
loop:   LD A <- [D1]+
        ADD A <- [sum]
        LD [sum] <- A
        LD [SP]- <- D1
        LD D1 <- [SP]+
        LD A <- [D1+1]
        LD [D2+2] <- A
        JZ loop
        HLT
.ram
sum:    db 0
list:   db 1, 2, 3
.data
tab:    db sin(0.25), cos(0.5))";
    Assembled p = ok(program);
    CHECK(slice(p.ram, p.ramLength) == std::vector<uint8_t>{0, 1, 2, 3});
    CHECK(p.cart == std::vector<uint8_t>{255, 1});
    CHECK(p.labels.at("loop") == Label{Label::Kind::Code, 1});
    CHECK(p.labels.at("sum") == Label{Label::Kind::Ram, 0});
    CHECK(p.labels.at("list") == Label{Label::Kind::Ram, 1});
    CHECK(p.labels.at("tab") == Label{Label::Kind::Data, 0});
    std::vector<uint8_t> want = {
        opOf("LD D1 <- imm16"), opOf("LD A <- [D1]+"), opOf("ADD A <- [addr8]"),
        opOf("LD [addr8] <- A"), opOf("PUSHW D1"), opOf("POPW D1"),
        opOf("LD A <- [D1+n]"), opOf("LD [D2+n] <- A"), opOf("JZ"), opOf("HLT")};
    std::vector<uint8_t> got;
    for (const Instr& i : p.program) got.push_back(i.op);
    CHECK(got == want);

    Assembled before = assemble(std::regex_replace(program, std::regex("\\[([^\\]]*)\\]"), "($1)"));
    CHECK_EQ(before.errors.size(), 7u);
    for (const AsmError& e : before.errors) CHECK(has(e.message, "round brackets no longer dereference"));
  }

  TEST_CASE("refuses a bracket group that is not the whole operand") {
    for (const char* src : {"LD A <- [$1C]+", "LD A <- ($1C)+", "LD A <- [zp]x", "LD A <- (zp)x",
                            "LD A <- pre[zp]", "LD A <- pre(zp)"}) {
      CAPTURE(src);
      Assembled a = assemble(std::string(src) + RAM);
      REQUIRE(!a.errors.empty());
      CHECK(has(a.errors[0].message, "undefined label"));
    }
  }

  TEST_CASE("says brackets when it names an addressing mode") {
    CHECK(has(firstError("LD [SP]+ <- A"), "[SP]-"));
    CHECK(has(firstError("LD A <- [SP]-"), "[SP]+"));
    CHECK(has(firstError("LD D1 <- [D1]"), "LD D1 <- [D1]"));
    CHECK(has(firstError("LD D2 <- [D2+3]"), "LD D2 <- [D2+n]"));
    CHECK(has(firstError("LD D1 <- [D1+A]"), "LD D1 <- [D1+A]"));
    CHECK(has(firstError("LD [A]+ -> A"), "LD [A]+ -> A"));
    CHECK(has(firstError("LD [D1] <- D1"), "LD [D1] <- D1"));
    CHECK(has(firstError("LD [D1+2] <- D1"), "LD [D1+n] <- D1"));
    CHECK(has(firstError("LD [A] <- A"), "stores through [A]"));
  }
}

TEST_SUITE("constant expressions in source") {
  TEST_CASE("takes an expression as an immediate") {
    CHECK_EQ(ok("        LD A <- 1 | 2").program[0].operand, 3);
    CHECK_EQ(ok("        LD A <- 4*12").program[0].operand, 48);
    CHECK_EQ(ok("        ADD A <- 1 << 3").program[0].operand, 8);
  }

  TEST_CASE("takes a round-bracket group as an immediate, which is the whole point") {
    CHECK_EQ(ok("        LD A <- (1 + 2)").program[0].operand, 3);
    CHECK_EQ(ok("        LD A <- (2 + 3) * 4").program[0].operand, 20);
  }

  TEST_CASE("still refuses a round-bracket dereference, and still names the square form") {
    const std::pair<const char*, const char*> CASES[] = {
        {"        LD A <- (D1)", "Use [D1] rather than (D1)."},
        {"        LD A <- (D1+A)", "Use [D1+A] rather than (D1+A)."},
        {"        LD A <- (D1+4)", "Use [D1+4] rather than (D1+4)."},
        {"        LD A <- (A)", "Use [A] rather than (A)."},
        {"        LD A <- (sum)", "Use [sum] rather than (sum)."},
    };
    for (const auto& [src, want] : CASES) {
      CAPTURE(src);
      CHECK(has(firstError(src), want));
    }
  }

  TEST_CASE("takes an expression as an OUT value") {
    CHECK_EQ(ok("        OUT GPU_DATA0, 1 | 2").program[0].operand & 0xff, 3);
    CHECK_EQ(ok("        AND A <- BTN_FIRE | BTN_UP").program[0].operand,
             *systemConstant("BTN_FIRE") | *systemConstant("BTN_UP"));
  }

  TEST_CASE("takes a label expression as an address, and uses the label's address") {
    Assembled r = ok("        LD D1 <- block + 8\n        HLT\n.ram\nblock:  db 0");
    CHECK_EQ(r.program[0].operand, r.labels.at("block").value + 8);
  }

  TEST_CASE("takes an expression inside a dereference") {
    Assembled r = ok("        LD A <- [zp + 1]\n        HLT\n.ram\nzp:     db 0\nnext:   db 0");
    CHECK_EQ(r.program[0].operand, r.labels.at("next").value);
  }

  TEST_CASE("takes an expression in db and dw") {
    Assembled r = ok(".ram\nsize:   db 4\nn:      db 3 * 5\nw:      dw 1 << 12");
    CHECK_EQ(r.ram[static_cast<size_t>(r.labels.at("n").value)], 15);
    const size_t w = static_cast<size_t>(r.labels.at("w").value);
    CHECK_EQ((r.ram[w] << 8) | r.ram[w + 1], 1 << 12);
  }

  TEST_CASE("folds inside a get_ macro argument") {
    Assembled r = ok("        OUT GPU_DATA2, get_lowbyte(blob + 2)\n        HLT\n.data\nblob:   db 1,2,3,4");
    CHECK_EQ(r.program[0].operand & 0xff, (r.labels.at("blob").value + 2) & 0xff);
  }

  TEST_CASE("still refuses a value too wide for the operand, with the old message") {
    CHECK_EQ(firstError("        LD A <- 200 + 100"), "value 300 does not fit in a byte");
  }

  TEST_CASE("refuses && and || by name") {
    CHECK(has(firstError("        LD A <- 1 && 2"), "|"));
    CHECK(has(firstError("        LD A <- 1 || 2"), "|"));
  }

  TEST_CASE("keeps the undefined-label message for a bare name") {
    CHECK_EQ(firstError("        LD D1 <- nosuch"), "undefined label: nosuch");
  }

  TEST_CASE("names the unknown part of an expression") {
    CHECK(has(firstError("        LD D1 <- nosuch + 1"), "nosuch"));
  }

  TEST_CASE("refuses a divide by zero at assembly time") {
    CHECK(has(firstError("        LD A <- 1 / 0"), "divide by zero"));
  }

  TEST_CASE("folds a constant expression in .addr") {
    CHECK_EQ(ok(".ram\nfixed:  .addr($10 + 2)").labels.at("fixed").value, 0x12);
  }

  TEST_CASE("folds a constant expression in a port operand, port names included") {
    const uint16_t plain = ok("        OUT GPU_DATA0, 1").program[0].operand;
    CHECK_EQ(ok("        OUT GPU_DATA0 + 0, 1").program[0].operand, plain);
    CHECK_EQ(ok("        OUT ACP_ADDR_HI + 1, 1").program[0].operand >> 8, *portNamed("ACP_ADDR_LO"));
  }

  TEST_CASE("refuses a label in either, because pass one has not placed them") {
    CHECK_EQ(firstError("        OUT nosuch + 1, 1"), "bad port number");
    CHECK(has(firstError(".ram\nfixed:  .addr(nosuch + 1)"), ".addr takes a RAM address"));
  }
}

TEST_SUITE("every registry name resolves wherever a number does") {
  // The old tool let a port name stand only as a port and a command name
  // only as an OUT value, so a command that had to travel through A was
  // written as its number. The three kinds now resolve alike.
  TEST_CASE("a command name is an immediate") {
    CHECK_EQ(ok("        LD A <- CMD_CLEAR").program[0].operand, *commandNamed("CMD_CLEAR"));
  }

  TEST_CASE("a command name joins an expression after the comma") {
    CHECK_EQ(ok("        OUT GPU_DATA0, CMD_CLEAR + 1").program[0].operand & 0xff, *commandNamed("CMD_CLEAR") + 1);
  }

  TEST_CASE("a port name is a number too, in an immediate and in data") {
    CHECK_EQ(ok("        LD A <- GPU_CMD").program[0].operand, *portNamed("GPU_CMD"));
    Assembled r = ok(".ram\np:      db GPU_CMD, CMD_CLEAR\nw:      dw ACP_ADDR_LO + BTN_FIRE");
    const size_t at = static_cast<size_t>(r.labels.at("p").value);
    CHECK_EQ(r.ram[at], *portNamed("GPU_CMD"));
    CHECK_EQ(r.ram[at + 1], *commandNamed("CMD_CLEAR"));
    const size_t w = static_cast<size_t>(r.labels.at("w").value);
    CHECK_EQ((r.ram[w] << 8) | r.ram[w + 1], *portNamed("ACP_ADDR_LO") + *systemConstant("BTN_FIRE"));
  }

  TEST_CASE("a command name folds in a count and a label expression") {
    Assembled r = ok("        LD D1 <- block + CMD_CLEAR\n        HLT\n.ram\nblock:  ds CMD_CLEAR + 2");
    CHECK_EQ(r.program[0].operand, r.labels.at("block").value + *commandNamed("CMD_CLEAR"));
    CHECK_EQ(r.ramLength, static_cast<size_t>(*commandNamed("CMD_CLEAR") + 2));
  }
}

TEST_SUITE(".equ names a number") {
  TEST_CASE("resolves as an immediate, in a port, in an OUT value, in data and in a count") {
    Assembled r = ok(
        ".equ SHIP, 3\n"
        ".equ PORT, GPU_DATA0 + 1\n"
        ".equ ROOM, SHIP * 4\n"
        "        LD A <- SHIP\n"
        "        OUT PORT, SHIP + 1\n"
        "        OUT GPU_DATA0, SHIP\n"
        "        LD D1 <- buf + SHIP\n"
        "        HLT\n"
        ".ram\n"
        "buf:    ds ROOM\n"
        "n:      db SHIP, ROOM\n"
        "fixed:  .addr(ROOM)\n");
    CHECK_EQ(r.program[0].operand, 3);
    CHECK_EQ(r.program[1].operand >> 8, *portNamed("GPU_DATA1"));
    CHECK_EQ(r.program[1].operand & 0xff, 4);
    CHECK_EQ(r.program[2].operand & 0xff, 3);
    CHECK_EQ(r.program[3].operand, r.labels.at("buf").value + 3);
    const size_t n = static_cast<size_t>(r.labels.at("n").value);
    CHECK_EQ(n, 12u);
    CHECK_EQ(r.ram[n], 3);
    CHECK_EQ(r.ram[n + 1], 12);
    CHECK_EQ(r.labels.at("fixed").value, 12);
  }

  TEST_CASE("is good in a get_ macro and in a dereference") {
    Assembled r = ok(".equ OFF, 2\n        LD A <- [zp + OFF]\n        OUT GPU_DATA2, get_lowbyte(blob + OFF)\n        HLT\n"
                     ".ram\nzp:     ds 4\n.data\nblob:   db 1,2,3,4");
    CHECK_EQ(r.program[0].operand, r.labels.at("zp").value + 2);
    CHECK_EQ(r.program[1].operand & 0xff, (r.labels.at("blob").value + 2) & 0xff);
  }

  TEST_CASE("a second definition is an error, either way round") {
    CHECK_EQ(firstError(".equ N, 1\n.equ N, 2"), "duplicate label: N");
    CHECK_EQ(firstError(".equ N, 1\nN:      HLT"), "duplicate label: N");
    CHECK_EQ(firstError("N:      HLT\n.equ N, 1"), "duplicate label: N");
  }

  TEST_CASE("refuses a machine constant's name, a label in the value and a missing value") {
    CHECK(has(firstError(".equ CMD_CLEAR, 1"), "machine's own constants"));
    CHECK(has(firstError(".equ N, buf + 1\n.ram\nbuf: db 0"), "not a label"));
    CHECK(has(firstError(".equ N"), ".equ needs"));
    CHECK(has(firstError(".equ 9, 1"), ".equ needs a name"));
  }

  TEST_CASE("an undefined name still says so") {
    CHECK_EQ(firstError("        LD A <- NOPE"), "undefined label: NOPE");
  }
}

TEST_SUITE("the ROM") {
  TEST_CASE("an assembly burns to a ROM that decodes back to the same thing") {
    Assets assets;
    assets.files["a.bin"] = {9, 8, 7};
    Assembled a = assemble("        LD A <- 1\n        HLT\n.ram\nv: db 1, dw 258\n.data\nblob: .file('a.bin')\nt: db 5",
                           &assets);
    REQUIRE(a.errors.empty());
    Cartridge c = a.cartridge();
    c.microcode = "@optimal";
    c.meta = {{"title", "test"}};
    c.basic = {{"HELLO", "10 PRINT \"HI\"\n20 GOTO 10\n"}};
    std::vector<uint8_t> bytes = encodeCartridge(c);
    CartridgeResult r = decodeCartridge(bytes);
    REQUIRE_MESSAGE(r.cartridge, r.error);
    CHECK(*r.cartridge == c);
    CHECK(r.cartridge->assets == std::vector<RomAsset>{{"file", "blob", 0, 3}});
    CHECK(r.cartridge->ram == std::vector<uint8_t>{1, 1, 2});
  }

  TEST_CASE("the optimal set never travels as text") {
    Cartridge c;
    c.microcode = serializeMicrocode(buildOptimal());
    CartridgeResult r = decodeCartridge(encodeCartridge(c));
    REQUIRE(r.cartridge);
    CHECK_EQ(r.cartridge->microcode, "@optimal");

    // A user's own set does travel as text, naive included.
    Cartridge n;
    n.microcode = serializeMicrocode(buildNaive());
    CartridgeResult rn = decodeCartridge(encodeCartridge(n));
    REQUIRE(rn.cartridge);
    CHECK_EQ(rn.cartridge->microcode, n.microcode);
  }

  TEST_CASE("refuses what is not a ROM") {
    CHECK(!decodeCartridge({1, 2, 3}).cartridge);
    std::vector<uint8_t> bytes = encodeCartridge(Cartridge{}, false);
    bytes.pop_back();
    CHECK(!decodeCartridge(bytes).cartridge);
    // A compressed one cut in half does not unpack.
    std::vector<uint8_t> packed = encodeCartridge(Cartridge{});
    packed.resize(packed.size() / 2);
    CHECK(!decodeCartridge(packed).cartridge);
  }

  TEST_CASE("a ROM is compressed on disk and comes back the same") {
    Cartridge c;
    c.program = {{0x01, 0}};
    c.microcode = "@naive";
    c.meta = {{"title", "Packed"}};
    c.sources = {{"main.asm", std::vector<uint8_t>(4000, 'A')}, {"assets/x.bin", {1, 2, 3}}};
    const std::vector<uint8_t> packed = encodeCartridge(c);
    const std::vector<uint8_t> plain = encodeCartridge(c, false);
    CHECK(packed.size() < plain.size() / 4);
    CartridgeResult r = decodeCartridge(packed);
    REQUIRE(r.cartridge);
    CHECK(*r.cartridge == c);
    CartridgeResult p = decodeCartridge(plain);
    REQUIRE(p.cartridge);
    CHECK(*p.cartridge == c);
  }
}

TEST_SUITE("program layout by slot") {
  TEST_CASE(".org places code at a slot and labels follow") {
    Assembled a = ok("        JSR handler\n        HLT\n.org $100\nhandler: LD A <- 7\n        RET");
    CHECK_EQ(a.program.size(), 0x102u);
    CHECK(a.labels.at("handler") == Label{Label::Kind::Code, 0x100});
    CHECK(a.program[0] == Instr{opOf("JSR"), 0x100});
    CHECK(a.program[2] == UNLOADED_SLOT);
    CHECK(a.program[0xff] == UNLOADED_SLOT);
    CHECK(a.program[0x100] == Instr{opOf("LD A <- imm8"), 7});
    CHECK_EQ(a.lineToInstr.at(4), 0x100);
    CHECK_EQ(a.instrToLine[0x101], 5);
  }

  TEST_CASE("two runs cannot land on one slot") {
    CHECK(has(firstError(".org 5\n        NOP\n.org 5\n        HLT"), "already used"));
    CHECK(has(firstError(".org later\nlater: NOP"), "not a label"));
  }

  TEST_CASE("a fetch from an unloaded slot crashes, a jump over it works") {
    Assembled a = ok("        JMP far\n.org 40\nfar:    HLT");
    Machine m(a.program, buildNaive());
    m.run(10);
    CHECK_EQ(m.status, Status::Halted);

    Assembled b = ok("        NOP\n.org 40\nfar:    HLT");
    Machine n(b.program, buildNaive());
    n.run(10);
    CHECK_EQ(n.status, Status::Crashed);
    CHECK_EQ(n.crash->kind, CrashKind::IllegalProgramAddress);
  }

  TEST_CASE("the ROM keeps the segments and the gaps between them") {
    Assembled a = ok("        JSR handler\n        HLT\n.org $200\nhandler: RET\n.org $300\nother:   RET");
    Cartridge c = a.cartridge();
    std::vector<uint8_t> bytes = encodeCartridge(c, false);
    int progChunks = 0;
    for (size_t i = 0; i + 4 <= bytes.size(); i++) {
      if (bytes[i] == 'P' && bytes[i + 1] == 'R' && bytes[i + 2] == 'O' && bytes[i + 3] == 'G') progChunks++;
    }
    CHECK_EQ(progChunks, 3);
    CartridgeResult r = decodeCartridge(bytes);
    REQUIRE_MESSAGE(r.cartridge, r.error);
    CHECK(r.cartridge->program == c.program);
    CHECK(r.cartridge->program[0x100] == UNLOADED_SLOT);
    CHECK_EQ(r.cartridge->program[0x300].op, opOf("RET"));
  }
}
