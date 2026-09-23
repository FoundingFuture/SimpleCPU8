// simplecpu-cc: compile C source files to assembly text for simplecpu-asm.
//
//   simplecpu-cc main.c lib.c -o main.asm
//
// Options:
//   -o file          where the assembly goes (default out.asm)
//   -D NAME[=VALUE]  a preprocessor definition, as on any cc command line
//   -msoft-mul       multiply, divide and shift on the CPU, not the ACP
//   -zp-reserve N    leave the first N bytes of the zero page to the program
//   --rom-header f   also write ROM.h, the cartridge map, to f

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "cc/cc.h"

using namespace sc8;

int main(int argc, char** argv) {
  std::vector<CcInput> inputs;
  std::string out = "out.asm";
  std::string romHeaderPath;
  cc::CcOptions opts;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "-o" && i + 1 < argc) {
      out = argv[++i];
      continue;
    }
    if (a == "--rom-header" && i + 1 < argc) {
      romHeaderPath = argv[++i];
      continue;
    }
    if (a == "-zp-reserve" && i + 1 < argc) {
      opts.zpReserve = std::stoi(argv[++i]);
      continue;
    }
    if (a == "-msoft-mul") {
      opts.defines["SOFT_MUL"] = "1";
      continue;
    }
    if (a.rfind("-D", 0) == 0) {
      std::string def = a.size() > 2 ? a.substr(2) : (i + 1 < argc ? argv[++i] : "");
      const size_t eq = def.find('=');
      if (eq == std::string::npos) opts.defines[def] = "1";
      else opts.defines[def.substr(0, eq)] = def.substr(eq + 1);
      continue;
    }
    std::ifstream in(a);
    if (!in) {
      std::fprintf(stderr, "cannot read %s\n", a.c_str());
      return 1;
    }
    inputs.push_back({a, std::string(std::istreambuf_iterator<char>(in), {})});
  }
  if (inputs.empty()) {
    std::fprintf(stderr, "usage: simplecpu-cc <file.c>... [-o out.asm] [-D NAME[=VALUE]] [-msoft-mul] [-zp-reserve N] [--rom-header ROM.h]\n");
    return 2;
  }
  CcResult r = compile(inputs, opts);
  for (const std::string& e : r.errors) std::fprintf(stderr, "%s\n", e.c_str());
  if (!r.errors.empty()) return 1;
  if (!romHeaderPath.empty()) {
    std::ofstream h(romHeaderPath);
    h << r.romHeader;
    if (!h) return 1;
  }
  std::ofstream o(out);
  o << r.assembly;
  return o ? 0 : 1;
}
