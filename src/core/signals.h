// Control signal metadata. The conflict detector and the row executor both
// derive their behavior from this single table.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sc8 {

// The X-macro keeps the enum, the name table and the metadata table in one
// place, so a new signal cannot exist in one and not the others.
//
// Columns: name, register atoms written, memory access, step unit, ALU
// select, ALU write, IO, address base, address modifier, stack modifier.
#define SC8_SIGNALS(X)                                                        \
  X(FETCH, W(IR), ProgRead, 0, AluNone, 0, 0, 0, 0, 0)                        \
  X(PC_INC, W(PCH, PCL), MemNone, 1, AluNone, 0, 0, 0, 0, 0)                  \
  X(PC_LOAD, W(PCH, PCL), MemNone, 0, AluNone, 0, 0, 0, 0, 0)                 \
  X(PC_LOAD_Z, W(PCH, PCL), MemNone, 0, AluNone, 0, 0, 0, 0, 0)               \
  X(PC_LOAD_C, W(PCH, PCL), MemNone, 0, AluNone, 0, 0, 0, 0, 0)               \
  X(PC_LOAD_N, W(PCH, PCL), MemNone, 0, AluNone, 0, 0, 0, 0, 0)               \
  X(PC_LOAD_V, W(PCH, PCL), MemNone, 0, AluNone, 0, 0, 0, 0, 0)               \
  X(PC_FROM_D1, W(PCH, PCL), MemNone, 0, AluNone, 0, 0, 0, 0, 0)              \
  X(PC_FROM_D2, W(PCH, PCL), MemNone, 0, AluNone, 0, 0, 0, 0, 0)              \
  X(HALT, W(), MemNone, 0, AluNone, 0, 0, 0, 0, 0)                            \
  X(ACC_TO_A, W(A), MemNone, 0, AluNone, 0, 0, 0, 0, 0)                       \
  X(IMM_TO_B, W(B), MemNone, 0, AluNone, 0, 0, 0, 0, 0)                       \
  X(RAM_TO_B, W(B), RamRead, 0, AluNone, 0, 0, 0, 0, 0)                       \
  X(STK_TO_B, W(B), StackRead, 0, AluNone, 0, 0, 0, 0, 0)                     \
  X(ALU_ADD, W(), MemNone, 0, Add, 0, 0, 0, 0, 0)                             \
  X(ALU_SUB, W(), MemNone, 0, Sub, 0, 0, 0, 0, 0)                             \
  X(ALU_ADC, W(), MemNone, 0, Adc, 0, 0, 0, 0, 0)                             \
  X(ALU_SBC, W(), MemNone, 0, Sbc, 0, 0, 0, 0, 0)                             \
  X(ALU_AND, W(), MemNone, 0, And, 0, 0, 0, 0, 0)                             \
  X(ALU_OR, W(), MemNone, 0, Or, 0, 0, 0, 0, 0)                               \
  X(ALU_XOR, W(), MemNone, 0, Xor, 0, 0, 0, 0, 0)                             \
  X(ALU_PASS_B, W(), MemNone, 0, PassB, 0, 0, 0, 0, 0)                        \
  X(ACC_LOAD_ALU, W(ACC), MemNone, 0, AluNone, 1, 0, 0, 0, 0)                 \
  X(FLAGS_LOAD, W(FLAGS), MemNone, 0, AluNone, 1, 0, 0, 0, 0)                 \
  X(RAM_WRITE_ACC, W(), RamWrite, 0, AluNone, 0, 0, 0, 0, 0)                  \
  X(RAM_TO_D1H, W(D1H), RamRead, 0, AluNone, 0, 0, 0, 0, 0)                   \
  X(RAM_TO_D1L, W(D1L), RamRead, 0, AluNone, 0, 0, 0, 0, 0)                   \
  X(RAM_TO_D2H, W(D2H), RamRead, 0, AluNone, 0, 0, 0, 0, 0)                   \
  X(RAM_TO_D2L, W(D2L), RamRead, 0, AluNone, 0, 0, 0, 0, 0)                   \
  X(RAM_WRITE_D1H, W(), RamWrite, 0, AluNone, 0, 0, 0, 0, 0)                  \
  X(RAM_WRITE_D1L, W(), RamWrite, 0, AluNone, 0, 0, 0, 0, 0)                  \
  X(RAM_WRITE_D2H, W(), RamWrite, 0, AluNone, 0, 0, 0, 0, 0)                  \
  X(RAM_WRITE_D2L, W(), RamWrite, 0, AluNone, 0, 0, 0, 0, 0)                  \
  X(ADDR_OP8, W(), MemNone, 0, AluNone, 0, 0, 1, 0, 0)                        \
  X(ADDR_OP16, W(), MemNone, 0, AluNone, 0, 0, 1, 0, 0)                       \
  X(ADDR_D1, W(), MemNone, 0, AluNone, 0, 0, 1, 0, 0)                         \
  X(ADDR_D2, W(), MemNone, 0, AluNone, 0, 0, 1, 0, 0)                         \
  X(ADDR_A, W(), MemNone, 0, AluNone, 0, 0, 1, 0, 0)                          \
  X(EA_OFF_OP8, W(), MemNone, 0, AluNone, 0, 0, 0, 1, 0)                      \
  X(EA_OFF_A, W(), MemNone, 0, AluNone, 0, 0, 0, 1, 0)                        \
  X(EA_CIN, W(), MemNone, 0, AluNone, 0, 0, 0, 1, 0)                          \
  X(D1_LOAD_OP16, W(D1H, D1L), MemNone, 0, AluNone, 0, 0, 0, 0, 0)            \
  X(D2_LOAD_OP16, W(D2H, D2L), MemNone, 0, AluNone, 0, 0, 0, 0, 0)            \
  X(D1_TSTZ, W(FLAGS), MemNone, 0, AluNone, 0, 0, 0, 0, 0)                    \
  X(D2_TSTZ, W(FLAGS), MemNone, 0, AluNone, 0, 0, 0, 0, 0)                    \
  X(D1_INC, W(D1H, D1L), MemNone, 1, AluNone, 0, 0, 0, 0, 0)                  \
  X(D2_INC, W(D2H, D2L), MemNone, 1, AluNone, 0, 0, 0, 0, 0)                  \
  X(ACC_INC, W(ACC), MemNone, 1, AluNone, 0, 0, 0, 0, 0)                      \
  X(SP_INC, W(SP), MemNone, 1, AluNone, 0, 0, 0, 0, 0)                        \
  X(SP_DEC, W(SP), MemNone, 1, AluNone, 0, 0, 0, 0, 0)                        \
  X(STK_WRITE_ACC, W(), StackWrite, 0, AluNone, 0, 0, 0, 0, 0)                \
  X(STK_WRITE_PCH, W(), StackWrite, 0, AluNone, 0, 0, 0, 0, 0)                \
  X(STK_WRITE_PCL, W(), StackWrite, 0, AluNone, 0, 0, 0, 0, 0)                \
  X(STK_WRITE_D1H, W(), StackWrite, 0, AluNone, 0, 0, 0, 0, 0)                \
  X(STK_WRITE_D1L, W(), StackWrite, 0, AluNone, 0, 0, 0, 0, 0)                \
  X(STK_WRITE_D2H, W(), StackWrite, 0, AluNone, 0, 0, 0, 0, 0)                \
  X(STK_WRITE_D2L, W(), StackWrite, 0, AluNone, 0, 0, 0, 0, 0)                \
  X(STK_TO_PCL, W(PCL), StackRead, 0, AluNone, 0, 0, 0, 0, 0)                 \
  X(STK_TO_PCH, W(PCH), StackRead, 0, AluNone, 0, 0, 0, 0, 0)                 \
  X(STK_TO_D1H, W(D1H), StackRead, 0, AluNone, 0, 0, 0, 0, 0)                 \
  X(STK_TO_D1L, W(D1L), StackRead, 0, AluNone, 0, 0, 0, 0, 0)                 \
  X(STK_TO_D2H, W(D2H), StackRead, 0, AluNone, 0, 0, 0, 0, 0)                 \
  X(STK_TO_D2L, W(D2L), StackRead, 0, AluNone, 0, 0, 0, 0, 0)                 \
  X(STK_CIN, W(), MemNone, 0, AluNone, 0, 0, 0, 0, 1)                         \
  X(IO_WRITE_IMM, W(), MemNone, 0, AluNone, 0, 1, 0, 0, 0)                    \
  X(IO_WRITE_ACC, W(), MemNone, 0, AluNone, 0, 1, 0, 0, 0)                    \
  X(IO_READ, W(ACC), MemNone, 0, AluNone, 0, 1, 0, 0, 0)                      \
  X(IO_READ_D1H, W(D1H), MemNone, 0, AluNone, 0, 1, 0, 0, 0)                  \
  X(IO_READ_D1L, W(D1L), MemNone, 0, AluNone, 0, 1, 0, 0, 0)                  \
  X(IO_READ_D2H, W(D2H), MemNone, 0, AluNone, 0, 1, 0, 0, 0)                  \
  X(IO_READ_D2L, W(D2L), MemNone, 0, AluNone, 0, 1, 0, 0, 0)

enum class Signal : uint8_t {
#define SC8_ENUM(name, ...) name,
  SC8_SIGNALS(SC8_ENUM)
#undef SC8_ENUM
};

constexpr int SIGNAL_COUNT = 0
#define SC8_COUNT(...) +1
    SC8_SIGNALS(SC8_COUNT)
#undef SC8_COUNT
    ;

// The register atoms a signal may write, as bits. PC and the D registers
// are two atoms each, because half loads exist.
enum RegAtom : uint16_t {
  PCH = 1 << 0,
  PCL = 1 << 1,
  IR = 1 << 2,
  ACC = 1 << 3,
  A = 1 << 4,
  B = 1 << 5,
  D1H = 1 << 6,
  D1L = 1 << 7,
  D2H = 1 << 8,
  D2L = 1 << 9,
  SP = 1 << 10,
  FLAGS = 1 << 11,
};

enum class MemAccess : uint8_t { MemNone, ProgRead, RamRead, RamWrite, StackRead, StackWrite };

enum class AluOp : uint8_t { AluNone, Add, Sub, Adc, Sbc, And, Or, Xor, PassB };

struct SignalMeta {
  std::string_view name;
  uint16_t writes;  // RegAtom bits
  MemAccess mem;
  bool step;
  AluOp aluSelect;
  bool aluWrite;
  bool io;
  bool addrBase;
  bool addrMod;
  bool stackMod;
};

const SignalMeta& signalMeta(Signal s);
std::string_view signalName(Signal s);

// Parse a signal name. Empty when the name is unknown.
std::optional<Signal> signalByName(std::string_view name);

// Every signal name, in table order.
std::span<const std::string_view> signalNames();

// One microcode row: the signals asserted in one cycle.
using Row = std::vector<Signal>;

// Build a row from names, for tests and for hand written microcode. Unknown
// names are an error in the caller's hands, so this one throws.
Row rowOf(std::initializer_list<std::string_view> names);

// The row as comma separated names.
std::string rowText(const Row& row);

}  // namespace sc8
