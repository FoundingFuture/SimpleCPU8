// Accumulator code generation, in the Small-C tradition the design names.
//
// Every expression evaluates into a two byte zero page temp. A binary
// operator puts its left side in slot n and its right in slot n + 1, so the
// number of slots a program needs is the depth of its deepest expression.
// That is the "register file" this machine has: the ALU can only reach an
// immediate or a zero page address, so there is nowhere else for a temp.
//
// A word is high byte then low, like every word on the machine. A byte value
// occupies the LOW half of its slot and the high half means nothing.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "asm/asm.h"
#include "cc/ast.h"
#include "cc/zeropage.h"

namespace sc8::cc {

struct ZpEntry {
  std::string name;
  int addr;
  int size;
  std::string why;
};

// A cartridge object, as laid out. ROM.h is written from these.
struct RomEntry {
  std::string name;
  int addr;
  int size;
  std::string file;
  int line;
  // Set when this object's bytes are identical to an earlier one's, which the
  // assembler deduplicates. Both labels then name one address.
  std::optional<std::string> sameAs;
  // The bytes themselves. An asset form reads a file to get them, so the
  // layout keeps them rather than reading the file again for the emit.
  std::vector<uint8_t> bytes;
  // For an asset initializer: its kind, as the ROM's asset table names it,
  // and the file it read. Empty for plain data.
  std::string assetKind;
  std::string assetFile;
};

struct Compiled {
  std::string text;
  std::vector<ZpEntry> zeroPage;
  // Every global, wherever it landed. The zero page map above is the part of
  // this that fitted; a profile needs the rest too, to say what should have.
  std::vector<ZpEntry> globals;
  std::vector<RomEntry> rom;
  // "file:line" to the 1-based line of the generated assembly that C line
  // produced first. The breakpoint map: a stop on a C line is a stop on the
  // first instruction that line became.
  std::map<std::string, int> lineOf;
};

// The cartridge layout, from the declarations alone. No code is generated to
// work it out, which is what lets ROM.h exist before codegen runs. An asset
// initializer resolves through `assets`, the maps first and then the
// loaders, the way the assembler's directives do.
std::vector<RomEntry> layoutRom(const std::vector<VarDecl>& vars, const Assets* assets = nullptr);

// A ROM object's bytes, from its initializer.
std::vector<uint8_t> romBytesOf(const VarDecl& v, const Assets* assets = nullptr);

// How many bytes each conversion in a printf template reads, in order. The
// browser project asks the GPU's own formatter. The GPU is not in this tree
// yet, so the same rules live here until it lands.
std::vector<int> templateArgWidths(const std::vector<uint8_t>& tmpl);

// The template with every float conversion given the l length, so %f reads
// the eight bytes of the double the compiler sends. A %lf stays as it is.
std::vector<uint8_t> widenFloatSpecs(const std::vector<uint8_t>& tmpl);

// One flag per conversion: true where it reads a real number.
std::vector<bool> templateFloatConvs(const std::vector<uint8_t>& tmpl);

Compiled compileUnit(const std::string& src, const std::string& file = "main.c");

// The driver merges every file into one tree and hands it here.
// zpReserve leaves that many bytes at the start of the zero page to the
// program, as a system page it reaches through fixed addresses.
// heapStackSize and heapStackTop are CcOptions' own.
Compiled compileUnitTree(const Unit& unit, bool softMul = false, const Profile* profile = nullptr,
                         int zpReserve = 0, const Assets* assets = nullptr, int heapStackSize = 0,
                         int heapStackTop = 0);

}  // namespace sc8::cc
