// The seam between assets.cpp and the third party decoders it does not
// include. stb_image lives in stb_impl.cpp alone, see the note there.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sc8 {

struct Rgba {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> bytes;  // four bytes a pixel, row-major
};

// Decode an image file to RGBA. PNG, JPG, BMP and the first frame of a
// GIF. Nothing when the file is missing or not an image. stb_image takes
// a narrow path: on Windows a name outside the ANSI code page would need
// the wide API, which is left for a later port.
std::optional<Rgba> decodeImageFile(const std::string& path);

}  // namespace sc8
