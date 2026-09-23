#include "basic/basic_rom.h"

// Generated into the build tree by cmake/EmbedBinary.cmake.
#include "basic_rom_data.h"

namespace sc8 {

std::span<const uint8_t> basicRom() { return {BASIC_ROM, static_cast<size_t>(BASIC_ROM_SIZE)}; }

}  // namespace sc8
