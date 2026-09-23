// The printf formatter, a pure function from a template and a parameter run
// to a stream of output bytes. The GPU calls it, but it holds no GPU state, so
// tests drive it directly.
//
// It follows the C printf grammar: %[flags][width][.precision][length]spec.
// Flags are - + space 0 and #. Width is a minimum field width. Precision is
// digits after a dot. Length hh h l ll sets how many bytes the format reads.
// The bits carry no type. The format says how to read them.
//
// On this machine an int is 16 bits. So %d reads 2 bytes, %hhd reads 1, %ld
// reads 4, %lld reads 8. Floats read 4 bytes, or 8 with the l length. Values
// arrive big-endian, high byte first, the same order the CPU stores a word.
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace sc8 {

constexpr std::string_view NO_PARAM = "-no parameter-";

// Read one byte of data RAM at an address. The %s and %r conversions walk
// this. The template is in ROM, but the strings it points at live in RAM.
// The GPU supplies this. Tests may leave it empty, and then every read is 0.
using PrintfReader = std::function<uint8_t(uint32_t)>;

// How many bytes each conversion in a template reads, in order. The C
// compiler marshals gpu_printf's arguments into RAM from this, so the block
// it writes is the block the device reads.
std::vector<int> templateArgWidths(std::span<const uint8_t> tpl);

// Format a template into a byte stream. Newline bytes pass through unchanged,
// so the caller handles the cursor and any scroll.
std::vector<uint8_t> formatTemplate(std::span<const uint8_t> tpl, std::span<const uint8_t> params,
                                    const PrintfReader& readByte = {});

}  // namespace sc8
