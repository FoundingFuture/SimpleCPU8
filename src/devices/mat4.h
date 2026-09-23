// Four by four matrices in 16.16 fixed point, for the world renderer.
//
// DESIGN: row major, and the conventions are OpenGL's. Right handed, Y up,
// the camera looks down -Z, and the fourth component of a clip position is
// -z. Those choices are not arbitrary: they are what a GPU already speaks, so
// a hardware backend later needs no transposing and no sign flipping. See
// docs/design/gpu-world-design.md, "The compatibility contract".
//
// Row major means element (r, c) lives at index r * 4 + c. A translation's x
// therefore sits at index 3, where column major would put it at index 12.
#pragma once

#include <array>

#include "devices/fixed.h"

namespace sc8 {

using Mat4 = std::array<Fx, 16>;
using Vec3 = std::array<Fx, 3>;
using Vec4 = std::array<Fx, 4>;

// WebGL clips z to -1..1 and WebGPU clips it to 0..1. The two differ in the
// projection matrix's third row and nothing else, which is what keeps the
// swap cheap. docs/design/gpu-world-design.md, "The depth convention".
enum DepthRange : int {
  DEPTH_GL = 0,
  DEPTH_ZERO_TO_ONE = 1,
};

Mat4 mat4Identity();

// A times B, in that order. Applying the result is the same as applying B and
// then A, which is why a model view projection is P times V times M.
Mat4 mat4Multiply(const Mat4& a, const Mat4& b);

Vec4 mat4Apply(const Mat4& m, const Vec4& v);

// DESIGN: scale, then rotate, then translate, in that order. Any other order
// gives a different matrix. Rotating before scaling would turn the scale
// itself, so a non-uniform scale would shear rather than stretch, and
// translating before rotating would swing an object around the origin
// instead of spinning it in place.
Mat4 mat4Model(const Vec3& position, const Vec3& rotation, Fx scale);

// DESIGN: the view matrix is the inverse of the camera's own transform. For a
// rotation and a translation that inverse is the transposed rotation times
// the negated translation, so no general inverse is needed or wanted.
Mat4 mat4View(const Vec3& eye, const Vec3& rotation);

// A perspective projection. `focal` is the distance to the projection plane
// in the same units as a screen half width, so FX_ONE is a 90 degree field of
// view. The screen is square here, so there is no aspect term.
//
// DESIGN: the third row is the ONLY difference between the two depth ranges.
// Everything else, including w, is identical, which is what lets the renderer
// draw the same picture under either one.
Mat4 mat4Perspective(Fx focal, Fx near, Fx far, int range = DEPTH_GL);

}  // namespace sc8
