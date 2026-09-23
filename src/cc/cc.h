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
  std::vector<std::string> errors;
};

CcResult compile(const std::vector<CcInput>& inputs, const cc::CcOptions& opts = {});

}  // namespace sc8
