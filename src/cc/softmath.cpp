#include "cc/softmath.h"

#include "cc/layout.h"
#include "cc/runtime.h"

namespace sc8::cc {

namespace {

std::string S(const char* s) { return std::string(s); }

// Every routine here works left to right, from the top bit down, because
// this machine has no right shift. Doubling is an add, and the top bit of a
// word is the N flag after a load of its high byte. Those two facts are the
// whole trick.

// dst += src, sixteen bits, low byte first so the carry chains.
void add16(const Emit& e, const std::string& dst, const std::string& src) {
  e("        LD A <- [" + dst + "+1]");
  e("        ADD A <- [" + src + "+1]");
  e("        LD [" + dst + "+1] <- A");
  e("        LD A <- [" + dst + "]");
  e("        ADC A <- [" + src + "]");
  e("        LD [" + dst + "] <- A");
}

// dst -= src, and C is left holding the borrow for the caller to test.
void sub16(const Emit& e, const std::string& dst, const std::string& src) {
  e("        LD A <- [" + dst + "+1]");
  e("        SUB A <- [" + src + "+1]");
  e("        LD [" + dst + "+1] <- A");
  e("        LD A <- [" + dst + "]");
  e("        SBC A <- [" + src + "]");
  e("        LD [" + dst + "] <- A");
}

void zero16(const Emit& e, const std::string& at) {
  e("        LD D2 <- 0");
  e("        LD [" + at + "] <- D2");
}

void copy16(const Emit& e, const std::string& dst, const std::string& src) {
  e("        LD D2 <- [" + src + "]");
  e("        LD [" + dst + "] <- D2");
}

std::string bare(const std::string& name) { return name.starts_with("__") ? name.substr(2) : name; }

// Multiply, left to right. Sixteen rounds of: double the product, and add B
// when A's top bit is set, then double A.
void mul(const Emit& e) {
  e("");
  e("; __mul16, in software. Doubling is an add and there is no right shift,");
  e("; so this walks A from its TOP bit down. Compare it with one ACP_MUL.");
  e("__mul16:");
  zero16(e, ZP_RP);
  e("        LD A <- 16");
  e("        LD [" + S(ZP_RC) + "] <- A");
  e("__mul16_loop:");
  add16(e, ZP_RP, ZP_RP);  // product <<= 1
  e("        LD A <- [" + S(ZP_RA) + "]");  // the load sets N from bit 15
  e("        JN __mul16_add");
  e("        JMP __mul16_next");
  e("__mul16_add:");
  add16(e, ZP_RP, ZP_RB);
  e("__mul16_next:");
  add16(e, ZP_RA, ZP_RA);  // a <<= 1
  e("        LD A <- [" + S(ZP_RC) + "]");
  e("        SUB A <- 1");
  e("        LD [" + S(ZP_RC) + "] <- A");
  e("        JZ __mul16_done");
  e("        JMP __mul16_loop");
  e("__mul16_done:");
  copy16(e, ZP_RA, ZP_RP);
  e("        RET");
}

// Restoring division, most significant bit first. The remainder is shifted
// up a bit at a time and the divisor subtracted when it fits, which is long
// division in base two.
void divmod(const Emit& e, const std::string& name, bool wantRemainder) {
  const std::string L = bare(name);
  e("");
  e("; " + name + ", in software. Restoring division, one bit a round.");
  e(name + ":");
  // Dividing by zero gives all ones and the dividend back, which is what
  // div8 does and what the manual documents.
  e("        LD A <- [" + S(ZP_RB) + "]");
  e("        OR A <- [" + S(ZP_RB) + "+1]");
  e("        JZ __" + L + "_byzero");
  zero16(e, ZP_RP);
  zero16(e, ZP_RQ);
  e("        LD A <- 16");
  e("        LD [" + S(ZP_RC) + "] <- A");
  e("__" + L + "_loop:");
  add16(e, ZP_RQ, ZP_RQ);  // quotient <<= 1
  add16(e, ZP_RP, ZP_RP);  // remainder <<= 1
  e("        LD A <- [" + S(ZP_RA) + "]");  // bring down the dividend's top bit
  e("        JN __" + L + "_one");
  e("        JMP __" + L + "_shifted");
  e("__" + L + "_one:");
  e("        LD A <- [" + S(ZP_RP) + "+1]");
  e("        OR A <- 1");
  e("        LD [" + S(ZP_RP) + "+1] <- A");
  e("__" + L + "_shifted:");
  add16(e, ZP_RA, ZP_RA);  // dividend <<= 1
  // Try the subtract. C set after the high byte means it did not fit.
  sub16(e, ZP_RP, ZP_RB);
  e("        JC __" + L + "_restore");
  e("        LD A <- [" + S(ZP_RQ) + "+1]");  // it fitted: set the quotient's low bit
  e("        OR A <- 1");
  e("        LD [" + S(ZP_RQ) + "+1] <- A");
  e("        JMP __" + L + "_next");
  e("__" + L + "_restore:");
  add16(e, ZP_RP, ZP_RB);
  e("__" + L + "_next:");
  e("        LD A <- [" + S(ZP_RC) + "]");
  e("        SUB A <- 1");
  e("        LD [" + S(ZP_RC) + "] <- A");
  e("        JZ __" + L + "_done");
  e("        JMP __" + L + "_loop");
  e("__" + L + "_done:");
  copy16(e, ZP_RA, wantRemainder ? ZP_RP : ZP_RQ);
  e("        RET");
  e("__" + L + "_byzero:");
  if (wantRemainder) {
    e("        RET");  // the dividend is already in __ra
  } else {
    e("        LD D2 <- 65535");
    e("        LD [" + S(ZP_RA) + "] <- D2");
    e("        RET");
  }
}

// Two's complement in place: invert both bytes and add one.
void negate(const Emit& e, const std::string& at) {
  e("        LD A <- [" + at + "+1]");
  e("        XOR A <- $FF");
  e("        ADD A <- 1");
  e("        LD [" + at + "+1] <- A");
  e("        LD A <- [" + at + "]");
  e("        XOR A <- $FF");
  e("        ADC A <- 0");
  e("        LD [" + at + "] <- A");
}

// A signed divide or remainder: take the signs off, do it unsigned, put the
// sign back. C truncates toward zero and the remainder follows the dividend.
void signedDiv(const Emit& e, const std::string& name, const std::string& base, bool wantRemainder) {
  const std::string L = bare(name);
  e("");
  e("; " + name + ": signs off, unsigned, sign back. C truncates toward zero.");
  e(name + ":");
  e("        LD A <- 0");
  e("        LD [" + S(ZP_RC) + "] <- A");  // the sign of the answer
  e("        LD A <- [" + S(ZP_RA) + "]");
  e("        JN __" + L + "_nega");
  e("        JMP __" + L + "_posa");
  e("__" + L + "_nega:");
  negate(e, ZP_RA);
  e("        LD A <- 1");
  e("        LD [" + S(ZP_RC) + "] <- A");
  e("__" + L + "_posa:");
  e("        LD A <- [" + S(ZP_RB) + "]");
  e("        JN __" + L + "_negb");
  e("        JMP __" + L + "_posb");
  e("__" + L + "_negb:");
  negate(e, ZP_RB);
  if (!wantRemainder) {
    // The remainder takes the dividend's sign alone, so only a quotient
    // flips for a negative divisor.
    e("        LD A <- [" + S(ZP_RC) + "]");
    e("        XOR A <- 1");
    e("        LD [" + S(ZP_RC) + "] <- A");
  }
  e("__" + L + "_posb:");
  e("        PUSH A");
  e("        LD A <- [" + S(ZP_RC) + "]");
  e("        PUSH A");
  e("        JSR " + base);
  e("        POP A");
  e("        LD [" + S(ZP_RC) + "] <- A");
  e("        POP A");
  e("        LD A <- [" + S(ZP_RC) + "]");
  e("        JZ __" + L + "_out");
  negate(e, ZP_RA);
  e("__" + L + "_out:");
  e("        RET");
}

// Invert both bytes, in place. Not a negation: the arithmetic shift wants
// this one, because ~((~v) >>u n) is exactly floor(v / 2^n) for a negative
// v. Negating twice instead truncates toward zero, which made -9 >> 1 come
// out as -4 where an arithmetic shift has to floor it to -5.
void complement(const Emit& e, const std::string& at) {
  e("        LD A <- [" + at + "+1]");
  e("        XOR A <- $FF");
  e("        LD [" + at + "+1] <- A");
  e("        LD A <- [" + at + "]");
  e("        XOR A <- $FF");
  e("        LD [" + at + "] <- A");
}

// Shift left by __rb, which is adds.
void shl(const Emit& e) {
  e("");
  e("; __shl16: doubling, __rb times. The one shift this CPU does cheaply.");
  e("__shl16:");
  e("        LD A <- [" + S(ZP_RB) + "+1]");
  e("        LD [" + S(ZP_RC) + "] <- A");
  e("__shl16_loop:");
  e("        LD A <- [" + S(ZP_RC) + "]");
  e("        JZ __shl16_done");
  add16(e, ZP_RA, ZP_RA);
  e("        LD A <- [" + S(ZP_RC) + "]");
  e("        SUB A <- 1");
  e("        LD [" + S(ZP_RC) + "] <- A");
  e("        JMP __shl16_loop");
  e("__shl16_done:");
  e("        RET");
}

// Shift right, which the CPU cannot do at all. One bit a round, from the top
// down, rebuilding the answer as it goes.
void shr(const Emit& e, const std::string& name, bool arithmetic) {
  const std::string L = bare(name);
  e("");
  e("; " + name + ": sixteen rounds, top bit down. There is no right shift, so");
  e("; the answer is BUILT rather than shifted.");
  e(name + ":");
  e("        LD A <- [" + S(ZP_RB) + "+1]");
  e("        LD [" + S(ZP_RC) + "] <- A");
  e("        JZ __" + L + "_out");  // a shift by zero is the value itself
  e("        LD A <- 16");
  e("        SUB A <- [" + S(ZP_RC) + "]");
  e("        JC __" + L + "_allgone");  // shifting by more than the width
  e("        LD [" + S(ZP_RC) + "] <- A");
  e("        JZ __" + L + "_allgone");
  zero16(e, ZP_RP);
  if (arithmetic) {
    // A negative value is COMPLEMENTED, shifted unsigned, and complemented
    // back. That identity floors, which is what an arithmetic shift does and
    // the one thing a divide cannot.
    e("        LD A <- 0");
    e("        LD [" + S(ZP_RQ) + "] <- A");
    e("        LD A <- [" + S(ZP_RA) + "]");
    e("        JN __" + L + "_neg");
    e("        JMP __" + L + "_body");
    e("__" + L + "_neg:");
    complement(e, ZP_RA);
    e("        LD A <- 1");
    e("        LD [" + S(ZP_RQ) + "] <- A");
    e("__" + L + "_body:");
  }
  // Keep the top (16 - n) bits: double the answer and bring a bit down, n
  // times fewer than the width.
  e("__" + L + "_loop:");
  add16(e, ZP_RP, ZP_RP);
  e("        LD A <- [" + S(ZP_RA) + "]");
  e("        JN __" + L + "_one");
  e("        JMP __" + L + "_next");
  e("__" + L + "_one:");
  e("        LD A <- [" + S(ZP_RP) + "+1]");
  e("        OR A <- 1");
  e("        LD [" + S(ZP_RP) + "+1] <- A");
  e("__" + L + "_next:");
  add16(e, ZP_RA, ZP_RA);
  e("        LD A <- [" + S(ZP_RC) + "]");
  e("        SUB A <- 1");
  e("        LD [" + S(ZP_RC) + "] <- A");
  e("        JZ __" + L + "_finish");
  e("        JMP __" + L + "_loop");
  e("__" + L + "_finish:");
  copy16(e, ZP_RA, ZP_RP);
  if (arithmetic) {
    e("        LD A <- [" + S(ZP_RQ) + "]");
    e("        JZ __" + L + "_out");
    complement(e, ZP_RA);
    e("__" + L + "_out:");
    e("        RET");
    e("__" + L + "_allgone:");
    // Every bit gone: zero, or all ones when it was negative.
    e("        LD A <- [" + S(ZP_RA) + "]");
    e("        JN __" + L + "_ones");
    e("        LD D2 <- 0");
    e("        LD [" + S(ZP_RA) + "] <- D2");
    e("        RET");
    e("__" + L + "_ones:");
    e("        LD D2 <- 65535");
    e("        LD [" + S(ZP_RA) + "] <- D2");
    e("        RET");
  } else {
    e("__" + L + "_out:");
    e("        RET");
    e("__" + L + "_allgone:");
    e("        LD D2 <- 0");
    e("        LD [" + S(ZP_RA) + "] <- D2");
    e("        RET");
  }
}

}  // namespace

std::vector<std::string> emitSoftRuntime(const std::set<std::string>& used) {
  std::vector<std::string> out;
  const Emit e = [&](const std::string& l) { out.push_back(l); };
  if (used.empty()) return out;

  if (used.count("__mul16")) mul(e);
  // A signed divide is the unsigned one with the signs taken off, so asking
  // for either brings the unsigned one in.
  const bool needU = used.count("__udiv16") || used.count("__sdiv16");
  const bool needUR = used.count("__urem16") || used.count("__srem16");
  if (needU) divmod(e, "__udiv16", false);
  if (needUR) divmod(e, "__urem16", true);
  if (used.count("__sdiv16")) signedDiv(e, "__sdiv16", "__udiv16", false);
  if (used.count("__srem16")) signedDiv(e, "__srem16", "__urem16", true);
  if (used.count("__shl16")) shl(e);
  if (used.count("__ushr16")) shr(e, "__ushr16", false);
  if (used.count("__sshr16")) shr(e, "__sshr16", true);
  return out;
}

}  // namespace sc8::cc
