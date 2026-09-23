#include "devices/clip.h"

#include "devices/mat4.h"

namespace sc8 {

namespace {

// How far a vertex is inside the near plane, in clip space. Positive or zero
// is inside, and the boundary itself counts as inside.
//
// WebGL's near plane is z >= -w and WebGPU's is z >= 0. That is the only
// difference between the two conventions in this file.
Fx insideNear(const ClipVertex& v, int range) { return range == DEPTH_ZERO_TO_ONE ? v.z : v.z + v.w; }

Fx lerp(Fx a, Fx b, Fx t) { return a + roundDiv((b - a) * t, FX_ONE); }

// The point where the edge crosses the plane, found in CLIP space. Doing this
// after the divide would interpolate along a line that is already wrong.
ClipVertex split(const ClipVertex& a, const ClipVertex& b, Fx da, Fx db) {
  const Fx t = fxDiv(da, da - db);  // 0 at a, FX_ONE at b
  ClipVertex out;
  out.x = lerp(a.x, b.x, t);
  out.y = lerp(a.y, b.y, t);
  out.z = lerp(a.z, b.z, t);
  out.w = lerp(a.w, b.w, t);
  // The varying rides along. Dropping it here shades a clipped edge wrongly
  // from the cut onward.
  out.shade = lerp(a.shade, b.shade, t);
  return out;
}

}  // namespace

std::optional<ClipEdge> clipNear(const ClipVertex& a, const ClipVertex& b, int range) {
  const Fx da = insideNear(a, range);
  const Fx db = insideNear(b, range);
  if (da < 0 && db < 0) return std::nullopt;  // both behind: nothing to draw
  ClipEdge edge;
  if (da >= 0 && db >= 0) {
    edge = {a, b};
  } else if (da >= 0) {
    edge = {a, split(a, b, da, db)};
  } else {
    edge = {split(b, a, db, da), b};
  }
  // A real projection cannot put an inside vertex at a w of zero or less,
  // because w is minus the view space z and anything inside is in front.
  // This only fires on a matrix that is not a projection, and dropping the
  // edge keeps one invariant unconditional: nothing downstream ever divides
  // by a w that is zero or negative.
  if (edge.a.w <= 0 || edge.b.w <= 0) return std::nullopt;
  return edge;
}

NdcVertex toNdc(const ClipVertex& v) {
  NdcVertex n;
  n.x = fxDiv(v.x, v.w);
  n.y = fxDiv(v.y, v.w);
  n.z = fxDiv(v.z, v.w);
  n.w = v.w;
  n.shade = v.shade;
  return n;
}

int toViewport(Fx ndc, int size) {
  // t is (ndc + 1) / 2: 0 at the left edge, FX_ONE at the right. The halving
  // is folded into the divisor so the integer rounding matches the double.
  return static_cast<int>(roundDiv((ndc + FX_ONE) * (size - 1), 2 * FX_ONE));
}

double perspectiveVarying(double a, double wa, double b, double wb, double t) {
  const double invA = wa == 0 ? 0 : 1 / wa;
  const double invB = wb == 0 ? 0 : 1 / wb;
  const double inv = invA + (invB - invA) * t;
  if (inv == 0) return a;
  const double over = a * invA + (b * invB - a * invA) * t;
  return over / inv;
}

}  // namespace sc8
