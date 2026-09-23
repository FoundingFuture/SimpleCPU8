// The GPU's drawing primitives and the 2D and 3D path projection.
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "devices/gpu.h"

namespace sc8 {

using namespace sc8::gpu;

namespace {

// The GPU's sine table: 256 angle steps per turn, values -127..127. A real
// GPU would burn this into ROM. Built once at load from the host's sin,
// as the browser built it from V8's.
const std::array<int, 256>& sinTable() {
  static const std::array<int, 256> table = [] {
    std::array<int, 256> t{};
    for (int a = 0; a < 256; a++) {
      t[static_cast<size_t>(a)] = static_cast<int>(jsRound(127 * std::sin((2 * std::numbers::pi * a) / 256)));
    }
    return t;
  }();
  return table;
}

int cosOf(int a) { return sinTable()[static_cast<size_t>((a + 64) & 255)]; }
int sinOf(int a) { return sinTable()[static_cast<size_t>(a & 255)]; }

// JavaScript's >> on a 32 bit value floors toward negative infinity; C's
// division truncates. The arithmetic shift on a signed int is what the
// TypeScript did, so it is spelled as a shift here too.
int shr7(int v) { return v >> 7; }

}  // namespace

void Gpu::plotAt(int px, int py, uint8_t c) {
  if (px < 0 || px >= SCREEN_W || py < 0 || py >= SCREEN_H) return;  // clip
  vram[static_cast<size_t>(py * SCREEN_W + px)] = c;
}

void Gpu::ringPoints(int dx, int dy) {
  const uint8_t c = static_cast<uint8_t>(color);
  const int x = penX;
  const int y = penY;
  plotAt(x + dx, y + dy, c);
  plotAt(x - dx, y + dy, c);
  plotAt(x + dx, y - dy, c);
  plotAt(x - dx, y - dy, c);
  plotAt(x + dy, y + dx, c);
  plotAt(x - dy, y + dx, c);
  plotAt(x + dy, y - dx, c);
  plotAt(x - dy, y - dx, c);
}

void Gpu::line(int x0, int y0, int x1, int y1, uint8_t c) {
  // Bresenham over integers: deterministic everywhere.
  const int dx = std::abs(x1 - x0);
  const int dy = -std::abs(y1 - y0);
  const int sx = x0 < x1 ? 1 : -1;
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  int px = x0;
  int py = y0;
  for (;;) {
    plotAt(px, py, c);
    if (px == x1 && py == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      px += sx;
    }
    if (e2 <= dx) {
      err += dx;
      py += sy;
    }
  }
}

// A polyline drawn at the pen, like every other drawing command. The blob
// leads with its point count. Each point is two cartridge bytes, or three
// for the 3D form, normalized around 128 so rotation spins the shape about
// itself.
void Gpu::drawPath(uint32_t base, bool threeD) {
  const int n = cartByte(base);
  const uint32_t stride = threeD ? 3 : 2;
  bool havePrev = false;
  std::array<int, 2> prev{};
  for (int i = 0; i < n; i++) {
    const uint32_t o = base + 1 + static_cast<uint32_t>(i) * stride;
    const std::array<int, 2> p =
        threeD ? project3(cartByte(o), cartByte(o + 1), cartByte(o + 2)) : project2(cartByte(o), cartByte(o + 1));
    if (havePrev) line(prev[0], prev[1], p[0], p[1], static_cast<uint8_t>(color));
    else if (n == 1) plotAt(p[0], p[1], static_cast<uint8_t>(color));
    prev = p;
    havePrev = true;
  }
  version++;
}

std::array<int, 2> Gpu::project2(int bx, int by) const {
  const int x = bx - 128;
  const int y = by - 128;
  const int cz = cosOf(rotZA);
  const int sz = sinOf(rotZA);
  const int x1 = shr7(x * cz - y * sz);
  const int y1 = shr7(x * sz + y * cz);
  const int s = pathScale;
  // Math.trunc on a divide: C's integer division truncates the same way.
  return {penX + (x1 * s) / 256, penY + (y1 * s) / 256};
}

std::array<int, 2> Gpu::project3(int bx, int by, int bz) const {
  int x = bx - 128;
  int y = by - 128;
  int z = bz - 128;
  const int cz = cosOf(rotZA);
  const int sz = sinOf(rotZA);
  const int cy = cosOf(rotYA);
  const int sy = sinOf(rotYA);
  const int cx = cosOf(rotXA);
  const int sx = sinOf(rotXA);
  // Rotate around z, then y, then x.
  const int x1 = shr7(x * cz - y * sz);
  const int y1 = shr7(x * sz + y * cz);
  x = x1;
  y = y1;
  const int x2 = shr7(x * cy + z * sy);
  const int z1 = shr7(-x * sy + z * cy);
  x = x2;
  z = z1;
  const int y2 = shr7(y * cx - z * sx);
  const int z2 = shr7(y * sx + z * cx);
  y = y2;
  z = z2;
  // Perspective: the eye sits at z = -512 in centered units. Closer
  // vertices (negative z) grow. The denominator clamp keeps runaway
  // points finite instead of crashing anything.
  const int denom = std::max(512 + z, 128);
  const int p = (512 * 256) / denom;
  const int s = pathScale;
  return {penX + (x * s * p) / 65536, penY + (y * s * p) / 65536};
}

}  // namespace sc8
