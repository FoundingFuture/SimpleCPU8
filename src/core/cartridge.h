// The cartridge: the .rom file simplecpu-asm burns and simplecpu boots. One
// container holds everything the machine needs, so a ROM is one file that
// goes into the machine and needs no other file beside it.
//
// Program memory is read-only and separate from RAM. FETCH is its only
// reader. The data section is what the GPU and the audio chip read through
// their cartridge address latches. RAM holds data and memory mapped IO.
//
// File layout, little-endian, chunked so a reader can skip what it does
// not know and the IDE can list what a ROM holds. On disk the chunks are
// one zlib stream behind the header, since a ROM carries its project and
// its pictures; a reader takes the plain form as well:
//
//   "SC8ROM\0\0"   magic, 8 bytes
//   u32            format version: 1 for plain chunks, 2 for the chunks
//                  as one zlib stream
//   chunks         each: tag (4 bytes), length (u32), payload
//
// Chunk tags:
//
//   PROG   a program segment: u32 first instruction slot, then 3 bytes per
//          instruction: opcode, operand high, operand low. A ROM may hold
//          several, one per .org run, so a driver can sit at a known slot
//          apart from the program that calls it. Slots no segment covers
//          are unloaded, and a fetch from one crashes.
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
//   SRC    the project the ROM was built from: its sources, its assets
//          and its README, so the IDE opens a ROM as a project with a
//          listing and breakpoints. Entries: name (NUL terminated), u32
//          size, bytes. A ROM burned for distribution alone leaves it out.
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
  // The program by slot, sparse: UNLOADED_SLOT marks a gap between segments.
  std::vector<Instr> program;
  std::vector<uint8_t> ram;
  std::vector<uint8_t> data;
  std::vector<RomAsset> assets;
  // "@naive", "@optimal", or the user's microcode text. Empty means @naive.
  std::string microcode;
  std::vector<std::pair<std::string, std::string>> meta;
  // Saved BASIC programs by slot name.
  std::vector<std::pair<std::string, std::string>> basic;
  // The project's files by name: sources as text, assets as their bytes.
  std::vector<std::pair<std::string, std::vector<uint8_t>>> sources;

  bool operator==(const Cartridge&) const = default;
};

// The file bytes. Compressed by default, version 2; plain, version 1,
// for a test that pins bytes or a reader outside this tree.
std::vector<uint8_t> encodeCartridge(const Cartridge& c, bool compress = true);

struct CartridgeResult {
  std::optional<Cartridge> cartridge;
  std::string error;
};

CartridgeResult decodeCartridge(const std::vector<uint8_t>& bytes);

}  // namespace sc8
