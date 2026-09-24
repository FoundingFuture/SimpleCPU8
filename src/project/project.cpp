#include "project/project.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

#include "asm/asm.h"
#include "assets/assets.h"
#include "cc/cc.h"

namespace fs = std::filesystem;

namespace sc8::project {

namespace {

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

std::string titleFor(const Layout& l) {
  if (auto readme = readText(l.root / "README.md")) {
    std::string first = readme->substr(0, readme->find('\n'));
    while (!first.empty() && (first.front() == '#' || first.front() == ' ')) first.erase(first.begin());
    if (!first.empty()) return first;
  }
  return l.name;
}

// An asset lives in assets/ or beside the sources. The first that reads
// wins, so a flat project and a nested one look the same to a loader.
fs::path assetPath(const Layout& l, std::string_view name) {
  const fs::path inAssets = l.assets / fs::path(name);
  if (fs::exists(inAssets)) return inAssets;
  return l.sources / fs::path(name);
}

}  // namespace

Assets loaders(const Layout& layout, std::vector<std::string>* notes) {
  Assets assets;
  auto report = [notes](std::string_view name, const std::string& note) {
    if (notes && !note.empty()) notes->push_back(std::string(name) + ": " + note);
  };
  assets.loadFile = [layout](std::string_view name) { return readBytes(assetPath(layout, name)); };
  assets.loadImage = [layout, report](std::string_view name) {
    std::string note;
    auto img = loadImageFile(assetPath(layout, name), &note);
    report(name, note);
    return img;
  };
  assets.loadSample = [layout, report](std::string_view name) {
    std::string note;
    auto pcm = loadSampleFile(assetPath(layout, name), &note);
    report(name, note);
    return pcm;
  };
  return assets;
}

Layout layoutOf(const fs::path& dir) {
  Layout l;
  l.root = dir;
  l.name = fs::absolute(dir).filename().string();
  if (l.name.empty() || l.name == ".") l.name = fs::absolute(dir).parent_path().filename().string();
  l.nested = fs::is_directory(dir / "src");
  if (l.nested) {
    l.sources = dir / "src";
    l.assets = fs::is_directory(dir / "assets") ? dir / "assets" : l.sources;
    l.build = dir / "build";
  } else {
    l.sources = dir;
    l.assets = dir;
    l.build = dir;
  }
  return l;
}

Built build(const Layout& layout, const Options& opts) {
  Built b;
  const std::vector<fs::path> cFiles = filesWith(layout.sources, ".c");
  const std::vector<fs::path> hFiles = filesWith(layout.sources, ".h");
  const std::vector<fs::path> asmFiles = filesWith(layout.sources, ".asm");
  const std::vector<fs::path> basFiles = filesWith(layout.sources, ".bas");
  if (cFiles.empty() && asmFiles.empty()) {
    b.errors.push_back(layout.sources.string() + " holds no .c and no .asm file");
    return b;
  }

  // The assembly text, and where each line came from for the messages.
  struct Span {
    int firstLine;
    std::string file;
  };
  std::vector<Span> spans;
  int lineCount = 0;
  auto append = [&](const std::string& file, std::string part) {
    if (!part.empty() && part.back() != '\n') part += '\n';
    spans.push_back({lineCount + 1, file});
    b.assembly += ".code\n" + part;
    lineCount += 1 + static_cast<int>(std::count(part.begin(), part.end(), '\n'));
  };

  // The compiler and the assembler resolve an asset name the same way, so
  // one Assets serves both.
  Assets assets = loaders(layout, &b.notes);

  if (!cFiles.empty()) {
    cc::CcOptions ccOpts;
    ccOpts.defines = opts.defines;
    ccOpts.assets = &assets;
    std::vector<CcInput> inputs;
    for (const fs::path& p : cFiles) {
      auto t = readText(p);
      if (!t) {
        b.errors.push_back("cannot read " + p.string());
        return b;
      }
      inputs.push_back({p.filename().string(), *t});
      b.sources.push_back(p.filename().string());
    }
    for (const fs::path& p : hFiles) {
      if (auto t = readText(p)) ccOpts.extra[p.filename().string()] = *t;
    }
    CcResult r = compile(inputs, ccOpts);
    for (const std::string& e : r.errors) b.errors.push_back(e);
    if (!r.errors.empty()) return b;
    append("generated.asm", r.assembly);
  }
  for (const fs::path& p : asmFiles) {
    if (p.filename() == "generated.asm") continue;
    auto t = readText(p);
    if (!t) {
      b.errors.push_back("cannot read " + p.string());
      return b;
    }
    b.sources.push_back(p.filename().string());
    append(p.filename().string(), *t);
  }
  auto where = [&](int line) -> std::string {
    const Span* s = &spans.front();
    for (const Span& sp : spans) {
      if (sp.firstLine <= line) s = &sp;
    }
    return (layout.sources / s->file).string() + ":" + std::to_string(line - s->firstLine);
  };

  Assembled a = assemble(b.assembly, &assets);
  for (const AsmError& e : a.errors) b.errors.push_back(where(e.line) + ": " + e.message);
  if (!a.errors.empty()) return b;

  Cartridge c = a.cartridge();
  c.microcode = opts.microcode;
  std::vector<std::pair<std::string, std::string>> meta = opts.meta;
  bool titled = false;
  for (const auto& [k, v] : meta) titled = titled || k == "title";
  if (!titled) meta.emplace_back("title", titleFor(layout));
  c.meta = meta;
  for (const fs::path& p : basFiles) {
    if (auto t = readText(p)) {
      c.basic.emplace_back(slotNameFor(p), *t);
      b.sources.push_back(p.filename().string());
    }
  }
  b.instructions = a.program.size();
  b.ramBytes = a.ramLength;
  b.dataBytes = a.cart.size();
  b.cartridge = std::move(c);
  return b;
}

Written buildAndWrite(const Layout& layout, const Options& opts, fs::path out) {
  Written w;
  w.built = build(layout, opts);
  if (!w.built.cartridge) return w;
  std::error_code ec;
  fs::create_directories(layout.build, ec);
  if (out.empty()) out = layout.build / (layout.name + ".rom");
  if (opts.keepAsm) {
    std::ofstream o(layout.build / (layout.name + ".asm"));
    o << w.built.assembly;
  }
  std::vector<uint8_t> bytes = encodeCartridge(*w.built.cartridge);
  std::ofstream o(out, std::ios::binary);
  o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!o) {
    w.built.errors.push_back("cannot write " + out.string());
    w.built.cartridge.reset();
    return w;
  }
  w.rom = out;
  return w;
}

}  // namespace sc8::project
