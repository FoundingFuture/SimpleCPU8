// double, on the coprocessor.
//
// The CPU can move eight bytes and do nothing else with them, so every
// operation on a double is an ACP command. A double therefore lives in RAM
// and never in a register: there is no register on this machine that could
// hold one.
//
// DESIGN: the temps are laid out so the coprocessor reads them WHERE THEY
// ALREADY ARE. The device's block is A, then B, then the result, each eight
// bytes for a 1 by 1 f64. So if the left operand is in slot n and the right
// in slot n + 1, pointing the device at slot n makes them its A and B with
// no copying at all, and the answer lands in slot n + 2.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cc/runtime.h"

namespace sc8::cc {

// The temp area. Slot n is at DT + n * 8.
constexpr const char* DT = "__dt";
constexpr int DSIZE = 8;

// Where a double return value waits, like __ret for the narrow types.
constexpr const char* DRET = "__dret";

std::string slotAt(int n);

extern const int F64;
extern const int I64;

// An address for a block move: a constant the assembler folds, or a zero
// page word that holds it at run time.
struct Where {
  bool isZp = false;
  std::string text;
  static Where at(std::string s) { return Where{false, std::move(s)}; }
  static Where zp(std::string s) { return Where{true, std::move(s)}; }
};

// Point the device at a block and run one command. Every latch is sent every
// time: none of them reads back, and the integer runtime uses the device too.
void acpRun(const Emit& e, const std::string& block, const std::string& cmd, int fmt,
            std::optional<int> rfmt = std::nullopt);

// Eight bytes from one fixed address to another, in one GPU command. Both
// addresses are compile time constants here, so every argument is an
// immediate.
void moveConst(const Emit& e, const std::string& dst, const std::string& src, int len = DSIZE);

// The same, where one end is a frame address worked out at run time. The
// address is in a zero page word, so its two bytes go out through A.
void moveDyn(const Emit& e, const Where& dst, const Where& src, int len = DSIZE);

// The eight bytes of a double, big-endian IEEE 754, as assembler db values.
std::vector<uint8_t> f64Bytes(double v);

// Which ACP command a C operator becomes. Empty for an operator with no
// meaning on a double.
std::string doubleOp(const std::string& op);

}  // namespace sc8::cc
