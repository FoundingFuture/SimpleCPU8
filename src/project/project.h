// A project is a directory. Building it makes one ROM.
//
// Two layouts are read. The flat one puts everything in one folder: the
// .c, .h, .asm and .bas files and the images and sounds they name. The
// larger one keeps sources in src/, assets in assets/ and writes the ROM
// into build/. A folder with a src/ directory is the larger layout.
//
//   mygame/                    mygame/
//     main.c  ship.png           src/main.c  src/ship.h
//     README.md                  assets/ship.png  assets/song.mid
//     mygame.rom                 build/mygame.rom
//
// Every .c file is compiled together, with every .h beside them reachable
// by #include. Every .asm file is appended after the generated assembly,
// in name order, so a driver sits at its .org slot in the same ROM. Every
// .bas file becomes a slot in the ROM's BAS chunk, named after the file.
// An asset name resolves in assets/, then beside the sources. The title is
// the first line of README.md when there is one, else the folder's name.
//
// simplecpu-make and the IDE's Build button both come here.
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "asm/asm.h"
#include "core/cartridge.h"

namespace sc8::project {

struct Layout {
  std::filesystem::path root;
  std::filesystem::path sources;  // where the .c, .h, .asm and .bas files are
  std::filesystem::path assets;   // where an asset name is looked up first
  std::filesystem::path build;    // where the ROM goes
  std::string name;               // the folder's name, which names the ROM
  bool nested = false;            // src/ found: the larger layout
};

// Read a folder's shape. The build directory is not created here.
Layout layoutOf(const std::filesystem::path& dir);

// The loaders a compiler or an assembler resolves an asset name with. A
// conversion note, when one comes back, is appended to notes.
Assets loaders(const Layout& layout, std::vector<std::string>* notes);

struct Options {
  std::string microcode = "@naive";  // @naive, @optimal or a set's text
  std::vector<std::pair<std::string, std::string>> meta;
  std::map<std::string, std::string> defines;
  bool keepAsm = false;  // also write the generated assembly beside the ROM
};

struct Built {
  std::optional<Cartridge> cartridge;  // nothing on an error
  std::string assembly;                // the whole text the assembler saw
  std::vector<std::string> errors;     // file:line: message, ready to print
  std::vector<std::string> notes;      // conversion notes and the like
  size_t instructions = 0;
  size_t ramBytes = 0;
  size_t dataBytes = 0;
  std::vector<std::string> sources;  // the files that went in, in order
};

// Compile and assemble. Nothing is written to disk.
Built build(const Layout& layout, const Options& opts);

// Build and write the ROM, and the assembly when asked. The path written
// comes back, or nothing with the errors in the Built.
struct Written {
  Built built;
  std::filesystem::path rom;
};
Written buildAndWrite(const Layout& layout, const Options& opts, std::filesystem::path out = {});

}  // namespace sc8::project
