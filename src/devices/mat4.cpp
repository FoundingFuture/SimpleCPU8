#include "devices/mat4.h"

namespace sc8 {

namespace {

// Rotation about each axis, from a 16 bit angle. One turn is 65536.
Mat4 rotX(int64_t a) {
  Mat4 m = mat4Identity();
  const Fx s = fxSin(a);
  const Fx c = fxCos(a);
  m[5] = c;
  m[6] = -s;
  m[9] = s;
  m[10] = c;
  return m;
}

Mat4 rotY(int64_t a) {
  Mat4 m = mat4Identity();
  const Fx s = fxSin(a);
  const Fx c = fxCos(a);
  m[0] = c;
  m[2] = s;
  m[8] = -s;
  m[10] = c;
  return m;
}

Mat4 rotZ(int64_t a) {
  Mat4 m = mat4Identity();
  const Fx s = fxSin(a);
  const Fx c = fxCos(a);
  m[0] = c;
  m[1] = -s;
  m[4] = s;
  m[5] = c;
  return m;
}

// Yaw about Y, then pitch about X, then roll about Z.
Mat4 rotation(const Vec3& r) { return mat4Multiply(rotZ(r[2]), mat4Multiply(rotX(r[1]), rotY(r[0]))); }

}  // namespace

Mat4 mat4Identity() {
  Mat4 m{};
  m[0] = FX_ONE;
  m[5] = FX_ONE;
  m[10] = FX_ONE;
  m[15] = FX_ONE;
  return m;
}

Mat4 mat4Multiply(const Mat4& a, const Mat4& b) {
  Mat4 out{};
  for (size_t r = 0; r < 4; r++) {
    for (size_t c = 0; c < 4; c++) {
      // The sum runs from k zero upward, as everywhere else in this project.
      Fx s = 0;
      for (size_t k = 0; k < 4; k++) s += fxMul(a[r * 4 + k], b[k * 4 + c]);
      out[r * 4 + c] = s;
    }
  }
  return out;
}

Vec4 mat4Apply(const Mat4& m, const Vec4& v) {
  Vec4 out{};
  for (size_t r = 0; r < 4; r++) {
    Fx s = 0;
    for (size_t k = 0; k < 4; k++) s += fxMul(m[r * 4 + k], v[k]);
    out[r] = s;
  }
  return out;
}

Mat4 mat4Model(const Vec3& position, const Vec3& rotationAngles, Fx scale) {
  Mat4 s = mat4Identity();
  s[0] = scale;
  s[5] = scale;
  s[10] = scale;
  Mat4 m = mat4Multiply(rotation(rotationAngles), s);
  m[3] = position[0];
  m[7] = position[1];
  m[11] = position[2];
  return m;
}

Mat4 mat4View(const Vec3& eye, const Vec3& rotationAngles) {
  const Mat4 r = rotation(rotationAngles);
  // Transpose the upper 3 by 3, which inverts a rotation.
  Mat4 v = mat4Identity();
  for (size_t i = 0; i < 3; i++) {
    for (size_t j = 0; j < 3; j++) v[i * 4 + j] = r[j * 4 + i];
  }
  // Then translate by the negated eye, expressed in the rotated frame.
  for (size_t i = 0; i < 3; i++) {
    Fx s = 0;
    for (size_t j = 0; j < 3; j++) s += fxMul(v[i * 4 + j], -eye[j]);
    v[i * 4 + 3] = s;
  }
  return v;
}

Mat4 mat4Perspective(Fx focal, Fx near, Fx far, int range) {
  Mat4 m{};
  m[0] = focal;
  m[5] = focal;
  const Fx span = far - near;
  if (range == DEPTH_ZERO_TO_ONE) {
    m[10] = fxDiv(-far, span);
    m[11] = fxMul(m[10], near);
  } else {
    m[10] = fxDiv(-(far + near), span);
    m[11] = fxDiv(fxMul(-2 * near, far), span);
  }
  m[14] = -FX_ONE;  // w becomes -z, which is what hardware divides by
  return m;
}

}  // namespace sc8
