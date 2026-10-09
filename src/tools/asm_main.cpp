// simplecpu-asm: assemble a source file and burn a ROM.
//
//   simplecpu-asm main.asm                  writes main.rom beside the source
//   simplecpu-asm main.asm -o game.rom      names the ROM
//   simplecpu-asm main.asm --microcode optimal
//   simplecpu-asm main.asm --title "Pac-Man" --author "Eddie"
//   simplecpu-asm main.asm --listing        prints labels and sizes
//   simplecpu-asm main.asm --no-sources     a ROM without its sources inside
//   simplecpu-asm basic.asm driver.asm      several sources, one ROM: each
//                                           file starts in the code section
//   simplecpu-asm main.asm --bas DEMO=demo.bas
//                                           puts a BASIC program in the ROM
//
// .file, .image and .sample resolve relative to the source file's directory.
// An image is decoded and fitted to the screen, audio becomes 8 bit mono at
// 8 kHz. A conversion that lost something says so on stderr.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "asm/asm.h"
#include "assets/assets.h"
#include "core/cartridge.h"
#include "core/mcparse.h"
#include "project/text_file.h"

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
               "                     [--title T] [--author A] [--listing] [--no-sources]\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<fs::path> sources;
  bool embedSources = true;
  fs::path out;
  std::vector<std::pair<std::string, std::string>> basicSlots;
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
        microcode = lf(std::string(std::istreambuf_iterator<char>(in), {}));
        McParsed parsed = parseMicrocode(microcode);
        for (const McError& e : parsed.errors) {
          std::fprintf(stderr, "%s:%d: %s\n", m.c_str(), e.line, e.message.c_str());
        }
        if (!parsed.errors.empty()) return 1;
      }
    } else if (a == "--title") meta.emplace_back("title", next());
    else if (a == "--author") meta.emplace_back("author", next());
    else if (a == "--listing") listing = true;
    else if (a == "--no-sources") embedSources = false;
    else if (a == "--bas") {
      const std::string spec = next();
      const size_t eq = spec.find('=');
      if (eq == std::string::npos) {
        std::fprintf(stderr, "--bas takes NAME=file.bas\n");
        return 2;
      }
      std::ifstream in(spec.substr(eq + 1));
      if (!in) {
        std::fprintf(stderr, "cannot read %s\n", spec.substr(eq + 1).c_str());
        return 1;
      }
      basicSlots.emplace_back(spec.substr(0, eq), lf(std::string(std::istreambuf_iterator<char>(in), {})));
    }
    else if (a == "-h" || a == "--help") return usage();
    else if (!a.empty() && a[0] == '-') {
      std::fprintf(stderr, "unknown option %s\n", a.c_str());
      return usage();
    } else sources.push_back(a);
  }
  if (sources.empty()) return usage();

  // Several sources become one text, each starting in the code section.
  // Errors report the file and its own line, so the map from combined
  // line to file is kept.
  std::string text;
  struct Span {
    int firstLine;
    fs::path file;
  };
  std::vector<Span> spans;
  int lineCount = 0;
  for (const fs::path& source : sources) {
    std::ifstream in(source);
    if (!in) {
      std::fprintf(stderr, "cannot read %s\n", source.string().c_str());
      return 1;
    }
    std::string part(std::istreambuf_iterator<char>(in), {});
    if (!part.empty() && part.back() != '\n') part += '\n';
    spans.push_back({lineCount + 1, source});
    text += ".code\n" + part;
    lineCount += 1 + static_cast<int>(std::count(part.begin(), part.end(), '\n'));
  }
  auto where = [&](int line) -> std::string {
    const Span* s = &spans.front();
    for (const Span& sp : spans) {
      if (sp.firstLine <= line) s = &sp;
    }
    return s->file.string() + ":" + std::to_string(line - s->firstLine);
  };
  const fs::path source = sources.front();
  const fs::path dir = source.parent_path();

  // Every asset read goes into the ROM's project as well, under assets/,
  // beside the sources, so the ROM opens in the IDE as it was built.
  std::vector<std::pair<std::string, std::vector<uint8_t>>> carried;
  auto carry = [&](std::string_view name) {
    if (auto raw = readBytes(dir / fs::path(name))) carried.emplace_back("assets/" + std::string(name), *raw);
  };
  Assets assets;
  assets.loadFile = [&](std::string_view name) {
    carry(name);
    return readBytes(dir / fs::path(name));
  };
  auto report = [&](std::string_view name, const std::string& note) {
    if (note.empty()) return;
    std::fprintf(stderr, "%s: %.*s: %s\n", source.string().c_str(), static_cast<int>(name.size()), name.data(),
                 note.c_str());
  };
  assets.loadImage = [&](std::string_view name) {
    std::string note;
    auto img = loadImageFile(dir / fs::path(name), &note);
    if (img) carry(name);
    report(name, note);
    return img;
  };
  assets.loadSample = [&](std::string_view name) {
    std::string note;
    auto pcm = loadSampleFile(dir / fs::path(name), &note);
    if (pcm) carry(name);
    report(name, note);
    return pcm;
  };
  assets.loadFont = [&](std::string_view name) {
    std::string note;
    auto blob = loadFontFile(dir / fs::path(name), &note);
    if (blob) carry(name);
    report(name, note);
    return blob;
  };

  Assembled a = assemble(text, &assets);
  for (const AsmError& e : a.errors) {
    std::fprintf(stderr, "%s: %s\n", where(e.line).c_str(), e.message.c_str());
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
  c.basic = basicSlots;
  // The text in the ROM is LF, so a CR LF file burns the ROM its LF copy
  // burns. The assets stay byte for byte.
  if (embedSources) {
    for (const fs::path& src : sources) {
      if (auto raw = readBytes(src)) {
        const std::string asLf = lf(std::string(raw->begin(), raw->end()));
        c.sources.emplace_back(src.filename().string(), std::vector<uint8_t>(asLf.begin(), asLf.end()));
      }
    }
    for (auto& f : carried) c.sources.push_back(std::move(f));
  }
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
