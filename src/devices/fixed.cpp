#include "devices/fixed.h"

#include <array>
#include <cmath>
#include <numbers>

namespace sc8 {

namespace {

// Angles are 16 bit, so one turn is 65536 and a quarter turn is 16384. The
// table samples 1024 points of a turn, leaving 64 angle steps between two
// entries for the interpolation to cover.
constexpr int TABLE_BITS = 10;
constexpr int TABLE_SIZE = 1 << TABLE_BITS;
constexpr int FRAC_BITS = 16 - TABLE_BITS;
constexpr int64_t FRAC_MASK = (1 << FRAC_BITS) - 1;

// DESIGN: built once, from the host's sin, and then never consulted again.
// The table is what the renderer reads, so the picture is the same on every
// machine whatever the host's sine does. The browser built it from V8's sin;
// a host whose sin differs by an ulp could flip one entry at a half.
const std::array<Fx, TABLE_SIZE>& sinTable() {
  static const std::array<Fx, TABLE_SIZE> table = [] {
    std::array<Fx, TABLE_SIZE> t{};
    for (int i = 0; i < TABLE_SIZE; i++) {
      t[static_cast<size_t>(i)] =
          static_cast<Fx>(jsRound(std::sin((2 * std::numbers::pi * i) / TABLE_SIZE) * static_cast<double>(FX_ONE)));
    }
    return t;
  }();
  return table;
}

Fx floorDiv(Fx num, Fx den) {
  Fx q = num / den;
  Fx r = num % den;
  if (r != 0 && ((r < 0) != (den < 0))) q--;
  return q;
}

}  // namespace

Fx roundDiv(Fx num, Fx den) {
  if (den < 0) {
    num = -num;
    den = -den;
  }
  // floor(num / den + 1/2) with everything doubled to stay in integers.
  return floorDiv(2 * num + den, 2 * den);
}

double jsRound(double v) {
  double f = std::floor(v);
  return (v - f >= 0.5) ? f + 1 : f;
}

Fx fxMul(Fx a, Fx b) { return roundDiv(a * b, FX_ONE); }

Fx fxDiv(Fx a, Fx b) {
  if (b == 0) return 0;
  return roundDiv(a * FX_ONE, b);
}

Fx fxSin(int64_t angle) {
  const int64_t a = angle & 0xffff;
  const size_t i = static_cast<size_t>(a >> FRAC_BITS);
  const int64_t frac = a & FRAC_MASK;
  const auto& t = sinTable();
  const Fx lo = t[i];
  if (frac == 0) return lo;
  const Fx hi = t[(i + 1) & (TABLE_SIZE - 1)];
  return lo + roundDiv((hi - lo) * frac, FRAC_MASK + 1);
}

Fx fxCos(int64_t angle) {
  // A quarter turn ahead of the sine, which keeps one table for both.
  return fxSin((angle + 0x4000) & 0xffff);
}

}  // namespace sc8
