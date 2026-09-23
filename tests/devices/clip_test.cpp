#include <doctest.h>

#include <cstdlib>

#include "devices/clip.h"
#include "devices/fixed.h"
#include "devices/mat4.h"

using namespace sc8;

// Clipping happens in homogeneous coordinates, BEFORE the perspective
// divide. Dividing by a negative w turns a vertex behind the camera into a
// plausible coordinate in front of it, which is the classic 3D bug and the
// reason this file exists. See docs/design/gpu-world-design.md.

namespace {

ClipVertex V(int x, int y, int z, int w, Fx shade = 0) { return {fxFrom(x), fxFrom(y), fxFrom(z), fxFrom(w), shade}; }

}  // namespace

TEST_SUITE("clipping against the near plane") {
  TEST_CASE("keeps an edge fully in front") {
    // Under GL the near plane is z >= -w, so both of these are inside.
    const auto e = clipNear(V(0, 0, -1, 2), V(1, 1, 0, 3), DEPTH_GL);
    REQUIRE(e.has_value());
    CHECK_EQ(e->a.w, fxFrom(2));
    CHECK_EQ(e->b.w, fxFrom(3));
  }

  TEST_CASE("drops an edge fully behind") {
    // Both have z below -w, so nothing survives and nothing is drawn.
    CHECK_FALSE(clipNear(V(0, 0, -5, 1), V(0, 0, -6, 2), DEPTH_GL).has_value());
  }

  TEST_CASE("splits an edge crossing the near plane, in clip space") {
    const auto e = clipNear(V(0, 0, 2, 2), V(0, 0, -4, 1), DEPTH_GL);
    REQUIRE(e.has_value());
    // The surviving end is untouched and the cut end sits ON the plane.
    CHECK_EQ(e->a.w, fxFrom(2));
    CHECK_LT(std::abs(e->b.z + e->b.w), 100);
  }

  TEST_CASE("never produces a negative w after clipping") {
    // The property that matters. A build clipping after the divide passes
    // every test above this one and fails here on the first vertex behind
    // the eye, because dividing by a negative w flips it to the front.
    for (int z = -8; z <= 8; z++) {
      for (int w = -4; w <= 4; w++) {
        if (w == 0) continue;
        const auto e = clipNear(V(0, 0, z, w), V(0, 0, 1, 2), DEPTH_GL);
        if (!e) continue;
        CHECK_GT(e->a.w, 0);
        CHECK_GT(e->b.w, 0);
      }
    }
  }

  TEST_CASE("carries a varying to the split point") {
    // The depth shade is a varying. Losing it at the cut shades a clipped
    // edge wrongly from there onward.
    const auto e = clipNear(V(0, 0, 2, 2, 0), V(0, 0, -4, 1, 120), DEPTH_GL);
    REQUIRE(e.has_value());
    CHECK_GT(e->b.shade, 0);
    CHECK_LT(e->b.shade, 120);
  }

  TEST_CASE("keeps a vertex exactly on the plane") {
    // The boundary is inside, not outside. An edge lying on it must survive.
    CHECK(clipNear(V(0, 0, -2, 2), V(0, 0, 0, 2), DEPTH_GL).has_value());
  }

  TEST_CASE("uses z >= 0 under the WebGPU range instead of z >= -w") {
    // The one place the two conventions differ in this file.
    const auto gl = clipNear(V(0, 0, -1, 2), V(0, 0, 1, 2), DEPTH_GL);
    const auto wg = clipNear(V(0, 0, -1, 2), V(0, 0, 1, 2), DEPTH_ZERO_TO_ONE);
    REQUIRE(gl.has_value());
    REQUIRE(wg.has_value());
    // Under GL the first vertex is inside and untouched. Under ZERO_TO_ONE
    // it is behind the plane and gets cut.
    CHECK_EQ(gl->a.z, fxFrom(-1));
    CHECK_GE(wg->a.z, 0);
  }
}

TEST_SUITE("the divide and the viewport") {
  TEST_CASE("divides x and y by w") {
    const NdcVertex n = toNdc({fxFrom(6), fxFrom(-3), 0, fxFrom(2), 0});
    CHECK_EQ(n.x, fxFrom(3));
    CHECK_EQ(n.y, -3 * FX_ONE / 2);
  }

  TEST_CASE("maps -1 and 1 to the screen edges, and 0 to the centre") {
    // 256 wide, so -1 is column 0, 1 is column 255 and 0 is the middle.
    CHECK_EQ(toViewport(-FX_ONE, 256), 0);
    CHECK_EQ(toViewport(FX_ONE, 256), 255);
    CHECK_EQ(toViewport(0, 256), 128);
  }

  TEST_CASE("keeps the viewport out of the projection, so it is its own step") {
    // Folding the viewport into the projection matrix gives a matrix no GPU
    // can use. This checks the two are separable by scaling only here.
    CHECK_EQ(toViewport(FX_ONE / 2, 256), toViewport(FX_ONE / 2, 256));
    CHECK_EQ(toViewport(0, 64), 32);
  }
}

TEST_SUITE("interpolating a varying") {
  TEST_CASE("is perspective correct, so the screen midpoint is not the average") {
    // The property the whole depth cue rests on. An edge from w = 2 to
    // w = 100 recedes hard, so its screen midpoint sits far nearer the close
    // end in world terms. A linear blend would read 7.5 there.
    const double mid = perspectiveVarying(0, 2, 15, 100, 0.5);
    CHECK_LT(mid, 1);
    CHECK_GE(mid, 0);
  }

  TEST_CASE("still hits both ends exactly") {
    CHECK_EQ(perspectiveVarying(3, 2, 11, 100, 0), doctest::Approx(3).epsilon(1e-9));
    CHECK_EQ(perspectiveVarying(3, 2, 11, 100, 1), doctest::Approx(11).epsilon(1e-9));
  }

  TEST_CASE("is the plain average when both ends share a w") {
    // With no perspective to correct for, the two agree. This is what makes
    // the test above about perspective rather than about arithmetic.
    CHECK_EQ(perspectiveVarying(0, 5, 10, 5, 0.5), doctest::Approx(5).epsilon(1e-9));
  }
}
