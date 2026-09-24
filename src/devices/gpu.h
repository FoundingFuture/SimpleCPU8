// The GPU: a display device on the IO bus.
//
// It is a peripheral, not part of the CPU. It claims ports $00 to $1F and
// forwards everything else to a fallback bus. Like the rest of the machine
// it is fully deterministic: every visible effect is a function of the
// values written and the machine's cycle counter, its only clock. The GPU
// never crashes the CPU. Off-screen writes clip, bad values wrap, unknown
// commands do nothing.
//
// Screen model: 256 x 256 pixels, 8 bits per pixel, indexed through a
// 256 entry RGB888 palette. The default palette is 3-3-2 (RRRGGGBB).
//
// gpu.cpp holds the bus, the commands and the sprites. gpu_text.cpp holds
// the overlay, text mode and printf. gpu_world.cpp holds MODE_WORLD.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "devices/device.h"
#include "devices/font.h"
#include "devices/gpu_ports.h"
#include "devices/mat4.h"

namespace sc8 {

namespace gpu {

constexpr int64_t CYCLES_PER_FRAME = 65536;

// Text grid: the 6 by 8 font over the 256 by 256 screen gives 42 by 32.
// Derived from the font's own cell, so a font change is one number.
constexpr int TEXT_COLS = SCREEN_W / FONT_W;
constexpr int TEXT_ROWS = SCREEN_H / FONT_H;
constexpr int TEXT_CELLS = TEXT_COLS * TEXT_ROWS;

constexpr int SPRITE_MAX = 64;  // pixels per side
constexpr int SPRITE_COUNT = 256;
constexpr int SPRITE_FRAMES_MAX = 16;  // frames in one animated strip

// Eight groups, because eight bits is a byte and a byte is what the CPU tests
// in one AND. The controller packs seven buttons the same way.
constexpr int GROUP_COUNT = 8;

// MODE_WORLD's memories and record shapes. The world lives in the GPU, the
// scene lives in the CPU's data RAM. See docs/design/gpu-world-design.md.
constexpr int WORLD_RAM_SIZE = 256 * 1024;
constexpr int MESH_COUNT = 256;
constexpr int MESH_STRIDE = 20;  // 12 used, then the reserved face range
constexpr int CAMERA_BYTES = 32;
// DESIGN: sixteen, because object N sits at base + 32 + N * 16 and the CPU
// has no multiply. Sixteen is four doublings; twenty four would need a real
// multiply or a table on every object access.
constexpr int OBJ_STRIDE = 16;
// DESIGN: a matrix is sixteen float64, big-endian, row major. 128 bytes.
// float64 is forced, not chosen: the ACP is the only device that can compose
// a transform, it writes data RAM, and its element types have no 16.16.
constexpr int MATRIX_BYTES = 128;
constexpr int VERTEX_BYTES = 6;  // three signed 16 bit, a native gl.SHORT
constexpr int EDGE_BYTES = 4;    // two unsigned 16 bit indices

// The numbers the implementation dispatches on. gpu_ports.h publishes the
// same names as text for the assembler, and a test pins the two together.
enum Port : uint8_t {
  GPU_CMD = 0x00,
  GPU_CMD_MOD = 0x01,
  GPU_DATA0 = 0x02,
  GPU_DATA1 = 0x03,
  GPU_DATA2 = 0x04,
  GPU_DATA3 = 0x05,
  GPU_DATA4 = 0x06,
  GPU_DATA5 = 0x07,
  GPU_DATA6 = 0x08,
  GPU_RAND = 0x1e,
  GPU_FRAME = 0x1f,
};

// Names for the data ports, by the job a command gives them. Several names
// share a port because several commands put different things there.
enum Alias : uint8_t {
  GPU_X_HI = 0x02,
  GPU_X = 0x03,
  GPU_Y_HI = 0x04,
  GPU_Y = 0x05,
  GPU_PIXEL = 0x06,
  GPU_COLOR = 0x02,
  GPU_RADIUS = 0x02,
  GPU_RADIUS_Y = 0x03,
  GPU_CART_BANK = 0x02,
  GPU_CART_HI = 0x03,
  GPU_CART_LO = 0x04,
  GPU_SRC_BANK = 0x03,
  GPU_SRC_HI = 0x04,
  GPU_SRC_LO = 0x05,
  GPU_ADDR_HI = 0x02,
  GPU_ADDR_LO = 0x03,
  GPU_COUNT_HI = 0x04,
  GPU_COUNT_LO = 0x05,
  GPU_FROM_HI = 0x03,
  GPU_FROM_LO = 0x04,
  GPU_DEST_HI = 0x05,
  GPU_DEST_LO = 0x06,
  GPU_LEN_HI = 0x07,
  GPU_LEN_LO = 0x08,
  GPU_MAP = 0x02,
  GPU_MAP_HI = 0x03,
  GPU_MAP_LO = 0x04,
  GPU_SPRITE = 0x02,
  GPU_SPRITE_B = 0x03,
  GPU_SPRITE_FRAME = 0x03,
  GPU_SPRITE_FLIP = 0x03,
  GPU_SPRITE_X_HI = 0x03,
  GPU_SPRITE_X = 0x04,
  GPU_SPRITE_Y_HI = 0x05,
  GPU_SPRITE_Y = 0x06,
  GPU_HIT = 0x02,
  GPU_GROUP = 0x02,
  GPU_GROUP_B = 0x03,
  GPU_HIT_FROM = 0x04,
  GPU_SPRITE_GROUP = 0x06,
  GPU_ANGLE = 0x02,
  GPU_SCALE = 0x02,
  GPU_SPEED = 0x02,
  GPU_DIR = 0x02,
  GPU_FIRST = 0x02,
  GPU_LAST = 0x03,
  GPU_TEXT_COLOR = 0x02,
  GPU_TEXT_BG = 0x03,
  GPU_TEXT_FLAGS = 0x04,
  GPU_TEXT_COL = 0x02,
  GPU_TEXT_ROW = 0x03,
  GPU_TEXT_CHAR = 0x02,
  GPU_TEXT_ARG_HI = 0x05,
  GPU_TEXT_ARG_LO = 0x06,
  GPU_MESH = 0x02,
  GPU_MESH_BYTE = 0x02,
  GPU_RAMP = 0x02,
  GPU_RED = 0x03,
  GPU_GREEN = 0x04,
  GPU_BLUE = 0x05,
};

// Modifier bits, held until written again. A mode, not a one-shot.
enum Mod : uint8_t {
  MOD_STICKY = 1,  // data ports keep their values after the command
  MOD_INC0 = 2,    // DATA0 steps by one after the command
};

// Command bytes for GPU_CMD. One block of sixteen per family. $00 is not a
// command, so a stray write lands on nothing.
enum Cmd : uint8_t {
  CMD_CLEAR = 0x01,
  CMD_SET_COLOR = 0x02,
  CMD_MOVE_TO = 0x03,
  CMD_LINE_TO = 0x04,
  CMD_RECT = 0x05,
  CMD_CIRCLE = 0x06,
  CMD_RING = 0x07,
  CMD_PLOT = 0x08,
  CMD_READ_PIXEL = 0x09,
  CMD_SAVE_SCREEN = 0x0a,
  CMD_RESTORE_SCREEN = 0x0b,
  CMD_RESET_PALETTE = 0x10,
  CMD_LOAD_PALETTE = 0x11,
  CMD_STORE_PALETTE = 0x12,
  CMD_FETCH_PALETTE = 0x13,
  CMD_ROTATE_LEFT = 0x14,
  CMD_ROTATE_RIGHT = 0x15,
  CMD_ROTATE_SPEED = 0x16,
  CMD_ROTATE_DIR = 0x17,
  CMD_ROTATE_RANGE = 0x18,
  CMD_SPRITE_DEF = 0x20,
  CMD_SPRITE_MOVE = 0x21,
  CMD_SPRITE_SHOW = 0x22,
  CMD_SPRITE_HIDE = 0x23,
  CMD_SPRITE_FRAME = 0x24,
  CMD_SPRITE_FLIP = 0x25,
  CMD_STAMP = 0x26,
  CMD_BLIT = 0x27,
  CMD_HIT_TEST = 0x30,
  CMD_HIT_SCAN = 0x31,
  CMD_COLLIDE_ALL = 0x32,
  CMD_SPRITE_HITS = 0x33,
  CMD_GROUP_HITS = 0x34,
  CMD_HIT_IN_GROUP = 0x35,
  CMD_COLLIDE_GROUP_ALL = 0x36,
  CMD_ROT_X = 0x40,
  CMD_ROT_Y = 0x41,
  CMD_ROT_Z = 0x42,
  CMD_SET_SCALE = 0x43,
  CMD_DRAW_PATH = 0x44,
  CMD_DRAW_PATH3D = 0x45,
  CMD_SET_TEXTMODE = 0x50,
  CMD_SET_GRAPHICSMODE = 0x51,
  CMD_TEXT_STYLE = 0x52,
  CMD_TEXT_AT = 0x53,
  CMD_TEXT_CHAR = 0x54,
  CMD_TEXT_CLEAR = 0x55,
  CMD_PRINTF = 0x56,
  CMD_LOAD_FONT = 0x57,
  CMD_SET_WORLDMODE = 0x60,
  CMD_MESH_LOAD = 0x61,
  CMD_MESH_WRITE = 0x62,
  CMD_WORLD_RAMP = 0x63,
  CMD_MATRIX_MAP = 0x64,
  CMD_COPY = 0x70,
  CMD_MEMMAP = 0x71,
  CMD_RAM_MOVE = 0x72,
};

// What CMD_MEMMAP points at.
enum Map : uint8_t {
  MAP_PALETTE_IN = 0,
  MAP_PALETTE_OUT = 1,
  MAP_PALETTE = 2,
  MAP_COLLIDE = 3,
  MAP_GROUPS = 4,
};

// Video modes. Graphics is the power-on mode.
enum Mode : uint8_t {
  MODE_GRAPHICS = 0,
  MODE_TEXT = 1,
  MODE_WORLD = 2,
};

// MODE_WORLD's record flag bits.
enum WorldFlag : uint8_t {
  OBJ_FLAG_ACTIVE = 1,
  OBJ_FLAG_MATRIX = 2,
  CAM_FLAG_VIEW_MATRIX = 1,
  CAM_FLAG_PROJ_MATRIX = 2,
};

// Which alias each command reads, in the order it reads them. The device's
// own knowledge: the manual renders it and the C library generates one
// wrapper per command from it. CMD_COPY is the widest, at seven.
struct CmdArgs {
  std::string_view cmd;
  std::array<std::string_view, DATA_COUNT> names{};
  size_t count = 0;

  constexpr CmdArgs(std::string_view c, std::initializer_list<std::string_view> a) : cmd(c) {
    for (std::string_view n : a) names[count++] = n;
  }
  constexpr std::span<const std::string_view> args() const { return {names.data(), count}; }
};

extern const std::vector<CmdArgs> CMD_ARGS;

// The commands that leave an answer on a data port. A result survives the
// clear that wipes the arguments, because it exists BECAUSE the command
// produced it.
constexpr std::string_view RESULT_CMDS[] = {
    "CMD_READ_PIXEL", "CMD_HIT_TEST", "CMD_HIT_SCAN", "CMD_SPRITE_HITS", "CMD_GROUP_HITS", "CMD_HIT_IN_GROUP",
};

}  // namespace gpu

using Palette = std::array<uint8_t, 768>;

Palette default332();

// DESIGN: the depth cue, and the formula is written down so a fragment
// shader could reproduce it exactly. It reads VIEW space z, before the
// projection matrix, which is what keeps the picture identical under either
// depth convention.
//
//   t     = (z - near) / (far - near)   clamped to 0..1
//   shade = floor(t * 16)               clamped to 0..15
//
// Shade 0 is nearest and brightest. Something past `far` draws at 15 rather
// than vanishing, because a world that pops out of existence looks like a bug.
int shadeFor(int64_t z, int64_t near, int64_t far);

struct Sprite {
  int w = 0;
  int h = 0;
  int frames = 1;  // 1 for a plain sprite, more for a strip
  int frame = 0;   // which frame renders now
  std::vector<uint8_t> data;  // frames * w * h indexes; 0 is transparent
  int x = 0;
  int y = 0;
  bool visible = false;
  bool flipH = false;  // mirror left to right when drawn
  bool flipV = false;  // mirror top to bottom
  // Which of the eight groups this sprite belongs to, one bit each. Zero is
  // no group at all, which is what every sprite defined before groups existed
  // gets, and what keeps those programs answering exactly as they did.
  int group = 0;
};

struct MeshInfo {
  uint32_t firstVertex = 0;
  int vertexCount = 0;
  uint32_t firstEdge = 0;
  int edgeCount = 0;
  uint32_t firstFace = 0;
  int faceCount = 0;
  int scaleExp = 0;
};

class Gpu : public ChainedDevice {
 public:
  using Frame = std::vector<uint8_t>;  // SCREEN_W * SCREEN_H indexes

  // DESIGN: the seed is injected like the cycle clock, not baked in. The app
  // passes the wall clock so every run differs. Tests pass nothing and get a
  // fixed stream, because a reproducible test needs a reproducible generator.
  explicit Gpu(std::function<uint64_t()> cycles, IoBus* fallback = nullptr,
               std::function<uint32_t()> seed = nullptr);

  // The cartridge: read-only asset memory, GPU side only. The device keeps
  // the span, so the owner keeps the bytes alive.
  void attachCart(CartView cart) { cart_ = cart; }
  CartView cart() const { return cart_; }

  // Give the GPU a read-only view of data RAM, the second port that makes
  // text mode work. The host wires this once, since it owns both the CPU and
  // the GPU. Text mode and MODE_WORLD read this buffer every frame, and the
  // memory commands write it. RAM_SIZE bytes, kept alive by the owner.
  void attachRam(uint8_t* ram) { ram_ = ram; }
  uint8_t* ram() const { return ram_; }

  // Reset the device to power-on state. The CPU's Reset button does NOT do
  // this (the screen is not inside the CPU); Restart does.
  void powerOn();

  void write(uint8_t port, uint8_t value) override;
  uint8_t read(uint8_t port) override;

  // Device time: frames tick every CYCLES_PER_FRAME machine cycles, plus
  // whatever fast-frame reads have forced.
  int64_t frame() const;

  // The pen, which every drawing command works from.
  int x() const { return penX; }
  int y() const { return penY; }

  // The screen as shown: background shifted by the scroll registers, then the
  // visible sprites on top in id order, then the overlay. Text mode and
  // MODE_WORLD compose their own picture instead.
  Frame composeFrame() const;

  // The composed frame through the effective palette, as 256 x 256 RGBA8.
  // This is what a display texture takes.
  void compose(std::span<uint8_t> rgba) const;

  // A key that changes whenever the screen would look different, including
  // time-driven palette rotation with no port writes at all.
  std::string displayKey() const;

  // Total rotation steps right now: manual offset plus the frame-clocked
  // part. Pure function of state and the cycle counter.
  int64_t rotationSteps() const;

  // The palette as the screen currently shows it: the stored palette with
  // the rotating range shifted by the current step count.
  Palette effectivePalette() const;

  // Path projection. All integer math (7-bit trig, truncating divides): the
  // same path draws the same pixels on every machine.
  std::array<int, 2> project2(int bx, int by) const;
  std::array<int, 2> project3(int bx, int by, int bz) const;

  // MODE_WORLD.
  void setDepthRange(int range) { depthRange_ = range; }
  MeshInfo meshInfo(int id) const;

  // ---- state, public the way the TypeScript fields were, for tests and
  // the host's inspectors ----

  std::vector<uint8_t> vram;  // 64KB of video RAM: one byte per pixel, row-major
  Palette palette;
  std::vector<uint8_t> backbuffer;

  // The data ports. A command reads its arguments here and leaves any small
  // result here. They clear after every command unless MOD_STICKY holds them.
  std::array<uint8_t, gpu::DATA_COUNT> data{};
  int cmdMod = 0;

  int color = 0;
  int scrollX = 0;
  int scrollY = 0;

  // Pen state. Position and colour, and every drawing command works from it.
  int penX = 0;
  int penY = 0;

  // Where CMD_MEMMAP points the one-shot transfers. Unset is -1, which is not
  // a RAM address, so a transfer with no mapping does nothing.
  int mapPaletteIn = -1;
  int mapPaletteOut = -1;
  int mapCollide = -1;
  int mapGroups = -1;
  std::array<std::optional<Sprite>, gpu::SPRITE_COUNT> sprites;
  // The last collision result, kept for the host's inspectors. Every answer
  // now comes back on GPU_HIT, so nothing reads it.
  int hitResult = 0;
  std::array<uint8_t, gpu::SPRITE_COUNT> collideTable{};

  // Path transform state. Angles are bytes (256 steps per turn); scale is
  // the pixel size the whole normalized 0..1 range maps to.
  int rotXA = 0;
  int rotYA = 0;
  int rotZA = 0;
  int pathScale = 255;

  // Palette rotation: a manual offset stepped by commands, plus an
  // automatic frame-clocked rotation over [rotStart..rotEnd].
  int64_t rotOffset = 0;
  int rotSpeed = 0;  // frames per automatic step, 0 = off
  int rotDir = 1;    // 1 = left (toward lower indexes), -1 = right
  int rotStart = 0;
  int rotEnd = 255;

  // Text state. The overlay is a GPU-owned plane of character cells drawn on
  // top of the graphics. A cell char of 0 is empty and shows the graphics
  // through. One foreground and one background color serve all text.
  std::array<uint8_t, gpu::TEXT_CELLS> overlayChar{};
  int textCol = 0;
  int textRow = 0;
  int textColor = 0xff;  // foreground palette index, white-ish by default
  int textBg = 0;        // background palette index
  bool textOpaqueBg = false;

  // Video mode. Graphics is the power-on mode. Text mode shows a full screen
  // of characters read from a mapped region of data RAM.
  int videoMode = gpu::MODE_GRAPHICS;
  int textBase = 0;         // data RAM offset of the character buffer
  bool textMapped = false;  // set once CMD_SET_TEXTMODE has run

  // MODE_WORLD. World RAM holds the geometry and the CPU cannot address it,
  // the same as VRAM and the cartridge.
  std::vector<uint8_t> worldRam;

  // The active font, 256 glyphs of FONT_H bytes, one byte a row with bit 0 on
  // the left. It starts as a copy of the built-in font, and CMD_LOAD_FONT
  // replaces glyphs from the cartridge.
  std::array<uint8_t, 256 * FONT_H> fontRam{};

  // Bumped on every visible change, so the host can skip unchanged frames.
  int64_t version = 0;

  // Fast-frame mode: a debug switch that lives outside the machine, like a
  // forced-vblank pin. While on, a read of GPU_FRAME can act as if a frame
  // boundary just passed. Forced frames are rationed by wall clock
  // (fastFrameGapMs). A gap of 0 forces on every read, the tests' setting.
  bool fastFrame = false;
  double fastFrameGapMs = 120;
  std::function<double()> now;  // milliseconds; the wall clock by default

 private:
  // Argument readers, named for the SHAPE of the value, not for a register.
  // Multi-byte values go high byte first, because the CPU is big-endian.
  int u8(size_t i) const { return data[i]; }
  int u16(size_t i) const { return (data[i] << 8) | data[i + 1]; }
  int s16(size_t i) const;
  uint32_t cartAddr(size_t i) const;
  uint8_t cartByte(uint32_t addr) const;
  uint8_t ramByte(uint32_t at) const { return ram_ ? ram_[at & 0xffff] : 0; }

  void reseed();
  void seedFont();
  uint8_t glyphRowOf(int code, int row) const;
  uint8_t nextRandom();

  void command(uint8_t cmd);

  // Drawing, in gpu_draw.cpp.
  void plotAt(int px, int py, uint8_t c);
  void line(int x0, int y0, int x1, int y1, uint8_t c);
  void ringPoints(int dx, int dy);
  void ellipseOutline(int rx, int ry);
  void drawPath(uint32_t base, bool threeD);

  // Sprites and collision.
  void defineSprite(int id, int w, int h, int frames, uint32_t base, int group);
  void blit(std::span<uint8_t> dest, const Sprite& s, int atX, int atY) const;
  int firstHit(int i, int none) const;
  void buildGroupCache();
  int hitsForGroups(int mask);
  int hitInGroup(int i, int mask, int from) const;

  // Text, in gpu_text.cpp.
  void printf(uint32_t base, int argAddr);
  void putChar(uint8_t code);
  void textNewline();
  void scrollText();
  void drawGlyph(std::span<uint8_t> dest, int col, int row, int code, uint8_t fg, bool opaque) const;
  void drawOverlay(std::span<uint8_t> out) const;
  void composeText(std::span<uint8_t> out) const;
  void fillNoise(std::span<uint8_t> out) const;

  // MODE_WORLD, in gpu_world.cpp.
  void defineMesh(int id, uint32_t firstVertex, int vertexCount, uint32_t firstEdge, int edgeCount);
  int sceneU16(int at) const { return (ramByte(static_cast<uint32_t>(at)) << 8) | ramByte(static_cast<uint32_t>(at + 1)); }
  int sceneI16(int at) const;
  std::optional<Mat4> matrixFor(int id) const;
  std::optional<Vec3> objectOrigin(int rec) const;
  void drawObject(std::span<uint8_t> out, int rec, const Mat4& view, const Mat4& proj) const;
  void worldLine(std::span<uint8_t> out, int x0, int y0, Fx s0, Fx w0, int x1, int y1, Fx s1, Fx w1,
                 int ramp) const;
  void composeWorld(std::span<uint8_t> out) const;
  void worldRamp(int ramp, int r0, int g0, int b0);
  void meshLoad(int id, uint32_t src);
  uint32_t worldHash() const;

  std::function<uint64_t()> cycles_;
  std::function<uint32_t()> seed_;
  CartView cart_;
  uint8_t* ram_ = nullptr;

  // How many data ports the command just run left a RESULT in.
  //
  // DESIGN: this settles the question the design left open, which was what
  // MOD_STICKY does to a command that writes back onto the data ports. The
  // answer is that the two features do not compete, because a result is not
  // an argument. Arguments clear; results survive to be read.
  size_t resultBytes_ = 0;

  // The random generator, a 32-bit xorshift. Reading GPU_RAND advances it and
  // returns a byte. Writing GPU_RAND reseeds it.
  uint32_t rngState_ = 0;

  int64_t frameBoost_ = 0;
  double lastForcedAt_ = 0;  // minus infinity until the first forced frame

  // The group collision cache, keyed on `version`. See buildGroupCache.
  int64_t groupVersion_ = -1;
  std::array<uint8_t, gpu::SPRITE_COUNT> spriteHits_{};
  std::array<uint8_t, gpu::GROUP_COUNT> groupHits_{};

  // The mesh table. The TypeScript packed it into MESH_STRIDE bytes per entry;
  // a struct per entry reads the same and needs no byte decoding.
  std::array<MeshInfo, gpu::MESH_COUNT> meshTable_{};
  uint32_t worldTop_ = 0;  // next free byte of world RAM, for CMD_MESH_LOAD
  int sceneBase_ = 0;
  int sceneObjects_ = 0;
  bool sceneMapped_ = false;
  // The matrix table, also in data RAM, also nominated by a command.
  int matrixBase_ = 0;
  int matrixCount_ = 0;
  // DESIGN: WebGL clips z to -1..1 and WebGPU to 0..1. This selects which,
  // and it is the only thing that changes between them.
  int depthRange_ = DEPTH_GL;
};

}  // namespace sc8
