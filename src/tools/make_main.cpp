// simplecpu-make: a directory is a program. Build its ROM.
//
//   simplecpu-make mygame/                writes mygame/mygame.rom, or
//                                         mygame/build/mygame.rom when the
//                                         folder has a src/ directory
//   simplecpu-make mygame/ -o out.rom
//   simplecpu-make mygame/ --microcode optimal --title "My Game"
//   simplecpu-make mygame/ --asm-out       also keep the generated .asm
//
// src/project/project.h says what a project folder holds and how it is
// read. This file is the command line around it.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "core/mcparse.h"
#include "project/project.h"

namespace fs = std::filesystem;
using namespace sc8;

namespace {

int usage() {
  std::fprintf(stderr,
               "usage: simplecpu-make <directory> [-o out.rom] [--microcode naive|optimal|<file>]\n"
               "                      [--title T] [--author A] [-D NAME[=VALUE]] [--asm-out] [--listing]\n");
  return 2;
}

std::optional<std::string> readText(const fs::path& p) {
  std::ifstream in(p);
  if (!in) return std::nullopt;
  return std::string(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

int main(int argc, char** argv) {
  fs::path dir;
  fs::path out;
  project::Options opts;
  bool listing = false;

  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s needs a value\n", a.c_str());
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "-o") out = next();
    else if (a == "--microcode") {
      const std::string m = next();
      if (m == "naive" || m == "optimal") opts.microcode = "@" + m;
      else {
        auto text = readText(m);
        if (!text) {
          std::fprintf(stderr, "cannot read microcode file %s\n", m.c_str());
          return 1;
        }
        McParsed parsed = parseMicrocode(*text);
        for (const McError& e : parsed.errors) std::fprintf(stderr, "%s:%d: %s\n", m.c_str(), e.line, e.message.c_str());
        if (!parsed.errors.empty()) return 1;
        opts.microcode = *text;
      }
    } else if (a == "--title") opts.meta.emplace_back("title", next());
    else if (a == "--author") opts.meta.emplace_back("author", next());
    else if (a == "--asm-out") opts.keepAsm = true;
    else if (a == "--listing") listing = true;
    else if (a.rfind("-D", 0) == 0) {
      std::string def = a.size() > 2 ? a.substr(2) : next();
      const size_t eq = def.find('=');
      if (eq == std::string::npos) opts.defines[def] = "1";
      else opts.defines[def.substr(0, eq)] = def.substr(eq + 1);
    } else if (a == "-h" || a == "--help") return usage();
    else if (!a.empty() && a[0] == '-') {
      std::fprintf(stderr, "unknown option %s\n", a.c_str());
      return usage();
    } else if (dir.empty()) dir = a;
    else return usage();
  }
  if (dir.empty()) return usage();
  if (!fs::is_directory(dir)) {
    std::fprintf(stderr, "%s is not a directory\n", dir.string().c_str());
    return 1;
  }

  const project::Layout layout = project::layoutOf(dir);
  project::Written w = project::buildAndWrite(layout, opts, out);
  for (const std::string& n : w.built.notes) std::fprintf(stderr, "%s\n", n.c_str());
  for (const std::string& e : w.built.errors) std::fprintf(stderr, "%s\n", e.c_str());
  if (!w.built.cartridge) return 1;
  if (listing) {
    std::printf("%zu instructions, %zu bytes of .ram, %zu bytes of .data, %zu BASIC slot(s)\n", w.built.instructions,
                w.built.ramBytes, w.built.dataBytes, w.built.cartridge->basic.size());
    for (const std::string& s : w.built.sources) std::printf("  %s\n", s.c_str());
  }
  std::printf("wrote %s\n", w.rom.string().c_str());
  return 0;
}
