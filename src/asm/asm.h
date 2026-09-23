// The assembler. Two passes over one source text: pass one places labels
// and collects instructions and data items, pass two resolves every
// expression and emits the program, the RAM image and the cartridge data.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/cartridge.h"
#include "core/isa.h"

namespace sc8 {

struct AsmError {
  int line;
  std::string message;
  bool operator==(const AsmError&) const = default;
};

struct Label {
  enum class Kind { Code, Ram, Data };
  Kind kind;
  int value;
  bool operator==(const Label&) const = default;
};

// An image already decoded to indexed pixels. The assembler runs everywhere,
// so it takes decoded data. Whoever calls it does the decoding.
struct ImageAsset {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;   // width * height palette indexes, row-major
  std::vector<uint8_t> palette;  // 768 bytes: 256 x RGB888
};

// Assets referenced by .data directives, by the name the directive uses.
// A name missing from a map is asked of the matching loader, so a tool can
// read from disk without preloading everything the source might name.
struct Assets {
  std::map<std::string, std::vector<uint8_t>> files;
  std::map<std::string, ImageAsset> images;
  // Audio converted to unsigned 8 bit mono at AUDIO_RATE.
  std::map<std::string, std::vector<uint8_t>> samples;

  std::function<std::optional<std::vector<uint8_t>>(std::string_view name)> loadFile;
  std::function<std::optional<ImageAsset>(std::string_view name)> loadImage;
  std::function<std::optional<std::vector<uint8_t>>(std::string_view name)> loadSample;
};

struct Assembled {
  // The program by slot. A .org gap holds UNLOADED_SLOT entries.
  std::vector<Instr> program;
  std::vector<uint8_t> ram;  // RAM_SIZE bytes
  size_t ramLength = 0;       // how many of them .ram defined
  std::vector<uint8_t> cart;  // the data section, built from .data
  std::map<int, int> ramLineAddr;  // source line -> RAM address it defines
  std::vector<int> instrToLine;
  std::map<int, int> lineToInstr;
  std::map<std::string, Label> labels;
  // Every .data directive that placed a blob, for the ROM's asset table.
  std::vector<RomAsset> assets;
  std::vector<AsmError> errors;

  // The ROM this assembly produces: program, RAM image and data in one.
  Cartridge cartridge() const;
};

// The .data directives that place an asset blob.
constexpr std::string_view DATA_DIRECTIVES[] = {"file", "image", "palette", "sample"};

Assembled assemble(std::string_view source, const Assets* assets = nullptr);

}  // namespace sc8
