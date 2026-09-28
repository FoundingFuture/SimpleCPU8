#include "assets/sprite.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <string_view>

#include "assets/assets.h"
#include "assets/decoders.h"
#include "core/zlib.h"

namespace sc8::sprite {

namespace {

// PNG's CRC-32, the table built once.
uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0xffffffffu) {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  for (size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
  return crc;
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v >> 24));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}

uint32_t get32(const std::vector<uint8_t>& in, size_t at) {
  return (static_cast<uint32_t>(in[at]) << 24) | (static_cast<uint32_t>(in[at + 1]) << 16) |
         (static_cast<uint32_t>(in[at + 2]) << 8) | in[at + 3];
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
  put32(out, static_cast<uint32_t>(data.size()));
  const size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  put32(out, crc32(out.data() + start, out.size() - start) ^ 0xffffffffu);
}

constexpr std::array<uint8_t, 8> SIGNATURE = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

// The 4 x 4 Bayer matrix, 0 to 15.
constexpr std::array<int, 16> BAYER = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

// The nearest palette colour, index 0 left out: 0 is transparent, and a
// shade must not punch a hole.
uint8_t nearestOpaque(const std::vector<uint8_t>& palette, double r, double g, double b) {
  uint8_t best = 1;
  double bestD = 1e18;
  for (int i = 1; i < 256; i++) {
    const double dr = palette[static_cast<size_t>(i * 3)] - r;
    const double dg = palette[static_cast<size_t>(i * 3 + 1)] - g;
    const double db = palette[static_cast<size_t>(i * 3 + 2)] - b;
    const double d = dr * dr + dg * dg + db * db;
    if (d < bestD) {
      bestD = d;
      best = static_cast<uint8_t>(i);
    }
  }
  return best;
}

// The pixels of the 4-connected area of one colour around (x, y).
std::vector<int> area(const Frame& f, int x, int y) {
  std::vector<int> out;
  if (!f.inside(x, y)) return out;
  const uint8_t from = f.at(x, y);
  std::vector<bool> seen(f.px.size(), false);
  std::vector<int> todo{y * f.width + x};
  seen[static_cast<size_t>(todo[0])] = true;
  while (!todo.empty()) {
    const int i = todo.back();
    todo.pop_back();
    out.push_back(i);
    const int px = i % f.width, py = i / f.width;
    const int nx[4] = {px - 1, px + 1, px, px};
    const int ny[4] = {py, py, py - 1, py + 1};
    for (int k = 0; k < 4; k++) {
      if (!f.inside(nx[k], ny[k])) continue;
      const int j = ny[k] * f.width + nx[k];
      if (seen[static_cast<size_t>(j)] || f.px[static_cast<size_t>(j)] != from) continue;
      seen[static_cast<size_t>(j)] = true;
      todo.push_back(j);
    }
  }
  return out;
}

int wrap(int v, int n) { return ((v % n) + n) % n; }

}  // namespace

Strip blank(int width, int height, int count) {
  Strip s;
  s.width = width;
  s.height = height;
  s.frames.assign(static_cast<size_t>(count), Frame(width, height));
  return s;
}

void line(Frame& f, int x0, int y0, int x1, int y1, uint8_t c) {
  const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    f.set(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void rect(Frame& f, int x0, int y0, int x1, int y1, uint8_t c, bool filled) {
  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      if (filled || x == x0 || x == x1 || y == y0 || y == y1) f.set(x, y, c);
    }
  }
}

void ellipse(Frame& f, int x0, int y0, int x1, int y1, uint8_t c, bool filled) {
  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);
  const int w = x1 - x0 + 1, h = y1 - y0 + 1;
  const double rx = w / 2.0, ry = h / 2.0;
  // Inside when the pixel's centre is, measured from the box's centre.
  auto in = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= w || y >= h) return false;
    const double ex = (x + 0.5 - rx) / rx, ey = (y + 0.5 - ry) / ry;
    return ex * ex + ey * ey <= 1.0;
  };
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      if (!in(x, y)) continue;
      if (filled || !in(x - 1, y) || !in(x + 1, y) || !in(x, y - 1) || !in(x, y + 1)) f.set(x0 + x, y0 + y, c);
    }
  }
}

void halfCircle(Frame& f, int x0, int y0, int x1, int y1, Facing facing, uint8_t c, bool filled) {
  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);
  const int w = x1 - x0 + 1, h = y1 - y0 + 1;
  // The whole ellipse's centre sits on the flat edge, and its radius
  // across the flat side is the box's full depth.
  double cx = w / 2.0, cy = h / 2.0, rx = w / 2.0, ry = h / 2.0;
  switch (facing) {
    case Facing::Up:
      cy = ry = h;
      break;
    case Facing::Down:
      cy = 0;
      ry = h;
      break;
    case Facing::Right:
      cx = 0;
      rx = w;
      break;
    case Facing::Left:
      cx = rx = w;
      break;
  }
  auto in = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= w || y >= h) return false;
    const double ex = (x + 0.5 - cx) / rx, ey = (y + 0.5 - cy) / ry;
    return ex * ex + ey * ey <= 1.0;
  };
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      if (!in(x, y)) continue;
      if (filled || !in(x - 1, y) || !in(x + 1, y) || !in(x, y - 1) || !in(x, y + 1)) f.set(x0 + x, y0 + y, c);
    }
  }
}

void fill(Frame& f, int x, int y, uint8_t c) {
  for (int i : area(f, x, y)) f.px[static_cast<size_t>(i)] = c;
}

void gradient(Frame& f, int x, int y, int x0, int y0, int x1, int y1, uint8_t a, uint8_t b,
              const std::vector<uint8_t>& palette, bool dither) {
  const std::vector<int> pixels = area(f, x, y);
  const double vx = x1 - x0, vy = y1 - y0;
  const double len2 = vx * vx + vy * vy;
  auto channel = [&](uint8_t idx, int k) { return static_cast<double>(palette[static_cast<size_t>(idx * 3 + k)]); };
  // The spread of the dither: about one palette step per channel. The
  // 3-3-2 palette steps by 36 in red and green and by 85 in blue.
  const double spread[3] = {36.0, 36.0, 85.0};
  const bool clearEnd = a == 0 || b == 0;
  for (int i : pixels) {
    const int px = i % f.width, py = i / f.width;
    double t = len2 == 0 ? 0.0 : ((px - x0) * vx + (py - y0) * vy) / len2;
    t = std::clamp(t, 0.0, 1.0);
    if (clearEnd) {
      // A blend with transparency: the ordered pattern decides which
      // pixels are drawn, so the colour fades out as a stipple.
      const double threshold = dither ? (BAYER[static_cast<size_t>((py & 3) * 4 + (px & 3))] + 0.5) / 16.0 : 0.5;
      const bool towardB = t >= threshold;
      f.px[static_cast<size_t>(i)] = towardB ? b : a;
      continue;
    }
    double rgb[3];
    for (int k = 0; k < 3; k++) {
      rgb[k] = channel(a, k) + (channel(b, k) - channel(a, k)) * t;
      if (dither) rgb[k] += (BAYER[static_cast<size_t>((py & 3) * 4 + (px & 3))] / 16.0 - 0.5 + 1.0 / 32) * spread[k];
    }
    f.px[static_cast<size_t>(i)] = nearestOpaque(palette, rgb[0], rgb[1], rgb[2]);
  }
}

void rollRow(Frame& f, int y, int dx) {
  if (y < 0 || y >= f.height) return;
  Frame old = f;
  for (int x = 0; x < f.width; x++) f.set(wrap(x + dx, f.width), y, old.at(x, y));
}

void rollColumn(Frame& f, int x, int dy) {
  if (x < 0 || x >= f.width) return;
  Frame old = f;
  for (int y = 0; y < f.height; y++) f.set(x, wrap(y + dy, f.height), old.at(x, y));
}

void roll(Frame& f, int dx, int dy) {
  Frame old = f;
  for (int y = 0; y < f.height; y++) {
    for (int x = 0; x < f.width; x++) f.set(wrap(x + dx, f.width), wrap(y + dy, f.height), old.at(x, y));
  }
}

void flipH(Frame& f) {
  for (int y = 0; y < f.height; y++) {
    for (int x = 0; x < f.width / 2; x++) {
      std::swap(f.px[static_cast<size_t>(y * f.width + x)], f.px[static_cast<size_t>(y * f.width + f.width - 1 - x)]);
    }
  }
}

void flipV(Frame& f) {
  for (int y = 0; y < f.height / 2; y++) {
    for (int x = 0; x < f.width; x++) {
      std::swap(f.px[static_cast<size_t>(y * f.width + x)], f.px[static_cast<size_t>((f.height - 1 - y) * f.width + x)]);
    }
  }
}

Frame joinBlock(const Frame& tl, const Frame& tr, const Frame& bl, const Frame& br) {
  const int w = tl.width, h = tl.height;
  Frame big(w * 2, h * 2);
  const Frame* parts[4] = {&tl, &tr, &bl, &br};
  for (int i = 0; i < 4; i++) {
    const int ox = (i % 2) * w, oy = (i / 2) * h;
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) big.set(ox + x, oy + y, parts[i]->at(x, y));
    }
  }
  return big;
}

void splitBlock(const Frame& big, Frame& tl, Frame& tr, Frame& bl, Frame& br) {
  Frame* parts[4] = {&tl, &tr, &bl, &br};
  for (int i = 0; i < 4; i++) {
    Frame& f = *parts[i];
    const int ox = (i % 2) * f.width, oy = (i / 2) * f.height;
    for (int y = 0; y < f.height; y++) {
      for (int x = 0; x < f.width; x++) {
        if (big.inside(ox + x, oy + y)) f.set(x, y, big.at(ox + x, oy + y));
      }
    }
  }
}

void resize(Strip& s, int width, int height) {
  for (Frame& f : s.frames) {
    Frame n(width, height);
    for (int y = 0; y < std::min(height, f.height); y++) {
      for (int x = 0; x < std::min(width, f.width); x++) n.set(x, y, f.at(x, y));
    }
    f = std::move(n);
  }
  s.width = width;
  s.height = height;
}

std::vector<uint8_t> encodePng(const Strip& s, const std::vector<uint8_t>& palette) {
  const int count = static_cast<int>(s.frames.size());
  const int w = s.width * count, h = s.height;
  std::vector<uint8_t> out(SIGNATURE.begin(), SIGNATURE.end());

  std::vector<uint8_t> ihdr;
  put32(ihdr, static_cast<uint32_t>(w));
  put32(ihdr, static_cast<uint32_t>(h));
  ihdr.insert(ihdr.end(), {8, 3, 0, 0, 0});  // 8 bits, indexed, deflate, no filter, not interlaced
  chunk(out, "IHDR", ihdr);

  std::vector<uint8_t> plte(palette.begin(), palette.begin() + static_cast<long>(std::min<size_t>(palette.size(), 768)));
  plte.resize(768, 0);
  chunk(out, "PLTE", plte);
  chunk(out, "tRNS", {0});  // index 0 transparent; the entries not listed are opaque

  auto text = [&](const char* key, const std::string& value) {
    std::vector<uint8_t> t(key, key + std::string_view(key).size());
    t.push_back(0);
    t.insert(t.end(), value.begin(), value.end());
    chunk(out, "tEXt", t);
  };
  text(FRAMES_KEY, std::to_string(count));
  text(FPS_KEY, std::to_string(s.fps));

  // Each row starts with filter type 0, then the frames' rows side by side.
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>((w + 1) * h));
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    for (const Frame& f : s.frames) {
      raw.insert(raw.end(), f.px.begin() + y * f.width, f.px.begin() + (y + 1) * f.width);
    }
  }
  chunk(out, "IDAT", zlibCompress(raw));
  chunk(out, "IEND", {});
  return out;
}

std::optional<std::string> pngText(const std::vector<uint8_t>& png, const std::string& key) {
  if (png.size() < 8 || !std::equal(SIGNATURE.begin(), SIGNATURE.end(), png.begin())) return std::nullopt;
  size_t at = 8;
  while (at + 12 <= png.size()) {
    const size_t len = get32(png, at);
    if (len > png.size() - at - 12) return std::nullopt;
    const std::string_view type(reinterpret_cast<const char*>(png.data() + at + 4), 4);
    if (type == "IEND") break;
    if (type == "tEXt") {
      const auto* data = png.data() + at + 8;
      const auto* end = data + len;
      const auto* nul = std::find(data, end, uint8_t{0});
      if (nul != end && std::string(data, nul) == key) return std::string(nul + 1, end);
    }
    at += len + 12;
  }
  return std::nullopt;
}

std::optional<int> pngFrames(const std::vector<uint8_t>& png) {
  const std::optional<std::string> v = pngText(png, FRAMES_KEY);
  if (!v || v->empty() || v->size() > 3) return std::nullopt;
  int n = 0;
  for (char ch : *v) {
    if (ch < '0' || ch > '9') return std::nullopt;
    n = n * 10 + (ch - '0');
  }
  if (n < 1 || n > MAX_FRAMES) return std::nullopt;
  return n;
}

std::optional<Loaded> load(const std::vector<uint8_t>& bytes, const std::vector<uint8_t>& palette,
                           std::string* error) {
  auto refuse = [&](const std::string& why) -> std::optional<Loaded> {
    if (error) *error = why;
    return std::nullopt;
  };
  const std::optional<Rgba> rgba = decodeImageBytes(bytes);
  if (!rgba) return refuse("not a picture this editor can read");
  const int w = rgba->width, h = rgba->height;

  int count = 1;
  if (const std::optional<int> said = pngFrames(bytes); said && w % *said == 0) {
    count = *said;
  } else if (h > 0 && w % h == 0 && w / h <= MAX_FRAMES) {
    count = w / h;
  }
  const int fw = w / count;
  if (fw > MAX_SIDE || h > MAX_SIDE) {
    return refuse("a frame is " + std::to_string(fw) + " x " + std::to_string(h) + " and a sprite is at most " +
                  std::to_string(MAX_SIDE) + " pixels a side");
  }

  Loaded out;
  out.strip = blank(fw, h, count);
  if (const std::optional<std::string> fps = pngText(bytes, FPS_KEY)) {
    const int v = std::atoi(fps->c_str());
    if (v >= 1 && v <= 60) out.strip.fps = v;
  }
  const std::map<uint32_t, uint8_t> exact = paletteIndex(palette);
  int moved = 0;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const size_t o = static_cast<size_t>((y * w + x) * 4);
      const uint8_t r = rgba->bytes[o], g = rgba->bytes[o + 1], b = rgba->bytes[o + 2], a = rgba->bytes[o + 3];
      uint8_t idx = 0;
      if (a >= 128) {
        const uint32_t key = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
        auto it = exact.find(key);
        if (it != exact.end() && it->second != 0) {
          idx = it->second;
        } else {
          // An opaque pixel is drawn, so it never lands on index 0. The
          // build does the same with a black pixel: index 1.
          idx = nearestIndex(palette, r, g, b);
          if (idx == 0) idx = 1;
          moved++;
        }
      }
      out.strip.frames[static_cast<size_t>(x / fw)].set(x % fw, y, idx);
    }
  }
  if (moved) {
    out.note = std::to_string(moved) + " pixel(s) moved to the nearest palette colour";
  }
  return out;
}

}  // namespace sc8::sprite
