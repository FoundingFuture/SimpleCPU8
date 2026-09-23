// The cartridge: the .rom file simplecpu-asm burns and simplecpu boots. One
// container holds everything the machine needs, so a ROM is one file that
// goes into the machine and needs no other file beside it.
//
// Program memory is read-only and separate from RAM. FETCH is its only
// reader. The data section is what the GPU and the audio chip read through
// their cartridge address latches. RAM holds data and memory mapped IO.
//
// File layout, little-endian, chunked so a reader can skip what it does
// not know and the IDE can list what a ROM holds:
//
//   "SC8ROM\0\0"   magic, 8 bytes
//   u32            format version, 1
//   chunks         each: tag (4 bytes), length (u32), payload
//
// Chunk tags:
//
//   PROG   3 bytes per instruction: opcode, operand high, operand low
//   RAM    the bytes .ram defined, loaded at data RAM address 0
//   DATA   the .data section, loaded at cartridge address 0
//   ASET   the asset table: one line per resource, "kind\tname\toffset\tsize"
//   UCOD   the microcode set. "@optimal" and "@naive" name the two sets
//          the computer holds. A user's own set travels as its full text.
//          The optimal set never travels: it is hidden in the computer,
//          and the encoder replaces its text with the reference.
//   META   "key=value" lines: title, author, and whatever a tool adds
//   BAS    saved BASIC programs, one per named slot: "name\n" then the
//          program text, entries separated by a form feed. The storage
//          device reads and writes this chunk, and the IDE's BASIC editor
//          saves into it.
//
// PROG is the only chunk a ROM must have.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/isa.h"

namespace sc8 {

constexpr int CART_DATA_SIZE = 1 << 20;  // 1MB: 16 banks of 64KB, flat addressing

// One resource the .data section carries, as the assembler placed it.
struct RomAsset {
  std::string kind;  // file, image, palette, sample, or data for db/dw runs
  std::string name;
  uint32_t offset;
  uint32_t size;
  bool operator==(const RomAsset&) const = default;
};

struct Cartridge {
  std::vector<Instr> program;
  std::vector<uint8_t> ram;
  std::vector<uint8_t> data;
  std::vector<RomAsset> assets;
  // "@naive", "@optimal", or the user's microcode text. Empty means @naive.
  std::string microcode;
  std::vector<std::pair<std::string, std::string>> meta;
  // Saved BASIC programs by slot name.
  std::vector<std::pair<std::string, std::string>> basic;

  bool operator==(const Cartridge&) const = default;
};

std::vector<uint8_t> encodeCartridge(const Cartridge& c);

struct CartridgeResult {
  std::optional<Cartridge> cartridge;
  std::string error;
};

CartridgeResult decodeCartridge(const std::vector<uint8_t>& bytes);

}  // namespace sc8
