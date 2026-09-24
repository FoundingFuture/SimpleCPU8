// The one translation unit in core that compiles stb's zlib code: the
// decoder from stb_image (zlib only) and the encoder from stb_image_write.
// Both static, so they never collide with raylib's or the assets library's
// copies at the link.
#include <climits>
#include <cstdlib>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_ZLIB
#define STBI_SUPPORT_ZLIB
#define STBI_NO_STDIO
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#include "core/zlib.h"

namespace sc8 {

std::vector<uint8_t> zlibCompress(const std::vector<uint8_t>& raw) {
  if (raw.empty() || raw.size() > static_cast<size_t>(INT_MAX)) return {};
  int outLen = 0;
  // stb's encoder writes into memory it mallocs; the level is its best.
  unsigned char* out = stbi_zlib_compress(const_cast<unsigned char*>(raw.data()), static_cast<int>(raw.size()), &outLen, 8);
  if (!out) return {};
  std::vector<uint8_t> result(out, out + outLen);
  std::free(out);
  return result;
}

std::optional<std::vector<uint8_t>> zlibDecompress(const uint8_t* stream, size_t size) {
  if (size == 0 || size > static_cast<size_t>(INT_MAX)) return std::nullopt;
  int outLen = 0;
  char* out = stbi_zlib_decode_malloc_guesssize_headerflag(reinterpret_cast<const char*>(stream), static_cast<int>(size),
                                                          static_cast<int>(size) * 4, &outLen, 1);
  if (!out) return std::nullopt;
  std::vector<uint8_t> result(reinterpret_cast<uint8_t*>(out), reinterpret_cast<uint8_t*>(out) + outLen);
  std::free(out);
  return result;
}

}  // namespace sc8
