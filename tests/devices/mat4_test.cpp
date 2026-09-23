#include <doctest.h>

#include <cmath>

#include "devices/fixed.h"
#include "devices/mat4.h"

using namespace sc8;

// Row major, four by four, 16.16 fixed point. The conventions are OpenGL's:
// right handed, Y up, the camera looks down -Z, and w is -z.
//
// Every matrix here is asymmetric on purpose. A symmetric one cannot tell row
// major from column major, nor A times B from B times A.

namespace {

constexpr Fx I = FX_ONE;
Fx at(const Mat4& m, size_t r, size_t c) { return m[r * 4 + c]; }
double whole(Fx v) { return static_cast<double>(v) / static_cast<double>(FX_ONE); }

}  // namespace

TEST_SUITE("matrix basics") {
  TEST_CASE("has an identity that changes nothing") {
    const Vec4 p = {fxFrom(3), fxFrom(-4), fxFrom(5), I};
    CHECK(mat4Apply(mat4Identity(), p) == p);
  }

  TEST_CASE("multiplies in the order A times B, not B times A") {
    // Translate by x, then scale by two. Applying to the origin must give
    // the translation scaled, or the other order, and they differ.
    Mat4 t = mat4Identity();
    t[0 * 4 + 3] = fxFrom(10);  // row 0, column 3 is the x translation
    Mat4 s = mat4Identity();
    s[0] = fxFrom(2);
    const Mat4 scaleThenTranslate = mat4Multiply(t, s);
    const Mat4 translateThenScale = mat4Multiply(s, t);
    const Vec4 origin = {0, 0, 0, I};
    CHECK_EQ(mat4Apply(scaleThenTranslate, origin)[0], fxFrom(10));
    CHECK_EQ(mat4Apply(translateThenScale, origin)[0], fxFrom(20));
  }

  TEST_CASE("stores row major, read at a computed index") {
    // The x translation of a translation matrix belongs at row 0, column 3,
    // which is index 3. Column major would put it at index 12.
    Mat4 m = mat4Identity();
    m[0 * 4 + 3] = fxFrom(7);
    CHECK_EQ(at(m, 0, 3), fxFrom(7));
    CHECK_EQ(m[3], fxFrom(7));
    CHECK_EQ(m[12], 0);
  }
}

TEST_SUITE("the model matrix") {
  TEST_CASE("composes scale, then rotation, then translation") {
    // With a quarter turn about Y, a point on +X goes to -Z. Scaling first
    // then translating puts it at the translation plus the scaled point.
    // The rotation is yaw, pitch, roll, so element 0 turns about Y.
    const Mat4 m = mat4Model({fxFrom(100), 0, 0}, {16384, 0, 0}, 2 * FX_ONE);
    const Vec4 p = mat4Apply(m, {fxFrom(1), 0, 0, I});
    // A quarter turn about Y takes +X to -Z, and the scale doubles it.
    CHECK_EQ(p[0], fxFrom(100));
    CHECK_EQ(p[1], 0);
    CHECK_EQ(std::lround(whole(p[2])), -2);
  }

  TEST_CASE("keeps the upper three by three a rotation scaled uniformly") {
    // A uniform scale commutes with a rotation, so scale-then-rotate and
    // rotate-then-scale give the same matrix and no test can separate them.
    // What IS checkable is that every row of the upper 3 by 3 has length
    // equal to the scale, which a wrong rotation or a lost scale breaks.
    const Mat4 m = mat4Model({0, 0, 0}, {8000, 3000, 0}, 3 * FX_ONE);
    for (size_t r = 0; r < 3; r++) {
      const double len =
          std::hypot(static_cast<double>(at(m, r, 0)), static_cast<double>(at(m, r, 1)), static_cast<double>(at(m, r, 2))) /
          static_cast<double>(FX_ONE);
      CHECK_EQ(len, doctest::Approx(3).epsilon(0.005));
    }
  }
}

TEST_SUITE("the view matrix") {
  TEST_CASE("undoes the camera's own transform") {
    // A point at the camera's position maps to the origin in view space.
    const Vec3 eye = {fxFrom(5), fxFrom(-2), fxFrom(9)};
    const Mat4 v = mat4View(eye, {0, 0, 0});
    const Vec4 p = mat4Apply(v, {eye[0], eye[1], eye[2], I});
    CHECK_EQ(p[0], 0);
    CHECK_EQ(p[1], 0);
    CHECK_EQ(p[2], 0);
  }

  TEST_CASE("puts a point in front of the camera at a negative z") {
    // The camera looks down -Z, so something ahead of it has negative view
    // space z. Getting this backwards turns the world inside out.
    const Mat4 v = mat4View({0, 0, 0}, {0, 0, 0});
    const Vec4 ahead = mat4Apply(v, {0, 0, fxFrom(-10), I});
    CHECK_LT(ahead[2], 0);
  }

  TEST_CASE("turns the world the opposite way to the camera") {
    // A yaw of a quarter turn swings the camera's forward from -Z to -X.
    // A point at -X is then straight ahead, so its view space z is -10, and
    // a point at +X is behind it at +10.
    const Mat4 v = mat4View({0, 0, 0}, {16384, 0, 0});
    const Vec4 ahead = mat4Apply(v, {fxFrom(-10), 0, 0, I});
    CHECK_EQ(std::lround(whole(ahead[2])), -10);
    const Vec4 behind = mat4Apply(v, {fxFrom(10), 0, 0, I});
    CHECK_EQ(std::lround(whole(behind[2])), 10);
  }
}

TEST_SUITE("the projection matrix") {
  const Fx near = fxFrom(1);
  const Fx far = fxFrom(100);
  const Fx focal = FX_ONE;  // a 90 degree field of view

  struct Projected {
    double ndcZ;
    Fx w;
  };
  auto project = [](const Mat4& m, Fx z) -> Projected {
    const Vec4 p = mat4Apply(m, {0, 0, z, I});
    return {p[3] == 0 ? 0 : static_cast<double>(p[2]) / static_cast<double>(p[3]), p[3]};
  };

  TEST_CASE("gives w equal to minus the view space z") {
    // The whole point. Hardware divides by w, and w must be the depth.
    const Mat4 m = mat4Perspective(focal, near, far, DEPTH_GL);
    CHECK_EQ(project(m, fxFrom(-7)).w, fxFrom(7));
  }

  TEST_CASE("projects near to -1 and far to 1 under GL") {
    const Mat4 m = mat4Perspective(focal, near, far, DEPTH_GL);
    CHECK_EQ(project(m, -near).ndcZ, doctest::Approx(-1).epsilon(0.001));
    CHECK_EQ(project(m, -far).ndcZ, doctest::Approx(1).epsilon(0.001));
  }

  TEST_CASE("projects near to 0 and far to 1 under ZERO_TO_ONE") {
    // WebGPU's range. Only the third row differs, and this is what keeps
    // that swap to one line.
    const Mat4 m = mat4Perspective(focal, near, far, DEPTH_ZERO_TO_ONE);
    CHECK_LT(std::fabs(project(m, -near).ndcZ), 0.001);
    CHECK_EQ(project(m, -far).ndcZ, doctest::Approx(1).epsilon(0.001));
  }

  TEST_CASE("leaves x and y identical under both conventions") {
    // The property the renderer leans on: the two ranges differ in z alone,
    // so a picture that never reads a projected z is the same under either.
    const Mat4 gl = mat4Perspective(focal, near, far, DEPTH_GL);
    const Mat4 wg = mat4Perspective(focal, near, far, DEPTH_ZERO_TO_ONE);
    const Vec4 p = {fxFrom(3), fxFrom(-4), fxFrom(-20), I};
    const Vec4 a = mat4Apply(gl, p);
    const Vec4 b = mat4Apply(wg, p);
    CHECK_EQ(a[0], b[0]);
    CHECK_EQ(a[1], b[1]);
    CHECK_EQ(a[3], b[3]);
  }
}
