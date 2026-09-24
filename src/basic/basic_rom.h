// The BASIC interpreter's ROM, compiled from the C sources in this
// directory and embedded at build time. The virtual computer boots it for
// --basic, and the tests boot it into a headless machine.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace sc8 {

// The bytes of basic.rom, a cartridge file as encodeCartridge writes it.
// Decode with decodeCartridge from core/cartridge.h.
std::span<const uint8_t> basicRom();

// The interpreter's assembly, as simplecpu-cc wrote it. A project that
// adds a driver appends its own assembly to this and assembles both.
std::string_view basicAsm();

}  // namespace sc8
