// Near plane clipping, the perspective divide, and the viewport transform.
//
// DESIGN: clipping happens in homogeneous coordinates, BEFORE the divide.
// Dividing by a negative w turns a vertex behind the camera into a plausible
// coordinate in front of it, so a world seen from inside turns itself out.
// The GPU's older MODE_GRAPHICS path clamps its denominator instead, which
// works only because its eye cannot move. See docs/design/gpu-world-design.md.
#pragma once

#include <optional>

#include "devices/fixed.h"

namespace sc8 {

// A vertex in clip space, with its varyings. `shade` is the depth cue, and it
// is interpolated exactly as a GPU interpolates a varying.
struct ClipVertex {
  Fx x = 0;
  Fx y = 0;
  Fx z = 0;
  Fx w = 0;
  Fx shade = 0;
};

struct ClipEdge {
  ClipVertex a;
  ClipVertex b;
};

std::optional<ClipEdge> clipNear(const ClipVertex& a, const ClipVertex& b, int range);

// The vertex after the divide. w is kept, because a perspective correct
// varying needs it after the divide as much as before.
struct NdcVertex {
  Fx x = 0;
  Fx y = 0;
  Fx z = 0;
  Fx w = 0;
  Fx shade = 0;
};

NdcVertex toNdc(const ClipVertex& v);

// DESIGN: the viewport is its own step and is never folded into the
// projection matrix. Hardware keeps them apart, and a projection with the
// viewport baked in is not one a GPU can be handed.
//
// -1 maps to pixel 0 and 1 maps to the last pixel, so 0 lands in the middle.
int toViewport(Fx ndc, int size);

// DESIGN: a varying is interpolated the way hardware interpolates one, by
// carrying value over w against one over w and dividing back. Interpolating
// linearly in screen space is wrong on any edge running away from the
// viewer, and it is the standard mistake.
double perspectiveVarying(double a, double wa, double b, double wb, double t);

}  // namespace sc8
