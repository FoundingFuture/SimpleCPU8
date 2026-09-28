#include "project/project.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

#include "asm/asm.h"
#include "asm/format.h"
#include "assets/assets.h"
#include "cc/cc.h"
#include "core/cartridge.h"
#include "core/conflicts.h"
#include "core/isa.h"
#include "core/mcparse.h"
#include "core/microcode.h"
#if SC8_HAVE_BASIC
#include "basic/basic_rom.h"
#include "basic/program.h"
#endif

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

// CALL name, JSR name and JMP name in a BASIC line, name being an identifier rather
// than a number, resolved to the instruction slot of that code label. The
// same for the target of USR(width, name, ...), the word after the first
// comma. The
// interpreter knows only numbers, so the text that reaches the ROM holds
// the slot. A one letter name, or a letter and a digit, is a BASIC
// variable and stays. So does a name followed by a parenthesis, which is
// a function call. Any other name that is not a label is an error, with
// the line it was on.
struct LabelError {
  int line;
  std::string name;
};

bool isVarName(const std::string& n) {
  if (n.size() == 1) return std::isalpha(static_cast<unsigned char>(n[0])) != 0;
  return n.size() == 2 && std::isalpha(static_cast<unsigned char>(n[0])) && std::isdigit(static_cast<unsigned char>(n[1]));
}

std::string resolveLabels(const std::string& text, const std::map<std::string, Label>& labels,
                          std::vector<LabelError>& errors) {
  std::string out;
  int lineNo = 0;
  size_t at = 0;
  while (at <= text.size()) {
    size_t nl = text.find('\n', at);
    if (nl == std::string::npos) nl = text.size();
    std::string line = text.substr(at, nl - at);
    lineNo++;
    at = nl + 1;
    // Walk the line: skip strings, stop at REM, rewrite after CALL, JSR and JMP.
    std::string rewritten;
    size_t i = 0;
    bool quoted = false;
    auto word = [&](size_t from) {
      size_t n = 0;
      while (from + n < line.size() && (line[from + n] == '_' || std::isalnum(static_cast<unsigned char>(line[from + n])))) n++;
      return line.substr(from, n);
    };
    while (i < line.size()) {
      const char c = line[i];
      if (c == '"') quoted = !quoted;
      if (quoted || !(c == '_' || std::isalpha(static_cast<unsigned char>(c))) ||
          (i > 0 && (line[i - 1] == '_' || std::isalnum(static_cast<unsigned char>(line[i - 1]))))) {
        rewritten += c;
        i++;
        continue;
      }
      const std::string w = word(i);
      std::string upper = w;
      for (char& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      rewritten += w;
      i += w.size();
      if (upper == "REM") {
        rewritten += line.substr(i);
        break;
      }
      if (upper != "CALL" && upper != "JSR" && upper != "JMP" && upper != "USR") continue;
      size_t j = i;
      while (j < line.size() && line[j] == ' ') j++;
      if (upper == "USR") {
        // Past the opening parenthesis and the width, to the target.
        if (j >= line.size() || line[j] != '(') continue;
        int depth = 0;
        size_t p = j;
        for (; p < line.size(); p++) {
          if (line[p] == '"') break;
          if (line[p] == '(') depth++;
          if (line[p] == ')' && --depth == 0) break;
          if (line[p] == ',' && depth == 1) break;
        }
        if (p >= line.size() || line[p] != ',') continue;
        j = p + 1;
        while (j < line.size() && line[j] == ' ') j++;
      }
      if (j >= line.size() || !(line[j] == '_' || std::isalpha(static_cast<unsigned char>(line[j])))) continue;
      const std::string name = word(j);
      size_t k = j + name.size();
      while (k < line.size() && line[k] == ' ') k++;
      auto hit = labels.find(name);
      if (hit != labels.end() && hit->second.kind == Label::Kind::Code) {
        rewritten += line.substr(i, j - i) + std::to_string(hit->second.value);
        i = j + name.size();
        continue;
      }
      if (isVarName(name) || (k < line.size() && line[k] == '(')) continue;
      errors.push_back({lineNo, name});
      rewritten += line.substr(i, j + name.size() - i);
      i = j + name.size();
    }
    out += rewritten;
    if (nl < text.size()) out += '\n';
  }
  return out;
}

// The names a BASIC program calls by: the targets of CALL, JSR, JMP and
// USR that are not numbers or variables. resolveLabels with no labels
// reports exactly those.
std::set<std::string> basicCallNames(const std::string& text) {
  std::vector<LabelError> names;
  resolveLabels(text, {}, names);
  std::set<std::string> out;
  for (const LabelError& e : names) out.insert(e.name);
  return out;
}

// Every identifier in an assembly file outside its comments and strings.
// Any of them may name a C function: JSR helper, LD D2 <- helper, dw helper.
std::set<std::string> asmNames(const std::string& text) {
  std::set<std::string> out;
  bool quoted = false;
  for (size_t i = 0; i < text.size();) {
    const char c = text[i];
    if (c == '\n') quoted = false;
    if (c == '"' || c == '\'') {
      quoted = !quoted;
      i++;
      continue;
    }
    if (!quoted && c == ';') {
      while (i < text.size() && text[i] != '\n') i++;
      continue;
    }
    if (!quoted && (c == '_' || std::isalpha(static_cast<unsigned char>(c)))) {
      size_t n = 1;
      while (i + n < text.size() && (text[i + n] == '_' || std::isalnum(static_cast<unsigned char>(text[i + n])))) n++;
      out.insert(text.substr(i, n));
      i += n;
      continue;
    }
    i++;
  }
  return out;
}

}  // namespace

Assets loaders(const Layout& layout, std::vector<std::string>* notes) {
  Assets assets;
  auto report = [notes](std::string_view name, const std::string& note) {
    if (!notes || note.empty()) return;
    const std::string line = std::string(name) + ": " + note;
    if (std::find(notes->begin(), notes->end(), line) == notes->end()) notes->push_back(line);
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
  assets.loadFont = [layout, report](std::string_view name) {
    std::string note;
    auto blob = loadFontFile(assetPath(layout, name), &note);
    report(name, note);
    return blob;
  };
  assets.note = report;
  return assets;
}

std::vector<fs::path> assetFiles(const Layout& layout) {
  std::vector<fs::path> out;
  std::error_code ec;
  if (layout.assets != layout.sources && fs::is_directory(layout.assets, ec)) {
    for (const auto& entry : fs::directory_iterator(layout.assets, ec)) {
      if (entry.is_regular_file()) out.push_back(entry.path());
    }
  }
  for (const auto& entry : fs::directory_iterator(layout.sources, ec)) {
    if (!entry.is_regular_file()) continue;
    const std::string name = entry.path().filename().string();
    const std::string ext = entry.path().extension().string();
    const bool source = ext == ".c" || ext == ".h" || ext == ".asm" || ext == ".bas" || name == "microcode.txt";
    if (source || ext == ".rom" || name == "README.md") continue;
    out.push_back(entry.path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

namespace {

// DESIGN: BASIC finds an asset by name at run time, through STO_FIND over
// the ASET chunk. So in a project with BASIC every asset goes on the
// cartridge, listed by its kind and its stem. docs/design/font-design.md.
//
// A name BASIC can write is a letter, then letters and digits, at most 10
// characters, the most its lexer keeps of a name.
constexpr size_t BASIC_NAME_MAX = 10;

bool basicCanWrite(std::string_view stem) {
  if (stem.empty() || stem.size() > BASIC_NAME_MAX) return false;
  if (!std::isalpha(static_cast<unsigned char>(stem[0]))) return false;
  return std::all_of(stem.begin(), stem.end(), [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0; });
}

std::string lowered(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}

// The kind an asset file places as, and its bytes, the way the directive
// for that kind places it: a .font as its blob, a picture as .image's
// pixels, a sound as .sample's bytes, anything else as the file itself.
struct Placement {
  std::string kind;
  std::vector<uint8_t> bytes;
};

std::optional<Placement> placementOf(const std::string& name, const Assets& assets, Built& b) {
  const auto raw = assets.loadFile ? assets.loadFile(name) : std::nullopt;
  if (!raw) {
    b.errors.push_back(name + ": cannot read the asset");
    return std::nullopt;
  }
  if (lowered(fs::path(name).extension().string()) == ".font") {
    std::string why;
    auto blob = loadFontBytes(*raw, &why);
    if (!blob) {
      // "line 3: ..." from the reader, as "small.font:3: ...".
      b.errors.push_back(name + ":" + (why.starts_with("line ") ? why.substr(5) : " " + why));
      return std::nullopt;
    }
    return Placement{"font", std::move(*blob)};
  }
  std::string note;
  if (auto img = loadImageBytes(*raw, &note)) {
    if (!note.empty()) b.notes.push_back(name + ": " + note);
    return Placement{"image", std::move(img->pixels)};
  }
  if (auto pcm = loadSampleBytes(*raw, nullptr)) return Placement{"sample", std::move(*pcm)};
  return Placement{"file", *raw};
}

// Place a project's assets for BASIC: the name rules first, then every file
// by kind and stem. A file a directive already placed with the same bytes
// keeps its place and gains a second entry under its stem, so the chunk
// stays one line per name and nothing is on the cartridge twice.
void placeForBasic(Cartridge& c, const std::vector<RomAsset>& placed, const Assets& assets,
                   std::vector<std::string> names, Built& b) {
  std::sort(names.begin(), names.end());
  std::map<std::string, std::string> byStem;
  for (const std::string& n : names) {
    const std::string stem = fs::path(n).stem().string();
    auto [it, fresh] = byStem.emplace(lowered(stem), n);
    if (!fresh) {
      b.errors.push_back(it->second + " and " + n + " have names that differ only in case, and BASIC finds an "
                         "asset by its name in any case. Rename one of them.");
    }
    if (!basicCanWrite(stem)) {
      b.notes.push_back(n + ": BASIC cannot write the name " + stem + ". A BASIC name is a letter, then "
                        "letters and digits, at most 10 of them. C and assembly still reach the file.");
    }
  }
  if (!b.errors.empty()) return;
  for (const std::string& n : names) {
    auto p = placementOf(n, assets, b);
    if (!p) continue;
    const std::string stem = fs::path(n).stem().string();
    const auto same = std::find_if(placed.begin(), placed.end(), [&](const RomAsset& r) {
      return r.kind == p->kind && r.name == n && r.size == p->bytes.size() && r.offset + r.size <= c.data.size() &&
             std::equal(p->bytes.begin(), p->bytes.end(), c.data.begin() + r.offset);
    });
    if (same != placed.end()) {
      c.assets.push_back({p->kind, stem, same->offset, same->size});
      continue;
    }
    const auto offset = static_cast<uint32_t>(c.data.size());
    c.data.insert(c.data.end(), p->bytes.begin(), p->bytes.end());
    c.assets.push_back({p->kind, stem, offset, static_cast<uint32_t>(p->bytes.size())});
  }
}

const std::vector<uint8_t>* carriedFile(const Cartridge& rom, std::string_view name) {
  for (const std::string prefix : {"assets/", ""}) {
    const std::string want = prefix + std::string(name);
    for (const auto& [n, bytes] : rom.sources) {
      if (n == want) return &bytes;
    }
  }
  return nullptr;
}

}  // namespace

Assets loadersFrom(const Cartridge& rom, std::vector<std::string>* notes) {
  Assets assets;
  auto report = [notes](std::string_view name, const std::string& note) {
    if (notes && !note.empty()) notes->push_back(std::string(name) + ": " + note);
  };
  assets.loadFile = [rom](std::string_view name) -> std::optional<std::vector<uint8_t>> {
    const std::vector<uint8_t>* b = carriedFile(rom, name);
    if (!b) return std::nullopt;
    return *b;
  };
  assets.loadImage = [rom, report](std::string_view name) -> std::optional<ImageAsset> {
    const std::vector<uint8_t>* b = carriedFile(rom, name);
    if (!b) return std::nullopt;
    std::string note;
    auto img = loadImageBytes(*b, &note);
    report(name, note);
    return img;
  };
  assets.loadSample = [rom, report](std::string_view name) -> std::optional<std::vector<uint8_t>> {
    const std::vector<uint8_t>* b = carriedFile(rom, name);
    if (!b) return std::nullopt;
    std::string note;
    auto pcm = loadSampleBytes(*b, &note);
    report(name, note);
    return pcm;
  };
  assets.loadFont = [rom, report](std::string_view name) -> std::optional<std::vector<uint8_t>> {
    const std::vector<uint8_t>* b = carriedFile(rom, name);
    if (!b) return std::nullopt;
    std::string note;
    auto blob = loadFontBytes(*b, &note);
    report(name, note);
    return blob;
  };
  return assets;
}

Carried carried(const Cartridge& rom) {
  Carried out;
  for (const auto& [name, bytes] : rom.sources) {
    const std::string ext = fs::path(name).extension().string();
    const bool source = name.find('/') == std::string::npos &&
                        (ext == ".c" || ext == ".h" || ext == ".asm" || ext == ".bas" || name == "microcode.txt");
    if (source) out.sources.push_back({name, std::string(bytes.begin(), bytes.end())});
    else out.files.emplace_back(name, bytes);
  }
  return out;
}

void embed(Cartridge& c, const std::vector<Source>& sources,
           const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
  c.sources.clear();
  for (const Source& src : sources) c.sources.emplace_back(src.name, std::vector<uint8_t>(src.text.begin(), src.text.end()));
  for (const auto& f : files) c.sources.push_back(f);
}

Created unpack(const Cartridge& rom, const fs::path& dir) {
  Created out;
  std::error_code ec;
  if (fs::exists(dir, ec)) {
    out.error = dir.string() + " exists already";
    return out;
  }
  if (rom.sources.empty()) {
    out.error = "this ROM carries no project";
    return out;
  }
  fs::create_directories(dir / "src", ec);
  for (const auto& [name, bytes] : rom.sources) {
    // Names are what embed() wrote: bare sources, assets/x, README.md.
    // A name that walks out of the folder is refused.
    if (name.empty() || name.find("..") != std::string::npos || name.front() == '/') continue;
    const fs::path at = name.find('/') == std::string::npos && name != "README.md" ? dir / "src" / name : dir / name;
    fs::create_directories(at.parent_path(), ec);
    std::ofstream o(at, std::ios::binary);
    o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!o) {
      out.error = "cannot write " + at.string();
      return out;
    }
    out.files.push_back(at);
  }
  std::ofstream(dir / ".gitignore") << "build/\n";
  out.files.push_back(dir / ".gitignore");
  return out;
}

Layout layoutOf(const fs::path& dir) {
  Layout l;
  l.root = dir;
  // A source named with no directory, `pong.asm`, has an empty parent and
  // an empty path cannot be made absolute: the IDE aborted on it. Empty
  // means the working directory.
  const fs::path here = dir.empty() ? fs::path(".") : dir;
  l.name = fs::absolute(here).filename().string();
  if (l.name.empty() || l.name == ".") l.name = fs::absolute(here).parent_path().filename().string();
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

// The cycle table for a microcode project. Every row of a microprogram is
// one microcycle and every row runs, so an instruction's cost is the fetch
// program's rows plus its own. Counted here, at build time, from the
// project's own set and from the optimal set. A program prints the lines
// with CMD_PRINTF and finds them through cycles_addr, a table of cartridge
// addresses laid down in RAM, three bytes each.
// The rows of a set that break one of the eight row rules, as warnings
// with their line in the text. The machine runs such a set and stops on
// the row with a signal conflict, which is its own lesson, so the build
// goes on. The warning says where to look before that happens.
static std::vector<std::string> rowWarnings(const std::string& text, const Microcode& set) {
  // The line of each row: a line ending in a colon opens a section, every
  // other line with something on it before a # is a row of it.
  std::map<std::pair<std::string, size_t>, int> lineOf;
  std::string section;
  size_t row = 0;
  int lineNo = 0;
  for (size_t at = 0; at <= text.size();) {
    size_t nl = text.find('\n', at);
    if (nl == std::string::npos) nl = text.size();
    std::string line = text.substr(at, nl - at);
    at = nl + 1;
    lineNo++;
    const size_t hash = line.find('#');
    if (hash != std::string::npos) line.resize(hash);
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
    size_t first = 0;
    while (first < line.size() && std::isspace(static_cast<unsigned char>(line[first]))) first++;
    if (first >= line.size()) continue;
    if (line.back() == ':') {
      section = line.substr(first, line.size() - first - 1);
      row = 0;
    } else {
      lineOf[{section, row++}] = lineNo;
    }
  }
  std::vector<std::string> out;
  for (const Microcode::Section& sec : set.sections()) {
    for (size_t r = 0; r < sec.rows.size(); r++) {
      const std::optional<Conflict> c = checkRow(sec.rows[r]);
      if (!c) continue;
      const auto hit = lineOf.find({sec.name, r});
      const std::string where = hit == lineOf.end() ? "" : std::to_string(hit->second) + ":";
      out.push_back(where + " warning: " + sec.name + ", row " + std::to_string(r + 1) + ", breaks rule " +
                    std::to_string(c->rule) + ": " + c->message +
                    ". The machine stops with a signal conflict when it runs this row.");
    }
  }
  return out;
}

std::string cyclesUnit(const std::string& microcodeText, std::vector<std::string>* errors) {
  Microcode mine;
  if (microcodeText == "@naive") mine = buildNaive();
  else if (microcodeText == "@optimal") mine = buildOptimal();
  else {
    McParsed parsed = parseMicrocode(microcodeText);
    if (!parsed.errors.empty()) {
      if (errors) errors->push_back("the microcode set does not parse, so no cycle table");
      return "";
    }
    mine = parsed.microcode;
  }
  const Microcode best = buildOptimal();
  auto cost = [](const Microcode& m, std::string_view name) -> int {
    const Rows* fetch = m.get("fetch");
    const Rows* rows = m.get(name);
    if (!fetch || !rows) return -1;
    return static_cast<int>(fetch->size() + rows->size());
  };
  // The display name: operand words shortened so a line fits a 21 column
  // half of the text screen with two counts beside it.
  auto shortName = [](std::string_view name) {
    std::string n(name);
    for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{
             {"addr16", "a16"}, {"addr8", "a8"}, {"imm16", "i16"}, {"imm8", "i8"}}) {
      size_t at;
      while ((at = n.find(from)) != std::string::npos) n.replace(at, from.size(), to);
    }
    return n;
  };
  auto count = [](int c) {
    if (c < 0) return std::string(" --");
    std::string t = std::to_string(c);
    while (t.size() < 3) t = " " + t;
    return t;
  };
  // Fourteen characters of name, so a line is twenty and the two halves of
  // the screen keep a column between them. The few names that run to
  // fifteen lose the spaces round their arrow.
  auto squeeze = [](std::string n) {
    if (n.size() > 14) {
      const size_t at = n.find(" <- ");
      if (at != std::string::npos) n.replace(at, 4, "<-");
    }
    return n;
  };
  std::string ram = ".ram\ncycles_n:    db " + std::to_string(ops().size()) + "\ncycles_addr:\n";
  std::string data = ".data\n";
  int i = 0;
  for (const OpDef& op : ops()) {
    const std::string label = "cyc_" + std::to_string(i++);
    std::string line = squeeze(shortName(op.name));
    while (line.size() < 14) line += ' ';
    line += count(cost(mine, op.name)) + count(cost(best, op.name));
    ram += "        db get_bankbyte(" + label + "), get_highbyte(" + label + "), get_lowbyte(" + label + ")\n";
    data += label + ": db \"" + line + "\", 0\n";
  }
  return "; cycles.asm: generated by the build from the microcode set. Do not edit.\n" + ram + data;
}

Built buildSources(const std::vector<Source>& sources, const Assets& assets, const Options& opts,
                   const std::string& title, const std::string& where, const std::optional<Cartridge>& base,
                   const std::vector<std::string>& assetNames) {
  Built b;
  auto ext = [](const std::string& n) { return fs::path(n).extension().string(); };
  std::vector<const Source*> cFiles, hFiles, asmFiles, basFiles;
  const Source* microcodeFile = nullptr;
  for (const Source& src : sources) {
    const std::string e = ext(src.name);
    if (e == ".c") cFiles.push_back(&src);
    else if (e == ".h") hFiles.push_back(&src);
    else if (e == ".asm") asmFiles.push_back(&src);
    else if (e == ".bas") basFiles.push_back(&src);
    else if (src.name == "microcode.txt") microcodeFile = &src;
  }
  auto byName = [](const Source* x, const Source* y) { return x->name < y->name; };
  std::sort(cFiles.begin(), cFiles.end(), byName);
  std::sort(asmFiles.begin(), asmFiles.end(), byName);
  std::sort(basFiles.begin(), basFiles.end(), byName);
  const bool onBase = base && cFiles.empty() && asmFiles.empty();
  const bool basicProject = !onBase && cFiles.empty() && !basFiles.empty();
  // BASIC and C together: one program, the interpreter's sources and the
  // user's compiled as one. The .bas files are slots as in a BASIC
  // project, and BASIC calls the C by name with CALL.
  const bool mixedProject = !cFiles.empty() && !basFiles.empty();
  if (!onBase && cFiles.empty() && asmFiles.empty() && basFiles.empty()) {
    b.errors.push_back(where + " holds no .c, .asm or .bas file");
    return b;
  }
  auto at = [&](const std::string& name) { return where.empty() ? name : (fs::path(where) / name).string(); };

  // The microcode set: asked for, or microcode.txt, or the kind's default.
  // BASIC runs on the optimal set, the way its own ROM is burned.
  std::string microcode = opts.microcode;
  if (microcode.empty()) {
    if (microcodeFile) {
      McParsed parsed = parseMicrocode(microcodeFile->text);
      for (const McError& e : parsed.errors) {
        b.errors.push_back(at("microcode.txt") + ":" + std::to_string(e.line) + ": " + e.message);
      }
      if (!parsed.errors.empty()) return b;
      microcode = microcodeFile->text;
      for (const std::string& w : rowWarnings(microcodeFile->text, parsed.microcode)) {
        b.notes.push_back(at("microcode.txt") + ":" + w);
      }
    } else if (onBase) {
      microcode = base->microcode;
    } else {
      microcode = (basicProject || mixedProject) ? "@optimal" : "@naive";
    }
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

  // The C program's __ROM objects, so its asset initializers join ASET.
  std::vector<cc::RomEntry> romEntries;
  if (!cFiles.empty()) {
    cc::CcOptions ccOpts;
    ccOpts.defines = opts.defines;
    ccOpts.assets = &assets;
    std::vector<CcInput> inputs;
    if (mixedProject) {
#if SC8_HAVE_BASIC
      // The interpreter's files go in first, so main is the interpreter's
      // and the program boots into BASIC. They keep their system page.
      // Every public function in the user's files is a root: BASIC calls
      // them by slot, which the compiler cannot see. The interpreter's
      // build line names its own files, which would leave the user's
      // out, so it is turned into a plain comment of the same length.
      ccOpts.zpReserve = basic::SYSTEM_PAGE_SIZE;
      ccOpts.heapStackTop = basic::SCREEN;
      for (const auto& [name, text] : basicSources()) {
        if (name.ends_with(".h")) {
          ccOpts.extra[name] = text;
          continue;
        }
        std::string t = text;
        const size_t pos = t.find("// build:");
        if (pos != std::string::npos) t.replace(pos, 9, "// built:");
        inputs.push_back({"basic/" + name, t});
      }
      b.sources.push_back("the BASIC interpreter");
      for (const Source* h : hFiles) {
        if (h->name == "basic.h") {
          b.errors.push_back(at(h->name) + ": basic.h is the interpreter's header in a project with BASIC. Rename this one.");
          return b;
        }
      }
#else
      b.errors.push_back("this build has no BASIC interpreter to put a .bas project on");
      return b;
#endif
    }
    // What BASIC and the assembly files call by name comes first: those C
    // functions are the roots nothing in the C can show. The rest of the C
    // is kept only if one of them, or main, reaches it.
    for (const Source* p : basFiles) {
      for (const std::string& n : basicCallNames(p->text)) ccOpts.externalCalls.insert(n);
    }
    for (const Source* p : asmFiles) {
      for (const std::string& n : asmNames(p->text)) ccOpts.externalCalls.insert(n);
    }
    for (const Source* c : cFiles) {
      inputs.push_back({c->name, c->text});
      b.sources.push_back(c->name);
      if (mixedProject) ccOpts.guestFiles.insert(c->name);
    }
    for (const Source* h : hFiles) ccOpts.extra[h->name] = h->text;
    CcResult r = compile(inputs, ccOpts);
    for (const std::string& e : r.errors) b.errors.push_back(e);
    if (!r.errors.empty()) return b;
    append("generated.asm", r.assembly);
    romEntries = std::move(r.rom);
  } else if (basicProject) {
#if SC8_HAVE_BASIC
    append("basic.asm", std::string(basicAsm()));
    b.sources.push_back("the BASIC interpreter");
#else
    b.errors.push_back("this build has no BASIC interpreter to put a .bas project on");
    return b;
#endif
  }
  for (const Source* p : asmFiles) {
    if (p->name == "generated.asm") continue;
    b.sources.push_back(p->name);
    append(p->name, p->text);
  }
  if (microcodeFile && !onBase) {
    const std::string unit = cyclesUnit(microcode, &b.errors);
    if (!unit.empty()) append("cycles.asm", unit);
  }
  auto whereLine = [&](int line) -> std::string {
    const Span* s = &spans.front();
    for (const Span& sp : spans) {
      if (sp.firstLine <= line) s = &sp;
    }
    if (s->file == "basic.asm") return "the BASIC interpreter:" + std::to_string(line - s->firstLine);
    return at(s->file) + ":" + std::to_string(line - s->firstLine);
  };

  Cartridge c;
  if (onBase) {
    // An opened ROM: its program stays, the sources bring the slots and
    // the set. Its labels are gone, so CALL takes numbers only.
    c = *base;
    c.basic.clear();
  } else {
    b.assembled = assemble(b.assembly, &assets);
    for (const AsmError& e : b.assembled.errors) b.errors.push_back(whereLine(e.line) + ": " + e.message);
    if (!b.errors.empty()) return b;
    c = b.assembled.cartridge();
    // An asset initializer in C is a directive too: it joins ASET under its
    // label, as .image does, and the file it read counts as placed.
    std::vector<RomAsset> placed = b.assembled.placedFiles;
    for (const cc::RomEntry& e : romEntries) {
      if (e.assetKind.empty()) continue;
      auto label = b.assembled.labels.find(e.name);
      if (label == b.assembled.labels.end()) continue;
      const auto offset = static_cast<uint32_t>(label->second.value);
      c.assets.push_back({e.assetKind, e.name, offset, static_cast<uint32_t>(e.size)});
      placed.push_back({e.assetKind, e.assetFile, offset, static_cast<uint32_t>(e.size)});
    }
    if (basicProject || mixedProject) {
      placeForBasic(c, placed, assets, assetNames, b);
      if (!b.errors.empty()) return b;
    }
    b.instructions = b.assembled.program.size();
    b.ramBytes = b.assembled.ramLength;
    b.dataBytes = b.assembled.cart.size();
  }
  c.microcode = microcode;
  std::vector<std::pair<std::string, std::string>> meta = opts.meta;
  bool titled = false;
  for (const auto& [k, v] : meta) titled = titled || k == "title";
  if (!titled) {
    bool baseTitled = false;
    if (onBase) {
      for (const auto& [k, v] : c.meta) baseTitled = baseTitled || k == "title";
    }
    if (!baseTitled || !onBase) meta.emplace_back("title", title);
    else meta = c.meta;
  }
  c.meta = meta;
  for (const Source* p : basFiles) {
    std::vector<LabelError> unknown;
    const std::string resolved = onBase ? p->text : resolveLabels(p->text, b.assembled.labels, unknown);
    for (const LabelError& e : unknown) {
      b.errors.push_back(at(p->name) + ":" + std::to_string(e.line) + ": " + e.name +
                         " is not a label in this project. CALL and JMP take a slot number or the name of a C "
                         "function or an assembly label.");
    }
    c.basic.emplace_back(slotNameFor(p->name), resolved);
    b.sources.push_back(p->name);
  }
  if (!b.errors.empty()) return b;
  b.cartridge = std::move(c);
  return b;
}

Built build(const Layout& layout, const Options& opts) {
  std::vector<Source> sources;
  for (const char* ext : {".c", ".h", ".asm", ".bas"}) {
    for (const fs::path& p : filesWith(layout.sources, ext)) {
      auto t = readText(p);
      if (!t) {
        Built b;
        b.errors.push_back("cannot read " + p.string());
        return b;
      }
      sources.push_back({p.filename().string(), *t});
    }
  }
  if (auto t = readText(layout.sources / "microcode.txt")) sources.push_back({"microcode.txt", *t});
  std::vector<std::string> notes;
  Assets assets = loaders(layout, &notes);
  const std::vector<fs::path> assetPaths = assetFiles(layout);
  std::vector<std::string> assetNames;
  for (const fs::path& p : assetPaths) assetNames.push_back(p.filename().string());
  Built b = buildSources(sources, assets, opts, titleFor(layout), layout.sources.string(), std::nullopt, assetNames);
  b.notes.insert(b.notes.begin(), notes.begin(), notes.end());
  if (b.cartridge && opts.embedSources) {
    // The ROM carries the project: the README, every file in assets/, and
    // the sources. A picture beside the sources in a flat project is an
    // asset too.
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    if (auto readme = readBytes(layout.root / "README.md")) files.emplace_back("README.md", *readme);
    for (const fs::path& p : assetPaths) {
      if (auto bytes = readBytes(p)) files.emplace_back("assets/" + p.filename().string(), *bytes);
    }
    embed(*b.cartridge, sources, files);
  }
  return b;
}

namespace {

const char* const README_C = R"(# %NAME%

A C program for the SimpleCPU-8. The sources are in src/, pictures and
sounds go in assets/, and the ROM lands in build/.

    simplecpu-make .
    simplecpu --rom build/%NAME%.rom

src/main.c draws a circle you steer with the arrow keys. graphics.h,
sound.h, keys.h, math.h and disk.h are the libraries to include.
)";

const char* const MAIN_C = R"(/* %NAME%: a circle you steer with the arrow keys. */
#include <graphics.h>
#include <keys.h>
#include <sound.h>

int x = 128;
int y = 128;

int main(void)
{
    sound_init();
    while (1) {
        if (left() && x > 20) x = x - 2;
        if (right() && x < 235) x = x + 2;
        if (up() && y > 20) y = y - 2;
        if (down() && y < 235) y = y + 2;
        if (fire()) beep(C5, 2);

        cls();
        setcolor(YELLOW);
        fillcircle(x, y, 20);
        setcolor(WHITE);
        at(1, 1);
        printf("X %d  Y %d", x, y);
        nextframe();
    }
}
)";

const char* const README_BASIC = R"(# %NAME%

A BASIC program for the SimpleCPU-8. The program is src/autorun.bas: it
goes into the ROM under the name AUTORUN, and a slot of that name runs
when the ROM boots. Other .bas files become slots of their own, which
!LOAD "NAME" fetches. An .asm file in src/ is a driver, appended to the
interpreter.

    simplecpu-make .
    simplecpu --rom build/%NAME%.rom
)";

const char* const AUTORUN_BAS = R"(10 REM %NAME%
20 CLS
30 FOR I = 1 TO 10
40 PRINT "HELLO FROM %NAME% "; I
50 NEXT I
60 PRINT "TYPE LIST TO SEE ME, RUN TO RUN ME AGAIN"
70 END
)";

const char* const README_ASM = R"(# %NAME%

An assembly program for the SimpleCPU-8. Every .asm in src/ goes into the
ROM in name order, and src/main.asm is the first. Pictures and sounds in
assets/ are placed with .image, .sample and .file.

    simplecpu-make .
    simplecpu --rom build/%NAME%.rom
)";

const char* const MAIN_ASM = R"(; %NAME%: a bouncing dot, in assembly.
; Each frame plots the dot in a fresh colour, moves it, and turns it round
; at an edge. GPU_FRAME ticks once a frame, so waiting for it to change is
; the pacing. The frame counter is the colour, with the red bits kept on
; so it stays bright, and the trail cycles through the palette.
loop:   OUT GPU_X_HI, 0
        LD A <- [x]
        OUTA GPU_X
        OUT GPU_Y_HI, 0
        LD A <- [y]
        OUTA GPU_Y
        IN GPU_FRAME
        OR A <- 0xE0
        OUTA GPU_PIXEL
        OUT GPU_CMD, CMD_PLOT
        LD A <- [x]
        ADD A <- [dx]
        LD [x] <- A
        JZ turnx
        SUB A <- 255
        JNZ ystep
turnx:  LD A <- [dx]
        XOR A <- 0xFE
        LD [dx] <- A
ystep:  LD A <- [y]
        ADD A <- [dy]
        LD [y] <- A
        JZ turny
        SUB A <- 255
        JNZ wait
turny:  LD A <- [dy]
        XOR A <- 0xFE
        LD [dy] <- A
wait:   IN GPU_FRAME
        LD [frame] <- A
again:  IN GPU_FRAME
        SUB A <- [frame]
        JZ again
        JMP loop
.ram
x:      db 10
y:      db 20
dx:     db 1
dy:     db 1
frame:  db 0
)";

const char* const CYCLES_ASM = R"(; %NAME%: what every instruction costs, in microcycles, under this ROM's
; own microcode set (MY) and under the optimal set (OP).
;
; The build counts the rows, the fetch program plus the instruction's own,
; from src/microcode.txt and from the optimal set, and writes the lines
; into cycles.asm, which it generates and appends to this file. Change a
; row in microcode.txt, build, run: the MY column moves.
;
; Forty instructions a page, two columns of twenty. A key turns the page,
; and so do four seconds. cycles_n says how many there are.
        OUT GPU_TEXT_COLOR, 0xFF
        OUT GPU_TEXT_BG, 0
        OUT GPU_TEXT_FLAGS, 0
        OUT GPU_CMD, CMD_TEXT_STYLE
        LD A <- 0
        LD [page] <- A
show:   OUT GPU_CMD, CMD_TEXT_CLEAR
        OUT GPU_TEXT_COL, 0
        OUT GPU_TEXT_ROW, 0
        OUT GPU_CMD, CMD_TEXT_AT
        JSR head
        OUT GPU_TEXT_COL, 21
        OUT GPU_TEXT_ROW, 0
        OUT GPU_CMD, CMD_TEXT_AT
        JSR head
        LD A <- 0
        LD [col] <- A
        LD A <- 2
        LD [row] <- A
        ; D1 walks the address table, three bytes an entry. Each page
        ; before this one skips forty entries and takes forty off left.
        LD D1 <- &cycles_addr
        LD A <- [cycles_n]
        LD [left] <- A
        LD A <- [page]
        JZ fit
skip:   LD [count] <- A
        LD D1 <- D1+120
        LD A <- [left]
        SUB A <- 40
        LD [left] <- A
        LD A <- [count]
        DEC A
        JNZ skip
        ; More than forty left means another page follows this one.
fit:    LD A <- 0
        LD [more] <- A
        LD A <- [left]
        CMP A, 41
        JC entry
        LD A <- 1
        LD [more] <- A
        LD A <- 40
        LD [left] <- A
entry:  LD A <- [left]
        JZ foot
        LD A <- [col]
        OUTA GPU_TEXT_COL
        LD A <- [row]
        OUTA GPU_TEXT_ROW
        OUT GPU_CMD, CMD_TEXT_AT
        LD A <- [D1]+
        OUTA GPU_CART_BANK
        LD A <- [D1]+
        OUTA GPU_CART_HI
        LD A <- [D1]+
        OUTA GPU_CART_LO
        OUT GPU_CMD, CMD_PRINTF
        LD A <- [left]
        DEC A
        LD [left] <- A
        LD A <- [row]
        INC A
        LD [row] <- A
        CMP A, 22
        JNZ entry
        LD A <- 2
        LD [row] <- A
        LD A <- 21
        LD [col] <- A
        JMP entry
foot:   OUT GPU_TEXT_COL, 0
        OUT GPU_TEXT_ROW, 30
        OUT GPU_CMD, CMD_TEXT_AT
        OUT GPU_CART_BANK, get_bankbyte(footer)
        OUT GPU_CART_HI, get_highbyte(footer)
        OUT GPU_CART_LO, get_lowbyte(footer)
        OUT GPU_CMD, CMD_PRINTF
        LD A <- 0
        LD [ticks] <- A
        IN GPU_FRAME
        LD [frame] <- A
; IN leaves the flags alone, so the byte is kept and tested with TST.
poll:   IN IO_KEY -> A
        TST A, 0x7F
        JZ tick
        TST A, 0x80
        JZ flip
        JMP poll
tick:   IN GPU_FRAME
        CMP A, [frame]
        JZ poll
        LD [frame] <- A
        LD A <- [ticks]
        INC A
        LD [ticks] <- A
        CMP A, 240
        JNZ poll
flip:   LD A <- [more]
        JZ first
        LD A <- [page]
        INC A
        LD [page] <- A
        JMP show
first:  LD A <- 0
        LD [page] <- A
        JMP show
head:   OUT GPU_CART_BANK, get_bankbyte(header)
        OUT GPU_CART_HI, get_highbyte(header)
        OUT GPU_CART_LO, get_lowbyte(header)
        OUT GPU_CMD, CMD_PRINTF
        RET
.ram
page:   db 0
count:  db 0
more:   db 0
col:    db 0
row:    db 0
left:   db 0
frame:  db 0
ticks:  db 0
.data
header: db "INSTRUCTION   MY OP", 0
footer: db "MY = MICROCODE.TXT   OP = OPTIMAL   KEY", 0
)";

const char* const README_MICROCODE = R"(# %NAME%

An assembly program with its own microcode set. src/microcode.txt started
as the naive set, one section per instruction, one row of signals per
line. Edit a row, build, and the machine runs your rows. The IDE's
CPU level shows them firing.

src/main.asm shows what every instruction costs: its microcycles under
your set and under the optimal set, side by side. The build counts the
rows and writes them into the ROM, so the table is the truth about the
set the ROM carries. Merge two rows, build again, run again, and watch
the number drop.

    simplecpu-make .
    simplecpu --rom build/%NAME%.rom
)";

// A file written only when the folder holds nothing of that name.
void put(const fs::path& p, std::string text, const std::string& name, Created& out) {
  size_t at;
  while ((at = text.find("%NAME%")) != std::string::npos) text.replace(at, 6, name);
  std::ofstream o(p);
  o << text;
  if (!o) out.error = "cannot write " + p.string();
  else out.files.push_back(p);
}

}  // namespace

Created create(const fs::path& dir, Kind kind) {
  Created out;
  std::error_code ec;
  if (fs::exists(dir) && !fs::is_empty(dir, ec)) {
    out.error = dir.string() + " exists and is not empty";
    return out;
  }
  fs::create_directories(dir / "src", ec);
  if (ec) {
    out.error = "cannot create " + (dir / "src").string();
    return out;
  }
  const std::string name = fs::absolute(dir).filename().string();
  put(dir / ".gitignore", "build/\n", name, out);
  switch (kind) {
    case Kind::C:
      fs::create_directories(dir / "assets", ec);
      put(dir / "README.md", README_C, name, out);
      put(dir / "src" / "main.c", MAIN_C, name, out);
      break;
    case Kind::Basic:
      put(dir / "README.md", README_BASIC, name, out);
      put(dir / "src" / "autorun.bas", AUTORUN_BAS, name, out);
      break;
    case Kind::Assembly:
      fs::create_directories(dir / "assets", ec);
      put(dir / "README.md", README_ASM, name, out);
      put(dir / "src" / "main.asm", formatAssembly(MAIN_ASM), name, out);
      break;
    case Kind::Microcode:
      fs::create_directories(dir / "assets", ec);
      put(dir / "README.md", README_MICROCODE, name, out);
      put(dir / "src" / "main.asm", formatAssembly(CYCLES_ASM), name, out);
      put(dir / "src" / "microcode.txt",
          "# " + name + ": the naive microcode set, yours to change.\n# One section per instruction, one row of "
          "signals per line.\n\n" + serializeMicrocode(buildNaive()),
          name, out);
      break;
  }
  return out;
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
