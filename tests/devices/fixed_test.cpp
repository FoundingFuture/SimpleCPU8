#include <doctest.h>

#include <cmath>
#include <numbers>
#include <set>

#include "devices/fixed.h"

using namespace sc8;

// 16.16 fixed point, for the world renderer's transform maths. The GPU is an
// integer machine, and this keeps it one. See docs/design/gpu-world-design.md.
//
// Angles are 16 bit, so one turn is 65536 and a quarter turn is 16384.

namespace {
constexpr int64_t QUARTER = 16384;
}

TEST_SUITE("16.16 arithmetic") {
  TEST_CASE("converts to and from whole numbers") {
    CHECK_EQ(fxFrom(1), FX_ONE);
    CHECK_EQ(fxFrom(-3), -3 * FX_ONE);
    CHECK_EQ(fxToInt(fxFrom(7)), 7);
    // Truncation toward zero, stated so a negative operand is not a surprise.
    CHECK_EQ(fxToInt(fxFrom(1) + FX_ONE / 2), 1);
    CHECK_EQ(fxToInt(-fxFrom(1) - FX_ONE / 2), -1);
  }

  TEST_CASE("multiplies without losing the high bits") {
    CHECK_EQ(fxMul(fxFrom(1), fxFrom(1)), FX_ONE);
    // 1.5 * 2.5 is 3.75, exactly representable.
    CHECK_EQ(fxMul(FX_ONE * 3 / 2, FX_ONE * 5 / 2), FX_ONE * 15 / 4);
    CHECK_EQ(fxMul(fxFrom(-4), fxFrom(3)), fxFrom(-12));
    CHECK_EQ(fxMul(0, fxFrom(9999)), 0);
  }

  TEST_CASE("rounds a product rather than truncating it") {
    // Every product above is exactly representable, so rounding and
    // truncation agree and neither is pinned. This one lands on a half:
    // one raw unit times a half is 0.5 raw units, which rounds to 1 and
    // truncates to 0. Truncating would bias every transform toward zero.
    CHECK_EQ(fxMul(1, FX_ONE / 2), 1);
    CHECK_EQ(fxMul(3, FX_ONE / 2), 2);  // 1.5 rounds to 2
  }

  TEST_CASE("multiplies a big coordinate by a small matrix element exactly") {
    // The renderer's widest product: a vertex at the edge of the world in
    // 16.16, times a matrix element near one. Both fit int64 with room.
    const Fx far = fxFrom(32767);
    CHECK_EQ(fxMul(far, FX_ONE), far);
    CHECK_EQ(fxMul(far, FX_ONE / 2), far / 2);
  }

  TEST_CASE("divides") {
    CHECK_EQ(fxDiv(fxFrom(1), fxFrom(2)), FX_ONE / 2);
    CHECK_EQ(fxDiv(fxFrom(-9), fxFrom(3)), fxFrom(-3));
    // A third is not representable, so this pins the rounding rather than
    // pretending the value is exact.
    CHECK_EQ(fxDiv(fxFrom(1), fxFrom(3)), static_cast<Fx>(std::lround(FX_ONE / 3.0)));
  }

  TEST_CASE("refuses a divide by zero rather than giving infinity") { CHECK_EQ(fxDiv(fxFrom(1), 0), 0); }

  TEST_CASE("rounds a half toward positive infinity, as Math.round did") {
    CHECK_EQ(roundDiv(5, 2), 3);
    CHECK_EQ(roundDiv(-5, 2), -2);
    CHECK_EQ(roundDiv(-7, 2), -3);
    CHECK_EQ(roundDiv(7, -2), -3);
    CHECK_EQ(jsRound(2.5), 3);
    CHECK_EQ(jsRound(-2.5), -2);
    CHECK_EQ(jsRound(-2.6), -3);
  }
}

TEST_SUITE("the 16.16 sine table") {
  TEST_CASE("gives exact sines at the four quarter turns") {
    // Exact, not close. Anything else means the table's phase is wrong, and
    // a tolerance would hide it.
    CHECK_EQ(fxSin(0), 0);
    CHECK_EQ(fxSin(QUARTER), FX_ONE);
    CHECK_EQ(fxSin(2 * QUARTER), 0);
    CHECK_EQ(fxSin(3 * QUARTER), -FX_ONE);
  }

  TEST_CASE("gives exact cosines at the four quarter turns") {
    CHECK_EQ(fxCos(0), FX_ONE);
    CHECK_EQ(fxCos(QUARTER), 0);
    CHECK_EQ(fxCos(2 * QUARTER), -FX_ONE);
    CHECK_EQ(fxCos(3 * QUARTER), 0);
  }

  TEST_CASE("is exactly periodic, so an angle and that angle plus a turn agree") {
    for (int64_t a : {0, 1, 1234, 16384, 40000, 65535}) {
      CHECK_EQ(fxSin((a + 65536) & 0xffff), fxSin(a));
    }
  }

  TEST_CASE("interpolates between table entries rather than stepping") {
    // The table has 1024 entries over a turn, so 64 angle steps sit between
    // two of them. Without interpolation every one of those 64 reads the
    // same value, which is what this refuses.
    std::set<Fx> seen;
    for (int64_t a = 0; a < 64; a++) seen.insert(fxSin(a));
    CHECK_GT(seen.size(), 8u);
  }

  TEST_CASE("rises monotonically across the first quarter turn") {
    Fx last = fxSin(0);
    for (int64_t a = 0; a <= QUARTER; a += 37) {
      const Fx v = fxSin(a);
      CHECK_GE(v, last);
      last = v;
    }
  }

  TEST_CASE("matches sin closely over the whole turn") {
    // Not exactly: the table is sampled and interpolated, so the error is
    // bounded rather than zero. The bound is stated here and tested, so a
    // change that widens it fails rather than passing quietly.
    double worst = 0;
    for (int64_t a = 0; a < 65536; a += 7) {
      const double want = std::sin((2 * std::numbers::pi * static_cast<double>(a)) / 65536);
      worst = std::max(worst, std::fabs(static_cast<double>(fxSin(a)) / static_cast<double>(FX_ONE) - want));
    }
    CHECK_LT(worst, 1.0 / 2048);
  }

  TEST_CASE("keeps the identity that sine squared plus cosine squared is one") {
    for (int64_t a : {0, 1000, 16384, 30000, 50000, 65535}) {
      const double s = static_cast<double>(fxSin(a)) / static_cast<double>(FX_ONE);
      const double c = static_cast<double>(fxCos(a)) / static_cast<double>(FX_ONE);
      CHECK_EQ(s * s + c * c, doctest::Approx(1).epsilon(0.001));
    }
  }
}
