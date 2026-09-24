// The one translation unit that compiles stb_image. Only the formats the
// browser version could decode and the machine can use are enabled.
//
// raylib compiles its own stb_image with external linkage, so this copy is
// static and reaches the rest of the library through decodeImageFile only.
// A second set of stbi_ symbols would otherwise collide at the IDE's link.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_STDIO_WIDE
#include "stb_image.h"

#include <cstdint>

#include "assets/decoders.h"

namespace sc8 {

namespace {

std::optional<Rgba> fromStbi(stbi_uc* data, int w, int h) {
  if (!data) return std::nullopt;
  Rgba out;
  out.width = w;
  out.height = h;
  out.bytes.assign(data, data + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
  stbi_image_free(data);
  if (w <= 0 || h <= 0) return std::nullopt;
  return out;
}

}  // namespace

// The load fills w and h, so it goes first: argument order is not
// evaluation order.
std::optional<Rgba> decodeImageFile(const std::string& path) {
  int w = 0, h = 0, channels = 0;
  stbi_uc* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
  return fromStbi(data, w, h);
}

std::optional<Rgba> decodeImageBytes(const std::vector<uint8_t>& bytes) {
  int w = 0, h = 0, channels = 0;
  if (bytes.size() > static_cast<size_t>(INT32_MAX)) return std::nullopt;
  stbi_uc* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
  return fromStbi(data, w, h);
}

}  // namespace sc8
