#include "cc/runtime.h"

#include "cc/layout.h"

namespace sc8::cc {

namespace {

// Where the operands and the result sit inside the block. A 1 by 1 integer
// multiply writes 128 bits, because two 64 bit integers multiply to exactly
// that, so the result slot is sixteen bytes and the block is 32.
constexpr int A_AT = 0;
constexpr int B_AT = 8;
constexpr int R_AT = 16;

std::string S(const char* s) { return std::string(s); }

void cfg(const Emit& e, const std::string& fmt) {
  // Every routine sends the whole configuration. None of it reads back, and
  // the program may have pointed the device at a block of its own.
  e("        OUT ACP_ADDR_HI, " + S(ACP_BLOCK) + " >> 8");
  e("        OUT ACP_ADDR_LO, " + S(ACP_BLOCK) + " & 255");
  e("        OUT ACP_FMT, " + fmt);
  e("        OUT ACP_ROWS, 1");
  e("        OUT ACP_COLS, 1");
}

// Fill the six high bytes of an operand slot, then its low two from a zero
// page word. Zero fill for unsigned, sign fill for signed.
//
// DESIGN: the block is reached through D2, not by direct addressing. Direct
// byte addressing is zero page only on this machine, and the scratch block
// sits after every global, which in a program the size of the BASIC
// interpreter is nowhere near the zero page. Going through D2 also makes the
// fill cheaper: the eight bytes are consecutive, so [D2]+ walks them and no
// store needs an address of its own.
//
// D2 is left pointing just past the slot, so A and B fill in one walk.
void operand(const Emit& e, int at, const std::string& src, bool isSigned, const std::string& tag) {
  if (isSigned) {
    // The fill byte is 0xFF when the value is negative, and the sign lives in
    // bit 7 of the high byte. N is set by the load, so JN reads it directly.
    // The label carries the routine's name: three routines sign fill, and
    // one label spelled from the operand alone would be defined three times.
    const std::string lbl = tag + "_neg" + std::to_string(at);
    e("        LD A <- [" + src + "]");
    e("        JN " + lbl);
    e("        LD A <- 0");
    e("        JMP " + lbl + "_done");
    e(lbl + ":  LD A <- $FF");
    e(lbl + "_done:");
  } else {
    e("        LD A <- 0");
  }
  for (int i = 0; i < 6; i++) e("        LD [D2]+ <- A");
  e("        LD A <- [" + src + "]");
  e("        LD [D2]+ <- A");
  e("        LD A <- [" + src + "+1]");
  e("        LD [D2]+ <- A");
}

// The low sixteen bits of the result, back into __ra. Big-endian, so they
// are the LAST two bytes of the slot. A 1 by 1 integer multiply widens to
// sixteen bytes, and reading its low word at +6 would read the middle of it.
void result(const Emit& e, int width) {
  e("        LD D2 <- " + S(ACP_BLOCK) + " + " + std::to_string(R_AT + width - 2));
  e("        LD A <- [D2]+");
  e("        LD [" + S(ZP_RA) + "] <- A");
  e("        LD A <- [D2]");
  e("        LD [" + S(ZP_RA) + "+1] <- A");
}

void binary(const Emit& e, const std::string& name, const std::string& cmd, const std::string& fmt,
            bool isSigned, int width) {
  e("");
  e("; " + name + ": [" + S(ZP_RA) + "] " + cmd + " [" + S(ZP_RB) + "] -> [" + S(ZP_RA) + "]");
  e(name + ":");
  e("        LD D2 <- " + S(ACP_BLOCK));
  operand(e, A_AT, ZP_RA, isSigned, name);
  // D2 already points at B: the two slots are consecutive.
  operand(e, B_AT, ZP_RB, isSigned, name);
  cfg(e, fmt);
  e("        OUT ACP_CMD, " + cmd);
  result(e, width);
  e("        RET");
}

}  // namespace

int blockSize(const std::set<std::string>& used) {
  if (used.empty()) return 0;
  return used.count("__mul16") ? 32 : 24;
}

std::vector<std::string> emitRuntime(const std::set<std::string>& used) {
  std::vector<std::string> out;
  const Emit e = [&](const std::string& l) { out.push_back(l); };
  if (used.empty()) return out;

  // A 1 by 1 integer multiply is the one widening case, so its result sits at
  // R and is sixteen bytes. Everything else writes eight.
  if (used.count("__mul16")) binary(e, "__mul16", "ACP_MUL", "ACP_U64", false, 16);
  if (used.count("__udiv16")) binary(e, "__udiv16", "ACP_DIV", "ACP_U64", false, 8);
  if (used.count("__urem16")) binary(e, "__urem16", "ACP_REM", "ACP_U64", false, 8);
  if (used.count("__sdiv16")) binary(e, "__sdiv16", "ACP_DIV", "ACP_I64", true, 8);
  if (used.count("__srem16")) binary(e, "__srem16", "ACP_REM", "ACP_I64", true, 8);
  if (used.count("__shl16")) binary(e, "__shl16", "ACP_SHL", "ACP_U64", false, 8);
  if (used.count("__ushr16")) binary(e, "__ushr16", "ACP_SHR", "ACP_U64", false, 8);
  if (used.count("__sshr16")) binary(e, "__sshr16", "ACP_ASR", "ACP_I64", true, 8);
  return out;
}

}  // namespace sc8::cc
