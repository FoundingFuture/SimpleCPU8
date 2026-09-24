// zlib streams for the ROM file, through the stb single header
// libraries the tree already carries. Nothing else in core knows about
// compression.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace sc8 {

// A zlib stream (header, deflate, checksum) of the bytes.
std::vector<uint8_t> zlibCompress(const std::vector<uint8_t>& raw);

// The bytes back, or nothing when the stream is not zlib or is cut short.
std::optional<std::vector<uint8_t>> zlibDecompress(const uint8_t* stream, size_t size);

}  // namespace sc8
