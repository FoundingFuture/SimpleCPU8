#include "basic/basic_rom.h"

// Generated into the build tree by cmake/EmbedBinary.cmake.
#include "basic_rom_data.h"
// And the assembly, by cmake/EmbedText.cmake.
#include "basic_asm_data.h"

namespace sc8 {

std::span<const uint8_t> basicRom() { return {BASIC_ROM, static_cast<size_t>(BASIC_ROM_SIZE)}; }

std::string_view basicAsm() { return BASIC_ASM; }

}  // namespace sc8
