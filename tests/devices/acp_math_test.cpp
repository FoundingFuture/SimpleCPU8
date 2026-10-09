#include <doctest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "asm/asm.h"
#include "core/machine.h"
#include "devices/acp.h"

using namespace sc8;
using namespace sc8::acp;

// Floats, complex, vectors, matrices and tables. The block tests live in
// acp_test.cpp; this file is about what the arithmetic answers.
//
// Float arithmetic IS the host's IEEE 754 double arithmetic, so the reference
// is the host and the comparison is EXACT. There is no tolerance to choose
// and none is introduced.

namespace {

constexpr int BASE = 0x1000;

struct Rig {
  std::unique_ptr<Acp> acp;
  std::vector<uint8_t> ram;

  Rig(int fmt, int rows = 1, int cols = 1, int rfmt = -1)
      : acp(std::make_unique<Acp>()), ram(RAM_SIZE, 0) {
    acp->attachRam(ram.data());
    out(ACP_ADDR_HI, BASE >> 8);
    out(ACP_ADDR_LO, BASE & 0xff);
    out(ACP_FMT, fmt);
    if (rfmt >= 0 && rfmt != fmt) out(ACP_RFMT, rfmt);
    out(ACP_ROWS, rows);
    out(ACP_COLS, cols);
  }
  void out(int port, int value) {
    acp->write(static_cast<uint8_t>(port), static_cast<uint8_t>(value & 0xff));
  }
  uint8_t flags() { return acp->read(ACP_FLAGS); }
  void run(int cmd) { out(ACP_CMD, cmd); }

  void putF(int at, double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    putU(at, bits);
  }
  double getF(int at) const {
    const uint64_t bits = getU(at);
    double v;
    std::memcpy(&v, &bits, 8);
    return v;
  }
  void putU(int at, uint64_t v) {
    for (int i = 0; i < 8; i++) ram[static_cast<size_t>(at + i)] = static_cast<uint8_t>(v >> (56 - 8 * i));
  }
  void putI(int at, int64_t v) { putU(at, static_cast<uint64_t>(v)); }
  uint64_t getU(int at) const {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | ram[static_cast<size_t>(at + i)];
    return v;
  }
  int64_t getI(int at) const { return static_cast<int64_t>(getU(at)); }
};

struct Result {
  double v;
  uint8_t flags;
};

// One float operation on two scalars, giving the result.
Result f2(int cmd, double a, double b) {
  Rig r(ACP_F64);
  r.putF(BASE, a);
  r.putF(BASE + 8, b);
  r.run(cmd);
  return {r.getF(BASE + 16), r.flags()};
}

// One float operation on one scalar. A unary operand takes no room for B, so
// the result sits at offset 8.
Result f1(int cmd, double a) {
  Rig r(ACP_F64);
  r.putF(BASE, a);
  r.run(cmd);
  return {r.getF(BASE + 8), r.flags()};
}

bool has(uint8_t flags, int mask) { return (flags & mask) != 0; }

// Bit for bit, the way Object.is compares: -0 is not 0 and NaN is NaN.
bool same(double a, double b) {
  uint64_t x, y;
  std::memcpy(&x, &a, 8);
  std::memcpy(&y, &b, 8);
  return x == y || (std::isnan(a) && std::isnan(b));
}

bool close(double a, double b, int digits) { return std::fabs(a - b) < std::pow(10.0, -digits) / 2; }

const double INF = std::numeric_limits<double>::infinity();
const double PI = std::numbers::pi;

}  // namespace

TEST_SUITE("ACP floats") {
  const std::vector<std::pair<double, double>> PAIRS = {
      {0, 0},
      {1, 3},
      {-2.5, 0.125},
      {1e300, 1e-300},
      {PI, std::numbers::e},
      {0.1, 0.2},  // the classic: 0.1 + 0.2 is not 0.3, and must not be
      {-0.0, 5},
      {1e308, 10},
  };

  TEST_CASE("adds, subtracts, multiplies and divides exactly as the host does") {
    for (const auto& [a, b] : PAIRS) {
      CAPTURE(a);
      CAPTURE(b);
      CHECK(same(f2(ACP_ADD, a, b).v, a + b));
      CHECK(same(f2(ACP_SUB, a, b).v, a - b));
      CHECK(same(f2(ACP_MUL, a, b).v, a * b));
      if (b != 0) CHECK(same(f2(ACP_DIV, a, b).v, a / b));
    }
    // Named so nobody "fixes" it later: this is the point of exact comparison.
    CHECK_EQ(f2(ACP_ADD, 0.1, 0.2).v, 0.30000000000000004);
  }

  TEST_CASE("writes infinity and sets divzero on a float divide by zero") {
    const Result r = f2(ACP_DIV, 1, 0);
    CHECK_EQ(r.v, INF);
    CHECK(has(r.flags, ACP_DIVZERO));
  }

  TEST_CASE("sets nan when the result is not a number") {
    const Result r = f2(ACP_DIV, 0, 0);
    CHECK(std::isnan(r.v));
    CHECK(has(r.flags, ACP_NAN));
  }

  TEST_CASE("takes a remainder the way fmod does, and refuses a zero divisor") {
    CHECK_EQ(f2(ACP_REM, 7.5, 2).v, 1.5);
    CHECK_EQ(f2(ACP_REM, -7.5, 2).v, -1.5);
    const Result r = f2(ACP_REM, 1, 0);
    CHECK(std::isnan(r.v));
    CHECK(has(r.flags, ACP_DIVZERO));
  }

  TEST_CASE("negates, takes a magnitude and compares") {
    CHECK_EQ(f1(ACP_NEG, 2.5).v, -2.5);
    CHECK_EQ(f1(ACP_ABS, -2.5).v, 2.5);
    CHECK_EQ(f2(ACP_CMP, 1, 2).v, -1);
    CHECK_EQ(f2(ACP_CMP, 2, 2).v, 0);
    CHECK_EQ(f2(ACP_CMP, 3, 2).v, 1);
  }
}

TEST_SUITE("ACP conversion") {
  TEST_CASE("converts between all three scalar types, nine ordered pairs") {
    for (int from : {ACP_I64, ACP_U64, ACP_F64}) {
      for (int to : {ACP_I64, ACP_U64, ACP_F64}) {
        CAPTURE(from);
        CAPTURE(to);
        Rig r(from, 1, 1, to);
        if (from == ACP_F64) r.putF(BASE, 42);
        else r.putI(BASE, 42);
        r.run(ACP_CVT);
        const double got = to == ACP_F64 ? r.getF(BASE + 8) : static_cast<double>(r.getI(BASE + 8));
        CHECK_EQ(got, 42);
      }
    }
  }

  TEST_CASE("truncates toward zero converting a float to an integer") {
    for (const auto& [v, want] : std::vector<std::pair<double, int64_t>>{{2.7, 2}, {-2.7, -2}, {0.9, 0}, {-0.9, 0}}) {
      CAPTURE(v);
      Rig r(ACP_F64, 1, 1, ACP_I64);
      r.putF(BASE, v);
      r.run(ACP_CVT);
      CHECK_EQ(r.getI(BASE + 8), want);
    }
  }

  TEST_CASE("sets overflow when a float will not fit the integer result") {
    Rig r(ACP_F64, 1, 1, ACP_I64);
    r.putF(BASE, 1e30);
    r.run(ACP_CVT);
    CHECK(has(r.flags(), ACP_OVERFLOW));
  }

  TEST_CASE("keeps the low bits of a float too wide for the integer result") {
    // BigInt(1e30) modulo 2^64 is what asUintN leaves behind. The double is
    // a 53 bit integer times a power of two. The reference is that integer
    // shifted, wrapping at 64 bits.
    Rig r(ACP_F64, 1, 1, ACP_U64);
    r.putF(BASE, 1e30);
    r.run(ACP_CVT);
    int e = 0;
    const double f = std::frexp(1e30, &e);
    const auto m = static_cast<uint64_t>(std::ldexp(f, 53));
    const int shift = e - 53;
    REQUIRE(shift > 0);
    REQUIRE(shift < 64);
    CHECK_EQ(r.getU(BASE + 8), m << shift);
    CHECK_NE(r.getU(BASE + 8), 0);
    CHECK(has(r.flags(), ACP_OVERFLOW));
  }

  TEST_CASE("sets nan and writes zero converting a nan, and overflow for an infinity") {
    Rig n(ACP_F64, 1, 1, ACP_I64);
    n.putF(BASE, std::nan(""));
    n.run(ACP_CVT);
    CHECK(has(n.flags(), ACP_NAN));
    CHECK_EQ(n.getI(BASE + 8), 0);
    Rig i(ACP_F64, 1, 1, ACP_I64);
    i.putF(BASE, -INF);
    i.run(ACP_CVT);
    CHECK(has(i.flags(), ACP_OVERFLOW));
    CHECK_FALSE(has(i.flags(), ACP_NAN));
  }

  TEST_CASE("converts a signed value to unsigned by its bits, and calls that an overflow") {
    Rig r(ACP_I64, 1, 1, ACP_U64);
    r.putI(BASE, -1);
    r.run(ACP_CVT);
    CHECK_EQ(r.getU(BASE + 8), ~uint64_t{0});
    CHECK(has(r.flags(), ACP_OVERFLOW));
  }

  TEST_CASE("multiplies two integers and writes floats, which is why the type is two ports") {
    Rig r(ACP_I64, 1, 1, ACP_F64);
    r.putI(BASE, 3);
    r.putI(BASE + 8, 4);
    r.run(ACP_MUL);
    CHECK_EQ(r.getF(BASE + 16), 12);
  }

  TEST_CASE("rounds a wide integer product to the nearest double") {
    // (2^63 - 1)^2 = 2^126 - 2^64 + 1 rounds up to 2^126 at 53 bits.
    Rig r(ACP_I64, 1, 1, ACP_F64);
    r.putI(BASE, static_cast<int64_t>((uint64_t{1} << 63) - 1));
    r.putI(BASE + 8, static_cast<int64_t>((uint64_t{1} << 63) - 1));
    r.run(ACP_MUL);
    CHECK_EQ(r.getF(BASE + 16), std::ldexp(1.0, 126));
    // 2^64 + 2^11 + 1 rounds to 2^64 + 2^12: the odd sticky bit breaks the
    // tie upward, as Number(bigint) does.
    Rig t(ACP_U64, 1, 1, ACP_F64);
    t.putU(BASE, (uint64_t{1} << 63) | (uint64_t{1} << 10));
    t.putU(BASE + 8, 2);
    t.run(ACP_MUL);
    CHECK_EQ(t.getF(BASE + 16), std::ldexp(1.0, 64) + std::ldexp(1.0, 11));
    Rig h(ACP_U64, 1, 1, ACP_F64);
    h.putU(BASE, ~uint64_t{0});
    h.putU(BASE + 8, ~uint64_t{0});
    h.run(ACP_MUL);
    CHECK_EQ(h.getF(BASE + 16), std::ldexp(1.0, 128));
  }
}

TEST_SUITE("ACP transcendentals") {
  using Fn = double (*)(double);
  const std::vector<std::pair<int, Fn>> UNARY = {
      {ACP_SQRT, [](double x) { return std::sqrt(x); }}, {ACP_SIN, [](double x) { return std::sin(x); }},
      {ACP_COS, [](double x) { return std::cos(x); }},   {ACP_TAN, [](double x) { return std::tan(x); }},
      {ACP_ATAN, [](double x) { return std::atan(x); }}, {ACP_EXP, [](double x) { return std::exp(x); }},
  };

  TEST_CASE("matches the host's math library exactly, at a spread of inputs") {
    // Exact, not within a tolerance: these ARE the host's functions, so any
    // difference would mean the device is doing something else.
    for (const auto& [cmd, fn] : UNARY) {
      for (double x : {0.0, 0.5, 1.0, 1.5, 2.0, 10.0, 0.0001, 1e6}) {
        CAPTURE(cmd);
        CAPTURE(x);
        CHECK(same(f1(cmd, x).v, fn(x)));
      }
    }
    for (double x : {-1.0, -0.5, 0.0, 0.5, 1.0}) {
      CHECK(same(f1(ACP_ASIN, x).v, std::asin(x)));
      CHECK(same(f1(ACP_ACOS, x).v, std::acos(x)));
    }
    for (double x : {0.5, 1.0, 10.0, 1000.0}) {
      CHECK(same(f1(ACP_LOG, x).v, std::log(x)));
      CHECK(same(f1(ACP_LOG10, x).v, std::log10(x)));
    }
  }

  TEST_CASE("takes atan2, pow and hypot from both operands") {
    for (const auto& [a, b] : std::vector<std::pair<double, double>>{{1, 2}, {-3, 4}, {0, -1}}) {
      CAPTURE(a);
      CAPTURE(b);
      CHECK(same(f2(ACP_ATAN2, a, b).v, std::atan2(a, b)));
      CHECK(same(f2(ACP_HYPOT, a, b).v, std::hypot(a, b)));
    }
    CHECK_EQ(f2(ACP_POW, 2, 10).v, 1024);
  }

  TEST_CASE("answers pow's two corners the way JavaScript does") {
    // C says one to any power is one. Math.pow says one to a NaN is NaN and
    // one to an infinity is NaN. The reference is the JavaScript.
    CHECK(std::isnan(f2(ACP_POW, 1, std::nan("")).v));
    CHECK(std::isnan(f2(ACP_POW, 1, INF).v));
    CHECK(std::isnan(f2(ACP_POW, -1, -INF).v));
    CHECK_EQ(f2(ACP_POW, std::nan(""), 0).v, 1);
  }

  TEST_CASE("refuses an integer operand type with badfmt") {
    Rig r(ACP_I64);
    r.putI(BASE, 4);
    r.run(ACP_SQRT);
    CHECK(has(r.flags(), ACP_BADFMT));
    CHECK_MESSAGE(r.getI(BASE + 8) == 0, "the result region was written anyway");
  }

  TEST_CASE("sets nan for a square root of a negative and a log of a negative") {
    CHECK(has(f1(ACP_SQRT, -1).flags, ACP_NAN));
    CHECK(has(f1(ACP_LOG, -1).flags, ACP_NAN));
  }

  TEST_CASE("works element by element over a shape") {
    Rig r(ACP_F64, 1, 4);
    for (int i = 0; i < 4; i++) r.putF(BASE + 8 * i, i);
    r.run(ACP_SIN);
    for (int i = 0; i < 4; i++) {
      CAPTURE(i);
      CHECK(same(r.getF(BASE + 32 + 8 * i), std::sin(static_cast<double>(i))));
    }
  }
}

TEST_SUITE("ACP complex") {
  void putC(Rig& r, int at, double re, double im) {
    r.putF(at, re);
    r.putF(at + 8, im);
  }
  std::pair<double, double> getC(const Rig& r, int at) { return {r.getF(at), r.getF(at + 8)}; }

  TEST_CASE("adds, subtracts and negates element by element") {
    Rig r(ACP_C64);
    putC(r, BASE, 1, 2);
    putC(r, BASE + 16, 3, -4);
    r.run(ACP_ADD);
    CHECK_EQ(getC(r, BASE + 32), std::pair<double, double>{4, -2});
    r.run(ACP_SUB);
    CHECK_EQ(getC(r, BASE + 32), std::pair<double, double>{-2, 6});
    r.run(ACP_NEG);
    CHECK_EQ(getC(r, BASE + 16), std::pair<double, double>{-1, -2});
  }

  TEST_CASE("multiplies by the real formula, not element by element") {
    // (1+2i)(3-4i) = 3 - 4i + 6i - 8i^2 = 11 + 2i. Element by element would
    // give 3 - 8i, which is what a wrong build writes.
    Rig r(ACP_C64);
    putC(r, BASE, 1, 2);
    putC(r, BASE + 16, 3, -4);
    r.run(ACP_MUL);
    CHECK_EQ(getC(r, BASE + 32), std::pair<double, double>{11, 2});
  }

  TEST_CASE("divides") {
    Rig r(ACP_C64);
    putC(r, BASE, 11, 2);
    putC(r, BASE + 16, 3, -4);
    r.run(ACP_DIV);
    const auto [re, im] = getC(r, BASE + 32);
    CHECK(close(re, 1, 12));
    CHECK(close(im, 2, 12));
  }

  TEST_CASE("divides without overflowing on large operands, which the naive formula does not") {
    // The textbook (ac+bd)/(cc+dd) squares the divisor, and 1e200 squared is
    // infinity. Smith's method scales first and survives.
    Rig r(ACP_C64);
    putC(r, BASE, 1e200, 1e200);
    putC(r, BASE + 16, 1e200, 1e200);
    r.run(ACP_DIV);
    const auto [re, im] = getC(r, BASE + 32);
    CHECK_EQ(re, 1);
    CHECK_EQ(im, 0);
  }

  TEST_CASE("divides by zero into infinities and sets divzero") {
    Rig r(ACP_C64);
    putC(r, BASE, 1, -1);
    putC(r, BASE + 16, 0, 0);
    r.run(ACP_DIV);
    CHECK_EQ(getC(r, BASE + 32), std::pair<double, double>{INF, -INF});
    CHECK(has(r.flags(), ACP_DIVZERO));
  }

  TEST_CASE("divides a zero part by zero into NaN, as JavaScript's 0 / 0") {
    Rig r(ACP_C64);
    putC(r, BASE, 0, 2);
    putC(r, BASE + 16, 0, 0);
    r.run(ACP_DIV);
    const auto [re, im] = getC(r, BASE + 32);
    CHECK(std::isnan(re));
    CHECK_EQ(im, INF);
    CHECK(has(r.flags(), ACP_DIVZERO));
  }

  TEST_CASE("gives the magnitude and the angle as reals") {
    Rig r(ACP_C64, 1, 1, ACP_F64);
    putC(r, BASE, 3, 4);
    r.run(ACP_ABS);
    CHECK_EQ(r.getF(BASE + 16), 5);
    r.run(ACP_ARG_OF);
    CHECK(same(r.getF(BASE + 16), std::atan2(4, 3)));
  }

  TEST_CASE("conjugates") {
    Rig r(ACP_C64);
    putC(r, BASE, 3, 4);
    r.run(ACP_CONJ);
    CHECK_EQ(getC(r, BASE + 16), std::pair<double, double>{3, -4});
  }

  TEST_CASE("refuses a comparison and a remainder, which complex numbers do not have") {
    for (int cmd : {ACP_CMP, ACP_REM}) {
      CAPTURE(cmd);
      Rig r(ACP_C64);
      r.run(cmd);
      CHECK(has(r.flags(), ACP_BADFMT));
    }
  }

  TEST_CASE("refuses conj and arg on a real type") {
    Rig r(ACP_F64);
    r.run(ACP_CONJ);
    CHECK(has(r.flags(), ACP_BADFMT));
  }

  TEST_CASE("scales a complex vector by one complex number") {
    Rig r(ACP_C64, 2, 1);
    putC(r, BASE, 1, 0);
    putC(r, BASE + 16, 0, 1);
    putC(r, BASE + 32, 0, 1);  // B is one element: multiply by i
    r.run(ACP_SCALE);
    CHECK_EQ(getC(r, BASE + 48), std::pair<double, double>{0, 1});
    CHECK_EQ(getC(r, BASE + 64), std::pair<double, double>{-1, 0});
  }
}

TEST_SUITE("ACP vectors") {
  Rig vec(const std::vector<double>& values, const std::vector<double>& b = {}) {
    Rig r(ACP_F64, static_cast<int>(values.size()), 1);
    for (size_t i = 0; i < values.size(); i++) r.putF(BASE + 8 * static_cast<int>(i), values[i]);
    const int bAt = BASE + 8 * static_cast<int>(values.size());
    for (size_t i = 0; i < b.size(); i++) r.putF(bAt + 8 * static_cast<int>(i), b[i]);
    return r;
  }

  TEST_CASE("sums a dot product from element zero upward") {
    // The order is part of the contract, because doubles do not add
    // associatively. This operand set gives a different answer the other way.
    // Forward: 1 + 1e16 loses the 1, then -1e16 gives 0.
    // Backward: -1e16 + 1e16 gives 0, then +1 gives 1.
    const std::vector<double> a = {1, 1e16, -1e16};
    const std::vector<double> b = {1, 1, 1};
    Rig r = vec(a, b);
    r.run(ACP_DOT);
    double want = 0;
    for (int i = 0; i < 3; i++) want += a[static_cast<size_t>(i)] * b[static_cast<size_t>(i)];
    CHECK_EQ(r.getF(BASE + 48), want);
    double backwards = 0;
    for (int i = 2; i >= 0; i--) backwards += a[static_cast<size_t>(i)] * b[static_cast<size_t>(i)];
    CHECK_MESSAGE(want != backwards, "the operands do not separate the two orders");
  }

  TEST_CASE("crosses two vectors by the right-hand rule") {
    Rig r = vec({1, 0, 0}, {0, 1, 0});  // x cross y is z
    r.run(ACP_CROSS);
    CHECK_EQ(std::vector<double>{r.getF(BASE + 48), r.getF(BASE + 56), r.getF(BASE + 64)},
             std::vector<double>{0, 0, 1});
  }

  TEST_CASE("crosses integer vectors exactly") {
    Rig r(ACP_I64, 3, 1);
    const int64_t big = int64_t{1} << 40;
    for (int i = 0; i < 3; i++) r.putI(BASE + 8 * i, big);
    r.putI(BASE + 24, 0);
    r.putI(BASE + 32, 1);
    r.putI(BASE + 40, 0);
    r.run(ACP_CROSS);
    // (b, b, b) cross (0, 1, 0) = (-b, 0, b)
    CHECK_EQ(r.getI(BASE + 48), -big);
    CHECK_EQ(r.getI(BASE + 56), 0);
    CHECK_EQ(r.getI(BASE + 64), big);
    CHECK_FALSE(has(r.flags(), ACP_OVERFLOW));
  }

  TEST_CASE("refuses a cross of anything but three rows") {
    for (int n : {2, 4}) {
      CAPTURE(n);
      Rig r = vec(std::vector<double>(static_cast<size_t>(n), 1), std::vector<double>(static_cast<size_t>(n), 1));
      r.run(ACP_CROSS);
      CHECK(has(r.flags(), ACP_BADDIM));
    }
  }

  TEST_CASE("gives length and distance as reals") {
    Rig r = vec({3, 4});
    r.run(ACP_NORM);
    CHECK_EQ(r.getF(BASE + 16), 5);
    Rig d = vec({1, 2}, {4, 6});
    d.run(ACP_DIST);
    CHECK_EQ(d.getF(BASE + 32), 5);
  }

  TEST_CASE("normalizes to length one, and sets divzero on a zero vector") {
    Rig r = vec({3, 4});
    r.run(ACP_NORMALIZE);
    CHECK_EQ(std::vector<double>{r.getF(BASE + 16), r.getF(BASE + 24)}, std::vector<double>{0.6, 0.8});
    Rig z = vec({0, 0});
    z.run(ACP_NORMALIZE);
    CHECK(has(z.flags(), ACP_DIVZERO));
    CHECK_EQ(std::vector<double>{z.getF(BASE + 16), z.getF(BASE + 24)}, std::vector<double>{0, 0});
  }

  TEST_CASE("normalizes a vector whose squares would overflow") {
    // Math.hypot scales by the largest magnitude before it squares. A
    // component near 1e200 then keeps a finite length.
    Rig r = vec({1e200, 0, 0});
    r.run(ACP_NORMALIZE);
    CHECK_EQ(r.getF(BASE + 24), 1);
    CHECK_EQ(r.getF(BASE + 32), 0);
  }

  TEST_CASE("refuses a vector operation on a matrix") {
    Rig r(ACP_F64, 2, 2);
    r.run(ACP_DOT);
    CHECK(has(r.flags(), ACP_BADDIM));
  }
}

TEST_SUITE("ACP matrices") {
  // Asymmetric on purpose: a symmetric matrix cannot tell row major from
  // column major, nor A times B from B times A.
  const std::vector<double> A = {1, 2, 3, 4, 5, 6};    // 2 by 3
  const std::vector<double> B = {7, 8, 9, 10, 11, 12};  // 3 by 2

  void load(Rig& r, int at, const std::vector<double>& m) {
    for (size_t i = 0; i < m.size(); i++) r.putF(at + 8 * static_cast<int>(i), m[i]);
  }
  std::vector<double> take(const Rig& r, int at, int n) {
    std::vector<double> out;
    for (int i = 0; i < n; i++) out.push_back(r.getF(at + 8 * i));
    return out;
  }

  TEST_CASE("multiplies matrices in the order A times B, not B times A") {
    Rig r(ACP_F64, 2, 3);
    r.out(ACP_COLS_B, 2);
    load(r, BASE, A);
    load(r, BASE + 48, B);
    r.run(ACP_MATMUL);
    // [1 2 3; 4 5 6] times [7 8; 9 10; 11 12] = [58 64; 139 154]
    CHECK_EQ(take(r, BASE + 48 + 48, 4), std::vector<double>{58, 64, 139, 154});
  }

  TEST_CASE("sums a matrix product from the inner index zero upward") {
    // Every matrix above holds small integers, where the summation order
    // cannot change the answer. None of them can catch a reversed loop.
    // These operands can: forward gives 0 and backward gives 1.
    Rig r(ACP_F64, 1, 3);
    r.out(ACP_COLS_B, 1);
    load(r, BASE, {1, 1e16, -1e16});
    load(r, BASE + 24, {1, 1, 1});
    r.run(ACP_MATMUL);
    CHECK_EQ(r.getF(BASE + 48), 0);
  }

  TEST_CASE("stores row major, checked by reading one element at a computed offset") {
    // A[1][2] of the 2 by 3 A is 6, and row major puts it at index 1*3+2 = 5.
    Rig r(ACP_F64, 2, 3);
    load(r, BASE, A);
    r.run(ACP_NEG);
    CHECK_EQ(r.getF(BASE + 48 + 8 * 5), -6);
  }

  TEST_CASE("applies a matrix to a vector") {
    // A quarter turn about the origin takes (1,0) to (0,1).
    Rig r(ACP_F64, 2, 2);
    load(r, BASE, {0, -1, 1, 0});
    load(r, BASE + 32, {1, 0});
    r.run(ACP_MATVEC);
    // B is a VECTOR of two, so it is 16 bytes, not a matrix-sized 32.
    CHECK_EQ(take(r, BASE + 32 + 16, 2), std::vector<double>{0, 1});
  }

  TEST_CASE("transposes, and identity ignores A entirely") {
    Rig r(ACP_F64, 2, 3);
    load(r, BASE, A);
    r.run(ACP_TRANSPOSE);
    CHECK_EQ(take(r, BASE + 48, 6), std::vector<double>{1, 4, 2, 5, 3, 6});

    Rig id(ACP_F64, 3, 3);
    load(id, BASE, {9, 9, 9, 9, 9, 9, 9, 9, 9});
    id.run(ACP_IDENTITY);
    CHECK_EQ(take(id, BASE + 72, 9), std::vector<double>{1, 0, 0, 0, 1, 0, 0, 0, 1});
  }

  TEST_CASE("gives a determinant as a real, for every size") {
    struct Case {
      int n;
      std::vector<double> m;
      double want;
    };
    const std::vector<Case> cases = {
        {1, {7}, 7},
        {2, {1, 2, 3, 4}, -2},
        {3, {6, 1, 1, 4, -2, 5, 2, 8, 7}, -306},
        {4, {1, 0, 2, -1, 3, 0, 0, 5, 2, 1, 4, -3, 1, 0, 5, 0}, 30},
    };
    for (const auto& c : cases) {
      CAPTURE(c.n);
      Rig r(ACP_F64, c.n, c.n);
      load(r, BASE, c.m);
      r.run(ACP_DET);
      CHECK(close(r.getF(BASE + 8 * c.n * c.n), c.want, 9));
    }
  }

  TEST_CASE("inverts, and A times its inverse is the identity") {
    const std::vector<double> m = {4, 7, 2, 6};
    Rig r(ACP_F64, 2, 2);
    load(r, BASE, m);
    r.run(ACP_INVERSE);
    const std::vector<double> inv = take(r, BASE + 32, 4);
    // Multiply by hand rather than through the device, so this checks the
    // inverse rather than checking MATMUL against itself.
    const double prod[4] = {
        m[0] * inv[0] + m[1] * inv[2],
        m[0] * inv[1] + m[1] * inv[3],
        m[2] * inv[0] + m[3] * inv[2],
        m[2] * inv[1] + m[3] * inv[3],
    };
    CHECK(close(prod[0], 1, 12));
    CHECK(close(prod[1], 0, 12));
    CHECK(close(prod[2], 0, 12));
    CHECK(close(prod[3], 1, 12));
  }

  TEST_CASE("inverts a 4 by 4 through the pivoting path") {
    // The first column starts with a zero, so the pivot must move.
    const std::vector<double> m = {0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 4};
    Rig r(ACP_F64, 4, 4);
    load(r, BASE, m);
    r.run(ACP_INVERSE);
    CHECK_EQ(take(r, BASE + 128, 16),
             std::vector<double>{0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0.5, 0, 0, 0, 0, 0.25});
  }

  TEST_CASE("sets singular on a matrix with two equal rows, and writes zero") {
    Rig r(ACP_F64, 2, 2);
    load(r, BASE, {1, 2, 1, 2});
    r.run(ACP_INVERSE);
    CHECK(has(r.flags(), ACP_SINGULAR));
    CHECK_EQ(take(r, BASE + 32, 4), std::vector<double>{0, 0, 0, 0});
  }

  TEST_CASE("refuses a determinant or an inverse of a matrix that is not square") {
    for (int cmd : {ACP_DET, ACP_INVERSE}) {
      CAPTURE(cmd);
      Rig r(ACP_F64, 2, 3);
      r.run(cmd);
      CHECK(has(r.flags(), ACP_BADDIM));
    }
  }

  TEST_CASE("multiplies integer matrices exactly") {
    Rig r(ACP_I64, 2, 2);
    r.out(ACP_COLS_B, 2);
    const int64_t a[4] = {1, 2, 3, 4};
    const int64_t b[4] = {5, 6, 7, 8};
    for (int i = 0; i < 4; i++) r.putI(BASE + 8 * i, a[i]);
    for (int i = 0; i < 4; i++) r.putI(BASE + 32 + 8 * i, b[i]);
    r.run(ACP_MATMUL);
    std::vector<int64_t> got;
    for (int i = 0; i < 4; i++) got.push_back(r.getI(BASE + 64 + 8 * i));
    CHECK_EQ(got, std::vector<int64_t>{19, 22, 43, 50});
  }

  TEST_CASE("sums an integer dot product past 64 bits and reports the overflow") {
    // Two products of 2^62 make 2^124, which the sum holds exactly and the
    // 64 bit result cannot. A sum that comes back down fits again.
    Rig r(ACP_I64, 2, 1);
    const int64_t big = int64_t{1} << 62;
    r.putI(BASE, big);
    r.putI(BASE + 8, big);
    r.putI(BASE + 16, big);
    r.putI(BASE + 24, big);
    r.run(ACP_DOT);
    CHECK_EQ(r.getI(BASE + 32), 0);
    CHECK(has(r.flags(), ACP_OVERFLOW));
    r.putI(BASE + 24, -big);
    r.run(ACP_DOT);
    CHECK_EQ(r.getI(BASE + 32), 0);
    CHECK_FALSE(has(r.flags(), ACP_OVERFLOW));
  }
}

TEST_SUITE("ACP tables") {
  Rig table(int func, int count, double start, double step) {
    Rig r(ACP_F64);
    r.putF(BASE, start);
    r.putF(BASE + 8, step);
    r.out(ACP_FUNC, func);
    r.out(ACP_ARG, count >> 8);
    r.out(ACP_ARG2, count & 0xff);
    r.run(ACP_GEN_TABLE);
    return r;
  }

  TEST_CASE("writes count entries and nothing past them") {
    Rig r = table(ACP_SIN, 8, 0, 0.25);
    for (int i = 0; i < 8; i++) {
      CAPTURE(i);
      CHECK(same(r.getF(BASE + 16 + 8 * i), std::sin(0.25 * i)));
    }
    // The ninth slot was never written, and RAM started at zero.
    CHECK_EQ(r.getF(BASE + 16 + 64), 0);
  }

  TEST_CASE("matches the single-value operation at every entry") {
    // The device is its own reference here, which is the point: no second
    // implementation of a sine exists to disagree with.
    Rig r = table(ACP_SIN, 256, 0, (2 * PI) / 256);
    for (int i = 0; i < 256; i++) {
      CAPTURE(i);
      const double single = f1(ACP_SIN, ((2 * PI) / 256) * i).v;
      CHECK(same(r.getF(BASE + 16 + 8 * i), single));
    }
  }

  TEST_CASE("steps the input by operand B") {
    Rig r = table(ACP_EXP, 4, 1, 2);
    for (int i = 0; i < 4; i++) CHECK(same(r.getF(BASE + 16 + 8 * i), std::exp(1 + 2.0 * i)));
  }

  TEST_CASE("writes nothing for a count of zero, and does not call it an error") {
    Rig r = table(ACP_SIN, 0, 0, 1);
    CHECK_EQ(r.getF(BASE + 16), 0);
    CHECK_FALSE(has(r.flags(), ACP_BADFMT));
    CHECK_FALSE(has(r.flags(), ACP_BADDIM));
  }

  TEST_CASE("takes the full 16 bit count from the two argument ports") {
    Rig r = table(ACP_EXP, 0x101, 0, 0);
    CHECK_EQ(r.getF(BASE + 16 + 8 * 0x100), 1);
    CHECK_EQ(r.getF(BASE + 16 + 8 * 0x101), 0);
  }

  TEST_CASE("refuses an integer type, because the functions are float only") {
    Rig r(ACP_I64);
    r.out(ACP_FUNC, ACP_SIN);
    r.out(ACP_ARG2, 4);
    r.run(ACP_GEN_TABLE);
    CHECK(has(r.flags(), ACP_BADFMT));
  }
}

// The two references in docs/design travel with the repository, and both
// are written by hand. The GPU's reference drifted by seven entries before
// it was pinned; these are pinned against the tables in acp_ports.h. The
// files are found from this source file's own path, so no build setting is
// needed.
namespace {

std::string readDoc(const char* name) {
  std::string here = __FILE__;
  const auto slash = here.find_last_of("/\\");
  const std::string dir = slash == std::string::npos ? "." : here.substr(0, slash);
  std::ifstream in(dir + "/../../docs/design/" + name);
  std::stringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

std::vector<std::string> lines(const std::string& text) {
  std::vector<std::string> out;
  std::stringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    out.push_back(line);
  }
  return out;
}

std::string trim(const std::string& s) {
  const auto a = s.find_first_not_of(" \t");
  if (a == std::string::npos) return "";
  const auto b = s.find_last_not_of(" \t");
  return s.substr(a, b - a + 1);
}

// Every "| 0xNN | NAME |" row, name to value.
std::map<std::string, int> tableRows(const std::string& doc) {
  std::map<std::string, int> rows;
  for (const std::string& line : lines(doc)) {
    if (line.rfind("| 0x", 0) != 0) continue;
    const auto bar = line.find('|', 1);
    if (bar == std::string::npos) continue;
    const auto bar2 = line.find('|', bar + 1);
    if (bar2 == std::string::npos) continue;
    const std::string hex = trim(line.substr(1, bar - 1));
    const std::string name = trim(line.substr(bar + 1, bar2 - bar - 1));
    rows[name] = static_cast<int>(std::stoul(hex, nullptr, 16));
  }
  return rows;
}

// Every ACP_ word in the text, as a regex \bACP_[A-Z_0-9]+\b would find it.
std::set<std::string> acpNames(const std::string& doc) {
  std::set<std::string> names;
  const auto wordy = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
  size_t at = 0;
  while ((at = doc.find("ACP_", at)) != std::string::npos) {
    if (at > 0 && wordy(doc[at - 1])) { at += 4; continue; }
    size_t end = at + 4;
    while (end < doc.size() && (std::isupper(static_cast<unsigned char>(doc[end])) ||
                                std::isdigit(static_cast<unsigned char>(doc[end])) || doc[end] == '_')) {
      end++;
    }
    if (end < doc.size() && wordy(doc[end])) { at = end; continue; }
    // A bare prefix, as in "the ACP_ prefix", is not a name.
    if (end > at + 4) names.insert(doc.substr(at, end - at));
    at = end;
  }
  return names;
}

std::set<std::string> machineNames() {
  std::set<std::string> real;
  for (const auto& p : PORTS) real.insert(std::string(p.name));
  for (const auto& f : FMTS) real.insert(std::string(f.name));
  for (const auto& c : CMDS) real.insert(std::string(c.name));
  for (const auto& b : FLAG_BITS) real.insert(std::string(b.name));
  return real;
}

void pin(const std::map<std::string, int>& rows, std::span<const NamedValue> table, const char* what) {
  for (const auto& entry : table) {
    const std::string name(entry.name);
    const auto it = rows.find(name);
    CHECK_MESSAGE(it != rows.end(), name, " missing from the ", what, " reference");
    if (it != rows.end()) CHECK_MESSAGE(it->second == entry.value, name, " documented at the wrong value");
  }
}

}  // namespace

TEST_SUITE("docs/design/acp-ports.md") {
  TEST_CASE("lists every port, type, command and flag at its real number") {
    const std::string doc = readDoc("acp-ports.md");
    REQUIRE_FALSE(doc.empty());
    const auto rows = tableRows(doc);
    pin(rows, PORTS, "port");
    pin(rows, FMTS, "type");
    pin(rows, CMDS, "command");
    pin(rows, FLAG_BITS, "flag");
  }

  TEST_CASE("names nothing the machine does not have") {
    const auto rows = tableRows(readDoc("acp-ports.md"));
    const auto real = machineNames();
    for (const auto& [name, value] : rows) {
      CHECK_MESSAGE(real.count(name) == 1, name, " is in the reference but not in the machine");
    }
  }

  TEST_CASE("has a worked example that assembles") {
    const std::string doc = readDoc("acp-ports.md");
    std::vector<std::string> blocks;
    size_t at = 0;
    while ((at = doc.find("```asm\n", at)) != std::string::npos) {
      const size_t start = at + 7;
      const size_t end = doc.find("```", start);
      REQUIRE(end != std::string::npos);
      blocks.push_back(doc.substr(start, end - start));
      at = end + 3;
    }
    CHECK_MESSAGE(!blocks.empty(), "no assembly listing in the reference");
    for (const std::string& listing : blocks) {
      CAPTURE(listing);
      CHECK(assemble(listing).errors.empty());
    }
  }
}

// docs/acp-design.md is the spec, and it was written before the code. Two
// names drifted while the device was built and no doc-to-machine check
// existed to catch them. The spec called the complex angle ACP_ARG, which is
// a PORT. It still carried ACP_VEC4 from the draft with fixed shapes.
// Both are found by comparing the names in the prose with the machine's own
// tables, in both directions.
TEST_SUITE("docs/design/acp-design.md") {
  TEST_CASE("names nothing the machine does not have") {
    const auto named = acpNames(readDoc("acp-design.md"));
    REQUIRE_FALSE(named.empty());
    const auto real = machineNames();
    std::vector<std::string> extra;
    for (const auto& n : named) {
      if (real.count(n) == 0) extra.push_back(n);
    }
    CHECK_EQ(extra, std::vector<std::string>{});
  }

  TEST_CASE("names everything the machine does have") {
    const auto named = acpNames(readDoc("acp-design.md"));
    std::vector<std::string> missing;
    for (const auto& n : machineNames()) {
      if (named.count(n) == 0) missing.push_back(n);
    }
    CHECK_EQ(missing, std::vector<std::string>{});
  }
}

// Renaming a heading breaks the link that points at it, and nothing shows
// that in a diff. Two of these were dead when the device was finished.
TEST_SUITE("the ACP documents' own contents lists") {
  std::string slug(const std::string& h) {
    std::string out;
    for (char c : h) {
      const auto lc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (lc == ' ') out.push_back('-');
      else if ((lc >= 'a' && lc <= 'z') || (lc >= '0' && lc <= '9') || lc == '-') out.push_back(lc);
    }
    return out;
  }

  void check(const std::string& doc, const char* name) {
    CAPTURE(name);
    std::vector<std::string> toc;
    std::vector<std::string> heads;
    for (const std::string& line : lines(doc)) {
      if (line.rfind("- [", 0) == 0 && line.size() > 2 && line.back() == ')') {
        const auto hash = line.find("](#");
        if (hash != std::string::npos) toc.push_back(line.substr(hash + 3, line.size() - hash - 4));
      }
      if (line.rfind("## ", 0) == 0) {
        const std::string s = slug(line.substr(3));
        if (s != "contents") heads.push_back(s);
      }
    }
    REQUIRE_FALSE(toc.empty());
    for (const auto& t : toc) CHECK_MESSAGE(std::find(heads.begin(), heads.end(), t) != heads.end(), t, " links to nothing");
    for (const auto& h : heads) CHECK_MESSAGE(std::find(toc.begin(), toc.end(), h) != toc.end(), h, " not listed");
  }

  TEST_CASE("links to a section that exists, and lists every one") {
    check(readDoc("acp-design.md"), "acp-design.md");
    check(readDoc("acp-ports.md"), "acp-ports.md");
  }
}

TEST_SUITE("what the spec says about a real in a complex slot") {
  TEST_CASE("widens rather than refusing, because that is what conversion does") {
    // The spec first claimed this was ACP_BADFMT. The machine widens the real
    // to a complex with a zero imaginary part, as every other conversion
    // here does. The spec was corrected to match the machine.
    Rig r(ACP_C64);
    r.putF(BASE, 3);
    r.putF(BASE + 8, 4);
    r.run(ACP_ABS);
    CHECK_EQ(r.getF(BASE + 16), 5);
    CHECK_EQ(r.getF(BASE + 24), 0);
    CHECK_FALSE(has(r.flags(), ACP_BADFMT));
  }
}

TEST_SUITE("the type restrictions the spec now states") {
  bool refuses(int fmt, int cmd, int rows = 1, int cols = 1) {
    Rig r(fmt, rows, cols);
    r.run(cmd);
    return has(r.flags(), ACP_BADFMT);
  }

  TEST_CASE("refuses a remainder and a comparison of complex operands") {
    CHECK(refuses(ACP_C64, ACP_REM));
    CHECK(refuses(ACP_C64, ACP_CMP));
  }

  TEST_CASE("refuses the square-root family on anything but floats") {
    for (int cmd : {ACP_NORM, ACP_NORMALIZE, ACP_DIST}) {
      CAPTURE(cmd);
      CHECK(refuses(ACP_I64, cmd, 2, 1));
      CHECK(refuses(ACP_C64, cmd, 2, 1));
    }
    for (int cmd : {ACP_DET, ACP_INVERSE}) {
      CAPTURE(cmd);
      CHECK(refuses(ACP_I64, cmd, 2, 2));
    }
  }

  TEST_CASE("takes any type for a dot product and a matrix product, so integers stay exact") {
    CHECK_FALSE(refuses(ACP_I64, ACP_DOT, 2, 1));
    CHECK_FALSE(refuses(ACP_I64, ACP_MATMUL, 2, 2));
    CHECK_FALSE(refuses(ACP_C64, ACP_MATMUL, 2, 2));
  }
}

// acpShapesFor and acpKindsFor are the rules a reference derives its shape
// and type columns from. They are pinned against the device's answers.
TEST_SUITE("acpShapesFor and acpKindsFor") {
  TEST_CASE("answer nothing for a shape the device refuses") {
    CHECK_FALSE(acpShapesFor(ACP_CROSS, 2, 1, 1).has_value());
    CHECK_FALSE(acpShapesFor(ACP_DET, 2, 3, 1).has_value());
    CHECK_FALSE(acpShapesFor(ACP_MATMUL, 2, 3, 0).has_value());
    CHECK_FALSE(acpShapesFor(0xfe, 1, 1, 1).has_value());
  }

  TEST_CASE("give a unary command no B and a reduction a 1 by 1 result") {
    const auto neg = acpShapesFor(ACP_NEG, 2, 3, 1);
    REQUIRE(neg.has_value());
    CHECK_FALSE(neg->b.has_value());
    CHECK_EQ(neg->r, Dim{2, 3});
    const auto dot = acpShapesFor(ACP_DOT, 4, 1, 1);
    REQUIRE(dot.has_value());
    CHECK_EQ(*dot->b, Dim{4, 1});
    CHECK_EQ(dot->r, Dim{1, 1});
    const auto mm = acpShapesFor(ACP_MATMUL, 2, 3, 4);
    REQUIRE(mm.has_value());
    CHECK_EQ(*mm->b, Dim{3, 4});
    CHECK_EQ(mm->r, Dim{2, 4});
  }

  TEST_CASE("list the kinds each family takes") {
    CHECK_EQ(acpKindsFor(ACP_AND), std::vector<Kind>{Kind::Int});
    CHECK_EQ(acpKindsFor(ACP_SQRT), std::vector<Kind>{Kind::Float});
    CHECK_EQ(acpKindsFor(ACP_CONJ), std::vector<Kind>{Kind::Complex});
    CHECK_EQ(acpKindsFor(ACP_REM), std::vector<Kind>{Kind::Int, Kind::Float});
    CHECK_EQ(acpKindsFor(ACP_ADD), std::vector<Kind>{Kind::Int, Kind::Float, Kind::Complex});
  }
}
