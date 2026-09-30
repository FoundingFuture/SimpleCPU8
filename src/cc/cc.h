// The compiler driver. Many files in, one assembly text out.
//
// The design puts the link step on assembly TEXT, with an archive as a block
// of it. This does the same job one stage earlier, on the trees: the units
// are merged, a `static` name is made private by mangling it, nothing that
// main cannot reach is emitted, and the runtime is linked whether or not
// anything asked, the way nobody writes -lc.
//
// The effect a program sees is the design's: many files, private labels,
// dead code that costs nothing. Doing it on trees rather than on text means
// the assembler is still the one place that knows an encoding, which was the
// property the design wanted from a text linker.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "cc/build.h"
#include "cc/codegen.h"
#include "cc/zeropage.h"

namespace sc8::cc {

struct SourceFile {
  std::string name;
  std::string text;
};

struct CcOptions {
  // Extra files a #include "name" can reach, beyond the ones being compiled.
  std::map<std::string, std::string> extra;
  std::map<std::string, std::string> defines;
  // Leave the runtime out. Only a test that is checking the runtime itself.
  bool noLibc = false;
  // Real accesses per variable, read off the machine after a run. The zero
  // page then goes by what the program DID rather than by how it reads.
  const Profile* profile = nullptr;
  // Bytes at the start of the zero page left to the program as a system
  // page of fixed addresses. BASIC keeps its vectors and pointers there.
  int zpReserve = 0;
  // Where __image, __sprite, __palette, __sample and __file find their
  // bytes: the preloaded maps first, then the loaders, as the assembler's
  // directives resolve a name. Without it every asset form is an error.
  const Assets* assets = nullptr;
  // Files a host program owns main for, such as BASIC in a project with
  // BASIC and C. A main in one of them is refused.
  std::set<std::string> guestFiles;
  // Functions something outside the C calls by name: a CALL or USR in a
  // BASIC program, or a JSR in an assembly file. They are roots of the
  // reachability pass, so they survive though no C calls them. A name that
  // is a static function is refused, since its label is private.
  std::set<std::string> externalCalls;
  // The heap stack's size in bytes: the C stack in RAM, under D3, that holds
  // parameters and locals. 0 gives it every byte between the program's
  // data and the text screen.
  int heapStackSize = 0;
  // Where the heap stack starts, growing down: the first address above it.
  // 0 is C_STACK_TOP, $FAC0. BASIC passes $F000, below its 4 KB screen.
  int heapStackTop = 0;
};

struct Program : Compiled {
  std::vector<std::string> files;
  std::vector<std::string> included;
  std::vector<std::string> dropped;
  std::vector<std::string> builds;
  BuildPlan plan;
  // The cartridge map, as a C header. Output, like the listing.
  std::string romHeader;
};

// ROM.h: the cartridge as built, in address order, regenerated every compile.
std::string romHeaderText(const std::vector<RomEntry>& rom);

// Throws CcError when the program is refused.
Program compileProgram(const std::vector<SourceFile>& files, const CcOptions& opts = {});

// One file, the common case and what every test uses.
Program compileSource(const std::string& src, const CcOptions& opts = {});

}  // namespace sc8::cc

namespace sc8 {

// The command line entry point. Every error is a line of text, so the tool
// never throws.
struct CcInput {
  std::string path;
  std::string text;
};

struct CcResult {
  std::string assembly;
  std::string romHeader;
  // The program's __ROM objects, for a build that lists its assets.
  std::vector<cc::RomEntry> rom;
  // "file:line" to the line of `assembly` that C line produced first, as
  // Compiled::lineOf. A breakpoint on a C line reads it.
  std::map<std::string, int> lineOf;
  std::vector<std::string> errors;
};

CcResult compile(const std::vector<CcInput>& inputs, const cc::CcOptions& opts = {});

}  // namespace sc8
