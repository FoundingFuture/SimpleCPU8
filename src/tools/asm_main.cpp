// simplecpu-asm: assemble a source file and burn a ROM.
//
//   simplecpu-asm main.asm                  writes main.rom beside the source
//   simplecpu-asm main.asm -o game.rom      names the ROM
//   simplecpu-asm main.asm --microcode optimal
//   simplecpu-asm main.asm --title "Pac-Man" --author "Eddie"
//   simplecpu-asm main.asm --listing        prints labels and sizes
//
// .file, .image and .sample resolve relative to the source file's directory.
// Image and sample decoding are pending: today .file works and the other
// two report that they need a decoder.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "asm/asm.h"
#include "core/cartridge.h"
#include "core/mcparse.h"

namespace fs = std::filesystem;
using namespace sc8;

namespace {

std::optional<std::vector<uint8_t>> readBytes(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return std::nullopt;
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

int usage() {
  std::fprintf(stderr,
               "usage: simplecpu-asm <source.asm> [-o out.rom] [--microcode naive|optimal|<file>]\n"
               "                     [--title T] [--author A] [--listing]\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  fs::path source;
  fs::path out;
  std::string microcode = "@naive";
  std::vector<std::pair<std::string, std::string>> meta;
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
      if (m == "naive" || m == "optimal") microcode = "@" + m;
      else {
        std::ifstream in(m);
        if (!in) {
          std::fprintf(stderr, "cannot read microcode file %s\n", m.c_str());
          return 1;
        }
        microcode.assign(std::istreambuf_iterator<char>(in), {});
        McParsed parsed = parseMicrocode(microcode);
        for (const McError& e : parsed.errors) {
          std::fprintf(stderr, "%s:%d: %s\n", m.c_str(), e.line, e.message.c_str());
        }
        if (!parsed.errors.empty()) return 1;
      }
    } else if (a == "--title") meta.emplace_back("title", next());
    else if (a == "--author") meta.emplace_back("author", next());
    else if (a == "--listing") listing = true;
    else if (a == "-h" || a == "--help") return usage();
    else if (!a.empty() && a[0] == '-') {
      std::fprintf(stderr, "unknown option %s\n", a.c_str());
      return usage();
    } else if (source.empty()) source = a;
    else return usage();
  }
  if (source.empty()) return usage();

  std::ifstream in(source);
  if (!in) {
    std::fprintf(stderr, "cannot read %s\n", source.string().c_str());
    return 1;
  }
  const std::string text(std::istreambuf_iterator<char>(in), {});
  const fs::path dir = source.parent_path();

  Assets assets;
  assets.loadFile = [&](std::string_view name) { return readBytes(dir / fs::path(name)); };
  assets.loadImage = [&](std::string_view name) -> std::optional<ImageAsset> {
    std::fprintf(stderr, "%s: .image('%.*s') needs the image decoder, which is not ported yet\n",
                 source.string().c_str(), static_cast<int>(name.size()), name.data());
    return std::nullopt;
  };
  assets.loadSample = [&](std::string_view name) -> std::optional<std::vector<uint8_t>> {
    std::fprintf(stderr, "%s: .sample('%.*s') needs the audio decoder, which is not ported yet\n",
                 source.string().c_str(), static_cast<int>(name.size()), name.data());
    return std::nullopt;
  };

  Assembled a = assemble(text, &assets);
  for (const AsmError& e : a.errors) {
    std::fprintf(stderr, "%s:%d: %s\n", source.string().c_str(), e.line, e.message.c_str());
  }
  if (!a.errors.empty()) return 1;

  if (listing) {
    std::printf("%zu instructions, %zu bytes of .ram, %zu bytes of .data\n", a.program.size(),
                a.ramLength, a.cart.size());
    for (const auto& [name, label] : a.labels) {
      const char* kind = label.kind == Label::Kind::Code ? "code" : label.kind == Label::Kind::Ram ? "ram" : "data";
      std::printf("  %-24s %-4s %6d\n", name.c_str(), kind, label.value);
    }
    for (const RomAsset& r : a.assets) {
      std::printf("  %-24s %-7s at %6u, %u bytes\n", r.name.c_str(), r.kind.c_str(), r.offset, r.size);
    }
  }

  Cartridge c = a.cartridge();
  c.microcode = microcode;
  c.meta = meta;
  if (out.empty()) out = fs::path(source).replace_extension(".rom");
  std::vector<uint8_t> bytes = encodeCartridge(c);
  std::ofstream o(out, std::ios::binary);
  o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!o) {
    std::fprintf(stderr, "cannot write %s\n", out.string().c_str());
    return 1;
  }
  std::printf("wrote %s (%zu bytes)\n", out.string().c_str(), bytes.size());
  return 0;
}
