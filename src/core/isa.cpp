#include "core/isa.h"

#include <array>
#include <string>
#include <unordered_map>

namespace sc8 {

namespace {

using enum OperandKind;

constexpr std::array<OpDef, 141> OPS = {{
    {0x00, "NOP", None},
    {0x01, "HLT", None},

    // The indirect pair, beside the direct jumps they extend. It is JMP D1
    // and not JMP [D1]: brackets are contents-of here, and this wants the
    // register.
    {0x04, "JMP D1", None},
    {0x05, "JMP D2", None},
    {0x06, "JSR D1", None},
    {0x07, "JSR D2", None},

    {0x08, "JMP", Target},
    {0x09, "JZ", Target},
    {0x0a, "JC", Target},
    {0x0b, "JN", Target},
    {0x0c, "JV", Target},
    {0x0e, "JSR", Target},
    {0x0f, "RET", None},

    {0x10, "LD A <- imm8", Imm8},
    {0x11, "LD A <- [addr8]", Addr8},
    {0x12, "LD [addr8] <- A", Addr8},
    {0x13, "LD A <- [A]", None},
    {0x14, "LD A <- [D1]", None},
    {0x15, "LD A <- [D2]", None},
    {0x16, "LD A <- [D1]+", None},
    {0x17, "LD A <- [D2]+", None},
    {0x18, "LD [D1] <- A", None},
    {0x19, "LD [D2] <- A", None},
    {0x1a, "LD [D1]+ <- A", None},
    {0x1b, "LD [D2]+ <- A", None},
    {0x1c, "LD A <- [D1+n]", Disp8},
    {0x1d, "LD A <- [D2+n]", Disp8},
    {0x1e, "LD A <- [D1+A]", None},
    {0x1f, "LD A <- [D2+A]", None},
    {0x20, "LD [D1+n] <- A", Disp8},
    {0x21, "LD [D2+n] <- A", Disp8},

    {0x22, "LD D1 <- imm16", Imm16},
    {0x23, "LD D2 <- imm16", Imm16},
    {0x24, "LD D1 <- [addr16]", Addr16},
    {0x25, "LD D2 <- [addr16]", Addr16},
    {0x26, "LD [addr16] <- D1", Addr16},
    {0x27, "LD [addr16] <- D2", Addr16},
    {0x28, "LD D2 <- [D1]", None},
    {0x29, "LD D1 <- [D2]", None},
    {0x2a, "LD D2 <- [D1]+", None},
    {0x2b, "LD D1 <- [D2]+", None},
    {0x2c, "LD D1 <- [A]", None},
    {0x2d, "LD D2 <- [A]", None},
    {0x2e, "LD D1 <- [A]+", None},
    {0x2f, "LD D2 <- [A]+", None},
    {0x30, "LD D2 <- [D1+n]", Disp8},
    {0x31, "LD D1 <- [D2+n]", Disp8},
    {0x32, "LD D2 <- [D1+A]", None},
    {0x33, "LD D1 <- [D2+A]", None},
    {0x34, "LD [D1] <- D2", None},
    {0x35, "LD [D2] <- D1", None},
    {0x36, "LD [D1+n] <- D2", Disp8},
    {0x37, "LD [D2+n] <- D1", Disp8},

    // Address arithmetic: the EA adder's sum loaded into a D register
    // instead of sent to RAM. D1+n takes the full operand, so D1-8 is
    // D1+$FFF8.
    {0x38, "LD D1 <- D1+n", Off16},
    {0x39, "LD D2 <- D2+n", Off16},
    {0x3a, "LD D1 <- D2+n", Off16},
    {0x3b, "LD D2 <- D1+n", Off16},
    {0x3c, "LD D1 <- D1+A", None},
    {0x3d, "LD D2 <- D2+A", None},
    {0x3e, "LD D1 <- D2+A", None},
    {0x3f, "LD D2 <- D1+A", None},

    {0x40, "ADD A <- [addr8]", Addr8},
    {0x41, "SUB A <- [addr8]", Addr8},
    {0x42, "AND A <- [addr8]", Addr8},
    {0x43, "OR A <- [addr8]", Addr8},
    {0x44, "XOR A <- [addr8]", Addr8},
    {0x45, "ADC A <- [addr8]", Addr8},
    {0x46, "SBC A <- [addr8]", Addr8},
    {0x47, "CMP A, [addr8]", Addr8},
    {0x48, "ADD A <- imm8", Imm8},
    {0x49, "SUB A <- imm8", Imm8},
    {0x4a, "AND A <- imm8", Imm8},
    {0x4b, "OR A <- imm8", Imm8},
    {0x4c, "XOR A <- imm8", Imm8},
    {0x4d, "ADC A <- imm8", Imm8},
    {0x4e, "SBC A <- imm8", Imm8},
    {0x4f, "CMP A, imm8", Imm8},

    {0x50, "PUSHB A", None},
    {0x51, "POPB A", None},
    {0x52, "PUSHW D1", None},
    {0x53, "PUSHW D2", None},
    {0x54, "POPW D1", None},
    {0x55, "POPW D2", None},

    {0x58, "INC D1", None},
    {0x59, "INC D2", None},

    {0x60, "OUT", PortImm},
    {0x61, "OUTA", Port},
    {0x62, "INB", Port},
    {0x63, "INW D1", Port},
    {0x64, "INW D2", Port},

    // The inverted conditional jumps. The low nibble is the jump they
    // invert: JZ is $09, JNZ is $79.
    {0x79, "JNZ", Target},
    {0x7a, "JNC", Target},
    {0x7b, "JP", Target},
    {0x7c, "JNV", Target},

    // The ALU on a byte a D register points at, n bytes in: a C local in
    // its frame, or a field of a record. The low nibble matches $40-$47.
    {0x80, "ADD A <- [D1+n]", Disp8},
    {0x81, "SUB A <- [D1+n]", Disp8},
    {0x82, "AND A <- [D1+n]", Disp8},
    {0x83, "OR A <- [D1+n]", Disp8},
    {0x84, "XOR A <- [D1+n]", Disp8},
    {0x85, "ADC A <- [D1+n]", Disp8},
    {0x86, "SBC A <- [D1+n]", Disp8},
    {0x87, "CMP A, [D1+n]", Disp8},
    {0x88, "ADD A <- [D2+n]", Disp8},
    {0x89, "SUB A <- [D2+n]", Disp8},
    {0x8a, "AND A <- [D2+n]", Disp8},
    {0x8b, "OR A <- [D2+n]", Disp8},
    {0x8c, "XOR A <- [D2+n]", Disp8},
    {0x8d, "ADC A <- [D2+n]", Disp8},
    {0x8e, "SBC A <- [D2+n]", Disp8},
    {0x8f, "CMP A, [D2+n]", Disp8},

    // AND that sets the flags and keeps A.
    {0x90, "TST A, [addr8]", Addr8},
    {0x91, "TST A, imm8", Imm8},
    {0x92, "TST A, [D1+n]", Disp8},
    {0x93, "TST A, [D2+n]", Disp8},
    {0x94, "TST A, [D3+n]", Disp8},

    // One bit left or right. C takes the bit that leaves.
    {0x98, "SHL A", None},
    {0x99, "SHR A", None},
    {0x9a, "ROL A", None},
    {0x9b, "ROR A", None},
    {0x9c, "ASR A", None},

    // D3, the data stack pointer. A C frame is [D3+n]: its locals, the
    // parameters above them, and every address the address adder makes.
    {0xa0, "LD A <- [D3+n]", Disp8},
    {0xa1, "LD [D3+n] <- A", Disp8},
    {0xa2, "LD D1 <- [D3+n]", Disp8},
    {0xa3, "LD D2 <- [D3+n]", Disp8},
    {0xa4, "LD [D3+n] <- D1", Disp8},
    {0xa5, "LD [D3+n] <- D2", Disp8},
    {0xa6, "LD D3 <- D3+n", Off16},
    {0xa7, "LD D1 <- D3+n", Off16},
    {0xa8, "LD D2 <- D3+n", Off16},
    {0xa9, "LD D3 <- D1+n", Off16},
    {0xaa, "LD D3 <- D2+n", Off16},
    {0xab, "LD D3 <- [addr16]", Addr16},
    {0xac, "LD [addr16] <- D3", Addr16},

    // The ALU on a byte of the frame. The low nibble matches $40-$47.
    {0xb0, "ADD A <- [D3+n]", Disp8},
    {0xb1, "SUB A <- [D3+n]", Disp8},
    {0xb2, "AND A <- [D3+n]", Disp8},
    {0xb3, "OR A <- [D3+n]", Disp8},
    {0xb4, "XOR A <- [D3+n]", Disp8},
    {0xb5, "ADC A <- [D3+n]", Disp8},
    {0xb6, "SBC A <- [D3+n]", Disp8},
    {0xb7, "CMP A, [D3+n]", Disp8},
}};

struct Tables {
  std::unordered_map<std::string_view, const OpDef*> byName;
  std::array<const OpDef*, 256> byCode{};
  Tables() {
    for (const OpDef& d : OPS) {
      byName.emplace(d.name, &d);
      byCode[d.op] = &d;
    }
  }
};

const Tables& tables() {
  static const Tables t;
  return t;
}

}  // namespace

std::span<const OpDef> ops() { return OPS; }

const OpDef* opByName(std::string_view name) {
  const auto& m = tables().byName;
  auto it = m.find(name);
  return it == m.end() ? nullptr : it->second;
}

const OpDef* opByCode(uint8_t op) { return tables().byCode[op]; }

}  // namespace sc8
