#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <numbers>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "devices/acp.h"
#include "devices/fixed.h"
#include "devices/gpu.h"
#include "gpu_rig.h"

using namespace sc8;
using namespace sc8::gpu;
using gpu_rig::litPixels;

// MODE_WORLD: the GPU holds meshes in its own memory and draws them from a
// camera every composed frame. The scene lives in the CPU's data RAM, so
// changing the world is a memory write. See docs/design/gpu-world-design.md.

namespace {

constexpr int SCENE = 0x2000;
constexpr int MATS = 0x4000;

using Frame = Gpu::Frame;
using Matrix = std::array<double, 16>;

// A pyramid on a square base: five vertices and eight edges. The base
// corners, then the apex.
const std::vector<std::array<int, 3>> PYRAMID_V = {
    {-100, 0, -100}, {100, 0, -100}, {100, 0, 100}, {-100, 0, 100}, {0, 200, 0},
};
const std::vector<std::array<int, 2>> PYRAMID_E = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 4}, {1, 4}, {2, 4}, {3, 4}};

void put16(std::vector<uint8_t>& b, size_t at, int v) {
  b[at] = static_cast<uint8_t>((v >> 8) & 0xff);
  b[at + 1] = static_cast<uint8_t>(v & 0xff);
}

// A mesh blob leads with its vertex and edge counts, both 16 bit. The command
// that loads it takes only a pointer, so a mesh that grows needs no change in
// the source that loads it.
std::vector<uint8_t> meshBytes() {
  std::vector<uint8_t> b(4 + PYRAMID_V.size() * 6 + PYRAMID_E.size() * 4);
  put16(b, 0, static_cast<int>(PYRAMID_V.size()));
  put16(b, 2, static_cast<int>(PYRAMID_E.size()));
  size_t o = 4;
  for (const auto& v : PYRAMID_V) {
    put16(b, o, v[0]);
    put16(b, o + 2, v[1]);
    put16(b, o + 4, v[2]);
    o += 6;
  }
  for (const auto& e : PYRAMID_E) {
    put16(b, o, e[0]);
    put16(b, o + 2, e[1]);
    o += 4;
  }
  return b;
}

struct Rig {
  gpu_rig::Clock clock;
  std::unique_ptr<gpu_rig::Bytes> ram = gpu_rig::newRam();
  std::vector<uint8_t> cart = std::vector<uint8_t>(4096, 0);
  Gpu& g = clock.g;
  Rig() {
    g.powerOn();
    g.attachRam(ram->data());
    const auto mesh = meshBytes();
    std::copy(mesh.begin(), mesh.end(), cart.begin());
    g.attachCart(cart);
  }
  // Load the data ports, then run the command. A data port means whatever
  // the running command says it means, so the test says it the same way.
  void cmd(int c, std::initializer_list<int> args = {}) { gpu_rig::cmd(g, c, args); }
  void put16(int at, int v) { ::put16(*ram, static_cast<size_t>(at), v & 0xffff); }
  uint8_t& byte(int at) { return (*ram)[static_cast<size_t>(at)]; }
  int obj(int id) const { return SCENE + CAMERA_BYTES + id * OBJ_STRIDE; }
  Frame frame() const { return g.composeFrame(); }

  // Write a row-major 4x4 of float64 into matrix slot `id` of a table.
  void putMatAt(int base, int id, const Matrix& m) {
    for (size_t i = 0; i < 16; i++) {
      uint64_t bits;
      std::memcpy(&bits, &m[i], sizeof bits);
      const size_t at = static_cast<size_t>(base + id * MATRIX_BYTES) + i * 8;
      for (size_t b = 0; b < 8; b++) (*ram)[at + b] = static_cast<uint8_t>(bits >> (56 - 8 * b));
    }
  }
  void putMat(int id, const Matrix& m) { putMatAt(MATS, id, m); }
  void mapMats(int count) { cmd(CMD_MATRIX_MAP, {MATS >> 8, MATS & 0xff, count >> 8, count & 0xff}); }
  // Point object 0 at matrix `id` by setting the flag and the two id bytes.
  void useMatrix(int id) {
    byte(obj(0)) = OBJ_FLAG_ACTIVE | OBJ_FLAG_MATRIX;
    byte(obj(0) + 4) = static_cast<uint8_t>((id >> 8) & 0xff);
    byte(obj(0) + 5) = static_cast<uint8_t>(id & 0xff);
  }
  // The camera's flags byte and the two ids live in the reserved tail.
  void useCam(int flags, int viewId = 0, int projId = 0) {
    byte(SCENE + 25) = static_cast<uint8_t>(flags);
    byte(SCENE + 26) = static_cast<uint8_t>((viewId >> 8) & 0xff);
    byte(SCENE + 27) = static_cast<uint8_t>(viewId & 0xff);
    byte(SCENE + 28) = static_cast<uint8_t>((projId >> 8) & 0xff);
    byte(SCENE + 29) = static_cast<uint8_t>(projId & 0xff);
  }
};

// Load the pyramid as mesh 0, map the scene, and switch to MODE_WORLD.
void world(Rig& r, int objects = 4) {
  // Mesh 0, from a blob that carries its own counts.
  r.cmd(CMD_MESH_LOAD, {0, 0, 0, 0});

  // A camera back along +Z looking at the origin, near 1 and far 2000.
  r.put16(SCENE + 0, 0);
  r.put16(SCENE + 2, 0);
  r.put16(SCENE + 4, 600);
  r.put16(SCENE + 6, 0);
  r.put16(SCENE + 8, 0);
  r.put16(SCENE + 10, 0);
  r.put16(SCENE + 12, 1);
  r.put16(SCENE + 14, 2000);
  r.put16(SCENE + 16, 256);  // focal length, 1.0

  // One pyramid at the origin, ramp 1, scale 64 which is 1.0.
  r.byte(r.obj(0) + 0) = 1;
  r.byte(r.obj(0) + 1) = 0;
  r.byte(r.obj(0) + 2) = 1;
  r.byte(r.obj(0) + 3) = 64;

  // One command sets the mode and points it at the scene, the way the text
  // mode command points at its framebuffer.
  r.cmd(CMD_SET_WORLDMODE, {SCENE >> 8, SCENE & 0xff, objects >> 8, objects & 0xff});
}

Matrix identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
Matrix translate(double x, double y, double z) {
  Matrix m = identity();
  m[3] = x;
  m[7] = y;
  m[11] = z;
  return m;
}
Matrix rotY(double a) {
  Matrix m = identity();
  const double c = std::cos(a);
  const double s = std::sin(a);
  m[0] = c;
  m[2] = s;
  m[8] = -s;
  m[10] = c;
  return m;
}
// The view matrix for a camera at (0, 0, 600) looking down -Z, which is
// what the rig's own camera record says. The view matrix is the INVERSE of
// the camera's transform, so the translation is negated.
Matrix viewAt600() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -600, 0, 0, 0, 1}; }
// mat4Perspective with the rig's own near, far and focal, as a matrix.
Matrix perspective1to2000() {
  const double near = 1;
  const double far = 2000;
  const double span = far - near;
  return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -(far + near) / span, (-2 * near * far) / span, 0, 0, -1, 0};
}
// A 1/600 orthographic scale, so the world's 600 units span the screen.
Matrix ortho600() {
  const double s = 1.0 / 600;
  return {s, 0, 0, 0, 0, s, 0, 0, 0, 0, -2.0 / 1999, -(2001.0 / 1999), 0, 0, 0, 1};
}

size_t countRamp(const Frame& px, int ramp) {
  size_t n = 0;
  for (uint8_t v : px) {
    if (v != 0 && (v >> 4) == ramp) n++;
  }
  return n;
}

size_t differing(const Frame& a, const Frame& b) {
  size_t n = 0;
  for (size_t i = 0; i < a.size(); i++) {
    if (a[i] != b[i]) n++;
  }
  return n;
}

std::string readDoc(const char* name) {
  const std::filesystem::path doc =
      std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "docs" / "design" / name;
  std::ifstream in(doc);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

TEST_SUITE("world memory and meshes") {
  TEST_CASE("has its own 256K, which the CPU cannot address") { CHECK_EQ(WORLD_RAM_SIZE, 256 * 1024); }

  TEST_CASE("loads geometry from the cartridge and defines a mesh") {
    Rig r;
    world(r);
    const MeshInfo m = r.g.meshInfo(0);
    CHECK_EQ(m.vertexCount, 5);
    CHECK_EQ(m.edgeCount, 8);
  }

  TEST_CASE("reserves the face range, and reads it as zero") {
    // Surfaces will use it. Nothing writes it today.
    Rig r;
    world(r);
    const MeshInfo m = r.g.meshInfo(0);
    CHECK_EQ(m.firstFace, 0u);
    CHECK_EQ(m.faceCount, 0);
  }

  TEST_CASE("clears world RAM on power on") {
    Rig r;
    world(r);
    CHECK_EQ(r.g.meshInfo(0).vertexCount, 5);
    r.g.powerOn();
    CHECK_EQ(r.g.meshInfo(0).vertexCount, 0);
  }

  TEST_CASE("takes every byte as a valid mesh id, because there are exactly 256") {
    // The selector is a byte and the table has 256 entries, so no id a
    // program can write is out of range. There is nothing to refuse.
    CHECK_EQ(MESH_COUNT, 256);
    Rig r;
    world(r);
    // The blob carries the counts, and the data ports cleared after the
    // last command, so this says only the id and the address.
    r.cmd(CMD_MESH_LOAD, {255, 0, 0, 0});
    CHECK_EQ(r.g.meshInfo(255).vertexCount, 5);
  }
}

TEST_SUITE("the scene in data RAM") {
  TEST_CASE("draws a mesh the camera can see") {
    Rig r;
    world(r);
    CHECK_GT(litPixels(r.frame()), 50u);
  }

  TEST_CASE("reads object N at base plus the camera plus N times the stride") {
    CHECK_EQ(OBJ_STRIDE, 16);
    CHECK_EQ(CAMERA_BYTES, 32);
    Rig r;
    world(r);
    const size_t one = litPixels(r.frame());
    // A second pyramid, off to one side, at the next id.
    r.byte(r.obj(1) + 0) = 1;
    r.byte(r.obj(1) + 2) = 2;
    r.byte(r.obj(1) + 3) = 64;
    r.put16(r.obj(1) + 4, 300);  // x
    CHECK_GT(litPixels(r.frame()), one);
  }

  TEST_CASE("skips an inactive object") {
    Rig r;
    world(r);
    const size_t lit = litPixels(r.frame());
    r.byte(r.obj(0) + 0) = 0;  // clear the active bit
    CHECK_EQ(litPixels(r.frame()), 0u);
    CHECK_GT(lit, 0u);
  }

  TEST_CASE("skips an object naming a mesh that was never defined") {
    Rig r;
    world(r);
    r.byte(r.obj(0) + 1) = 7;  // no mesh 7
    CHECK_EQ(litPixels(r.frame()), 0u);
  }

  TEST_CASE("changes what it draws when the CPU writes a record") {
    Rig r;
    world(r);
    const Frame before = r.frame();
    r.put16(r.obj(0) + 10, 16384);  // a quarter turn of yaw
    CHECK(r.frame() != before);
  }

  TEST_CASE("folds the scene into displayKey, so a store refreshes the screen") {
    Rig r;
    world(r);
    const std::string before = r.g.displayKey();
    r.put16(r.obj(0) + 10, 8000);
    CHECK_NE(r.g.displayKey(), before);
  }

  TEST_CASE("leaves MODE_GRAPHICS alone") {
    Rig r;
    world(r);
    r.cmd(CMD_SET_GRAPHICSMODE);
    // Graphics mode shows VRAM, which nothing has drawn into.
    CHECK_EQ(litPixels(r.frame()), 0u);
  }
}

TEST_SUITE("the depth cue and the palette") {
  TEST_CASE("draws a nearer object in a lighter shade than a farther one") {
    Rig r;
    world(r);
    // Two pyramids in the same ramp, one near and one far.
    r.byte(r.obj(0) + 2) = 1;
    r.put16(r.obj(0) + 4, 0xfe0c);  // x = -500
    r.put16(r.obj(0) + 8, 300);     // z = 300, nearer the camera at 600
    r.byte(r.obj(1) + 0) = 1;
    r.byte(r.obj(1) + 1) = 0;
    r.byte(r.obj(1) + 2) = 1;
    r.byte(r.obj(1) + 3) = 64;
    r.put16(r.obj(1) + 4, 500);
    r.put16(r.obj(1) + 8, 0xfc18);  // z = -1000, far away
    std::vector<int> all;
    for (uint8_t v : r.frame()) {
      if (v != 0) all.push_back(v & 15);
    }
    CHECK_GT(all.size(), 20u);
    // Nearer geometry must reach a lower shade number than farther geometry.
    CHECK_LT(*std::min_element(all.begin(), all.end()), *std::max_element(all.begin(), all.end()));
  }

  TEST_CASE("puts the ramp in the high nibble and the distance in the low one") {
    Rig r;
    world(r);
    r.byte(r.obj(0) + 2) = 5;  // ramp 5
    for (uint8_t v : r.frame()) {
      if (v != 0) CHECK_EQ(v >> 4, 5);
    }
  }

  TEST_CASE("fills a ramp's sixteen shades, fading toward black") {
    Rig r;
    // The colour is an argument. It used to be read back out of the palette
    // entry the program had written through the palette ports.
    r.cmd(CMD_WORLD_RAMP, {3, 255, 128, 64});
    const Palette& pal = r.g.palette;
    // Shade 0 is the base colour, and each shade after it is darker.
    CHECK_EQ(pal[3 * 16 * 3], 255);
    CHECK_EQ(pal[3 * 16 * 3 + 1], 128);
    CHECK_EQ(pal[3 * 16 * 3 + 2], 64);
    for (size_t s = 1; s < 16; s++) {
      const size_t i = (3 * 16 + s) * 3;
      CHECK_LT(pal[i], pal[i - 3]);
    }
  }

  TEST_CASE("fades toward palette entry 0, which is the background") {
    // The background colour is palette[0], mostly black but it can be
    // anything. A ramp that always faded to black would make distant
    // geometry stand OUT against a light sky, which is backwards.
    Rig r;
    r.g.palette[0] = 40;  // a blue-grey sky
    r.g.palette[1] = 60;
    r.g.palette[2] = 120;
    r.cmd(CMD_WORLD_RAMP, {1, 240, 160, 80});
    auto at = [&](int s) {
      const size_t i = static_cast<size_t>(16 + s) * 3;
      return std::vector<int>{r.g.palette[i], r.g.palette[i + 1], r.g.palette[i + 2]};
    };
    // Shade 0 is the base colour, untouched.
    CHECK(at(0) == std::vector<int>{240, 160, 80});
    // Every later shade is that colour mixed toward the background, in
    // sixteenths. Shade 15 keeps a sixteenth of the base, as before, so the
    // farthest geometry is still distinct from the sky.
    auto mix = [](int base, int bg, int s) { return static_cast<int>(std::lround((base * (16 - s) + bg * s) / 16.0)); };
    for (int s = 1; s < 16; s++) {
      CHECK(at(s) == std::vector<int>{mix(240, 40, s), mix(160, 60, s), mix(80, 120, s)});
    }
    // Blue RISES toward the sky here, because the sky is bluer than the
    // object. A fade that only ever darkens cannot do that.
    CHECK_GT(at(15)[2], at(0)[2]);
  }

  TEST_CASE("still fades to black when the background is black") {
    // The default background is palette entry 0, which is black, so every
    // program written before this change renders byte for byte the same.
    Rig r;
    CHECK_EQ(r.g.palette[0], 0);
    CHECK_EQ(r.g.palette[1], 0);
    CHECK_EQ(r.g.palette[2], 0);
    r.cmd(CMD_WORLD_RAMP, {1, 96, 255, 160});
    const size_t i = 31 * 3;
    CHECK_EQ(r.g.palette[i], 6);
    CHECK_EQ(r.g.palette[i + 1], 16);
    CHECK_EQ(r.g.palette[i + 2], 10);
  }

  TEST_CASE("touches only its own ramp") {
    Rig r;
    const Palette before = r.g.palette;
    r.cmd(CMD_WORLD_RAMP, {2, 200, 200, 200});
    const Palette& after = r.g.palette;
    for (size_t i = 0; i < 256; i++) {
      if ((i >> 4) == 2) continue;
      for (size_t k = 0; k < 3; k++) CHECK_EQ(after[i * 3 + k], before[i * 3 + k]);
    }
  }
}

TEST_SUITE("the printf overlay in MODE_WORLD") {
  // The overlay is a plane of character cells the GPU composites over the
  // picture. It rode over graphics and not over the world, so a flight
  // through a world could not show where it was.
  auto withText = [](Rig& r, const std::string& text) {
    r.cmd(CMD_TEXT_STYLE, {16});  // ramp 1 shade 0, legible in this mode
    for (char ch : text) r.cmd(CMD_TEXT_CHAR, {ch});
  };

  TEST_CASE("draws text over the world") {
    Rig r;
    world(r);
    const Frame before = r.frame();
    withText(r, "ALT 1200");
    const Frame after = r.frame();
    CHECK(after != before);
    // In the text colour, which is not a colour the world renderer uses for
    // this scene, so the glyphs are separable from the wireframe.
    CHECK_GT(std::count(after.begin(), after.end(), 16), 20);
  }

  TEST_CASE("leaves the world alone where there is no text") {
    // The overlay is transparent. Only cells holding a character paint.
    Rig r;
    world(r);
    const Frame before = r.frame();
    withText(r, "X");
    const Frame after = r.frame();
    const size_t changed = differing(before, after);
    // One 8 by 8 cell at most.
    CHECK_GT(changed, 0u);
    CHECK_LE(changed, 64u);
  }

  TEST_CASE("changes the display key, so the screen actually refreshes") {
    // The host redraws when displayKey changes. An overlay that composites
    // correctly but leaves the key alone paints once and then freezes.
    Rig r;
    world(r);
    const std::string before = r.g.displayKey();
    withText(r, "1");
    CHECK_NE(r.g.displayKey(), before);
  }

  TEST_CASE("clears with CMD_TEXT_CLEAR, the way it does over graphics") {
    Rig r;
    world(r);
    const Frame clean = r.frame();
    withText(r, "HELLO");
    CHECK(r.frame() != clean);
    r.g.write(GPU_CMD, CMD_TEXT_CLEAR);
    CHECK(r.frame() == clean);
  }
}

TEST_SUITE("the depth convention") {
  TEST_CASE("draws the same picture under both depth ranges") {
    // The contract with the future. x and y are untouched by the range and
    // the shade comes from view space z, so nothing reads a projected z.
    // This fails the day someone shades from the value after the divide.
    Rig a;
    world(a);
    a.g.setDepthRange(0);
    const Frame gl = a.frame();
    Rig b;
    world(b);
    b.g.setDepthRange(1);
    const Frame webgpu = b.frame();
    CHECK(webgpu == gl);
    CHECK_GT(litPixels(gl), 50u);
  }
}

TEST_SUITE("the depth cue formula") {
  const Fx near = fxFrom(10);
  const Fx far = fxFrom(110);

  TEST_CASE("gives 0 at the near plane and 15 at the far one") {
    // The formula is floor(t * 16) clamped, where t runs 0 to 1 across the
    // span. Spanning 15 instead of 16 would give 7 at the midpoint.
    CHECK_EQ(shadeFor(fxFrom(10), near, far), 0);
    CHECK_EQ(shadeFor(fxFrom(110), near, far), 15);
    CHECK_EQ(shadeFor(fxFrom(60), near, far), 8);
    CHECK_EQ(shadeFor(fxFrom(35), near, far), 4);
  }

  TEST_CASE("clamps outside the span rather than running off the palette") {
    CHECK_EQ(shadeFor(fxFrom(0), near, far), 0);
    CHECK_EQ(shadeFor(fxFrom(-50), near, far), 0);
    // Past the far plane draws dim, not invisible.
    CHECK_EQ(shadeFor(fxFrom(5000), near, far), 15);
  }

  TEST_CASE("gives 0 rather than dividing by zero when near equals far") {
    CHECK_EQ(shadeFor(fxFrom(50), near, near), 0);
  }
}

TEST_SUITE("the picture's geometry") {
  TEST_CASE("puts the pyramid's apex above its base") {
    // The apex is one vertex where four edges meet, and the base is a wide
    // outline. If the screen y were not flipped they would swap, and a test
    // that only counts lit pixels would never notice.
    Rig r;
    world(r);
    const Frame px = r.frame();
    int top = -1;
    int bottom = -1;
    auto rowCount = [&](int y) {
      int n = 0;
      for (int x = 0; x < SCREEN_W; x++) {
        if (gpu_rig::px(px, x, y) != 0) n++;
      }
      return n;
    };
    for (int y = 0; y < SCREEN_W; y++) {
      if (rowCount(y) > 0) {
        if (top < 0) top = y;
        bottom = y;
      }
    }
    CHECK_GE(top, 0);
    CHECK_GT(bottom, top);
    // Few pixels where the edges converge at the apex, many along the base.
    CHECK_LT(rowCount(top), rowCount(bottom));
  }

  TEST_CASE("draws nearer objects over farther ones") {
    // Two pyramids on the same line of sight, in different ramps. Painter's
    // order draws the far one first, so the near one overwrites it where
    // their outlines cross. Front to back would leave the far one intact.
    Rig r;
    world(r);
    r.byte(r.obj(0) + 2) = 1;      // the far one, ramp 1
    r.put16(r.obj(0) + 8, 0xff38);  // z = -200
    const size_t farAlone = countRamp(r.frame(), 1);

    r.byte(r.obj(1) + 0) = 1;  // a nearer one, ramp 2, same line of sight
    r.byte(r.obj(1) + 1) = 0;
    r.byte(r.obj(1) + 2) = 2;
    r.byte(r.obj(1) + 3) = 64;
    r.put16(r.obj(1) + 8, 300);
    const size_t farWithNear = countRamp(r.frame(), 1);

    CHECK_GT(farAlone, 0u);
    CHECK_LT(farWithNear, farAlone);
  }
}

TEST_SUITE("object transforms from a matrix") {
  // An object carries a position, three angles and a scale, and the GPU
  // builds the model matrix from them. That cannot say everything a matrix
  // can, and it cannot be composed by the ACP, which is the device that can
  // actually do the arithmetic. So an object may instead name a matrix by id.
  //
  // The elements are float64, big-endian, row major. That is forced, not
  // chosen: it is exactly what the ACP writes, and the CPU cannot compose a
  // matrix at all, having no multiply.

  TEST_CASE("takes a matrix id of two bytes, like every other id") {
    CHECK_EQ(MATRIX_BYTES, 128);  // sixteen float64, seven doublings
    CHECK_EQ(OBJ_FLAG_MATRIX, 2);  // bit 1, beside the active bit
  }

  TEST_CASE("draws the same picture from an identity matrix as from a bare record") {
    // The equivalence that makes the feature safe to adopt: a matrix that
    // says nothing draws what saying nothing says.
    Rig plain;
    world(plain);
    const Frame want = plain.frame();

    Rig r;
    world(r);
    r.mapMats(4);
    r.putMat(0, identity());
    r.useMatrix(0);
    CHECK(r.frame() == want);
  }

  TEST_CASE("moves an object by the matrix's translation") {
    Rig plain;
    world(plain);
    plain.put16(plain.obj(0) + 4, 150);  // the same shift, the old way
    const Frame want = plain.frame();

    Rig r;
    world(r);
    r.mapMats(4);
    r.putMat(0, translate(150, 0, 0));
    r.useMatrix(0);
    CHECK(r.frame() == want);
  }

  TEST_CASE("turns an object by the matrix's rotation") {
    // A quarter turn about Y, written as a matrix, against the same quarter
    // turn written as an angle. 16384 is a quarter of the 16 bit turn.
    Rig plain;
    world(plain);
    plain.put16(plain.obj(0) + 10, 16384);
    const Frame want = plain.frame();

    Rig r;
    world(r);
    r.mapMats(4);
    r.putMat(0, rotY(std::numbers::pi / 2));
    r.useMatrix(0);
    const Frame got = r.frame();
    // Not byte identical: the angle path reads a 1024-entry sine table and
    // this one reads a double. So compare the drawn shape, generously.
    CHECK_GT(litPixels(got), 100u);
    CHECK_LT(differing(got, want), litPixels(want) / 4);
  }

  TEST_CASE("sorts by the matrix's translation, so a matrix object draws in order") {
    // The painter's order reads an object's origin. In matrix mode bytes 4
    // to 9 are the id, not a position, so the origin has to come out of the
    // matrix or a near object would draw behind a far one.
    Rig r;
    world(r, 2);
    r.mapMats(4);
    // Object 0 far away, object 1 near, both on the same spot in x and y.
    r.putMat(0, translate(0, 0, -900));
    r.putMat(1, translate(0, 0, 300));
    r.useMatrix(0);
    r.byte(r.obj(1)) = OBJ_FLAG_ACTIVE | OBJ_FLAG_MATRIX;
    r.byte(r.obj(1) + 1) = 0;
    r.byte(r.obj(1) + 2) = 2;  // ramp 2
    r.byte(r.obj(1) + 4) = 0;
    r.byte(r.obj(1) + 5) = 1;
    const Frame px = r.frame();
    // Where the two overlap, the near one wins. Its ramp is 2.
    std::set<int> ramps;
    for (uint8_t v : px) {
      if (v != 0) ramps.insert(v >> 4);
    }
    CHECK(ramps == std::set<int>{1, 2});
    // The near object is nearer, so it reaches a brighter shade.
    int best1 = 16;
    int best2 = 16;
    for (uint8_t v : px) {
      if (v == 0) continue;
      if ((v >> 4) == 1) best1 = std::min(best1, v & 15);
      if ((v >> 4) == 2) best2 = std::min(best2, v & 15);
    }
    CHECK_LT(best2, best1);
  }

  TEST_CASE("keeps position and angles when the flag is clear") {
    // Every scene written before this change still means what it meant.
    Rig r;
    world(r);
    r.mapMats(4);
    r.putMat(0, translate(9999, 9999, 9999));  // must be ignored
    r.put16(r.obj(0) + 4, 150);
    Rig plain;
    world(plain);
    plain.put16(plain.obj(0) + 4, 150);
    CHECK(r.frame() == plain.frame());
  }

  TEST_CASE("draws nothing for a matrix id past the mapped count") {
    // Silent, like an object naming a mesh that was never defined.
    Rig r;
    world(r, 1);
    r.mapMats(2);
    r.useMatrix(7);
    CHECK_EQ(litPixels(r.frame()), 0u);
  }

  TEST_CASE("draws nothing when no matrix table is mapped at all") {
    Rig r;
    world(r, 1);
    r.useMatrix(0);
    CHECK_EQ(litPixels(r.frame()), 0u);
  }

  TEST_CASE("reads a two byte id, not one") {
    // Every id on this machine is two bytes. A reader that took the low
    // byte alone passed every test here, because none used an id above 255.
    Rig r;
    world(r, 1);
    const int BASE = 0x6000;  // slot 300 ends at 0xF680, clear of the scene
    r.cmd(CMD_MATRIX_MAP, {BASE >> 8, BASE & 0xff, 301 >> 8, 301 & 0xff});
    r.putMatAt(BASE, 300, translate(150, 0, 0));
    r.byte(r.obj(0)) = OBJ_FLAG_ACTIVE | OBJ_FLAG_MATRIX;
    r.byte(r.obj(0) + 4) = 300 >> 8;    // 0x01
    r.byte(r.obj(0) + 5) = 300 & 0xff;  // 0x2C

    Rig plain;
    world(plain, 1);
    plain.put16(plain.obj(0) + 4, 150);
    CHECK(r.frame() == plain.frame());
  }

  TEST_CASE("puts matrix N at base plus N times 128, exactly") {
    // A stride of 64 reads the back half of one matrix as the front half of
    // the next. That produced a picture, which is why only an exact
    // comparison catches it.
    Rig r;
    world(r, 1);
    r.mapMats(4);
    r.putMat(0, translate(-200, 0, 0));
    r.putMat(1, translate(200, 0, 0));
    r.useMatrix(1);

    Rig plain;
    world(plain, 1);
    plain.put16(plain.obj(0) + 4, 200);
    CHECK(r.frame() == plain.frame());
  }

  TEST_CASE("refuses an id past the count even when the slot holds a real matrix") {
    // An all-zero matrix collapses w to zero and the near-plane clip drops
    // every edge, so the object drew nothing whether the count was checked
    // or not. The slot holds a usable matrix so the check is what is tested.
    Rig r;
    world(r, 1);
    r.mapMats(2);
    r.putMat(7, identity());
    r.useMatrix(7);
    CHECK_EQ(litPixels(r.frame()), 0u);
  }

  TEST_CASE("sorts on the matrix's own translation, so the nearer object wins") {
    // Two pyramids drawn to the same size and place: the far one is five
    // times further off and five times bigger, so their edges land on the
    // same pixels. Whichever is drawn last owns every one of them.
    //
    // An origin that ignored the matrix made both depths zero. The sort is
    // stable, so the objects then drew in id order and the FAR one finished
    // on top. Nothing else in this file could see that.
    Rig r;
    world(r, 2);
    r.mapMats(4);
    Matrix near = identity();
    near[11] = 300;  // camera sits at z 600, so this is 300 away
    Matrix far = identity();
    far[0] = 5;
    far[5] = 5;
    far[10] = 5;
    far[11] = -900;  // 1500 away, and five times the size
    r.putMat(0, near);
    r.putMat(1, far);
    r.byte(r.obj(0)) = OBJ_FLAG_ACTIVE | OBJ_FLAG_MATRIX;
    r.byte(r.obj(0) + 2) = 1;  // ramp 1, the near one
    r.byte(r.obj(0) + 5) = 0;
    r.byte(r.obj(1)) = OBJ_FLAG_ACTIVE | OBJ_FLAG_MATRIX;
    r.byte(r.obj(1) + 1) = 0;
    r.byte(r.obj(1) + 2) = 2;  // ramp 2, the far one
    r.byte(r.obj(1) + 5) = 1;

    const Frame px = r.frame();
    const size_t one = countRamp(px, 1);
    const size_t two = countRamp(px, 2);
    CHECK_GT(one, 50u);
    // The near one is painted last, so it owns the shared pixels.
    CHECK_GT(one, two);
  }

  TEST_CASE("changes the display key when a matrix changes") {
    // The GPU never sees data RAM written, so the key folds the scene in.
    // A matrix is scene data too. Leave it out and a world driven entirely
    // by matrices paints once and freezes, because nothing else changed.
    Rig r;
    world(r, 1);
    r.mapMats(1);
    r.putMat(0, identity());
    r.useMatrix(0);
    const std::string before = r.g.displayKey();
    r.putMat(0, translate(200, 0, 0));
    CHECK_NE(r.g.displayKey(), before);
  }

  TEST_CASE("ignores a matrix no active object names") {
    // The key folds in the matrices actually in use, not the whole table.
    // A table of three hundred would otherwise be rehashed every frame.
    Rig r;
    world(r, 1);
    r.mapMats(4);
    r.putMat(0, identity());
    r.useMatrix(0);
    const std::string before = r.g.displayKey();
    r.putMat(2, translate(500, 0, 0));  // nothing points at matrix 2
    CHECK_EQ(r.g.displayKey(), before);
  }

  TEST_CASE("scales by the matrix") {
    Rig r;
    world(r, 1);
    r.mapMats(1);
    r.putMat(0, identity());
    r.useMatrix(0);
    const size_t one = litPixels(r.frame());
    Matrix big = identity();
    big[0] = 2;
    big[5] = 2;
    big[10] = 2;
    r.putMat(0, big);
    CHECK_GT(litPixels(r.frame()), one);
  }

  TEST_CASE("ignores the object's scale byte, because the matrix carries scale") {
    // Two ways to say the size would be two sources of truth. The matrix is
    // the one, and the byte at offset 3 says nothing in this mode.
    Rig r;
    world(r, 1);
    r.mapMats(1);
    r.putMat(0, identity());
    r.useMatrix(0);
    const Frame before = r.frame();
    r.byte(r.obj(0) + 3) = 200;  // would be triple the size the old way
    CHECK(r.frame() == before);
  }
}

TEST_SUITE("the ACP composes a transform the GPU draws from") {
  // This is the handoff the whole feature exists for, and it is the only
  // test that proves the two devices actually agree on a format.
  //
  // The ACP works in float64 and writes data RAM. The GPU reads float64 from
  // data RAM. Neither converts anything. The CPU's part is to write three
  // port values and one command byte.

  TEST_CASE("multiplies two matrices on the ACP and renders the result") {
    Rig r;
    world(r, 1);

    // The ACP's block is A, then B, then the result, each a 4 by 4 of
    // float64. So the result sits 256 bytes into the block, and the matrix
    // table is mapped right there.
    const int BLOCK = 0x5000;
    const int RESULT = BLOCK + 2 * MATRIX_BYTES;
    LogIoBus log;
    Acp acp(&log);
    acp.attachRam(r.ram->data());
    // A: move 200 along x. B: double the size. The product does both.
    r.putMatAt(BLOCK, 0, {1, 0, 0, 200, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
    r.putMatAt(BLOCK, 1, {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1});

    acp.write(acp::ACP_ADDR_HI, BLOCK >> 8);
    acp.write(acp::ACP_ADDR_LO, BLOCK & 0xff);
    acp.write(acp::ACP_FMT, acp::ACP_F64);
    acp.write(acp::ACP_ROWS, 4);
    acp.write(acp::ACP_COLS, 4);
    acp.write(acp::ACP_COLS_B, 4);
    acp.write(acp::ACP_CMD, acp::ACP_MATMUL);
    CHECK_MESSAGE(acp.read(acp::ACP_FLAGS) == 0, "the ACP refused the product");

    // The GPU is pointed at the ACP's result. Nothing copies it.
    r.cmd(CMD_MATRIX_MAP, {RESULT >> 8, RESULT & 0xff, 0, 1});
    r.byte(r.obj(0)) = OBJ_FLAG_ACTIVE | OBJ_FLAG_MATRIX;
    r.byte(r.obj(0) + 4) = 0;
    r.byte(r.obj(0) + 5) = 0;

    const Frame px = r.frame();
    CHECK_MESSAGE(litPixels(px) > 100u, "nothing drew from the composed matrix");

    // The product is a move AND a scale, so the drawn shape sits right of
    // centre and is bigger than the unscaled one. Both, from one matrix.
    auto span = [](const Frame& f) {
      int minX = 999;
      int maxX = -1;
      for (size_t i = 0; i < f.size(); i++) {
        if (f[i] == 0) continue;
        const int x = static_cast<int>(i % SCREEN_W);
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
      }
      return std::pair<int, int>{minX, maxX};
    };
    const auto [minX, maxX] = span(px);
    Rig plain;
    world(plain, 1);
    const auto [pMin, pMax] = span(plain.frame());
    CHECK_MESSAGE(maxX - minX > pMax - pMin, "the scale did not survive the handoff");
    CHECK_MESSAGE(minX + maxX > pMin + pMax, "the move did not survive the handoff");
  }
}

TEST_SUITE("the camera's own matrices") {
  // Cameras and lights carry their own matrices. Fixed-function OpenGL kept
  // GL_MODELVIEW and GL_PROJECTION as separate stacks, and a shadow-casting
  // light has a view and a projection of its own because the scene is
  // rendered from it.
  //
  // So the camera takes two matrix ids, not one. The view is the half that
  // lets the ACP aim the camera. The projection is the half that makes an
  // orthographic view possible at all: mat4Perspective is otherwise the only
  // projection this machine has.

  TEST_CASE("names the two flag bits") {
    CHECK_EQ(CAM_FLAG_VIEW_MATRIX, 1);
    CHECK_EQ(CAM_FLAG_PROJ_MATRIX, 2);
  }

  TEST_CASE("draws the same picture from a view matrix as from position and angles") {
    Rig plain;
    world(plain, 1);
    const Frame want = plain.frame();

    Rig r;
    world(r, 1);
    r.mapMats(2);
    r.putMat(0, viewAt600());
    r.useCam(CAM_FLAG_VIEW_MATRIX, 0);
    CHECK(r.frame() == want);
  }

  TEST_CASE("ignores position and angles once the view comes from a matrix") {
    Rig r;
    world(r, 1);
    r.mapMats(2);
    r.putMat(0, viewAt600());
    r.useCam(CAM_FLAG_VIEW_MATRIX, 0);
    const Frame before = r.frame();
    r.put16(SCENE + 0, 9999);   // a position that would put the world off screen
    r.put16(SCENE + 6, 12345);  // and a yaw that would spin it
    CHECK(r.frame() == before);
  }

  TEST_CASE("draws the same picture from a projection matrix as from the focal length") {
    // mat4Perspective with the rig's own near, far and focal, handed over as
    // a matrix instead of built from three numbers.
    Rig plain;
    world(plain, 1);
    const Frame want = plain.frame();

    Rig r;
    world(r, 1);
    r.mapMats(2);
    r.putMat(1, perspective1to2000());
    r.useCam(CAM_FLAG_PROJ_MATRIX, 0, 1);
    CHECK(r.frame() == want);
  }

  TEST_CASE("makes an orthographic view possible, which nothing else here can") {
    // The point of the projection half. Under perspective a farther object
    // is smaller. Under orthographic it is not, and there is no way to say
    // that with a focal length.
    Rig r;
    world(r, 2);
    r.mapMats(2);
    // Two pyramids, same size, one much further away.
    r.put16(r.obj(0) + 8, 0);
    r.byte(r.obj(1) + 0) = 1;
    r.byte(r.obj(1) + 1) = 0;
    r.byte(r.obj(1) + 2) = 2;
    r.byte(r.obj(1) + 3) = 64;
    r.put16(r.obj(1) + 4, 0xfe0c);  // x -500
    r.put16(r.obj(1) + 8, 0xfa24);  // z -1500, far behind the first

    const size_t nearPersp = countRamp(r.frame(), 1);
    const size_t farPersp = countRamp(r.frame(), 2);
    CHECK_MESSAGE(farPersp < nearPersp, "the far one should be smaller under perspective");

    r.putMat(1, ortho600());
    r.useCam(CAM_FLAG_PROJ_MATRIX, 0, 1);
    const size_t nearOrtho = countRamp(r.frame(), 1);
    const size_t farOrtho = countRamp(r.frame(), 2);
    CHECK_MESSAGE(nearOrtho > 50u, "nothing drew under the orthographic projection");
    // Same size now, within the slop of two rasterised wireframes.
    const size_t gap = farOrtho > nearOrtho ? farOrtho - nearOrtho : nearOrtho - farOrtho;
    CHECK_LT(gap, nearOrtho / 3);
  }

  TEST_CASE("shades identically under a supplied projection") {
    // The depth cue reads VIEW space z against near and far, never the
    // projected z. That is what keeps the picture the same under either
    // depth convention, and it means the projection cannot move a shade.
    //
    // So the claim is that the shades a projection can produce are fixed by
    // the geometry, not by the projection. Both are checked against shadeFor
    // run on the depths the vertices actually sit at.
    auto range = [](Rig& r) {
      int lo = 16;
      int hi = -1;
      for (uint8_t v : r.frame()) {
        if (v == 0) continue;
        lo = std::min(lo, v & 15);
        hi = std::max(hi, v & 15);
      }
      return std::pair<int, int>{lo, hi};
    };
    // The camera sits at z 600 and the pyramid spans 100 either side of the
    // origin, so its vertices are 500 to 700 away. near is 1 and far 2000.
    const Fx near = fxFrom(1);
    const Fx far = fxFrom(2000);
    const int nearest = shadeFor(fxFrom(500), near, far);
    const int farthest = shadeFor(fxFrom(700), near, far);
    CHECK_MESSAGE(nearest < farthest, "the fixture cannot show a range");

    Rig plain;
    world(plain, 1);
    const auto built = range(plain);

    Rig r;
    world(r, 1);
    r.mapMats(2);
    r.putMat(1, ortho600());
    r.useCam(CAM_FLAG_PROJ_MATRIX, 0, 1);
    const auto given = range(r);

    for (const auto& got : {built, given}) {
      CHECK_GE(got.first, nearest);
      CHECK_LE(got.second, farthest);
      CHECK_GT(got.second, got.first);
    }
  }

  TEST_CASE("takes both halves at once") {
    Rig plain;
    world(plain, 1);
    const Frame want = plain.frame();

    Rig r;
    world(r, 1);
    r.mapMats(2);
    r.putMat(0, viewAt600());
    r.putMat(1, perspective1to2000());
    r.useCam(CAM_FLAG_VIEW_MATRIX | CAM_FLAG_PROJ_MATRIX, 0, 1);
    CHECK(r.frame() == want);
  }

  TEST_CASE("reads both ids as two bytes, not one") {
    // The object path had this gap too: no test used an id past 255, so a
    // reader that took the low byte alone passed everything.
    Rig plain;
    world(plain, 1);
    const Frame want = plain.frame();

    Rig r;
    world(r, 1);
    const int BASE = 0x6000;  // slot 301 ends at 0xF700, clear of the scene
    r.cmd(CMD_MATRIX_MAP, {BASE >> 8, BASE & 0xff, 302 >> 8, 302 & 0xff});
    r.putMatAt(BASE, 300, viewAt600());
    r.putMatAt(BASE, 301, perspective1to2000());
    // 300 is 0x012C and 301 is 0x012D. Both need the high byte.
    r.useCam(CAM_FLAG_VIEW_MATRIX | CAM_FLAG_PROJ_MATRIX, 300, 301);
    CHECK(r.frame() == want);
  }

  TEST_CASE("falls back to the record when the id names no matrix") {
    // DIFFERENT from an object, which draws nothing. An object in matrix
    // mode has no position to fall back to, because its position bytes ARE
    // the id. The camera's position and angles sit elsewhere in the record
    // and are always valid, so there is a well defined thing to do.
    Rig plain;
    world(plain, 1);
    const Frame want = plain.frame();

    Rig r;
    world(r, 1);
    r.mapMats(1);
    r.useCam(CAM_FLAG_VIEW_MATRIX | CAM_FLAG_PROJ_MATRIX, 9, 9);
    CHECK(r.frame() == want);
  }

  TEST_CASE("changes the display key when the camera's matrix changes") {
    // The same defect the object matrices had: the GPU never sees data RAM
    // written, so a camera driven by a matrix would paint once and freeze.
    Rig r;
    world(r, 1);
    r.mapMats(2);
    r.putMat(0, viewAt600());
    r.useCam(CAM_FLAG_VIEW_MATRIX, 0);
    const std::string before = r.g.displayKey();
    r.putMat(0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -900, 0, 0, 0, 1});
    CHECK_NE(r.g.displayKey(), before);
  }
}

// docs/design/gpu-world-ports.md carries what the port and command tables
// cannot: the record layouts, the memory sizes and the palette layout. Those
// are numbers a program has to get exactly right, and a wrong one in the
// reference is worse than no reference at all. So every one is pinned to
// the machine here.
TEST_SUITE("docs/design/gpu-world-ports.md") {
  // Every table row's first two cells, so a layout can be read out of the doc.
  std::vector<std::pair<std::string, std::string>> rowsOf(const std::string& doc) {
    std::vector<std::pair<std::string, std::string>> rows;
    const std::regex row(R"(^\|([^|]+)\|([^|]+)\|)");
    auto trim = [](std::string s) {
      while (!s.empty() && s.back() == ' ') s.pop_back();
      while (!s.empty() && s.front() == ' ') s.erase(s.begin());
      return s;
    };
    std::istringstream in(doc);
    std::string line;
    while (std::getline(in, line)) {
      std::smatch m;
      if (std::regex_search(line, m, row)) rows.emplace_back(trim(m[1].str()), trim(m[2].str()));
    }
    return rows;
  }

  bool has(const std::string& doc, const std::string& s) { return doc.find(s) != std::string::npos; }

  TEST_CASE("gives the memory sizes the machine really has") {
    const std::string doc = readDoc("gpu-world-ports.md");
    REQUIRE_FALSE(doc.empty());
    CHECK(has(doc, std::to_string(WORLD_RAM_SIZE) + " bytes"));
    CHECK(has(doc, std::to_string(MESH_COUNT) + " meshes"));
  }

  TEST_CASE("describes a camera record of the right size") {
    const std::string doc = readDoc("gpu-world-ports.md");
    CHECK(has(doc, std::to_string(CAMERA_BYTES) + " bytes at the mapped base"));
    // The last offset named must be the record's last byte.
    bool found = false;
    for (const auto& [cell, second] : rowsOf(doc)) {
      if (cell == "30-" + std::to_string(CAMERA_BYTES - 1)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("describes an object record of the right size, at the right stride") {
    const std::string doc = readDoc("gpu-world-ports.md");
    CHECK(has(doc, std::to_string(OBJ_STRIDE) + " bytes each"));
    CHECK(has(doc, "base + 32 + N * " + std::to_string(OBJ_STRIDE)));
    // The camera comes first, so the objects start after it.
    CHECK(has(doc, "the base plus " + std::to_string(CAMERA_BYTES)));
    CHECK(has(doc, "10-" + std::to_string(OBJ_STRIDE - 1)));
  }

  TEST_CASE("gives the geometry formats the mesh loader really reads") {
    const std::string doc = readDoc("gpu-world-ports.md");
    std::string vertex;
    std::string edge;
    for (const auto& [cell, second] : rowsOf(doc)) {
      if (cell == "vertices") vertex = second;
      if (cell == "edges") edge = second;
    }
    CHECK_EQ(vertex, std::to_string(VERTEX_BYTES));
    CHECK_EQ(edge, std::to_string(EDGE_BYTES));
    CHECK(has(doc, std::to_string(MESH_STRIDE) + " bytes"));
  }

  TEST_CASE("documents the camera's own two matrix ids") {
    const std::string doc = readDoc("gpu-world-ports.md");
    CHECK(has(doc, "view matrix id"));
    CHECK(has(doc, "projection matrix id"));
    CHECK(has(doc, "ORTHOGRAPHIC"));
    std::set<std::string> cells;
    for (const auto& [cell, second] : rowsOf(doc)) cells.insert(cell);
    CHECK(cells.count("25") == 1);
    CHECK(cells.count("26-27") == 1);
    CHECK(cells.count("28-29") == 1);
  }

  TEST_CASE("names the commands that switch the mode") {
    const std::string doc = readDoc("gpu-world-ports.md");
    CHECK(has(doc, "CMD_SET_WORLDMODE"));
    CHECK(has(doc, "CMD_SET_GRAPHICSMODE"));
    bool worldMode = false;
    for (const NamedValue& m : MODES) {
      if (m.name == "MODE_WORLD") worldMode = true;
    }
    CHECK(worldMode);
  }

  TEST_CASE("states the palette layout in both directions") {
    const std::string doc = readDoc("gpu-world-ports.md");
    CHECK(has(doc, "high nibble is the ramp"));
    CHECK(has(doc, "low nibble is the shade"));
    CHECK(has(doc, "fills all 256 entries exactly"));
  }
}
