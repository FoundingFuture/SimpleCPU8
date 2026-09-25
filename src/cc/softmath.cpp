#include "cc/softmath.h"

#include "cc/layout.h"
#include "cc/runtime.h"

namespace sc8::cc {

namespace {

std::string S(const char* s) { return std::string(s); }

// A word is high byte first, so a shift left starts at the low byte with
// SHL and carries up with ROL, and a shift right starts at the high byte
// with SHR (or ASR) and carries down with ROR. C holds the bit in between.

std::string lo(const std::string& at) { return at + "+1"; }

// dst += src, sixteen bits, low byte first so the carry chains.
void add16(const Emit& e, const std::string& dst, const std::string& src) {
  e("        LD A <- [" + lo(dst) + "]");
  e("        ADD A <- [" + lo(src) + "]");
  e("        LD [" + lo(dst) + "] <- A");
  e("        LD A <- [" + dst + "]");
  e("        ADC A <- [" + src + "]");
  e("        LD [" + dst + "] <- A");
}

// dst -= src, and C is left holding the borrow.
void sub16(const Emit& e, const std::string& dst, const std::string& src) {
  e("        LD A <- [" + lo(dst) + "]");
  e("        SUB A <- [" + lo(src) + "]");
  e("        LD [" + lo(dst) + "] <- A");
  e("        LD A <- [" + dst + "]");
  e("        SBC A <- [" + src + "]");
  e("        LD [" + dst + "] <- A");
}

// at <<= 1. C comes out holding the old bit 15.
void shl16(const Emit& e, const std::string& at) {
  e("        LD A <- [" + lo(at) + "]");
  e("        SHL A");
  e("        LD [" + lo(at) + "] <- A");
  e("        LD A <- [" + at + "]");
  e("        ROL A");
  e("        LD [" + at + "] <- A");
}

// at = at << 1 | C, the old bit 15 into C. Brings a bit in from below.
void rol16(const Emit& e, const std::string& at) {
  e("        LD A <- [" + lo(at) + "]");
  e("        ROL A");
  e("        LD [" + lo(at) + "] <- A");
  e("        LD A <- [" + at + "]");
  e("        ROL A");
  e("        LD [" + at + "] <- A");
}

// at >>= 1, SHR or ASR on the high byte. C comes out holding the old bit 0.
void shr16(const Emit& e, const std::string& at, const char* top) {
  e("        LD A <- [" + at + "]");
  e(std::string("        ") + top + " A");
  e("        LD [" + at + "] <- A");
  e("        LD A <- [" + lo(at) + "]");
  e("        ROR A");
  e("        LD [" + lo(at) + "] <- A");
}

void zero16(const Emit& e, const std::string& at) {
  e("        LD D2 <- 0");
  e("        LD [" + at + "] <- D2");
}

void copy16(const Emit& e, const std::string& dst, const std::string& src) {
  e("        LD D2 <- [" + src + "]");
  e("        LD [" + dst + "] <- D2");
}

// The round counter: one off, and a jump back while rounds remain.
void countdown(const Emit& e, const std::string& loop) {
  e("        LD A <- [" + S(ZP_RC) + "]");
  e("        SUB A <- 1");
  e("        LD [" + S(ZP_RC) + "] <- A");
  e("        JNZ " + loop);
}

std::string bare(const std::string& name) { return name.starts_with("__") ? name.substr(2) : name; }

// Multiply, low bit first. Each round shifts A's low bit into C, adds B to
// the product when it was set, then doubles B. The loop ends when A has no
// set bits left, so a small multiplier costs few rounds.
void mul(const Emit& e) {
  e("");
  e("; __mul16, in software: shift and add, A's low bit first. It stops");
  e("; when A runs out of set bits. Compare it with one ACP_MUL.");
  e("__mul16:");
  zero16(e, ZP_RP);
  e("__mul16_loop:");
  shr16(e, ZP_RA, "SHR");
  e("        JNC __mul16_skip");
  add16(e, ZP_RP, ZP_RB);
  e("__mul16_skip:");
  shl16(e, ZP_RB);
  e("        LD A <- [" + S(ZP_RA) + "]");
  e("        OR A <- [" + lo(ZP_RA) + "]");
  e("        JNZ __mul16_loop");
  copy16(e, ZP_RA, ZP_RP);
  e("        RET");
}

// Restoring division, most significant bit first. The dividend shifts left
// into the remainder a bit at a time, and the divisor comes off when it
// fits. The quotient's bits go in at the bottom of the dividend as its
// bits leave the top, so after sixteen rounds __ra holds the quotient.
void divmod(const Emit& e, const std::string& name, bool wantRemainder) {
  const std::string L = "__" + bare(name);
  e("");
  e("; " + name + ", in software. Restoring division, one bit a round.");
  e(name + ":");
  // Dividing by zero gives all ones and the dividend back, which is what
  // div8 does and what the manual documents.
  e("        LD A <- [" + S(ZP_RB) + "]");
  e("        OR A <- [" + lo(ZP_RB) + "]");
  e("        JZ " + L + "_byzero");
  zero16(e, ZP_RP);
  e("        LD A <- 16");
  e("        LD [" + S(ZP_RC) + "] <- A");
  e(L + "_loop:");
  shl16(e, ZP_RA);  // the dividend's top bit into C
  rol16(e, ZP_RP);  // and into the remainder
  // A seventeenth bit means the remainder is past any divisor: it fits.
  e("        JC " + L + "_take");
  // Try the subtract without keeping it: the low byte waits in __rq.
  e("        LD A <- [" + lo(ZP_RP) + "]");
  e("        SUB A <- [" + lo(ZP_RB) + "]");
  e("        LD [" + lo(ZP_RQ) + "] <- A");
  e("        LD A <- [" + S(ZP_RP) + "]");
  e("        SBC A <- [" + S(ZP_RB) + "]");
  e("        JC " + L + "_next");  // a borrow: it did not fit
  e("        LD [" + S(ZP_RP) + "] <- A");
  e("        LD A <- [" + lo(ZP_RQ) + "]");
  e("        LD [" + lo(ZP_RP) + "] <- A");
  e("        JMP " + L + "_bit");
  e(L + "_take:");
  sub16(e, ZP_RP, ZP_RB);
  e(L + "_bit:");
  e("        LD A <- [" + lo(ZP_RA) + "]");  // a quotient bit of one
  e("        OR A <- 1");
  e("        LD [" + lo(ZP_RA) + "] <- A");
  e(L + "_next:");
  countdown(e, L + "_loop");
  if (wantRemainder) copy16(e, ZP_RA, ZP_RP);
  e("        RET");
  e(L + "_byzero:");
  if (wantRemainder) {
    e("        RET");  // the dividend is already in __ra
  } else {
    e("        LD D2 <- 65535");
    e("        LD [" + S(ZP_RA) + "] <- D2");
    e("        RET");
  }
}

// Two's complement in place: zero minus the word.
void negate(const Emit& e, const std::string& at) {
  e("        LD A <- 0");
  e("        SUB A <- [" + lo(at) + "]");
  e("        LD [" + lo(at) + "] <- A");
  e("        LD A <- 0");
  e("        SBC A <- [" + at + "]");
  e("        LD [" + at + "] <- A");
}

// A signed divide or remainder: take the signs off, do it unsigned, put the
// sign back. C truncates toward zero and the remainder follows the dividend.
void signedDiv(const Emit& e, const std::string& name, const std::string& base, bool wantRemainder) {
  const std::string L = "__" + bare(name);
  e("");
  e("; " + name + ": signs off, unsigned, sign back. C truncates toward zero.");
  e(name + ":");
  e("        LD A <- 0");
  e("        LD [" + S(ZP_RC) + "] <- A");  // the sign of the answer
  e("        LD A <- [" + S(ZP_RA) + "]");
  e("        JP " + L + "_posa");
  negate(e, ZP_RA);
  e("        LD A <- 1");
  e("        LD [" + S(ZP_RC) + "] <- A");
  e(L + "_posa:");
  e("        LD A <- [" + S(ZP_RB) + "]");
  e("        JP " + L + "_posb");
  negate(e, ZP_RB);
  if (!wantRemainder) {
    // The remainder takes the dividend's sign alone, so only a quotient
    // flips for a negative divisor.
    e("        LD A <- [" + S(ZP_RC) + "]");
    e("        XOR A <- 1");
    e("        LD [" + S(ZP_RC) + "] <- A");
  }
  e(L + "_posb:");
  // The unsigned routine counts its rounds in __rc, so the sign waits on
  // the stack. POP sets Z from it.
  e("        LD A <- [" + S(ZP_RC) + "]");
  e("        PUSH A");
  e("        JSR " + base);
  e("        POP A");
  e("        JZ " + L + "_out");
  negate(e, ZP_RA);
  e(L + "_out:");
  e("        RET");
}

// Shift left by __rb, one SHL and ROL a round.
void shl(const Emit& e) {
  e("");
  e("; __shl16: SHL and ROL, __rb times.");
  e("__shl16:");
  e("        LD A <- [" + lo(ZP_RB) + "]");
  e("        LD [" + S(ZP_RC) + "] <- A");
  e("        JZ __shl16_done");
  e("__shl16_loop:");
  shl16(e, ZP_RA);
  countdown(e, "__shl16_loop");
  e("__shl16_done:");
  e("        RET");
}

// Shift right by __rb: SHR and ROR a round, or ASR for a signed value,
// which floors the way C's arithmetic shift does.
void shr(const Emit& e, const std::string& name, bool arithmetic) {
  const std::string L = "__" + bare(name);
  e("");
  e("; " + name + ": " + (arithmetic ? "ASR" : "SHR") + " and ROR, __rb times.");
  e(name + ":");
  e("        LD A <- [" + lo(ZP_RB) + "]");
  e("        LD [" + S(ZP_RC) + "] <- A");
  e("        JZ " + L + "_out");  // a shift by zero is the value itself
  e("        CMP A, 16");
  e("        JNC " + L + "_allgone");  // shifting by the width or more
  e(L + "_loop:");
  shr16(e, ZP_RA, arithmetic ? "ASR" : "SHR");
  countdown(e, L + "_loop");
  e(L + "_out:");
  e("        RET");
  e(L + "_allgone:");
  if (arithmetic) {
    // Every bit gone: zero, or all ones when it was negative.
    e("        LD A <- [" + S(ZP_RA) + "]");
    e("        JN " + L + "_ones");
    zero16(e, ZP_RA);
    e("        RET");
    e(L + "_ones:");
    e("        LD D2 <- 65535");
    e("        LD [" + S(ZP_RA) + "] <- D2");
    e("        RET");
  } else {
    zero16(e, ZP_RA);
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
