#include <doctest.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"
#include "devices/acp.h"

using namespace sc8;
using namespace sc8::acp;

// The bitwise family. The CPU can mask and shift a byte and nothing wider.
// Addition only carries upward and there is no shift instruction at all.
// See the manual's shifting page for what that costs in software.
//
// DESIGN: ACP_OVERFLOW means "the bit that fell out", in both directions.
// That is the owner's decision. It makes a shift usable as the bit test the
// CPU cannot do on a 64 bit value: shift, then read one flag. For a shape it
// means at least one element expelled a set bit. That is how ACP_OVERFLOW
// already behaves for a result that did not fit.

namespace {

constexpr int BASE = 0x1000;

struct Rig {
  std::unique_ptr<Acp> acp;
  std::vector<uint8_t> ram;

  Rig(int fmt, int rows = 1, int cols = 1) : acp(std::make_unique<Acp>()), ram(RAM_SIZE, 0) {
    acp->attachRam(ram.data());
    out(ACP_ADDR_HI, BASE >> 8);
    out(ACP_ADDR_LO, BASE & 0xff);
    out(ACP_FMT, fmt);
    out(ACP_ROWS, rows);
    out(ACP_COLS, cols);
  }
  void out(int port, int value) {
    acp->write(static_cast<uint8_t>(port), static_cast<uint8_t>(value & 0xff));
  }
  void put(int at, uint64_t v) {
    for (int i = 0; i < 8; i++) ram[static_cast<size_t>(at + i)] = static_cast<uint8_t>(v >> (56 - 8 * i));
  }
  uint64_t get(int at) const {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | ram[static_cast<size_t>(at + i)];
    return v;
  }
  void run(int cmd) { out(ACP_CMD, cmd); }
  uint8_t flags() { return acp->read(ACP_FLAGS); }
};

struct Result {
  uint64_t v;
  uint8_t flags;
};

bool has(uint8_t flags, int mask) { return (flags & mask) != 0; }

// A two operand bitwise command on one pair, unsigned.
Result b2(int cmd, uint64_t a, uint64_t b, int fmt = ACP_U64) {
  Rig r(fmt);
  r.put(BASE, a);
  r.put(BASE + 8, b);
  r.run(cmd);
  return {r.get(BASE + 16), r.flags()};
}

// A one operand command. B takes no room, so the result sits at offset 8.
Result b1(int cmd, uint64_t a, int fmt = ACP_U64) {
  Rig r(fmt);
  r.put(BASE, a);
  r.run(cmd);
  return {r.get(BASE + 8), r.flags()};
}

constexpr uint64_t ALL = ~uint64_t{0};
constexpr uint64_t TOP = uint64_t{1} << 63;

uint64_t U(int64_t v) { return static_cast<uint64_t>(v); }

}  // namespace

TEST_SUITE("ACP bitwise logic") {
  constexpr uint64_t A = 0xf0f0f0f00f0f0f0full;
  constexpr uint64_t B = 0xff00ff0000ff00ffull;

  TEST_CASE("ands, ors and xors element by element") {
    CHECK_EQ(b2(ACP_AND, A, B).v, A & B);
    CHECK_EQ(b2(ACP_OR, A, B).v, A | B);
    CHECK_EQ(b2(ACP_XOR, A, B).v, A ^ B);
  }

  TEST_CASE("complements one operand") {
    CHECK_EQ(b1(ACP_NOT, A).v, ~A);
    CHECK_EQ(b1(ACP_NOT, 0).v, ALL);
    CHECK_EQ(b1(ACP_NOT, ALL).v, 0);
  }

  TEST_CASE("works on the whole 64 bits, which the CPU cannot reach") {
    CHECK_EQ(b2(ACP_AND, ALL, TOP).v, TOP);
  }

  TEST_CASE("sets ZERO when everything cancels, and never OVERFLOW") {
    const Result r = b2(ACP_XOR, A, A);
    CHECK_EQ(r.v, 0);
    CHECK(has(r.flags, ACP_ZERO));
    CHECK_FALSE(has(r.flags, ACP_OVERFLOW));
  }

  // A signed operand is a bit pattern like any other. The result comes back
  // in the same type, so an all-ones pattern reads as -1 and not as an
  // overflow: the writer fits it to the format rather than refusing it.
  TEST_CASE("keeps a signed operand signed, without calling it an overflow") {
    Rig r(ACP_I64);
    r.put(BASE, 0);
    r.run(ACP_NOT);
    CHECK_EQ(static_cast<int64_t>(r.get(BASE + 8)), -1);
    CHECK_FALSE(has(r.flags(), ACP_OVERFLOW));
    CHECK(has(r.flags(), ACP_NEGATIVE));
  }

  TEST_CASE("refuses a float or a complex") {
    for (int fmt : {ACP_F64, ACP_C64}) {
      CAPTURE(fmt);
      Rig r(fmt);
      r.run(ACP_AND);
      CHECK(has(r.flags(), ACP_BADFMT));
    }
  }
}

TEST_SUITE("ACP shifts") {
  TEST_CASE("shifts left, dropping bits off the top") {
    CHECK_EQ(b2(ACP_SHL, 1, 8).v, 0x100);
    CHECK_EQ(b2(ACP_SHL, ALL, 4).v, ALL << 4);
  }

  TEST_CASE("shifts right, filling with zero") {
    CHECK_EQ(b2(ACP_SHR, 0x100, 8).v, 1);
    CHECK_EQ(b2(ACP_SHR, ALL, 1).v, 0x7fffffffffffffffull);
  }

  // The one thing a divide cannot do. ACP_DIV truncates toward zero, so
  // -5 / 2 is -2 where an arithmetic shift floors it to -3.
  TEST_CASE("shifts right arithmetically, keeping the sign") {
    Rig r(ACP_I64);
    r.put(BASE, U(-5));
    r.put(BASE + 8, 1);
    r.run(ACP_ASR);
    CHECK_EQ(static_cast<int64_t>(r.get(BASE + 16)), -3);
  }

  TEST_CASE("fills an arithmetic shift with the sign bit, not with zero") {
    Rig r(ACP_I64);
    r.put(BASE, U(-1));
    r.put(BASE + 8, 40);
    r.run(ACP_ASR);
    CHECK_EQ(r.get(BASE + 16), ALL);
  }

  // A count is a 64 bit value and a program can write any of them. Without a
  // clamp the TypeScript shifter hands that straight to BigInt. It would try
  // to build a number of two to the sixty-three bits and never come back.
  // The answer matters less than the fact that there is one.
  TEST_CASE("survives an absurd count instead of trying to build it") {
    const auto started = std::chrono::steady_clock::now();
    CHECK_EQ(b2(ACP_SHL, ALL, 0x7fffffffffffffffull).v, 0);
    CHECK_EQ(b2(ACP_SHR, ALL, 0x7fffffffffffffffull).v, 0);
    const auto took = std::chrono::steady_clock::now() - started;
    CHECK_MESSAGE(took < std::chrono::seconds(1), "the shifter did real work on a huge count");
  }

  TEST_CASE("empties the register when the count reaches the width") {
    CHECK_EQ(b2(ACP_SHL, ALL, 64).v, 0);
    CHECK_EQ(b2(ACP_SHR, ALL, 64).v, 0);
    CHECK_EQ(b2(ACP_SHL, ALL, 200).v, 0);
  }

  TEST_CASE("leaves the value alone on a count of zero, and flags nothing") {
    const Result r = b2(ACP_SHL, 0xdeadbeef, 0);
    CHECK_EQ(r.v, 0xdeadbeef);
    CHECK_FALSE(has(r.flags, ACP_OVERFLOW));
  }

  TEST_CASE("shifts by one less than the width, the largest count that keeps a bit") {
    CHECK_EQ(b2(ACP_SHL, 1, 63).v, TOP);
    CHECK_EQ(b2(ACP_SHR, TOP, 63).v, 1);
    CHECK_EQ(b2(ACP_ASR, TOP, 63, ACP_I64).v, ALL);
  }
}

TEST_SUITE("ACP rotates") {
  TEST_CASE("rotates left, bringing the top bits round to the bottom") {
    CHECK_EQ(b2(ACP_ROL, TOP, 1).v, 1);
    CHECK_EQ(b2(ACP_ROL, 0x123456789abcdef0ull, 8).v, 0x3456789abcdef012ull);
  }

  TEST_CASE("rotates right, bringing the bottom bits round to the top") {
    CHECK_EQ(b2(ACP_ROR, 1, 1).v, TOP);
    CHECK_EQ(b2(ACP_ROR, 0x123456789abcdef0ull, 8).v, 0xf0123456789abcdeull);
  }

  TEST_CASE("loses nothing: a rotate all the way round is where it started") {
    const uint64_t v = 0x0123456789abcdefull;
    CHECK_EQ(b2(ACP_ROL, v, 64).v, v);
    CHECK_EQ(b2(ACP_ROR, v, 64).v, v);
    CHECK_EQ(b2(ACP_ROL, v, 70).v, b2(ACP_ROL, v, 6).v);
  }
}

// The owner's rule, and the reason the family is worth having: one flag says
// what left the register, whichever way it went.
TEST_SUITE("ACP_OVERFLOW is the bit that fell out") {
  bool over(int cmd, uint64_t a, uint64_t n) { return has(b2(cmd, a, n).flags, ACP_OVERFLOW); }

  TEST_CASE("reports the top bit a left shift expelled") {
    CHECK(over(ACP_SHL, TOP, 1));
    CHECK_FALSE(over(ACP_SHL, uint64_t{1} << 62, 1));
    CHECK(over(ACP_SHL, uint64_t{1} << 62, 2));
  }

  TEST_CASE("reports the bottom bit a right shift expelled") {
    CHECK(over(ACP_SHR, 1, 1));
    CHECK_FALSE(over(ACP_SHR, 2, 1));
    CHECK(over(ACP_SHR, 2, 2));
  }

  // The LAST bit out, not any bit out. Shifting 0b110 right by two expels the
  // 0 then the 1, and the flag is the 1. Shifting it right by one expels only
  // the 0. Anything else would make the flag an "any bit lost" bit. That is
  // what OVERFLOW already meant and not what was asked for.
  TEST_CASE("reports the LAST bit out, not whether any bit was lost") {
    CHECK_FALSE(over(ACP_SHR, 0b110, 1));
    CHECK(over(ACP_SHR, 0b110, 2));
    CHECK(over(ACP_SHR, 0b110, 3));
  }

  TEST_CASE("reports the bit a rotate brought round, both ways") {
    CHECK(over(ACP_ROL, TOP, 1));
    CHECK_FALSE(over(ACP_ROL, uint64_t{1} << 62, 1));
    CHECK(over(ACP_ROR, 1, 1));
    CHECK_FALSE(over(ACP_ROR, 2, 1));
  }

  TEST_CASE("takes the sign bit for an arithmetic shift past the width") {
    Rig r(ACP_I64);
    r.put(BASE, U(-1));
    r.put(BASE + 8, 200);
    r.run(ACP_ASR);
    CHECK(has(r.flags(), ACP_OVERFLOW));
    CHECK_EQ(r.get(BASE + 16), ALL);
    Rig p(ACP_I64);
    p.put(BASE, 0x7fffffffffffffffull);
    p.put(BASE + 8, 200);
    p.run(ACP_ASR);
    CHECK_FALSE(has(p.flags(), ACP_OVERFLOW));
    CHECK_EQ(p.get(BASE + 16), 0);
  }

  TEST_CASE("reports the bit at the width exactly") {
    // A shift by 64 expels the far bit last: bit 0 on a left shift, bit 63
    // on a right shift.
    CHECK(over(ACP_SHL, 1, 64));
    CHECK_FALSE(over(ACP_SHL, 2, 64));
    CHECK(over(ACP_SHR, TOP, 64));
    CHECK_FALSE(over(ACP_SHR, TOP >> 1, 64));
  }

  TEST_CASE("says nothing when nothing left the register") {
    CHECK_FALSE(over(ACP_SHL, ALL, 0));
    CHECK_FALSE(over(ACP_ROL, ALL, 0));
    CHECK_FALSE(has(b2(ACP_AND, ALL, ALL).flags, ACP_OVERFLOW));
  }

  // The bit test the whole family exists for: walk a 64 bit value out one bit
  // at a time, reading one flag each pass. The CPU cannot do this at all.
  TEST_CASE("walks a 64 bit value out one bit at a time") {
    Rig r(ACP_U64);
    const uint64_t value = 0xa000000000000005ull;
    r.put(BASE, value);
    r.put(BASE + 8, 1);
    std::string bits;
    for (int i = 0; i < 64; i++) {
      r.run(ACP_SHL);
      bits.push_back(has(r.flags(), ACP_OVERFLOW) ? '1' : '0');
      r.put(BASE, r.get(BASE + 16));  // the result becomes the next operand
      r.put(BASE + 8, 1);
    }
    std::string want;
    for (int i = 63; i >= 0; i--) want.push_back(((value >> i) & 1) != 0 ? '1' : '0');
    CHECK_EQ(bits, want);
  }
}

TEST_SUITE("the bitwise family over a shape") {
  TEST_CASE("ands a vector element by element") {
    Rig r(ACP_U64, 3, 1);
    const uint64_t a[3] = {0xff, 0xf0, 0x0f};
    const uint64_t b[3] = {0x0f, 0xff, 0xf0};
    for (int i = 0; i < 3; i++) r.put(BASE + i * 8, a[i]);
    for (int i = 0; i < 3; i++) r.put(BASE + 24 + i * 8, b[i]);
    r.run(ACP_AND);
    std::vector<uint64_t> got;
    for (int i = 0; i < 3; i++) got.push_back(r.get(BASE + 48 + i * 8));
    CHECK_EQ(got, std::vector<uint64_t>{0x0f, 0xf0, 0x00});
  }

  TEST_CASE("gives each element its own shift count") {
    Rig r(ACP_U64, 3, 1);
    const uint64_t counts[3] = {0, 4, 8};
    for (int i = 0; i < 3; i++) r.put(BASE + i * 8, 1);
    for (int i = 0; i < 3; i++) r.put(BASE + 24 + i * 8, counts[i]);
    r.run(ACP_SHL);
    std::vector<uint64_t> got;
    for (int i = 0; i < 3; i++) got.push_back(r.get(BASE + 48 + i * 8));
    CHECK_EQ(got, std::vector<uint64_t>{1, 0x10, 0x100});
  }

  // The set bit is in the FIRST element and the last expels a zero. Only
  // an OR across the elements can answer true. A flag that simply took the
  // last element would read false here. It would pass a test built the other
  // way round, which is exactly what the first draft of this did.
  TEST_CASE("raises the flag when any one element expelled a set bit") {
    Rig r(ACP_U64, 2, 1);
    r.put(BASE, TOP);
    r.put(BASE + 8, 1);
    r.put(BASE + 16, 1);
    r.put(BASE + 24, 1);
    r.run(ACP_SHL);
    CHECK(has(r.flags(), ACP_OVERFLOW));
  }

  TEST_CASE("keeps the flag clear when no element expelled anything") {
    Rig r(ACP_U64, 2, 1);
    r.put(BASE, 1);
    r.put(BASE + 8, 2);
    r.put(BASE + 16, 1);
    r.put(BASE + 24, 1);
    r.run(ACP_SHL);
    CHECK_FALSE(has(r.flags(), ACP_OVERFLOW));
  }

  TEST_CASE("forgets a carry once it has been reported") {
    // The bit that fell out belongs to one command. The next command must
    // not inherit it.
    Rig r(ACP_U64);
    r.put(BASE, TOP);
    r.put(BASE + 8, 1);
    r.run(ACP_SHL);
    CHECK(has(r.flags(), ACP_OVERFLOW));
    r.put(BASE, 1);
    r.run(ACP_SHL);
    CHECK_FALSE(has(r.flags(), ACP_OVERFLOW));
  }
}
