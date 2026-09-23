// simplecpu-cc: compile C source files to assembly text for simplecpu-asm.
//
//   simplecpu-cc main.c lib.c -o main.asm
//
// The compiler itself is pending the port from the TypeScript project.

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
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "-o" && i + 1 < argc) {
      out = argv[++i];
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
    std::fprintf(stderr, "usage: simplecpu-cc <file.c>... [-o out.asm]\n");
    return 2;
  }
  CcResult r = compile(inputs);
  for (const std::string& e : r.errors) std::fprintf(stderr, "%s\n", e.c_str());
  if (!r.errors.empty()) return 1;
  std::ofstream o(out);
  o << r.assembly;
  return o ? 0 : 1;
}
