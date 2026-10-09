// The GPU's bus, its command dispatch, the palette, the sprites and the
// memory commands. Drawing primitives live in gpu_draw.cpp, text in
// gpu_text.cpp and MODE_WORLD in gpu_world.cpp.
#include "devices/gpu.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace sc8 {

using namespace sc8::gpu;

namespace gpu {

const std::vector<CmdArgs> CMD_ARGS = {
    {"CMD_CLEAR", {"GPU_COLOR"}},
    {"CMD_SET_COLOR", {"GPU_COLOR"}},
    {"CMD_MOVE_TO", {"GPU_X_HI", "GPU_X", "GPU_Y_HI", "GPU_Y"}},
    {"CMD_LINE_TO", {"GPU_X_HI", "GPU_X", "GPU_Y_HI", "GPU_Y"}},
    {"CMD_RECT", {"GPU_X_HI", "GPU_X", "GPU_Y_HI", "GPU_Y"}},
    {"CMD_PLOT", {"GPU_X_HI", "GPU_X", "GPU_Y_HI", "GPU_Y", "GPU_PIXEL"}},
    {"CMD_READ_PIXEL", {"GPU_X_HI", "GPU_X", "GPU_Y_HI", "GPU_Y"}},
    {"CMD_RESTORE_SCREEN", {"GPU_X_HI", "GPU_X", "GPU_Y_HI", "GPU_Y"}},
    {"CMD_CIRCLE", {"GPU_RADIUS"}},
    {"CMD_RING", {"GPU_RADIUS"}},
    {"CMD_SAVE_SCREEN", {}},
    {"CMD_RESET_PALETTE", {}},
    {"CMD_LOAD_PALETTE", {"GPU_CART_BANK", "GPU_CART_HI", "GPU_CART_LO"}},
    {"CMD_STORE_PALETTE", {}},
    {"CMD_FETCH_PALETTE", {}},
    {"CMD_ROTATE_LEFT", {}},
    {"CMD_ROTATE_RIGHT", {}},
    {"CMD_ROTATE_SPEED", {"GPU_SPEED"}},
    {"CMD_ROTATE_DIR", {"GPU_DIR"}},
    {"CMD_ROTATE_RANGE", {"GPU_FIRST", "GPU_LAST"}},
    {"CMD_COPY",
     {"GPU_CART_BANK", "GPU_CART_HI", "GPU_CART_LO", "GPU_DEST_HI", "GPU_DEST_LO", "GPU_LEN_HI", "GPU_LEN_LO"}},
    {"CMD_RAM_MOVE", {"GPU_FROM_HI", "GPU_FROM_LO", "GPU_DEST_HI", "GPU_DEST_LO", "GPU_LEN_HI", "GPU_LEN_LO"}},
    {"CMD_MEMMAP", {"GPU_MAP", "GPU_MAP_HI", "GPU_MAP_LO"}},
    {"CMD_SPRITE_DEF", {"GPU_SPRITE", "GPU_SRC_BANK", "GPU_SRC_HI", "GPU_SRC_LO", "GPU_SPRITE_GROUP"}},
    {"CMD_SPRITE_MOVE", {"GPU_SPRITE", "GPU_SPRITE_X_HI", "GPU_SPRITE_X", "GPU_SPRITE_Y_HI", "GPU_SPRITE_Y"}},
    {"CMD_STAMP", {"GPU_SPRITE", "GPU_SPRITE_X_HI", "GPU_SPRITE_X", "GPU_SPRITE_Y_HI", "GPU_SPRITE_Y"}},
    {"CMD_SPRITE_SHOW", {"GPU_SPRITE"}},
    {"CMD_SPRITE_HIDE", {"GPU_SPRITE"}},
    {"CMD_SPRITE_FRAME", {"GPU_SPRITE", "GPU_SPRITE_FRAME"}},
    {"CMD_SPRITE_FLIP", {"GPU_SPRITE", "GPU_SPRITE_FLIP"}},
    {"CMD_BLIT", {"GPU_CART_BANK", "GPU_CART_HI", "GPU_CART_LO"}},
    {"CMD_HIT_TEST", {"GPU_SPRITE", "GPU_SPRITE_B"}},
    {"CMD_HIT_SCAN", {"GPU_SPRITE"}},
    {"CMD_COLLIDE_ALL", {}},
    {"CMD_SPRITE_HITS", {"GPU_SPRITE"}},
    {"CMD_GROUP_HITS", {"GPU_GROUP"}},
    {"CMD_HIT_IN_GROUP", {"GPU_SPRITE", "GPU_GROUP_B", "GPU_HIT_FROM"}},
    {"CMD_COLLIDE_GROUP_ALL", {}},
    {"CMD_ROT_X", {"GPU_ANGLE"}},
    {"CMD_ROT_Y", {"GPU_ANGLE"}},
    {"CMD_ROT_Z", {"GPU_ANGLE"}},
    {"CMD_SET_SCALE", {"GPU_SCALE"}},
    {"CMD_DRAW_PATH", {"GPU_CART_BANK", "GPU_CART_HI", "GPU_CART_LO"}},
    {"CMD_DRAW_PATH3D", {"GPU_CART_BANK", "GPU_CART_HI", "GPU_CART_LO"}},
    {"CMD_SET_TEXTMODE", {"GPU_ADDR_HI", "GPU_ADDR_LO"}},
    {"CMD_SET_GRAPHICSMODE", {}},
    {"CMD_TEXT_STYLE", {"GPU_TEXT_COLOR", "GPU_TEXT_BG", "GPU_TEXT_FLAGS"}},
    {"CMD_TEXT_AT", {"GPU_TEXT_COL", "GPU_TEXT_ROW"}},
    {"CMD_TEXT_CHAR", {"GPU_TEXT_CHAR"}},
    {"CMD_TEXT_CLEAR", {}},
    {"CMD_PRINTF", {"GPU_CART_BANK", "GPU_CART_HI", "GPU_CART_LO", "GPU_TEXT_ARG_HI", "GPU_TEXT_ARG_LO"}},
    {"CMD_LOAD_FONT", {"GPU_CART_BANK", "GPU_CART_HI", "GPU_CART_LO"}},
    {"CMD_TEXT_CELL", {"GPU_CELL_W", "GPU_CELL_H"}},
    {"CMD_RESET_FONT", {}},
    {"CMD_SET_WORLDMODE", {"GPU_ADDR_HI", "GPU_ADDR_LO", "GPU_COUNT_HI", "GPU_COUNT_LO"}},
    {"CMD_MESH_LOAD", {"GPU_MESH", "GPU_SPRITE", "GPU_SRC_BANK", "GPU_SRC_HI", "GPU_SRC_LO"}},
    {"CMD_MESH_WRITE", {"GPU_MESH_BYTE"}},
    {"CMD_WORLD_RAMP", {"GPU_RAMP", "GPU_RED", "GPU_GREEN", "GPU_BLUE"}},
    {"CMD_MATRIX_MAP", {"GPU_ADDR_HI", "GPU_ADDR_LO", "GPU_COUNT_HI", "GPU_COUNT_LO"}},
};

}  // namespace gpu

namespace {

// The stream a Gpu uses when nothing injects a seed. Fixed on purpose: it is
// what makes a headless test reproducible.
constexpr uint32_t RNG_DEFAULT_SEED = 0x1d872b41;

constexpr size_t PIXELS = static_cast<size_t>(SCREEN_W) * SCREEN_H;

// Axis-aligned overlap of two placed sprites. The collision test.
bool overlap(const Sprite& a, const Sprite& b) {
  return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

double wallClockMs() {
  using namespace std::chrono;
  return static_cast<double>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

}  // namespace

Palette default332() {
  Palette pal{};
  for (int i = 0; i < 256; i++) {
    const int r = (i >> 5) & 7;
    const int g = (i >> 2) & 7;
    const int b = i & 3;
    const size_t o = static_cast<size_t>(i) * 3;
    pal[o] = static_cast<uint8_t>(std::lround((r * 255) / 7.0));
    pal[o + 1] = static_cast<uint8_t>(std::lround((g * 255) / 7.0));
    pal[o + 2] = static_cast<uint8_t>(std::lround((b * 255) / 3.0));
  }
  return pal;
}

Gpu::Gpu(std::function<uint64_t()> cycles, IoBus* fallback, std::function<uint32_t()> seed)
    : ChainedDevice(fallback),
      vram(PIXELS, 0),
      palette(default332()),
      backbuffer(PIXELS, 0),
      worldRam(static_cast<size_t>(WORLD_RAM_SIZE), 0),
      now(wallClockMs),
      cycles_(std::move(cycles)),
      seed_(std::move(seed)),
      lastForcedAt_(-std::numeric_limits<double>::infinity()) {
  seedFont();
  reseed();
}

// DESIGN: a pure function of the injected seed, so the same source always
// gives the same stream. Making successive runs differ belongs to whoever
// supplies the seed, not here.
void Gpu::reseed() {
  const uint32_t s = seed_ ? seed_() : RNG_DEFAULT_SEED;
  rngState_ = s == 0 ? RNG_DEFAULT_SEED : s;  // an xorshift at 0 stays 0
}

// Advance the xorshift and return its high byte, the best-mixed one.
uint8_t Gpu::nextRandom() {
  uint32_t x = rngState_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rngState_ = x;
  return static_cast<uint8_t>(x >> 24);
}

int64_t Gpu::frame() const { return static_cast<int64_t>(cycles_() / CYCLES_PER_FRAME) + frameBoost_; }

int Gpu::s16(size_t i) const {
  const int v = u16(i);
  return v >= 0x8000 ? v - 0x10000 : v;
}

uint32_t Gpu::cartAddr(size_t i) const {
  return (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
}

uint8_t Gpu::cartByte(uint32_t addr) const { return addr < cart_.size() ? cart_[addr] : 0; }

void Gpu::powerOn() {
  std::fill(vram.begin(), vram.end(), uint8_t{0});
  std::fill(backbuffer.begin(), backbuffer.end(), uint8_t{0});
  palette = default332();
  data.fill(0);
  cmdMod = 0;
  resultBytes_ = 0;
  color = 0;
  scrollX = 0;
  scrollY = 0;
  penX = 0;
  penY = 0;
  mapPaletteIn = -1;
  mapPaletteOut = -1;
  mapCollide = -1;
  mapGroups = -1;
  for (auto& s : sprites) s.reset();
  hitResult = 0;
  collideTable.fill(0);
  rotXA = 0;
  rotYA = 0;
  rotZA = 0;
  pathScale = 255;
  rotOffset = 0;
  rotSpeed = 0;
  rotDir = 1;
  rotStart = 0;
  rotEnd = 255;
  // The boost resets with the device; the fastFrame switch does not,
  // because it belongs to the person at the controls, not the machine.
  frameBoost_ = 0;
  lastForcedAt_ = -std::numeric_limits<double>::infinity();
  overlayChar.fill(0);
  textCellW = FONT_W;
  textCellH = FONT_H;
  textCol = 0;
  textRow = 0;
  textColor = 0xff;
  textBg = 0;
  textOpaqueBg = false;
  videoMode = MODE_GRAPHICS;
  textBase = 0;
  textMapped = false;
  std::fill(worldRam.begin(), worldRam.end(), uint8_t{0});
  meshTable_.fill(MeshInfo{});
  worldTop_ = 0;
  sceneBase_ = 0;
  sceneObjects_ = 0;
  sceneMapped_ = false;
  matrixBase_ = 0;
  matrixCount_ = 0;
  reseed();
  seedFont();
  version++;
}

// ------------------------------------------------------------------ bus

void Gpu::write(uint8_t port, uint8_t value) {
  if (port < PORT_LO || port > PORT_HI) {
    fallback_->write(port, value);
    return;
  }
  if (port >= GPU_DATA0 && port <= GPU_DATA6) {
    data[static_cast<size_t>(port - GPU_DATA0)] = value;
    return;
  }
  switch (port) {
    case GPU_CMD_MOD:
      cmdMod = value;
      break;
    case GPU_CMD:
      resultBytes_ = 0;
      command(value);
      // DESIGN: the data ports clear after every command unless MOD_STICKY
      // says otherwise. The old machine kept sticky high bytes on the
      // coordinate latches, and forgetting to clear GPU_X_HI was the bug
      // the knowledge base called the classic one. Clearing by default
      // makes it unreachable. See docs/design/port-redesign.md.
      if ((cmdMod & MOD_STICKY) == 0) {
        // The arguments go. Anything the command left as a result stays,
        // or reading it back would be impossible.
        std::fill(data.begin() + static_cast<std::ptrdiff_t>(std::min(resultBytes_, data.size())), data.end(), uint8_t{0});
      }
      // MOD_INC0 walks an indexed resource. DATA0 is the index on every
      // command that takes one, so a sprite or palette walk sets it once.
      if ((cmdMod & MOD_INC0) != 0) data[0] = static_cast<uint8_t>(data[0] + 1);
      break;
    case GPU_RAND: {
      // Reseed from the byte through a hash, so one write gives a usable
      // seed. The state never becomes zero, which would freeze the xorshift.
      const uint32_t h = (static_cast<uint32_t>(value) ^ 0x9e3779b9u) * 0x85ebca6bu;
      rngState_ = h == 0 ? RNG_DEFAULT_SEED : h;
      break;
    }
    default:
      break;
  }
}

uint8_t Gpu::read(uint8_t port) {
  if (port < PORT_LO || port > PORT_HI) return fallback_->read(port);
  if (port >= GPU_DATA0 && port <= GPU_DATA6) {
    // A command that produces a small answer leaves it on a data port.
    // Reading one a command did not write gives the argument back.
    return data[static_cast<size_t>(port - GPU_DATA0)];
  }
  switch (port) {
    case GPU_FRAME:
      // Fast-frame: the act of asking advances the clock, rationed to
      // the device rate. Any compare idiom sees the fresh frame.
      if (fastFrame && now() - lastForcedAt_ >= fastFrameGapMs) {
        frameBoost_++;
        version++;
        lastForcedAt_ = now();
      }
      return static_cast<uint8_t>(frame() & 0xff);
    case GPU_RAND:
      return nextRandom();
    case GPU_TEXT_COLS:
      return static_cast<uint8_t>(textCols());
    case GPU_TEXT_ROWS:
      return static_cast<uint8_t>(textRows());
    case GPU_CMD_MOD:
      return static_cast<uint8_t>(cmdMod & 0xff);
    default:
      return 0;
  }
}

// ------------------------------------------------------------- commands

void Gpu::command(uint8_t cmd) {
  switch (cmd) {
    // ---- screen and pen ----
    case CMD_CLEAR:
      // Its own colour, so clearing does not disturb the pen's.
      std::fill(vram.begin(), vram.end(), static_cast<uint8_t>(u8(0)));
      version++;
      break;
    case CMD_SET_COLOR:
      color = u8(0);
      break;
    case CMD_MOVE_TO:
      penX = s16(0);
      penY = s16(2);
      break;
    case CMD_LINE_TO: {
      const int tx = s16(0);
      const int ty = s16(2);
      line(penX, penY, tx, ty, static_cast<uint8_t>(color));
      penX = tx;
      penY = ty;
      version++;
      break;
    }
    case CMD_RECT: {
      const int ax = s16(0);
      const int ay = s16(2);
      const int x0 = std::min(penX, ax);
      const int x1 = std::max(penX, ax);
      const int y0 = std::min(penY, ay);
      const int y1 = std::max(penY, ay);
      for (int py = std::max(y0, 0); py <= std::min(y1, SCREEN_H - 1); py++) {
        for (int px = std::max(x0, 0); px <= std::min(x1, SCREEN_W - 1); px++) {
          vram[static_cast<size_t>(py * SCREEN_W + px)] = static_cast<uint8_t>(color);
        }
      }
      version++;
      break;
    }
    case CMD_CIRCLE: {
      // A second radius on GPU_RADIUS_Y makes an ellipse. Zero, the value
      // a cleared port holds, keeps the circle round, so a program written
      // before ellipses draws what it drew.
      const int rx = u8(0);
      const int ry = u8(1) == 0 ? rx : u8(1);
      for (int dy = -ry; dy <= ry; dy++) {
        // Integer square root, so edges never depend on float rounding.
        // half = rx * sqrt(ry^2 - dy^2) / ry, in 64 bit so 255^4 fits.
        const int64_t n = ry == 0 ? 0 : (int64_t{rx} * rx * (int64_t{ry} * ry - int64_t{dy} * dy)) / (int64_t{ry} * ry);
        int half = static_cast<int>(std::sqrt(static_cast<double>(n)));
        while (int64_t{half + 1} * (half + 1) <= n) half++;
        while (int64_t{half} * half > n) half--;
        const int py = penY + dy;
        if (py < 0 || py >= SCREEN_H) continue;
        const int x0 = std::max(penX - half, 0);
        const int x1 = std::min(penX + half, SCREEN_W - 1);
        for (int px = x0; px <= x1; px++) vram[static_cast<size_t>(py * SCREEN_W + px)] = static_cast<uint8_t>(color);
      }
      version++;
      break;
    }
    case CMD_RING: {
      const int rx = u8(0);
      const int ry = u8(1);
      if (ry != 0 && ry != rx) {
        ellipseOutline(rx, ry);
        version++;
        break;
      }
      // Midpoint circle: the outline only.
      const int r = rx;
      int dx = r;
      int dy = 0;
      int err = 1 - r;
      while (dx >= dy) {
        ringPoints(dx, dy);
        dy++;
        if (err < 0) {
          err += 2 * dy + 1;
        } else {
          dx--;
          err += 2 * (dy - dx) + 1;
        }
      }
      version++;
      break;
    }
    case CMD_PLOT:
      plotAt(s16(0), s16(2), static_cast<uint8_t>(u8(4)));
      version++;
      break;
    case CMD_READ_PIXEL: {
      const int px = s16(0);
      const int py = s16(2);
      const bool on = px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H;
      // The answer goes back on DATA0, and it exists because this command
      // produced it. That is the rule the old read ports broke.
      data[0] = on ? vram[static_cast<size_t>(py * SCREEN_W + px)] : 0;
      resultBytes_ = 1;
      break;
    }
    case CMD_SAVE_SCREEN:
      backbuffer = vram;
      break;
    case CMD_RESTORE_SCREEN: {
      // Paste the saved screen shifted by (x, y), clipped at the edges.
      const int ox = s16(0);
      const int oy = s16(2);
      for (int py = 0; py < SCREEN_H; py++) {
        const int ty = py + oy;
        if (ty < 0 || ty >= SCREEN_H) continue;
        for (int px = 0; px < SCREEN_W; px++) {
          const int tx = px + ox;
          if (tx < 0 || tx >= SCREEN_W) continue;
          vram[static_cast<size_t>(ty * SCREEN_W + tx)] = backbuffer[static_cast<size_t>(py * SCREEN_W + px)];
        }
      }
      version++;
      break;
    }
    // ---- palette ----
    case CMD_RESET_PALETTE:
      palette = default332();
      version++;
      break;
    case CMD_LOAD_PALETTE: {
      // The blob leads with its entry count, so a partial palette is a
      // shorter blob rather than a length typed into the source. A byte
      // cannot say 256, so zero does.
      const uint32_t base = cartAddr(0);
      const int n = cartByte(base);
      const int count = n == 0 ? 256 : n;
      for (int i = 0; i < count * 3; i++) {
        palette[static_cast<size_t>(i)] = cartByte(base + 1 + static_cast<uint32_t>(i));
      }
      version++;
      break;
    }
    case CMD_STORE_PALETTE: {
      if (!ram_ || mapPaletteOut < 0) break;
      for (size_t i = 0; i < 768; i++) ram_[(static_cast<size_t>(mapPaletteOut) + i) & 0xffff] = palette[i];
      break;
    }
    case CMD_FETCH_PALETTE: {
      if (!ram_ || mapPaletteIn < 0) break;
      for (size_t i = 0; i < 768; i++) palette[i] = ram_[(static_cast<size_t>(mapPaletteIn) + i) & 0xffff];
      version++;
      break;
    }
    case CMD_ROTATE_LEFT:
      rotOffset++;
      version++;
      break;
    case CMD_ROTATE_RIGHT:
      rotOffset--;
      version++;
      break;
    case CMD_ROTATE_SPEED:
      rotSpeed = u8(0);
      version++;
      break;
    case CMD_ROTATE_DIR:
      rotDir = u8(0) == 0 ? 1 : -1;
      version++;
      break;
    case CMD_ROTATE_RANGE:
      // One command where there were two. The range is a pair, and setting
      // half of it was never useful on its own.
      rotStart = u8(0);
      rotEnd = u8(1);
      version++;
      break;
    // ---- memory ----
    case CMD_COPY: {
      // DESIGN: the only route cartridge data has into RAM. The CPU cannot
      // read the cartridge, so it names the addresses and the GPU moves the
      // bytes. One cycle, like every command: that is the magic box.
      if (!ram_) break;
      const uint32_t src = cartAddr(0);
      const uint32_t dst = static_cast<uint32_t>(u16(3));
      const uint32_t len = static_cast<uint32_t>(u16(5));
      // The destination wraps at 16 bits, because no data address is
      // illegal. cartByte reads zero past the end of the cartridge.
      for (uint32_t i = 0; i < len; i++) ram_[(dst + i) & 0xffff] = cartByte(src + i);
      version++;
      break;
    }
    case CMD_RAM_MOVE: {
      // DESIGN: a MOVE and not a copy. The regions may overlap, and the case
      // that asked for this command is a collector compacting a heap down
      // over itself. So the direction of the loop is chosen from the
      // addresses, the way memmove does it.
      //
      // Both ends wrap at 16 bits, so the direction cannot be picked by
      // asking which address is the larger number. A block that wraps has
      // its destination BELOW its source while spelling a higher address.
      // The distance from the source to the destination, measured the way
      // the block itself wraps, is the question: a destination less than a
      // length ahead sits inside the source, and only that case needs the
      // backward loop.
      if (!ram_) break;
      const uint32_t from = static_cast<uint32_t>(u16(1));
      const uint32_t to = static_cast<uint32_t>(u16(3));
      const uint32_t len = static_cast<uint32_t>(u16(5));
      const uint32_t ahead = (to - from) & 0xffff;
      if (ahead != 0 && ahead < len) {
        for (uint32_t i = len; i-- > 0;) ram_[(to + i) & 0xffff] = ram_[(from + i) & 0xffff];
      } else {
        for (uint32_t i = 0; i < len; i++) ram_[(to + i) & 0xffff] = ram_[(from + i) & 0xffff];
      }
      version++;
      break;
    }
    case CMD_MEMMAP: {
      // Where a transfer reads from or writes to. Nothing moves until a
      // command asks, which is what separates these from the framebuffer
      // modes the display reads every frame.
      const int at = u16(1);
      switch (u8(0)) {
        case MAP_PALETTE_IN: mapPaletteIn = at; break;
        case MAP_PALETTE_OUT: mapPaletteOut = at; break;
        case MAP_PALETTE:
          mapPaletteIn = at;
          mapPaletteOut = at;
          break;
        case MAP_COLLIDE: mapCollide = at; break;
        case MAP_GROUPS: mapGroups = at; break;
        default: break;
      }
      break;
    }
    // ---- sprites ----
    case CMD_SPRITE_DEF: {
      // The blob leads with its frame count, width and height, so a
      // one-frame blob is a plain sprite and a longer one is a strip.
      const int id = u8(0);
      const uint32_t base = cartAddr(1);
      const int frames = std::min(std::max<int>(cartByte(base), 1), SPRITE_FRAMES_MAX);
      const int w = std::min(std::max<int>(cartByte(base + 1), 1), SPRITE_MAX);
      const int h = std::min(std::max<int>(cartByte(base + 2), 1), SPRITE_MAX);
      defineSprite(id, w, h, frames, base + 3, u8(4));
      break;
    }
    case CMD_SPRITE_MOVE: {
      auto& s = sprites[static_cast<size_t>(u8(0))];
      if (!s) break;
      s->x = s16(1);
      s->y = s16(3);
      if (s->visible) version++;
      break;
    }
    case CMD_SPRITE_SHOW: {
      auto& s = sprites[static_cast<size_t>(u8(0))];
      if (s && !s->visible) {
        s->visible = true;
        version++;
      }
      break;
    }
    case CMD_SPRITE_HIDE: {
      auto& s = sprites[static_cast<size_t>(u8(0))];
      if (s && s->visible) {
        s->visible = false;
        version++;
      }
      break;
    }
    case CMD_SPRITE_FRAME: {
      auto& s = sprites[static_cast<size_t>(u8(0))];
      if (s && s->frames > 0) {
        const int f = u8(1) % s->frames;
        if (f != s->frame) {
          s->frame = f;
          if (s->visible) version++;
        }
      }
      break;
    }
    case CMD_SPRITE_FLIP: {
      // DESIGN: flips, and no rotation. A quarter turn suits a radially
      // symmetric sprite and breaks any sprite with a fixed bottom: rotate
      // a ghost and its skirt ends up on the side. Every 2D console of the
      // era carries flip bits and almost none carry rotation.
      auto& s = sprites[static_cast<size_t>(u8(0))];
      if (!s) break;
      const int bits = u8(1);
      s->flipH = (bits & 1) != 0;
      s->flipV = (bits & 2) != 0;
      if (s->visible) version++;
      break;
    }
    case CMD_STAMP: {
      // Bake the sprite into vram at (x, y): tiles, backgrounds, decor.
      const auto& s = sprites[static_cast<size_t>(u8(0))];
      if (!s) break;
      blit(vram, *s, s16(1), s16(3));
      version++;
      break;
    }
    case CMD_BLIT: {
      // A cartridge image drawn at the pen, with no sprite slot spent on
      // it. The blob leads with its width and height, so the command takes
      // only a pointer. This is the background case, which had no command.
      const uint32_t base = cartAddr(0);
      const int w = cartByte(base);
      const int h = cartByte(base + 1);
      const int wide = w == 0 ? 256 : w;
      const int high = h == 0 ? 256 : h;
      for (int py = 0; py < high; py++) {
        for (int px = 0; px < wide; px++) {
          const uint8_t c = cartByte(base + 2 + static_cast<uint32_t>(py * wide + px));
          if (c != 0) plotAt(penX + px, penY + py, c);
        }
      }
      version++;
      break;
    }
    // ---- collision ----
    case CMD_HIT_TEST: {
      const auto& a = sprites[static_cast<size_t>(u8(0))];
      const auto& b = sprites[static_cast<size_t>(u8(1))];
      data[0] = a && b && a->visible && b->visible && overlap(*a, *b) ? 1 : 0;
      resultBytes_ = 1;
      break;
    }
    case CMD_HIT_SCAN:
      // The lowest visible sprite the named one overlaps, else 255.
      data[0] = static_cast<uint8_t>(firstHit(u8(0), 0xff));
      resultBytes_ = 1;
      break;
    case CMD_SPRITE_HITS:
      buildGroupCache();
      data[0] = spriteHits_[static_cast<size_t>(u8(0))];
      resultBytes_ = 1;
      break;
    case CMD_GROUP_HITS:
      data[0] = static_cast<uint8_t>(hitsForGroups(u8(0)));
      resultBytes_ = 1;
      break;
    case CMD_HIT_IN_GROUP:
      data[0] = static_cast<uint8_t>(hitInGroup(u8(0), u8(1), u8(2)));
      resultBytes_ = 1;
      break;
    case CMD_COLLIDE_GROUP_ALL: {
      // The bulk form, for a game that asks about many sprites a frame. One
      // command and a load each, against one command each.
      if (!ram_ || mapGroups < 0) break;
      buildGroupCache();
      for (size_t i = 0; i < SPRITE_COUNT; i++) ram_[(static_cast<size_t>(mapGroups) + i) & 0xffff] = spriteHits_[i];
      break;
    }
    case CMD_COLLIDE_ALL: {
      // Every sprite's overlap partner at once. A table is 256 bytes, so it
      // goes to RAM rather than a byte at a time through a port. Zero means
      // no collision, so a program that needs that leaves sprite 0 unused.
      if (!ram_ || mapCollide < 0) break;
      for (int i = 0; i < SPRITE_COUNT; i++) {
        const auto& s = sprites[static_cast<size_t>(i)];
        ram_[(static_cast<size_t>(mapCollide + i)) & 0xffff] =
            s && s->visible ? static_cast<uint8_t>(firstHit(i, 0)) : 0;
      }
      break;
    }
    // ---- paths and the transform chain ----
    case CMD_ROT_X: rotXA = u8(0); break;
    case CMD_ROT_Y: rotYA = u8(0); break;
    case CMD_ROT_Z: rotZA = u8(0); break;
    case CMD_SET_SCALE: pathScale = u8(0); break;
    case CMD_DRAW_PATH:
      drawPath(cartAddr(0), false);
      break;
    case CMD_DRAW_PATH3D:
      drawPath(cartAddr(0), true);
      break;
    // ---- text ----
    case CMD_SET_TEXTMODE:
      // One command sets the mode and the framebuffer. Text mode with no
      // buffer mapped is then unreachable.
      textBase = u16(0);
      textMapped = true;
      videoMode = MODE_TEXT;
      version++;
      break;
    case CMD_SET_GRAPHICSMODE:
      videoMode = MODE_GRAPHICS;
      version++;
      break;
    case CMD_TEXT_STYLE:
      textColor = u8(0);
      textBg = u8(1);
      textOpaqueBg = (u8(2) & 1) == 1;
      version++;
      break;
    case CMD_TEXT_AT:
      textCol = u8(0) % textCols();
      textRow = u8(1) % textRows();
      break;
    case CMD_TEXT_CHAR:
      putChar(static_cast<uint8_t>(u8(0)));
      version++;
      break;
    case CMD_TEXT_CLEAR:
      overlayChar.fill(0);
      textCol = 0;
      textRow = 0;
      version++;
      break;
    case CMD_PRINTF:
      // The template is in ROM and the arguments are in RAM. Pushing them
      // a byte at a time through a port bounded their number by patience.
      printf(cartAddr(0), u16(3));
      version++;
      break;
    case CMD_LOAD_FONT: {
      // The blob is the cell's width and height, then 256 glyphs of 8
      // bytes, code 0 first. A bad cell changes nothing, glyphs included.
      const uint32_t base = cartAddr(0);
      if (!setCell(cartByte(base), cartByte(base + 1))) break;
      for (size_t i = 0; i < fontRam.size(); i++) fontRam[i] = cartByte(base + 2 + static_cast<uint32_t>(i));
      version++;
      break;
    }
    case CMD_TEXT_CELL:
      if (setCell(u8(0), u8(1))) version++;
      break;
    case CMD_RESET_FONT:
      // What power on does to the font and the cell, as one command.
      seedFont();
      setCell(FONT_W, FONT_H);
      version++;
      break;
    // ---- the 3D world ----
    case CMD_SET_WORLDMODE:
      // The scene is a framebuffer: the GPU reads it every frame, the way
      // the display reads the text buffer. So the mode command points at it.
      sceneBase_ = u16(0);
      sceneObjects_ = u16(2);
      sceneMapped_ = true;
      videoMode = MODE_WORLD;
      version++;
      break;
    case CMD_MESH_LOAD:
      // DESIGN: no range check on the id. The id is a byte and the table
      // has exactly 256 entries, so every value a program can write is a
      // valid mesh. A guard here would be code no test could reach.
      meshLoad(u8(0), cartAddr(1));
      break;
    case CMD_MESH_WRITE:
      // One byte at a time, for geometry a program builds at run time.
      if (worldTop_ < static_cast<uint32_t>(WORLD_RAM_SIZE)) {
        worldRam[worldTop_] = static_cast<uint8_t>(u8(0));
        worldTop_++;
        version++;
      }
      break;
    case CMD_WORLD_RAMP:
      worldRamp(u8(0) & 0x0f, u8(1), u8(2), u8(3));
      break;
    case CMD_MATRIX_MAP:
      // Read during a frame rather than defining one, and a program may
      // leave it unmapped, so it keeps its own command. A count of zero
      // unmaps the table.
      matrixBase_ = u16(0);
      matrixCount_ = u16(2);
      version++;
      break;
    default:
      break;  // unknown command: nothing happens
  }
}

// -------------------------------------------------------------- sprites

// Load a sprite from the cartridge, keeping its place and visibility.
// frames pixels of size w by h follow the base address, back to back.
void Gpu::defineSprite(int id, int w, int h, int frames, uint32_t base, int group) {
  Sprite s;
  s.w = w;
  s.h = h;
  s.frames = frames;
  s.frame = 0;
  s.data.resize(static_cast<size_t>(frames * w * h));
  for (size_t i = 0; i < s.data.size(); i++) s.data[i] = cartByte(base + static_cast<uint32_t>(i));
  auto& slot = sprites[static_cast<size_t>(id)];
  const bool wasVisible = slot && slot->visible;
  if (slot) {
    s.x = slot->x;
    s.y = slot->y;
    s.visible = slot->visible;
    s.flipH = slot->flipH;
    s.flipV = slot->flipV;
  }
  // DESIGN: the mask is NOT carried over from the previous sprite. A
  // redefine is how a sprite changes groups, which is the owner's decision,
  // so the byte on the command is the whole answer and an omitted byte means
  // no groups.
  s.group = group;
  slot = std::move(s);
  if (wasVisible) version++;
}

// Draw a sprite's current frame into a 256x256 buffer: index 0 is
// transparent, edges clip, coordinates are signed.
void Gpu::blit(std::span<uint8_t> dest, const Sprite& s, int atX, int atY) const {
  const int fo = s.frame * s.w * s.h;
  for (int sy = 0; sy < s.h; sy++) {
    const int py = atY + sy;
    if (py < 0 || py >= SCREEN_H) continue;
    for (int sx = 0; sx < s.w; sx++) {
      const int px = atX + sx;
      if (px < 0 || px >= SCREEN_W) continue;
      // The flip reads the mirrored source pixel. The destination walk is
      // unchanged, so a flip costs nothing beyond the blit already done.
      const int rx = s.flipH ? s.w - 1 - sx : sx;
      const int ry = s.flipV ? s.h - 1 - sy : sy;
      const uint8_t c = s.data[static_cast<size_t>(fo + ry * s.w + rx)];
      if (c == 0) continue;  // transparent
      dest[static_cast<size_t>(py * SCREEN_W + px)] = c;
    }
  }
}

// The lowest visible sprite that overlaps sprite i, or none if there is
// no such sprite. i itself and hidden sprites never count.
int Gpu::firstHit(int i, int none) const {
  const auto& a = sprites[static_cast<size_t>(i)];
  if (!a || !a->visible) return none;
  for (int j = 0; j < SPRITE_COUNT; j++) {
    if (j == i) continue;
    const auto& b = sprites[static_cast<size_t>(j)];
    if (b && b->visible && overlap(*a, *b)) return j;
  }
  return none;
}

// ---- the group collision cache ----
//
// A group question means testing every visible pair, and a game asks three
// or four of them a frame. So the pass runs once and the answers are kept
// until something moves.
//
// DESIGN: keyed on `version`, the counter every mutation already bumps.
// Four of those bumps are guarded by `s.visible`, so moving a HIDDEN sprite
// does not invalidate the cache. That is exactly right rather than a hole:
// a hidden sprite collides with nothing, so a move that skips the bump
// cannot change an answer. Showing it bumps, and that is the moment it
// starts to matter. sprite_groups_test.cpp walks that sequence.
void Gpu::buildGroupCache() {
  if (groupVersion_ == version) return;
  groupVersion_ = version;
  spriteHits_.fill(0);
  groupHits_.fill(0);
  // Each pair once, and each side told about the other's groups. An
  // ungrouped sprite contributes a mask of zero, so it is skipped by the
  // arithmetic rather than by a test for it.
  for (size_t i = 0; i < SPRITE_COUNT; i++) {
    const auto& a = sprites[i];
    if (!a || !a->visible) continue;
    for (size_t j = i + 1; j < SPRITE_COUNT; j++) {
      const auto& b = sprites[j];
      if (!b || !b->visible || !overlap(*a, *b)) continue;
      spriteHits_[i] |= static_cast<uint8_t>(b->group);
      spriteHits_[j] |= static_cast<uint8_t>(a->group);
    }
  }
  // A group touches whatever its members touch. A group whose two members
  // overlap each other therefore reports itself, which is the owner's rule.
  for (size_t i = 0; i < SPRITE_COUNT; i++) {
    const auto& a = sprites[i];
    if (!a || !a->visible || a->group == 0) continue;
    const uint8_t hits = spriteHits_[i];
    for (size_t g = 0; g < GROUP_COUNT; g++) {
      if (a->group & (1 << g)) groupHits_[g] |= hits;
    }
  }
}

// The groups a mask of groups touches: the union over the groups asked for.
int Gpu::hitsForGroups(int mask) {
  buildGroupCache();
  int out = 0;
  for (size_t g = 0; g < GROUP_COUNT; g++) {
    if (mask & (1 << g)) out |= groupHits_[g];
  }
  return out;
}

// The first sprite at or after `from` that is in `mask` and overlaps `i`.
// Zero when there is none, so a program walks by feeding back the last
// answer plus one.
int Gpu::hitInGroup(int i, int mask, int from) const {
  const auto& a = sprites[static_cast<size_t>(i)];
  if (!a || !a->visible || mask == 0) return 0;
  for (int j = std::max(0, from); j < SPRITE_COUNT; j++) {
    if (j == i) continue;
    const auto& b = sprites[static_cast<size_t>(j)];
    if (b && b->visible && (b->group & mask) != 0 && overlap(*a, *b)) return j;
  }
  return 0;
}

// -------------------------------------------------------------- compose

Gpu::Frame Gpu::composeFrame() const {
  Frame out(PIXELS, 0);
  if (videoMode == MODE_WORLD) {
    composeWorld(out);
    drawOverlay(out);
    return out;
  }
  if (videoMode == MODE_TEXT) {
    composeText(out);
    return out;
  }
  // The background shifted by the scroll registers, with wrap-around like
  // real scroll hardware, then the visible sprites on top in id order.
  for (int y = 0; y < SCREEN_H; y++) {
    const size_t srcRow = static_cast<size_t>((y + scrollY) & 255) * SCREEN_W;
    const size_t dstRow = static_cast<size_t>(y) * SCREEN_W;
    for (int x = 0; x < SCREEN_W; x++) {
      out[dstRow + static_cast<size_t>(x)] = vram[srcRow + static_cast<size_t>((x + scrollX) & 255)];
    }
  }
  for (const auto& s : sprites) {
    if (s && s->visible) blit(out, *s, s->x, s->y);
  }
  drawOverlay(out);
  return out;
}

void Gpu::compose(std::span<uint8_t> rgba) const {
  const Frame px = composeFrame();
  const Palette pal = effectivePalette();
  const size_t n = std::min(px.size(), rgba.size() / 4);
  for (size_t i = 0; i < n; i++) {
    const size_t c = static_cast<size_t>(px[i]) * 3;
    rgba[i * 4] = pal[c];
    rgba[i * 4 + 1] = pal[c + 1];
    rgba[i * 4 + 2] = pal[c + 2];
    rgba[i * 4 + 3] = 255;
  }
}

int64_t Gpu::rotationSteps() const {
  // JavaScript's Math.floor on a positive frame count. The frame counter
  // never goes negative, so plain division floors it too.
  const int64_t automatic = rotSpeed > 0 ? (frame() / rotSpeed) * rotDir : 0;
  return rotOffset + automatic;
}

Palette Gpu::effectivePalette() const {
  const int64_t steps = rotationSteps();
  const int lo = std::min(rotStart, rotEnd);
  const int hi = std::max(rotStart, rotEnd);
  const int len = hi - lo + 1;
  if (steps == 0 || len <= 1) return palette;
  Palette out = palette;
  const int shift = static_cast<int>(((steps % len) + len) % len);
  for (int i = 0; i < len; i++) {
    // Entry i shows what entry i+shift held: a left rotation.
    const size_t src = static_cast<size_t>(lo + ((i + shift) % len)) * 3;
    const size_t dst = static_cast<size_t>(lo + i) * 3;
    out[dst] = palette[src];
    out[dst + 1] = palette[src + 1];
    out[dst + 2] = palette[src + 2];
  }
  return out;
}

std::string Gpu::displayKey() const {
  std::string textSig;
  if (videoMode == MODE_WORLD) {
    // The scene lives in data RAM, which the GPU never sees written, so
    // fold it into the key. A CPU store then refreshes the screen, exactly
    // as it does for text mode.
    if (!sceneMapped_ || !ram_) return std::to_string(version) + ":worldnoise" + std::to_string(frame());
    return std::to_string(version) + ":" + std::to_string(rotationSteps()) + ":world" + std::to_string(worldHash());
  }
  if (videoMode == MODE_TEXT) {
    if (!textMapped || !ram_) {
      // Snow churns every frame, so key on the frame counter.
      textSig = ":noise" + std::to_string(frame());
    } else {
      // Text mode reads data RAM the GPU never sees written, so fold the
      // mapped bytes into the key. Wrap, do not clamp: the CPU wraps at 16
      // bits, so a wrapped screen must fold in the wrapped bytes too.
      uint32_t h = 2166136261u;
      // The cell decides how many bytes the screen is, so it keys too.
      h ^= static_cast<uint32_t>(textCellW * 16 + textCellH);
      h *= 16777619u;
      for (int a = textBase; a < textBase + textCols() * textRows(); a++) {
        h ^= ram_[static_cast<size_t>(a) & 0xffff];
        h *= 16777619u;
      }
      textSig = ":" + std::to_string(h);
    }
  }
  return std::to_string(version) + ":" + std::to_string(rotationSteps()) + textSig;
}

}  // namespace sc8
