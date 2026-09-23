// The instruction set. Canonical names double as microcode section keys.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace sc8 {

enum class OperandKind {
  None,     // operand must be 0
  Imm8,     // low byte used as a value
  Addr8,    // low byte used as a zero page address
  Disp8,    // low byte used as a displacement
  Imm16,    // full operand as a value (D loads)
  Addr16,   // full operand as a data RAM address
  Target,   // full operand as a program address
  Port,     // high byte = port
  PortImm,  // high byte = port, low byte = data
};

struct OpDef {
  uint8_t op;
  std::string_view name;
  OperandKind operand;
};

struct Instr {
  uint8_t op = 0;
  uint16_t operand = 0;
  bool operator==(const Instr&) const = default;
};

// The opcode of a program slot nothing was loaded into. A program with
// .org gaps is a sparse vector, and a fetch from such a slot crashes the
// machine the way a fetch past the end does. No instruction has this code.
constexpr uint8_t UNLOADED_OP = 0xff;
constexpr Instr UNLOADED_SLOT{UNLOADED_OP, 0xffff};

// Every opcode, in opcode order.
std::span<const OpDef> ops();

// Lookup by canonical name, such as "LD A <- [D1]+". Null when unknown.
const OpDef* opByName(std::string_view name);

// Lookup by opcode byte. Null for a hole in the map.
const OpDef* opByCode(uint8_t op);

}  // namespace sc8
