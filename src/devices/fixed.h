// 16.16 fixed point, for the world renderer's transform maths.
//
// DESIGN: the GPU is an integer machine and this keeps it one. Every value
// here is a whole number scaled by 65536, so a transform is integer
// arithmetic the way a chip of this era would do it. See
// docs/design/gpu-world-design.md for where it is used.
//
// The TypeScript held these in doubles and rounded with Math.round. Here
// they are int64 and every rounding is exact integer arithmetic. The widest
// product the renderer forms is about 2^49, which int64 holds with room.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sc8 {

using Fx = int64_t;

constexpr int FX_SHIFT = 16;
constexpr Fx FX_ONE = Fx{1} << FX_SHIFT;  // 65536, which is 1.0

// Math.round(num / den) in integers: half rounds toward positive infinity,
// which is JavaScript's rule and not C's. den must not be zero.
Fx roundDiv(Fx num, Fx den);

// Math.round of a double, with the same half-up rule.
double jsRound(double v);

constexpr Fx fxFrom(Fx whole) { return whole * FX_ONE; }

// DESIGN: truncation toward zero, which is what the CPU's own divide does and
// what C does. Rounding toward negative infinity is the other defensible
// answer, so the choice is written down rather than left to a negative
// operand to reveal.
constexpr Fx fxToInt(Fx v) { return v / FX_ONE; }

Fx fxMul(Fx a, Fx b);

// A divide by zero gives zero rather than an infinity. Nothing downstream can
// draw with an infinity, and a zero at least keeps the frame finite.
Fx fxDiv(Fx a, Fx b);

// A 16 bit angle, interpolated between the two nearest table entries. Without
// the interpolation every 64 angles in a row would read the same value, and a
// camera would turn in visible steps.
Fx fxSin(int64_t angle);
Fx fxCos(int64_t angle);

}  // namespace sc8
