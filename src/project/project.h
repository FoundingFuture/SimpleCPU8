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
// What the sources hold decides the kind of project. A .c file makes it a
// C project: every .c is compiled together, with every .h beside them
// reachable by #include, and every .asm is appended after the generated
// assembly in name order, so a driver sits at its .org slot in the same
// ROM. With no .c but a .bas, it is a BASIC project: the ROM is the
// interpreter, with the .asm files appended as drivers, and every .bas
// becomes a slot named after the file. A slot named AUTORUN runs at power
// on. With .bas and .c together, the ROM is one C program: the
// interpreter's own sources compiled with the user's, main being the
// interpreter's, so BASIC boots and calls the C by name with CALL. The
// builder resolves CALL name and JMP name in a .bas to the label's slot.
// With .asm files alone, it is an assembly project. A microcode.txt
// among the sources is the ROM's microcode set.
//
// An asset name resolves in assets/, then beside the sources. The title is
// the first line of README.md when there is one, else the folder's name.
//
// simplecpu-make and the IDE's Build button both come here, and so does
// simplecpu-make new, which lays a fresh project out.
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

// A source as text, named the way its file would be: main.c, game.bas,
// driver.asm, microcode.txt. The IDE builds from these when a project
// lives in memory: an opened ROM, or a scratch project not yet saved.
struct Source {
  std::string name;
  std::string text;
};

// What a command that writes files answers: the files written, or an
// error message.
struct Created {
  std::vector<std::filesystem::path> files;
  std::string error;
};

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

// Copy a project's files other than its sources into the folder to, in
// the larger layout. The README goes to to/README.md and every asset file
// to to/assets/, replacing a file of the same name. These are the files a
// ROM carries beside the sources. The IDE's Save as writes the sources
// itself and calls this for the rest. A project saved as its own folder
// copies nothing. The files written come back, or an error message.
Created copyProjectFiles(const Layout& from, const std::filesystem::path& to);

// The loaders a compiler or an assembler resolves an asset name with. A
// conversion note, when one comes back, is appended to notes.
Assets loaders(const Layout& layout, std::vector<std::string>* notes);

// A project's asset files: everything in assets/, and in a flat project
// every file beside the sources that is not a source, a ROM or the README.
// The ROM carries these, and a project with BASIC places them.
std::vector<std::filesystem::path> assetFiles(const Layout& layout);

// The same over the files a ROM carries in its SRC chunk: an asset name
// resolves to assets/<name>, then <name>.
Assets loadersFrom(const Cartridge& rom, std::vector<std::string>* notes);

// The project a ROM carries, split back into its sources and its other
// files (assets and README). Empty when the ROM has no SRC chunk.
struct Carried {
  std::vector<Source> sources;
  std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
};
Carried carried(const Cartridge& rom);

// Write the project a ROM carries into a folder in the larger layout:
// src/, assets/, README.md, the text files with LF line ends. The folder
// must not exist yet. The files written come back, or an error message.
Created unpack(const Cartridge& rom, const std::filesystem::path& dir);

// Put a project into a cartridge's SRC chunk: the sources and the README
// as text with LF line ends, the other files as bytes.
void embed(Cartridge& c, const std::vector<Source>& sources,
           const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files);

struct Options {
  // @naive, @optimal or a set's text. Empty means microcode.txt among the
  // sources when there is one, else the kind's own default.
  std::string microcode;
  // Carry the project in the ROM's SRC chunk: the sources, the assets and
  // the README, so the ROM opens as a project again. Off for a ROM burned
  // to hand out without its source.
  bool embedSources = true;
  std::vector<std::pair<std::string, std::string>> meta;
  std::map<std::string, std::string> defines;
  bool keepAsm = false;  // also write the generated assembly beside the ROM
};

enum class Kind { C, Basic, Assembly, Microcode };

// The folders under examples/, one for each kind of project, in the order
// the IDE's Open example menu lists them. Each has its folder and label.
// An example sits in the folder of its kind. examples/CMakeLists.txt
// builds every folder one level down, and a test holds the tree to this
// list.
struct ExampleKind {
  const char* folder;
  const char* label;
};
constexpr ExampleKind EXAMPLE_KINDS[] = {
    {"assembly", "Assembly"}, {"basic", "BASIC"}, {"c", "C"}, {"microcode", "Microcode"}};

// Lay out a new project of a kind at dir, which must not exist yet or be
// empty: the folders, a README, a .gitignore for build/ and a first
// source that builds and runs. Microcode is an assembly project with the
// naive set written out as microcode.txt. The files written come back, or
// an error message.
Created create(const std::filesystem::path& dir, Kind kind);

// Where each source's lines sit in the assembly a build made, so a
// breakpoint on a source line finds its address. breakpoints.h reads it.
struct LineMap {
  struct Span {
    int firstLine;  // the .code line before the file's line 1
    std::string file;
  };
  std::vector<Span> spans;  // in the order the build joined them
  // By C file, each line to the line of the compiler's output that its
  // first instruction came from. Lines count from 1.
  std::map<std::string, std::map<int, int>> cLines;
};

struct Built {
  std::optional<Cartridge> cartridge;  // nothing on an error
  Assembled assembled;                 // what the assembler made, for a listing
  std::string assembly;                // the whole text the assembler saw
  std::vector<std::string> errors;     // file:line: message, ready to print
  std::vector<std::string> notes;      // conversion notes and the like
  size_t instructions = 0;
  size_t ramBytes = 0;
  size_t dataBytes = 0;
  std::vector<std::string> sources;  // the files that went in, in order
  LineMap lines;
};

// Compile and assemble. Nothing is written to disk.
Built build(const Layout& layout, const Options& opts);

// The same with the sources given, where the folder would be read. The
// title, the assets and the files the ROM carries still come from the
// folder. The IDE builds an example this way, from its open documents.
Built build(const Layout& layout, const std::vector<Source>& sources, const Options& opts);

// The same from sources in memory. Errors name the sources by their names
// under `where`, a folder shown in messages. `base` is a cartridge whose
// program stands in when the sources hold no .c and no .asm: an opened
// ROM's program, with the sources' .bas slots and microcode.txt on top.
// `assetNames` are the project's asset files by name, which `assets`
// loads. A project with BASIC places every one of them on the cartridge
// and lists it in ASET by kind and by the name without its extension, so
// the running program finds it with STO_FIND.
Built buildSources(const std::vector<Source>& sources, const Assets& assets, const Options& opts,
                   const std::string& title, const std::string& where, const std::optional<Cartridge>& base = {},
                   const std::vector<std::string>& assetNames = {});

// Build and write the ROM, and the assembly when asked. The path written
// comes back, or nothing with the errors in the Built.
struct Written {
  Built built;
  std::filesystem::path rom;
};
Written buildAndWrite(const Layout& layout, const Options& opts, std::filesystem::path out = {});

}  // namespace sc8::project
