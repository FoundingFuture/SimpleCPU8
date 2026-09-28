// A sprite strip: the frames of one animated sprite, side by side in one
// PNG. The IDE's sprite editor works on this model, and the drawing tools
// are plain functions over it, so the tests reach them without a window.
//
// A pixel is a palette index. Index 0 is transparent, as the GPU treats it
// when it draws a sprite. The strip is saved as an indexed PNG with the
// palette the editor drew with, index 0 fully transparent and the rest
// opaque. Two text chunks carry what a plain picture cannot say: the frame
// count and the preview's frames per second. `__sprite("ship.png")` reads
// the frame count from the first.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "devices/gpu.h"

namespace sc8::sprite {

inline constexpr int MAX_SIDE = gpu::SPRITE_MAX;
inline constexpr int MAX_FRAMES = gpu::SPRITE_FRAMES_MAX;
inline constexpr int DEFAULT_FPS = 8;

// The PNG text chunk keywords.
inline constexpr const char* FRAMES_KEY = "SimpleCPU-8 frames";
inline constexpr const char* FPS_KEY = "SimpleCPU-8 fps";

// One frame: width * height indexes, row-major.
struct Frame {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> px;

  Frame() = default;
  Frame(int w, int h) : width(w), height(h), px(static_cast<size_t>(w * h), 0) {}
  bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
  uint8_t at(int x, int y) const { return px[static_cast<size_t>(y * width + x)]; }
  // Writes outside the frame are dropped, so a shape may run off an edge.
  void set(int x, int y, uint8_t c) {
    if (inside(x, y)) px[static_cast<size_t>(y * width + x)] = c;
  }
  bool operator==(const Frame&) const = default;
};

struct Strip {
  int width = 16;  // one frame
  int height = 16;
  int fps = DEFAULT_FPS;
  std::vector<Frame> frames;
  bool operator==(const Strip&) const = default;
};

// A strip of `count` transparent frames.
Strip blank(int width, int height, int count = 1);

// The drawing tools. Coordinates may lie outside the frame; what falls
// outside is not drawn.
void line(Frame& f, int x0, int y0, int x1, int y1, uint8_t c);
// The box between two corners, in either order.
void rect(Frame& f, int x0, int y0, int x1, int y1, uint8_t c, bool filled);
// The ellipse that fits the box between two corners. A pixel is inside
// when its centre is. The outline is the inside pixels with a neighbour
// outside, so it is one pixel thick and closed.
void ellipse(Frame& f, int x0, int y0, int x1, int y1, uint8_t c, bool filled);
// The side of a half circle that is round. The flat side is the box's
// opposite edge.
enum class Facing { Up, Right, Down, Left };
// The half of an ellipse whose flat side lies on one edge of the box
// between two corners. Inside and outline follow the ellipse's rules, and
// the flat side is part of the outline.
void halfCircle(Frame& f, int x0, int y0, int x1, int y1, Facing facing, uint8_t c, bool filled);
// The 4-connected area of one colour around (x, y), filled with c.
void fill(Frame& f, int x, int y, uint8_t c);
// The same area, shaded from colour a at (x0, y0) to colour b at (x1, y1).
// Each pixel takes the nearest palette colour to its blend. `dither` adds
// a 4 x 4 ordered pattern, so the blend reads as smooth on a palette with
// few steps. The shading never lands on index 0 unless a or b is 0.
void gradient(Frame& f, int x, int y, int x0, int y0, int x1, int y1, uint8_t a, uint8_t b,
              const std::vector<uint8_t>& palette, bool dither);

// Rolls: pixels that leave one side come back on the other.
void rollRow(Frame& f, int y, int dx);
void rollColumn(Frame& f, int x, int dy);
void roll(Frame& f, int dx, int dy);
void flipH(Frame& f);
void flipV(Frame& f);

// Four frames of one size as one picture twice as wide and twice as high:
// top left, top right, bottom left, bottom right. splitBlock cuts such a
// picture back into the four frames, each of which keeps its own size.
Frame joinBlock(const Frame& tl, const Frame& tr, const Frame& bl, const Frame& br);
void splitBlock(const Frame& big, Frame& tl, Frame& tr, Frame& bl, Frame& br);

// A new frame size for every frame. The picture stays at the top left;
// what no longer fits is cut and new pixels are transparent.
void resize(Strip& s, int width, int height);

// The indexed PNG with the text chunks. The palette is 768 bytes.
std::vector<uint8_t> encodePng(const Strip& s, const std::vector<uint8_t>& palette);

// The value of a PNG text chunk, or nothing. The bytes need not be a
// valid image: this walks the chunks only.
std::optional<std::string> pngText(const std::vector<uint8_t>& png, const std::string& key);

// The frame count a PNG says it holds, when the chunk is there and is a
// number from 1 to MAX_FRAMES.
std::optional<int> pngFrames(const std::vector<uint8_t>& png);

struct Loaded {
  Strip strip;
  std::string note;  // set when colours moved onto the palette
};

// A PNG, or any picture stb_image reads, as a strip on the palette. The
// frame count comes from the text chunk. Without one, a strip whose width
// is a whole number of squares is that many square frames, and any other
// picture is one frame. An error names what does not fit.
std::optional<Loaded> load(const std::vector<uint8_t>& bytes, const std::vector<uint8_t>& palette,
                           std::string* error);

}  // namespace sc8::sprite
