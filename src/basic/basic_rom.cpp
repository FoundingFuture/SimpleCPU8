#include "basic/basic_rom.h"

// Generated into the build tree by cmake/EmbedBinary.cmake.
#include "basic_rom_data.h"
// And the assembly, by cmake/EmbedText.cmake.
#include "basic_asm_data.h"
// And the C sources, by cmake/EmbedTexts.cmake.
#include "basic_src_data.h"

namespace sc8 {

std::span<const uint8_t> basicRom() { return {BASIC_ROM, static_cast<size_t>(BASIC_ROM_SIZE)}; }

std::string_view basicAsm() { return BASIC_ASM; }

const std::vector<std::pair<std::string, std::string>>& basicSources() {
  static const std::vector<std::pair<std::string, std::string>> S = [] {
    std::vector<std::pair<std::string, std::string>> v;
    for (unsigned long i = 0; i < BASIC_SRC_COUNT; i++) v.emplace_back(BASIC_SRC[i].name, BASIC_SRC[i].text);
    return v;
  }();
  return S;
}

}  // namespace sc8
