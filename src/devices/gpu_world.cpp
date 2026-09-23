// MODE_WORLD: the GPU holds meshes in its own memory and draws them from a
// camera every composed frame. The scene lives in the CPU's data RAM, so
// changing the world is a memory write. See docs/design/gpu-world-design.md.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "devices/clip.h"
#include "devices/gpu.h"

namespace sc8 {

using namespace sc8::gpu;

int shadeFor(int64_t z, int64_t near, int64_t far) {
  const int64_t span = far - near;
  if (span <= 0) return 0;
  const double t = static_cast<double>(z - near) / static_cast<double>(span);
  const double shade = std::floor(t * 16);
  return static_cast<int>(std::max(0.0, std::min(15.0, shade)));
}

void Gpu::defineMesh(int id, uint32_t firstVertex, int vertexCount, uint32_t firstEdge, int edgeCount) {
  MeshInfo& m = meshTable_[static_cast<size_t>(id)];
  m.firstVertex = firstVertex;
  m.vertexCount = vertexCount;
  m.firstEdge = firstEdge;
  m.edgeCount = edgeCount;
  // The face range and the scale exponent are reserved for surfaces. They
  // are left at zero here, and CMD_MESH_LOAD has no way to set them yet.
  m.firstFace = 0;
  m.faceCount = 0;
  m.scaleExp = 0;
}

MeshInfo Gpu::meshInfo(int id) const {
  if (id < 0 || id >= MESH_COUNT) return MeshInfo{};
  return meshTable_[static_cast<size_t>(id)];
}

// The blob leads with its vertex and edge counts, both 16 bit. A mesh that
// grows then needs no change in the source that loads it.
void Gpu::meshLoad(int id, uint32_t src) {
  const int vCount = (cartByte(src) << 8) | cartByte(src + 1);
  const int eCount = (cartByte(src + 2) << 8) | cartByte(src + 3);
  const uint32_t bytes = static_cast<uint32_t>(vCount * VERTEX_BYTES + eCount * EDGE_BYTES);
  if (worldTop_ + bytes > static_cast<uint32_t>(WORLD_RAM_SIZE)) return;
  for (uint32_t i = 0; i < bytes; i++) worldRam[worldTop_ + i] = cartByte(src + 4 + i);
  defineMesh(id, worldTop_, vCount, worldTop_ + static_cast<uint32_t>(vCount * VERTEX_BYTES), eCount);
  worldTop_ += bytes;
  version++;
}

// Sixteen shades of one colour. The high nibble is the ramp and the low
// nibble is the shade, so this fills entries ramp*16 to +15.
//
// DESIGN: the fade runs toward palette entry 0, the background. Fading
// always to black is right only on a black background. On a light sky it
// makes distant geometry stand OUT as it recedes, which is backwards, and
// it is what real fog gets right by blending toward the background.
void Gpu::worldRamp(int ramp, int r0, int g0, int b0) {
  const int rb = palette[0];
  const int gb = palette[1];
  const int bb = palette[2];
  const size_t base = static_cast<size_t>(ramp) * 16 * 3;
  palette[base] = static_cast<uint8_t>(r0);
  palette[base + 1] = static_cast<uint8_t>(g0);
  palette[base + 2] = static_cast<uint8_t>(b0);
  for (int s2 = 1; s2 < 16; s2++) {
    const size_t i = static_cast<size_t>(ramp * 16 + s2) * 3;
    // Shade 15 keeps a sixteenth of the base, so the farthest geometry
    // stays distinct rather than merging into the background. The mix
    // rounds half up, as Math.round did, and every term is positive.
    palette[i] = static_cast<uint8_t>((r0 * (16 - s2) + rb * s2 + 8) / 16);
    palette[i + 1] = static_cast<uint8_t>((g0 * (16 - s2) + gb * s2 + 8) / 16);
    palette[i + 2] = static_cast<uint8_t>((b0 * (16 - s2) + bb * s2 + 8) / 16);
  }
  version++;
}

int Gpu::sceneI16(int at) const {
  const int v = sceneU16(at);
  return v >= 0x8000 ? v - 0x10000 : v;
}

// One matrix out of the mapped table, as 16.16 row major, or nothing when
// the id names no matrix. Reading past the count is silent and draws
// nothing, the way an object naming an undefined mesh is silent.
std::optional<Mat4> Gpu::matrixFor(int id) const {
  if (!ram_ || id >= matrixCount_) return std::nullopt;
  Mat4 out{};
  for (size_t i = 0; i < 16; i++) {
    // Big-endian float64, assembled a byte at a time because the table
    // wraps at 16 bits like every other data RAM read in this file.
    const uint32_t at = static_cast<uint32_t>(matrixBase_ + id * MATRIX_BYTES) + static_cast<uint32_t>(i) * 8;
    uint64_t bits = 0;
    for (uint32_t b = 0; b < 8; b++) bits = (bits << 8) | ramByte(at + b);
    double v;
    std::memcpy(&v, &bits, sizeof v);
    // A NaN or an infinity in the table drew nothing in the browser, since
    // every comparison on it failed. Converting one to int64 is undefined
    // here, so it becomes zero, which also draws nothing.
    const double scaled = v * static_cast<double>(FX_ONE);
    out[i] = std::isfinite(scaled) ? static_cast<Fx>(jsRound(scaled)) : 0;
  }
  return out;
}

// An object's origin in world space, for the painter's sort. In matrix mode
// the position bytes are the matrix id, so the origin is the matrix's
// translation column instead.
std::optional<Vec3> Gpu::objectOrigin(int rec) const {
  if ((ramByte(static_cast<uint32_t>(rec)) & OBJ_FLAG_MATRIX) == 0) {
    return Vec3{fxFrom(sceneI16(rec + 4)), fxFrom(sceneI16(rec + 6)), fxFrom(sceneI16(rec + 8))};
  }
  const auto m = matrixFor(sceneU16(rec + 4));
  if (!m) return std::nullopt;
  return Vec3{(*m)[3], (*m)[7], (*m)[11]};
}

// Stage 2 for one object, then stages 3 to 8 for each of its edges.
void Gpu::drawObject(std::span<uint8_t> out, int rec, const Mat4& view, const Mat4& proj) const {
  const int flags = ramByte(static_cast<uint32_t>(rec));
  if ((flags & OBJ_FLAG_ACTIVE) == 0) return;  // inactive
  const MeshInfo mesh = meshInfo(ramByte(static_cast<uint32_t>(rec + 1)));
  // An object naming a mesh that was never defined draws nothing. That is
  // not an error, and it is silent on purpose.
  if (mesh.vertexCount == 0 || mesh.edgeCount == 0) return;
  const int ramp = ramByte(static_cast<uint32_t>(rec + 2)) & 0x0f;

  // The mesh's unit choice. Nothing sets the exponent today, so it is zero
  // and the unit is FX_ONE. A negative exponent would need a fraction,
  // which the TypeScript held in a double; a shift covers what is reachable.
  const Fx unit = mesh.scaleExp >= 0 ? (FX_ONE << mesh.scaleExp) : (FX_ONE >> -mesh.scaleExp);

  // DESIGN: flag bit 1 swaps where the transform comes from. Clear, and
  // bytes 3 to 15 are the scale, the position and the three angles, as
  // they always were. Set, and bytes 4 and 5 are a matrix id and the rest
  // say nothing, because a matrix already carries scale, position and
  // rotation. Two sources for one thing would be two sources of truth.
  Mat4 model;
  if ((flags & OBJ_FLAG_MATRIX) != 0) {
    const auto m = matrixFor(sceneU16(rec + 4));
    // An object naming a matrix that is not mapped draws nothing, exactly
    // as one naming an undefined mesh does. composeWorld drops the same
    // objects before it sorts, and this stays so drawObject is correct on
    // its own rather than because of what its one caller does.
    if (!m) return;
    model = *m;
    // The exponent is the MESH's unit choice, not the object's, so it
    // still applies. A matrix replaces the object's transform, never the
    // units its geometry is written in.
    if (mesh.scaleExp != 0) {
      Mat4 u = mat4Identity();
      u[0] = unit;
      u[5] = unit;
      u[10] = unit;
      model = mat4Multiply(model, u);
    }
  } else {
    const int scaleByte = ramByte(static_cast<uint32_t>(rec + 3));
    // 64 means 1.0, and the mesh's exponent shifts the whole thing.
    const Fx scale = fxMul(fxFrom(scaleByte) / 64, unit);
    model = mat4Model({fxFrom(sceneI16(rec + 4)), fxFrom(sceneI16(rec + 6)), fxFrom(sceneI16(rec + 8))},
                      {sceneU16(rec + 10), sceneU16(rec + 12), sceneU16(rec + 14)}, scale);
  }
  const Mat4 mv = mat4Multiply(view, model);

  const Fx near = fxFrom(sceneU16(sceneBase_ + 12));
  const Fx far = fxFrom(sceneU16(sceneBase_ + 14));

  // Stage 1, vertex fetch, and stage 2, the vertex stage.
  std::vector<ClipVertex> clip;
  clip.reserve(static_cast<size_t>(mesh.vertexCount));
  auto worldI16 = [this](uint32_t o) -> int {
    const int v = (worldRam[o] << 8) | worldRam[o + 1];
    return v >= 0x8000 ? v - 0x10000 : v;
  };
  for (int i = 0; i < mesh.vertexCount; i++) {
    const uint32_t o = mesh.firstVertex + static_cast<uint32_t>(i * VERTEX_BYTES);
    const Vec4 p = {fxFrom(worldI16(o)), fxFrom(worldI16(o + 2)), fxFrom(worldI16(o + 4)), FX_ONE};
    // The depth cue reads VIEW space z, before the projection. That is what
    // makes the picture the same under either depth convention.
    const Vec4 viewPos = mat4Apply(mv, p);
    const Vec4 c = mat4Apply(proj, viewPos);
    clip.push_back({c[0], c[1], c[2], c[3], shadeFor(-viewPos[2], near, far)});
  }

  // Stage 3, primitive assembly, then 4 to 8 per edge.
  for (int e = 0; e < mesh.edgeCount; e++) {
    const uint32_t o = mesh.firstEdge + static_cast<uint32_t>(e * EDGE_BYTES);
    const size_t ia = static_cast<size_t>((worldRam[o] << 8) | worldRam[o + 1]);
    const size_t ib = static_cast<size_t>((worldRam[o + 2] << 8) | worldRam[o + 3]);
    if (ia >= clip.size() || ib >= clip.size()) continue;
    const auto edge = clipNear(clip[ia], clip[ib], depthRange_);
    if (!edge) continue;
    const NdcVertex na = toNdc(edge->a);
    const NdcVertex nb = toNdc(edge->b);
    worldLine(out, toViewport(na.x, SCREEN_W), toViewport(-na.y, SCREEN_H), na.shade, na.w, toViewport(nb.x, SCREEN_W),
              toViewport(-nb.y, SCREEN_H), nb.shade, nb.w, ramp);
  }
}

// Stage 7 and 8: a one pixel line with the shade interpolated the way
// hardware interpolates a varying. Screen space linear interpolation is
// wrong on any edge running away from the viewer, so this interpolates
// shade over w against one over w, then divides back. The interpolation
// runs in doubles, as it did in the browser.
void Gpu::worldLine(std::span<uint8_t> out, int x0, int y0, Fx s0, Fx w0, int x1, int y1, Fx s1, Fx w1,
                    int ramp) const {
  const int dx = std::abs(x1 - x0);
  const int dy = -std::abs(y1 - y0);
  const int sx = x0 < x1 ? 1 : -1;
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  int x = x0;
  int y = y0;
  const int stepCount = std::max(dx, -dy);
  const double steps = stepCount == 0 ? 1 : stepCount;
  const double invA = w0 == 0 ? 0 : static_cast<double>(FX_ONE) / static_cast<double>(w0);
  const double invB = w1 == 0 ? 0 : static_cast<double>(FX_ONE) / static_cast<double>(w1);
  const double sa = static_cast<double>(s0);
  const double sb = static_cast<double>(s1);
  int n = 0;
  for (;;) {
    const double t = n / steps;
    const double inv = invA + (invB - invA) * t;
    const double soverw = sa * invA + (sb * invB - sa * invA) * t;
    const double shade = inv == 0 ? sa : jsRound(soverw / inv);
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) {
      const int s = static_cast<int>(std::max(0.0, std::min(15.0, shade)));
      out[static_cast<size_t>(y * SCREEN_W + x)] = static_cast<uint8_t>((ramp << 4) | s);
    }
    if (x == x1 && y == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y += sy;
    }
    n++;
    if (n > stepCount + 2) break;  // a guard, so a degenerate edge cannot spin
  }
}

// The whole world, back to front so nearer wireframes overwrite farther
// ones. There is no depth buffer, and for wireframe a painter's order is
// enough.
void Gpu::composeWorld(std::span<uint8_t> out) const {
  if (!sceneMapped_ || !ram_) {
    fillNoise(out);
    return;
  }
  const int b = sceneBase_;
  const Vec3 eye = {fxFrom(sceneI16(b + 0)), fxFrom(sceneI16(b + 2)), fxFrom(sceneI16(b + 4))};
  const Vec3 rot = {sceneU16(b + 6), sceneU16(b + 8), sceneU16(b + 10)};
  const Fx nearRaw = fxFrom(sceneU16(b + 12));
  const Fx near = nearRaw != 0 ? nearRaw : fxFrom(1);
  const Fx farRaw = fxFrom(sceneU16(b + 14));
  const Fx far = farRaw != 0 ? farRaw : fxFrom(1000);
  const Fx focalRaw = fxFrom(sceneU16(b + 16)) / 256;
  const Fx focal = focalRaw != 0 ? focalRaw : FX_ONE;
  // The camera may hand over either matrix, or both.
  //
  // A missing matrix falls back to the record, which is DIFFERENT from an
  // object, where it draws nothing. An object in matrix mode has no
  // position to fall back to: its position bytes ARE the id. The camera's
  // position, angles and focal length sit elsewhere in the record and stay
  // valid, so there is a well defined thing to do.
  const int camFlags = ramByte(static_cast<uint32_t>(b + 25));
  const auto viaView = (camFlags & CAM_FLAG_VIEW_MATRIX) != 0 ? matrixFor(sceneU16(b + 26)) : std::nullopt;
  const auto viaProj = (camFlags & CAM_FLAG_PROJ_MATRIX) != 0 ? matrixFor(sceneU16(b + 28)) : std::nullopt;
  const Mat4 view = viaView ? *viaView : mat4View(eye, rot);
  // near and far are NOT taken from a supplied projection. The depth cue
  // reads view space z against them directly, never the projected z, which
  // is what keeps the picture the same under either depth convention. So
  // only the focal length goes quiet here.
  const Mat4 proj = viaProj ? *viaProj : mat4Perspective(focal, near, far, depthRange_);

  // Order by the depth of each object's origin, farthest first.
  struct Entry {
    int rec;
    Fx depth;
  };
  std::vector<Entry> order;
  for (int id = 0; id < sceneObjects_; id++) {
    const int rec = b + CAMERA_BYTES + id * OBJ_STRIDE;
    if ((ramByte(static_cast<uint32_t>(rec)) & OBJ_FLAG_ACTIVE) == 0) continue;
    const auto o = objectOrigin(rec);
    if (!o) continue;  // a matrix id that names nothing
    const Vec4 p = mat4Apply(view, {(*o)[0], (*o)[1], (*o)[2], FX_ONE});
    order.push_back({rec, -p[2]});
  }
  // A stable sort, as JavaScript's is, so equal depths keep id order.
  std::stable_sort(order.begin(), order.end(), [](const Entry& p, const Entry& q) { return p.depth > q.depth; });
  for (const Entry& o : order) drawObject(out, o.rec, view, proj);
}

// The FNV-1a hash of the scene bytes and of the matrices in use, for the
// display key.
uint32_t Gpu::worldHash() const {
  uint32_t h = 2166136261u;
  auto fold = [&](uint32_t a) {
    h ^= ramByte(a);
    h *= 16777619u;
  };
  const int end = sceneBase_ + CAMERA_BYTES + sceneObjects_ * OBJ_STRIDE;
  for (int a = sceneBase_; a < end; a++) fold(static_cast<uint32_t>(a));
  // A matrix is scene data too, and the GPU never sees it written either.
  // Only the matrices actually named are folded in: a table of three
  // hundred would otherwise be rehashed on every frame to notice a change
  // to the one in use.
  auto foldMatrix = [&](int mid) {
    if (mid >= matrixCount_) return;
    const int at = matrixBase_ + mid * MATRIX_BYTES;
    for (int a = at; a < at + MATRIX_BYTES; a++) fold(static_cast<uint32_t>(a));
  };
  // The camera's own two, then one per object that names one.
  const int camFlags = ramByte(static_cast<uint32_t>(sceneBase_ + 25));
  if ((camFlags & CAM_FLAG_VIEW_MATRIX) != 0) foldMatrix(sceneU16(sceneBase_ + 26));
  if ((camFlags & CAM_FLAG_PROJ_MATRIX) != 0) foldMatrix(sceneU16(sceneBase_ + 28));
  for (int id = 0; id < sceneObjects_; id++) {
    const int rec = sceneBase_ + CAMERA_BYTES + id * OBJ_STRIDE;
    const int flags = ramByte(static_cast<uint32_t>(rec));
    if ((flags & OBJ_FLAG_ACTIVE) == 0 || (flags & OBJ_FLAG_MATRIX) == 0) continue;
    foldMatrix(sceneU16(rec + 4));
  }
  return h;
}

}  // namespace sc8
