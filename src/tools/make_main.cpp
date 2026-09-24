// simplecpu-make: a directory is a program. Build its ROM.
//
//   simplecpu-make mygame/                writes mygame/mygame.rom
//   simplecpu-make mygame/ -o out.rom
//   simplecpu-make mygame/ --microcode optimal --title "My Game"
//   simplecpu-make mygame/ --asm-out       also keep the generated .asm
//
// What the directory holds decides the build. Every .c file is compiled
// together, with every .h beside them reachable by #include. Every .asm
// file is appended after the generated assembly, in name order, so a
// driver sits at its .org slot in the same ROM. Every .bas file becomes a
// slot in the ROM's BAS chunk, named after the file. .file, .image and
// .sample resolve inside the directory. The title is the first line of
// README.md when there is one, else the directory's name.
//
// A directory with no .c file is an assembly program: its .asm files
// alone. A directory with neither is an error.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "asm/asm.h"
#include "assets/assets.h"
#include "cc/cc.h"
#include "core/cartridge.h"
#include "core/mcparse.h"

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

std::optional<std::vector<uint8_t>> readBytes(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return std::nullopt;
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

std::vector<fs::path> filesWith(const fs::path& dir, std::string_view ext) {
  std::vector<fs::path> out;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == ext) out.push_back(entry.path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

// A slot name from a file name: the stem, uppercase, letters and digits,
// at most 16, which is what the storage device accepts.
std::string slotNameFor(const fs::path& p) {
  std::string out;
  for (char c : p.stem().string()) {
    if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (out.size() == 16) break;
  }
  return out.empty() ? "PROGRAM" : out;
}

std::string titleFor(const fs::path& dir) {
  if (auto readme = readText(dir / "README.md")) {
    std::string first = readme->substr(0, readme->find('\n'));
    while (!first.empty() && (first.front() == '#' || first.front() == ' ')) first.erase(first.begin());
    if (!first.empty()) return first;
  }
  return fs::absolute(dir).filename().string();
}

}  // namespace

int main(int argc, char** argv) {
  fs::path dir;
  fs::path out;
  std::string microcode = "@naive";
  std::vector<std::pair<std::string, std::string>> meta;
  cc::CcOptions ccOpts;
  bool keepAsm = false;
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
        auto text = readText(m);
        if (!text) {
          std::fprintf(stderr, "cannot read microcode file %s\n", m.c_str());
          return 1;
        }
        McParsed parsed = parseMicrocode(*text);
        for (const McError& e : parsed.errors) std::fprintf(stderr, "%s:%d: %s\n", m.c_str(), e.line, e.message.c_str());
        if (!parsed.errors.empty()) return 1;
        microcode = *text;
      }
    } else if (a == "--title") meta.emplace_back("title", next());
    else if (a == "--author") meta.emplace_back("author", next());
    else if (a == "--asm-out") keepAsm = true;
    else if (a == "--listing") listing = true;
    else if (a.rfind("-D", 0) == 0) {
      std::string def = a.size() > 2 ? a.substr(2) : next();
      const size_t eq = def.find('=');
      if (eq == std::string::npos) ccOpts.defines[def] = "1";
      else ccOpts.defines[def.substr(0, eq)] = def.substr(eq + 1);
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

  const std::vector<fs::path> cFiles = filesWith(dir, ".c");
  const std::vector<fs::path> hFiles = filesWith(dir, ".h");
  const std::vector<fs::path> asmFiles = filesWith(dir, ".asm");
  const std::vector<fs::path> basFiles = filesWith(dir, ".bas");
  if (cFiles.empty() && asmFiles.empty()) {
    std::fprintf(stderr, "%s holds no .c and no .asm file\n", dir.string().c_str());
    return 1;
  }

  // The assembly text, and where each line came from for the messages.
  std::string text;
  struct Span {
    int firstLine;
    std::string file;
  };
  std::vector<Span> spans;
  int lineCount = 0;
  auto append = [&](const std::string& file, std::string part) {
    if (!part.empty() && part.back() != '\n') part += '\n';
    spans.push_back({lineCount + 1, file});
    text += ".code\n" + part;
    lineCount += 1 + static_cast<int>(std::count(part.begin(), part.end(), '\n'));
  };

  if (!cFiles.empty()) {
    std::vector<CcInput> inputs;
    for (const fs::path& p : cFiles) {
      auto t = readText(p);
      if (!t) {
        std::fprintf(stderr, "cannot read %s\n", p.string().c_str());
        return 1;
      }
      inputs.push_back({p.filename().string(), *t});
    }
    for (const fs::path& p : hFiles) {
      if (auto t = readText(p)) ccOpts.extra[p.filename().string()] = *t;
    }
    CcResult r = compile(inputs, ccOpts);
    for (const std::string& e : r.errors) std::fprintf(stderr, "%s\n", e.c_str());
    if (!r.errors.empty()) return 1;
    if (keepAsm) {
      std::ofstream o(dir / "generated.asm");
      o << r.assembly;
    }
    append("generated.asm", r.assembly);
  }
  for (const fs::path& p : asmFiles) {
    if (p.filename() == "generated.asm") continue;
    auto t = readText(p);
    if (!t) {
      std::fprintf(stderr, "cannot read %s\n", p.string().c_str());
      return 1;
    }
    append(p.filename().string(), *t);
  }
  auto where = [&](int line) -> std::string {
    const Span* s = &spans.front();
    for (const Span& sp : spans) {
      if (sp.firstLine <= line) s = &sp;
    }
    return (dir / s->file).string() + ":" + std::to_string(line - s->firstLine);
  };

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

  Assembled a = assemble(text, &assets);
  for (const AsmError& e : a.errors) std::fprintf(stderr, "%s: %s\n", where(e.line).c_str(), e.message.c_str());
  if (!a.errors.empty()) return 1;

  Cartridge c = a.cartridge();
  c.microcode = microcode;
  bool titled = false;
  for (const auto& [k, v] : meta) titled = titled || k == "title";
  if (!titled) meta.emplace_back("title", titleFor(dir));
  c.meta = meta;
  for (const fs::path& p : basFiles) {
    if (auto t = readText(p)) c.basic.emplace_back(slotNameFor(p), *t);
  }

  if (listing) {
    std::printf("%zu instructions, %zu bytes of .ram, %zu bytes of .data, %zu BASIC slot(s)\n", a.program.size(),
                a.ramLength, a.cart.size(), c.basic.size());
    for (const auto& [name, label] : a.labels) {
      const char* kind = label.kind == Label::Kind::Code ? "code" : label.kind == Label::Kind::Ram ? "ram" : "data";
      std::printf("  %-24s %-4s %6d\n", name.c_str(), kind, label.value);
    }
  }

  if (out.empty()) out = dir / (fs::absolute(dir).filename().string() + ".rom");
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
