#include "assets/assets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <unordered_map>

#include "miniaudio.h"

#include "assets/decoders.h"
#include "assets/sprite.h"
#include "devices/apu_ports.h"
#include "devices/gpu.h"
#include "devices/gpu_ports.h"

namespace sc8 {

namespace {

uint32_t packRgb(uint8_t r, uint8_t g, uint8_t b) {
  return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

uint8_t byteAt(const std::vector<uint8_t>& v, size_t i) { return i < v.size() ? v[i] : 0; }

}  // namespace

std::vector<uint8_t> machinePalette() {
  const Palette pal = default332();
  return std::vector<uint8_t>(pal.begin(), pal.end());
}

std::map<uint32_t, uint8_t> paletteIndex(const std::vector<uint8_t>& palette) {
  std::map<uint32_t, uint8_t> map;
  for (size_t i = 0; i < 256; i++) {
    const uint32_t key = packRgb(byteAt(palette, i * 3), byteAt(palette, i * 3 + 1), byteAt(palette, i * 3 + 2));
    map.try_emplace(key, static_cast<uint8_t>(i));
  }
  return map;
}

ClassifiedImage classifyImage(const DecodedImage& img, const std::vector<uint8_t>& machine) {
  const auto index = paletteIndex(machine);
  std::map<uint8_t, uint8_t> remap{{0, 0}};  // 0 is transparent, always
  bool transparentUsed = false;
  bool blackMerge = false;

  for (const uint8_t p : img.pixels) {
    if (p == 0) {
      transparentUsed = true;
      continue;
    }
    if (remap.contains(p)) continue;
    const size_t o = static_cast<size_t>(p) * 3;
    const uint32_t key = packRgb(byteAt(img.palette, o), byteAt(img.palette, o + 1), byteAt(img.palette, o + 2));
    const auto hit = index.find(key);
    if (hit == index.end()) return {false, img.pixels, img.palette, false};
    remap[p] = hit->second;
    if (hit->second == 0) blackMerge = true;
  }

  std::vector<uint8_t> pixels(img.pixels.size());
  for (size_t i = 0; i < pixels.size(); i++) pixels[i] = remap.at(img.pixels[i]);
  return {true, std::move(pixels), {}, blackMerge && transparentUsed};
}

DecodedImage imageAssetFor(const DecodedImage& img, const std::vector<uint8_t>& machine) {
  ClassifiedImage c = classifyImage(img, machine);
  return {img.width, img.height, std::move(c.pixels), c.onPalette ? machine : std::move(c.palette)};
}

std::string deriveLabel(std::string_view fileName, SubItemKind kind, const std::set<std::string>& taken) {
  const size_t dot = fileName.rfind('.');
  const std::string_view stem = (dot != std::string_view::npos && dot > 0) ? fileName.substr(0, dot) : fileName;
  std::string base;
  bool allUnderscore = true;
  for (const char ch : stem) {
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const bool ok = (lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') || lower == '_';
    base += ok ? lower : '_';
    if (base.back() != '_') allUnderscore = false;
  }
  if (base.empty() || allUnderscore) base = "asset";
  if (base[0] >= '0' && base[0] <= '9') base = "_" + base;
  if (kind == SubItemKind::PaletteBlob) base += "pal";
  if (!taken.contains(base)) return base;
  for (int n = 2;; n++) {
    const std::string candidate = base + std::to_string(n);
    if (!taken.contains(candidate)) return candidate;
  }
}

std::string_view directiveFor(SubItemKind kind) {
  switch (kind) {
    case SubItemKind::Pixels: return ".image";
    case SubItemKind::PaletteBlob: return ".palette";
    case SubItemKind::Sample: return ".sample";
    case SubItemKind::File: return ".file";
  }
  return ".file";
}

std::string directiveLine(std::string_view label, SubItemKind kind, std::string_view assetName) {
  std::string head = std::string(label) + ":";
  const std::string pad = head.size() >= MNEM_COL ? " " : std::string(MNEM_COL - head.size(), ' ');
  // The assembler accepts either quote. Pick the one the name does not hold.
  const char q = assetName.find('\'') != std::string_view::npos ? '"' : '\'';
  return head + pad + std::string(directiveFor(kind)) + "(" + q + std::string(assetName) + q + ")";
}

namespace {

// Each line of source with its comment stripped, the way labelsIn and
// placedAssets both read it.
template <class F>
void eachCodeLine(std::string_view source, F&& f) {
  size_t pos = 0;
  while (pos <= source.size()) {
    size_t nl = source.find('\n', pos);
    if (nl == std::string_view::npos) nl = source.size();
    std::string_view line = source.substr(pos, nl - pos);
    const size_t semi = line.find(';');
    if (semi != std::string_view::npos) line = line.substr(0, semi);
    f(line);
    pos = nl + 1;
  }
}

std::optional<SubItemKind> kindByDirective(std::string_view word) {
  for (const SubItemKind k : {SubItemKind::Pixels, SubItemKind::PaletteBlob, SubItemKind::Sample, SubItemKind::File}) {
    if (directiveFor(k).substr(1) == word) return k;
  }
  return std::nullopt;
}

bool isIdentChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// PLACED_DIRECTIVE_RE by hand: a dot, a directive word, an open paren, a
// quoted name in either quote style, a close paren. Whitespace is allowed
// inside the parens as the expression allowed it.
struct PlacedMatch {
  SubItemKind kind;
  std::string name;
};

std::optional<PlacedMatch> matchPlaced(std::string_view line) {
  for (size_t dot = line.find('.'); dot != std::string_view::npos; dot = line.find('.', dot + 1)) {
    size_t i = dot + 1;
    while (i < line.size() && isIdentChar(line[i])) i++;
    const auto kind = kindByDirective(line.substr(dot + 1, i - dot - 1));
    if (!kind || i >= line.size() || line[i] != '(') continue;
    i++;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= line.size() || (line[i] != '\'' && line[i] != '"')) continue;
    const char q = line[i++];
    const size_t close = line.find(q, i);
    if (close == std::string_view::npos) continue;
    size_t j = close + 1;
    while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) j++;
    if (j >= line.size() || line[j] != ')') continue;
    return PlacedMatch{*kind, std::string(line.substr(i, close - i))};
  }
  return std::nullopt;
}

}  // namespace

std::map<std::string, std::vector<SubItemKind>> placedAssets(std::string_view source) {
  std::map<std::string, std::vector<SubItemKind>> out;
  eachCodeLine(source, [&](std::string_view line) {
    const auto m = matchPlaced(line);
    if (!m) return;
    auto& kinds = out[m->name];
    if (std::find(kinds.begin(), kinds.end(), m->kind) == kinds.end()) kinds.push_back(m->kind);
  });
  return out;
}

std::string placedRefusal(std::string_view name, const std::vector<SubItemKind>& kinds) {
  std::string directives;
  for (const SubItemKind k : kinds) {
    if (!directives.empty()) directives += ", ";
    directives += directiveFor(k);
  }
  const std::string what = kinds.size() > 1 ? "those lines" : "that line";
  return std::string(name) + " is still placed by " + directives + " in .data. Remove " + what + " first.";
}

QuantizedPcm quantizePcm(const std::vector<float>& pcm) {
  const bool truncated = pcm.size() > MAX_SAMPLE_BYTES;
  const size_t n = truncated ? MAX_SAMPLE_BYTES : pcm.size();
  std::vector<uint8_t> bytes(n);
  for (size_t i = 0; i < n; i++) {
    // Math.round rounds half up. lround rounds half away from zero, which
    // differs only below zero, where the clamp sends everything to 0 anyway.
    const long v = std::lround((pcm[i] * 0.5f + 0.5f) * 255.0f);
    bytes[i] = static_cast<uint8_t>(std::clamp(v, 0L, 255L));
  }
  return {std::move(bytes), truncated};
}

bool isMidi(const std::vector<uint8_t>& bytes) {
  return bytes.size() >= 4 && bytes[0] == 0x4d && bytes[1] == 0x54 && bytes[2] == 0x68 && bytes[3] == 0x64;
}

std::vector<SubItem> subItemsForImage(const DecodedImage& img, const std::vector<uint8_t>& machine) {
  ClassifiedImage c = classifyImage(img, machine);
  std::vector<SubItem> items;
  items.push_back({SubItemKind::Pixels, std::move(c.pixels),
                   c.blackMerge ? std::optional<std::string>(BLACK_MERGE_NOTE) : std::nullopt});
  if (!c.onPalette) items.push_back({SubItemKind::PaletteBlob, std::move(c.palette), std::nullopt});
  return items;
}

std::vector<SubItem> subItemsForSample(const std::vector<float>& pcm) {
  QuantizedPcm q = quantizePcm(pcm);
  std::optional<std::string> note;
  if (q.truncated) note = "truncated to " + std::to_string(MAX_SAMPLE_BYTES) + " bytes, the APU ceiling";
  return {{SubItemKind::Sample, std::move(q.bytes), std::move(note)}};
}

std::vector<SubItem> subItemsForFile(const std::vector<uint8_t>& bytes) {
  return {{SubItemKind::File, bytes, std::nullopt}};
}

std::set<std::string> labelsIn(std::string_view source) {
  std::set<std::string> out;
  eachCodeLine(source, [&](std::string_view line) {
    // A label sits at column 0 and ends in a colon. An indented bare word
    // is a mnemonic.
    if (line.empty() || !(std::isalpha(static_cast<unsigned char>(line[0])) || line[0] == '_')) return;
    size_t i = 1;
    while (i < line.size() && isIdentChar(line[i])) i++;
    if (i < line.size() && line[i] == ':') out.insert(std::string(line.substr(0, i)));
  });
  return out;
}

Size fitToScreen(int w, int h, int max) {
  const int longest = std::max(w, h);
  if (longest <= max) return {w, h};
  const double scale = static_cast<double>(max) / longest;
  return {std::max(1, static_cast<int>(std::lround(w * scale))),
          std::max(1, static_cast<int>(std::lround(h * scale)))};
}

uint8_t nearestIndex(const std::vector<uint8_t>& palette, int r, int g, int b) {
  uint8_t best = 0;
  int bestD = std::numeric_limits<int>::max();
  for (size_t i = 0; i < 256; i++) {
    const int dr = r - byteAt(palette, i * 3);
    const int dg = g - byteAt(palette, i * 3 + 1);
    const int db = b - byteAt(palette, i * 3 + 2);
    const int d = dr * dr + dg * dg + db * db;
    if (d < bestD) {
      bestD = d;
      best = static_cast<uint8_t>(i);
    }
  }
  return best;
}

Quantized quantizeToPalette(const std::vector<uint8_t>& rgba, int width, int height,
                            const std::vector<uint8_t>& palette) {
  const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
  Quantized q{std::vector<uint8_t>(count), false};
  // A photograph repeats colors heavily and every miss scans 256 entries.
  std::unordered_map<uint32_t, uint8_t> cache;
  for (size_t i = 0; i < count; i++) {
    const size_t o = i * 4;
    if (byteAt(rgba, o + 3) < 128) continue;  // transparent: index 0
    const uint8_t r = byteAt(rgba, o);
    const uint8_t g = byteAt(rgba, o + 1);
    const uint8_t b = byteAt(rgba, o + 2);
    const uint32_t key = packRgb(r, g, b);
    auto it = cache.find(key);
    if (it == cache.end()) it = cache.emplace(key, nearestIndex(palette, r, g, b)).first;
    if (it->second == 0) q.opaqueBlack = true;
    q.pixels[i] = it->second;
  }
  return q;
}

std::optional<std::string> conversionNote(const ConversionFacts& f) {
  std::string out;
  auto add = [&](std::string_view part) {
    if (!out.empty()) out += "; ";
    out += part;
  };
  if (f.scaled) add("scaled to fit the screen");
  if (f.quantized) add("colors quantized to the current palette");
  if (f.opaqueBlack) add(QUANTIZE_BLACK_NOTE);
  if (out.empty()) return std::nullopt;
  return out;
}

Inserted insertDirective(std::string_view source, std::string_view line, size_t caret) {
  std::vector<std::string> lines;
  {
    size_t pos = 0;
    while (true) {
      const size_t nl = source.find('\n', pos);
      if (nl == std::string_view::npos) {
        lines.emplace_back(source.substr(pos));
        break;
      }
      lines.emplace_back(source.substr(pos, nl - pos));
      pos = nl + 1;
    }
  }
  auto trimmed = [](const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return std::string();
    const size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
  };
  size_t dataAt = lines.size();
  for (size_t i = 0; i < lines.size(); i++) {
    if (trimmed(lines[i]) == ".data") {
      dataAt = i;
      break;
    }
  }

  if (dataAt == lines.size()) {
    std::string body(source);
    if (!(body.empty() || body.back() == '\n')) body += "\n";
    const std::string prefix = body + "\n.data\n";
    return {prefix + std::string(line) + "\n", prefix.size()};
  }

  // Which line holds the caret.
  size_t seen = 0;
  size_t caretLine = lines.size();
  for (size_t i = 0; i < lines.size(); i++) {
    seen += lines[i].size() + 1;
    if (caret < seen) {
      caretLine = i;
      break;
    }
  }

  // End of the .data section: the last line holding anything.
  size_t end = lines.size();
  while (end > dataAt + 1 && trimmed(lines[end - 1]).empty()) end--;

  const size_t insertAt = (caretLine > dataAt && caretLine < end) ? caretLine : end;
  lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAt), std::string(line));
  std::string out;
  size_t at = 0;
  for (size_t i = 0; i < lines.size(); i++) {
    if (i == insertAt) at = out.size();
    out += lines[i];
    if (i + 1 < lines.size()) out += "\n";
  }
  if (out.empty() || out.back() != '\n') out += "\n";
  return {std::move(out), at};
}

namespace {

// Nearest neighbour, sampling the source pixel under the centre of each
// destination pixel, which is how a canvas draws without smoothing. No
// color appears that the source did not hold.
std::vector<uint8_t> scaleNearest(const std::vector<uint8_t>& src, int sw, int sh, int dw, int dh) {
  std::vector<uint8_t> out(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 4);
  for (int y = 0; y < dh; y++) {
    const int sy = std::min(sh - 1, static_cast<int>((static_cast<int64_t>(y) * 2 + 1) * sh / (2 * dh)));
    for (int x = 0; x < dw; x++) {
      const int sx = std::min(sw - 1, static_cast<int>((static_cast<int64_t>(x) * 2 + 1) * sw / (2 * dw)));
      const size_t s = (static_cast<size_t>(sy) * static_cast<size_t>(sw) + static_cast<size_t>(sx)) * 4;
      const size_t d = (static_cast<size_t>(y) * static_cast<size_t>(dw) + static_cast<size_t>(x)) * 4;
      std::copy_n(src.begin() + static_cast<std::ptrdiff_t>(s), 4, out.begin() + static_cast<std::ptrdiff_t>(d));
    }
  }
  return out;
}

// The smoothed redraw. A browser's smoothing filter is unspecified, so
// this takes the most logical reading: a box filter over the source area
// each destination pixel covers, color weighted by alpha the way a canvas
// blends premultiplied pixels. An image that was not scaled comes back
// unchanged, which is also what a canvas does at 1:1.
std::vector<uint8_t> scaleSmooth(const std::vector<uint8_t>& src, int sw, int sh, int dw, int dh) {
  if (sw == dw && sh == dh) return src;
  std::vector<uint8_t> out(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 4);
  for (int y = 0; y < dh; y++) {
    const int y0 = y * sh / dh;
    const int y1 = std::max(y0 + 1, (y + 1) * sh / dh);
    for (int x = 0; x < dw; x++) {
      const int x0 = x * sw / dw;
      const int x1 = std::max(x0 + 1, (x + 1) * sw / dw);
      double r = 0, g = 0, b = 0, a = 0;
      for (int sy = y0; sy < y1; sy++) {
        for (int sx = x0; sx < x1; sx++) {
          const size_t s = (static_cast<size_t>(sy) * static_cast<size_t>(sw) + static_cast<size_t>(sx)) * 4;
          const double alpha = src[s + 3];
          r += src[s] * alpha;
          g += src[s + 1] * alpha;
          b += src[s + 2] * alpha;
          a += alpha;
        }
      }
      const double n = static_cast<double>(y1 - y0) * (x1 - x0);
      const size_t d = (static_cast<size_t>(y) * static_cast<size_t>(dw) + static_cast<size_t>(x)) * 4;
      if (a > 0) {
        out[d] = static_cast<uint8_t>(std::lround(r / a));
        out[d + 1] = static_cast<uint8_t>(std::lround(g / a));
        out[d + 2] = static_cast<uint8_t>(std::lround(b / a));
      }
      out[d + 3] = static_cast<uint8_t>(std::lround(a / n));
    }
  }
  return out;
}

}  // namespace

DecodedReport decodeRgba(const std::vector<uint8_t>& rgba, int width, int height,
                         const std::vector<uint8_t>& machine) {
  const Size fit = fitToScreen(width, height, gpu::SCREEN_W);
  const bool scaled = fit.width != width || fit.height != height;
  const std::vector<uint8_t> nearest = scaleNearest(rgba, width, height, fit.width, fit.height);
  const size_t count = static_cast<size_t>(fit.width) * static_cast<size_t>(fit.height);

  // Index the opaque colors in the order they are first seen, from 1.
  std::vector<uint8_t> pixels(count);
  std::vector<uint8_t> palette(768);
  std::unordered_map<uint32_t, uint8_t> seen;
  int next = 1;
  bool overflowed = false;
  for (size_t i = 0; i < count; i++) {
    const size_t o = i * 4;
    if (nearest[o + 3] < 128) continue;  // transparent: index 0
    const uint32_t key = packRgb(nearest[o], nearest[o + 1], nearest[o + 2]);
    auto it = seen.find(key);
    if (it == seen.end()) {
      if (next > 255) {
        overflowed = true;
        break;
      }
      const auto idx = static_cast<uint8_t>(next++);
      it = seen.emplace(key, idx).first;
      palette[static_cast<size_t>(idx) * 3] = nearest[o];
      palette[static_cast<size_t>(idx) * 3 + 1] = nearest[o + 1];
      palette[static_cast<size_t>(idx) * 3 + 2] = nearest[o + 2];
    }
    pixels[i] = it->second;
  }
  if (!overflowed) {
    return {{fit.width, fit.height, std::move(pixels), std::move(palette)},
            conversionNote({scaled, false, false})};
  }

  // True color: redraw with smoothing and quantize to the machine palette.
  const std::vector<uint8_t> smooth = scaleSmooth(rgba, width, height, fit.width, fit.height);
  Quantized q = quantizeToPalette(smooth, fit.width, fit.height, machine);
  return {{fit.width, fit.height, std::move(q.pixels), machine}, conversionNote({scaled, true, q.opaqueBlack})};
}

std::optional<ImageAsset> imageAssetOf(const Rgba& rgba, std::string* note);

std::optional<ImageAsset> loadImageFile(const std::filesystem::path& path, std::string* note) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(in), {});
  return loadImageBytes(bytes, note);
}

std::optional<ImageAsset> loadImageBytes(const std::vector<uint8_t>& bytes, std::string* note) {
  const std::optional<Rgba> rgba = decodeImageBytes(bytes);
  if (!rgba) return std::nullopt;
  std::optional<ImageAsset> img = imageAssetOf(*rgba, note);
  // A strip the sprite editor saved says how many frames it holds.
  if (img) img->frames = sprite::pngFrames(bytes).value_or(0);
  return img;
}

std::optional<ImageAsset> imageAssetOf(const Rgba& rgba, std::string* note) {
  const std::vector<uint8_t> machine = machinePalette();
  DecodedReport decoded = decodeRgba(rgba.bytes, rgba.width, rgba.height, machine);
  // buildAsset in main.ts: the decode's own note leads, then any black
  // merge the classification found.
  std::vector<SubItem> items = subItemsForImage(decoded.image, machine);
  std::optional<std::string> merged = decoded.note;
  if (items[0].note) merged = merged ? *merged + "; " + *items[0].note : items[0].note;
  if (note && merged) *note = *merged;
  return imageAssetFor(decoded.image, machine);
}

std::optional<std::vector<uint8_t>> loadSampleFile(const std::filesystem::path& path, std::string* note) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(in), {});
  return loadSampleBytes(bytes, note);
}

std::optional<std::vector<uint8_t>> loadSampleBytes(const std::vector<uint8_t>& bytes, std::string* note) {
  // The browser checked the MIDI magic before offering audio decoding, and
  // a MIDI file only ever became a .file asset.
  if (isMidi(bytes)) {
    if (note) *note = "a MIDI file is not a sample; place it with .file";
    return std::nullopt;
  }

  // One channel at AUDIO_RATE: miniaudio downmixes and resamples in one
  // pass, as the one channel OfflineAudioContext did. Its resampler is
  // linear, where a browser's is whatever the platform ships.
  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, static_cast<ma_uint32>(apu::AUDIO_RATE));
  ma_decoder decoder;
  if (ma_decoder_init_memory(bytes.data(), bytes.size(), &cfg, &decoder) != MA_SUCCESS) return std::nullopt;

  std::vector<float> pcm;
  float chunk[4096];
  while (true) {
    ma_uint64 got = 0;
    const ma_result r = ma_decoder_read_pcm_frames(&decoder, chunk, 4096, &got);
    pcm.insert(pcm.end(), chunk, chunk + got);
    if (r != MA_SUCCESS || got == 0) break;
  }
  ma_decoder_uninit(&decoder);
  // decodeAudio rendered at least one frame, so a silent empty file still
  // yields one byte of silence rather than an empty blob.
  if (pcm.empty()) pcm.push_back(0.0f);

  std::vector<SubItem> items = subItemsForSample(pcm);
  if (note && items[0].note) *note = *items[0].note;
  return std::move(items[0].bytes);
}

}  // namespace sc8
