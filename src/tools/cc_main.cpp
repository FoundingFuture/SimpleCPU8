// simplecpu-cc: compile C source files to assembly text for simplecpu-asm.
//
//   simplecpu-cc main.c lib.c -o main.asm
//
// Options:
//   -o file          where the assembly goes (default out.asm)
//   -D NAME[=VALUE]  a preprocessor definition, as on any cc command line
//   -msoft-mul       multiply, divide and shift on the CPU, not the ACP
//   -zp-reserve N    leave the first N bytes of the zero page to the program
//   --heap-stack-size N  the C stack's size in RAM, as 4096 or 4K. The
//                    default is all the RAM the program's data leaves
//   --rom-header f   also write ROM.h, the cartridge map, to f
//
// __image, __sprite, __palette, __sample and __file name a file beside the
// first source file.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "assets/assets.h"
#include "cc/cc.h"

namespace fs = std::filesystem;
using namespace sc8;

namespace {

std::optional<std::vector<uint8_t>> readBytes(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return std::nullopt;
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

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
    if (a == "--heap-stack-size" && i + 1 < argc) {
      const std::string v = argv[++i];
      char* end = nullptr;
      const long n = std::strtol(v.c_str(), &end, 0);
      const long bytes = (*end == 'K' || *end == 'k') ? n * 1024 : n;
      if (end == v.c_str() || bytes < 1 || bytes > 65535) {
        std::fprintf(stderr, "--heap-stack-size takes a number of bytes, as 4096 or 4K\n");
        return 2;
      }
      opts.heapStackSize = static_cast<int>(bytes);
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
    std::fprintf(stderr, "usage: simplecpu-cc <file.c>... [-o out.asm] [-D NAME[=VALUE]] [-msoft-mul] [-zp-reserve N]\n"
                         "                   [--heap-stack-size BYTES] [--rom-header ROM.h]\n");
    return 2;
  }
  const fs::path dir = fs::path(inputs.front().path).parent_path();
  Assets assets;
  assets.loadFile = [&](std::string_view name) { return readBytes(dir / fs::path(name)); };
  auto report = [&](std::string_view name, const std::string& note) {
    if (!note.empty()) std::fprintf(stderr, "%.*s: %s\n", static_cast<int>(name.size()), name.data(), note.c_str());
  };
  assets.loadImage = [&](std::string_view name) {
    std::string note;
    auto img = loadImageFile(dir / fs::path(name), &note);
    report(name, note);
    return img;
  };
  assets.loadSample = [&](std::string_view name) {
    std::string note;
    auto pcm = loadSampleFile(dir / fs::path(name), &note);
    report(name, note);
    return pcm;
  };
  assets.loadFont = [&](std::string_view name) {
    std::string note;
    auto blob = loadFontFile(dir / fs::path(name), &note);
    report(name, note);
    return blob;
  };
  opts.assets = &assets;
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
