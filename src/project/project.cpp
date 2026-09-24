#include "project/project.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

#include "asm/asm.h"
#include "assets/assets.h"
#include "cc/cc.h"
#include "core/cartridge.h"
#include "core/isa.h"
#include "core/mcparse.h"
#include "core/microcode.h"
#if SC8_HAVE_BASIC
#include "basic/basic_rom.h"
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
      if (upper != "CALL" && upper != "JSR" && upper != "JMP") continue;
      size_t j = i;
      while (j < line.size() && line[j] == ' ') j++;
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

// The cycle table for a microcode project. Every row of a microprogram is
// one microcycle and every row runs, so an instruction's cost is the fetch
// program's rows plus its own. Counted here, at build time, from the
// project's own set and from the optimal set. A program prints the lines
// with CMD_PRINTF and finds them through cycles_addr, a table of cartridge
// addresses laid down in RAM, three bytes each.
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

Built build(const Layout& layout, const Options& opts) {
  Built b;
  const std::vector<fs::path> cFiles = filesWith(layout.sources, ".c");
  const std::vector<fs::path> hFiles = filesWith(layout.sources, ".h");
  const std::vector<fs::path> asmFiles = filesWith(layout.sources, ".asm");
  const std::vector<fs::path> basFiles = filesWith(layout.sources, ".bas");
  const bool basicProject = cFiles.empty() && !basFiles.empty();
  // BASIC and C together: one program, the interpreter's sources and the
  // user's compiled as one. The .bas files are slots as in a BASIC
  // project, and BASIC calls the C by name with CALL.
  const bool mixedProject = !cFiles.empty() && !basFiles.empty();
  if (cFiles.empty() && asmFiles.empty() && basFiles.empty()) {
    b.errors.push_back(layout.sources.string() + " holds no .c, .asm or .bas file");
    return b;
  }

  // The microcode set: asked for, or microcode.txt, or the kind's default.
  // BASIC runs on the optimal set, the way its own ROM is burned.
  std::string microcode = opts.microcode;
  if (microcode.empty()) {
    if (auto text = readText(layout.sources / "microcode.txt")) {
      McParsed parsed = parseMicrocode(*text);
      for (const McError& e : parsed.errors) {
        b.errors.push_back((layout.sources / "microcode.txt").string() + ":" + std::to_string(e.line) + ": " + e.message);
      }
      if (!parsed.errors.empty()) return b;
      microcode = *text;
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

  // The compiler and the assembler resolve an asset name the same way, so
  // one Assets serves both.
  Assets assets = loaders(layout, &b.notes);

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
      // build line names its own nine files, which would leave the user's
      // out, so it is turned into a plain comment of the same length.
      ccOpts.zpReserve = 32;
      for (const auto& [name, text] : basicSources()) {
        if (name == "basic.h") {
          ccOpts.extra[name] = text;
          continue;
        }
        std::string t = text;
        const size_t at = t.find("// build:");
        if (at != std::string::npos) t.replace(at, 9, "// built:");
        inputs.push_back({"basic/" + name, t});
      }
      b.sources.push_back("the BASIC interpreter");
      for (const fs::path& p : hFiles) {
        if (p.filename() == "basic.h") {
          b.errors.push_back(p.string() + ": basic.h is the interpreter's header in a project with BASIC. Rename this one.");
          return b;
        }
      }
#else
      b.errors.push_back("this build has no BASIC interpreter to put a .bas project on");
      return b;
#endif
    }
    for (const fs::path& p : cFiles) {
      auto t = readText(p);
      if (!t) {
        b.errors.push_back("cannot read " + p.string());
        return b;
      }
      inputs.push_back({p.filename().string(), *t});
      b.sources.push_back(p.filename().string());
      if (mixedProject) ccOpts.keepAllFrom.insert(p.filename().string());
    }
    for (const fs::path& p : hFiles) {
      if (auto t = readText(p)) ccOpts.extra[p.filename().string()] = *t;
    }
    CcResult r = compile(inputs, ccOpts);
    for (const std::string& e : r.errors) b.errors.push_back(e);
    if (!r.errors.empty()) return b;
    append("generated.asm", r.assembly);
  } else if (basicProject) {
#if SC8_HAVE_BASIC
    append("basic.asm", std::string(basicAsm()));
    b.sources.push_back("the BASIC interpreter");
#else
    b.errors.push_back("this build has no BASIC interpreter to put a .bas project on");
    return b;
#endif
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
  if (fs::exists(layout.sources / "microcode.txt")) {
    const std::string unit = cyclesUnit(microcode, &b.errors);
    if (!unit.empty()) append("cycles.asm", unit);
  }
  auto where = [&](int line) -> std::string {
    const Span* s = &spans.front();
    for (const Span& sp : spans) {
      if (sp.firstLine <= line) s = &sp;
    }
    if (s->file == "basic.asm") return "the BASIC interpreter:" + std::to_string(line - s->firstLine);
    return (layout.sources / s->file).string() + ":" + std::to_string(line - s->firstLine);
  };

  Assembled a = assemble(b.assembly, &assets);
  for (const AsmError& e : a.errors) b.errors.push_back(where(e.line) + ": " + e.message);
  if (!a.errors.empty()) return b;

  Cartridge c = a.cartridge();
  c.microcode = microcode;
  std::vector<std::pair<std::string, std::string>> meta = opts.meta;
  bool titled = false;
  for (const auto& [k, v] : meta) titled = titled || k == "title";
  if (!titled) meta.emplace_back("title", titleFor(layout));
  c.meta = meta;
  for (const fs::path& p : basFiles) {
    if (auto t = readText(p)) {
      std::vector<LabelError> unknown;
      const std::string resolved = resolveLabels(*t, a.labels, unknown);
      for (const LabelError& e : unknown) {
        b.errors.push_back(p.string() + ":" + std::to_string(e.line) + ": " + e.name +
                           " is not a label in this project. CALL and JMP take a slot number or the name of a C "
                           "function or an assembly label.");
      }
      c.basic.emplace_back(slotNameFor(p), resolved);
      b.sources.push_back(p.filename().string());
    }
  }
  if (!b.errors.empty()) return b;
  b.instructions = a.program.size();
  b.ramBytes = a.ramLength;
  b.dataBytes = a.cart.size();
  b.cartridge = std::move(c);
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
        JZ turnx
        JMP ystep
turnx:  LD A <- [dx]
        XOR A <- 0xFE
        LD [dx] <- A
ystep:  LD A <- [y]
        ADD A <- [dy]
        LD [y] <- A
        JZ turny
        SUB A <- 255
        JZ turny
        JMP wait
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
; Eighty instructions in two pages of forty, two columns of twenty. A key
; turns the page, and so do four seconds.
        OUT GPU_TEXT_COLOR, 0xFF
        OUT GPU_TEXT_BG, 0
        OUT GPU_TEXT_FLAGS, 0
        OUT GPU_CMD, CMD_TEXT_STYLE
        LD A <- 0
        LD [first] <- A
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
        LD A <- 40
        LD [left] <- A
        ; D1 walks the address table: three bytes an entry, and page two
        ; starts forty entries in.
        LD D1 <- &cycles_addr
        LD A <- [first]
        JZ entry
        LD D1 <- &cycles_addr + 120
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
        SUB A <- 1
        LD [left] <- A
        LD A <- [row]
        ADD A <- 1
        LD [row] <- A
        SUB A <- 22
        JZ nextcol
        JMP entry
nextcol: LD A <- 2
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
; IN leaves the flags alone, so the byte is kept and tested with AND.
poll:   IN IO_KEY -> A
        LD [key] <- A
        AND A <- 0x7F
        JZ tick
        LD A <- [key]
        AND A <- 0x80
        JZ flip
        JMP poll
tick:   IN GPU_FRAME
        SUB A <- [frame]
        JZ poll
        IN GPU_FRAME
        LD [frame] <- A
        LD A <- [ticks]
        ADD A <- 1
        LD [ticks] <- A
        SUB A <- 240
        JZ flip
        JMP poll
flip:   LD A <- [first]
        XOR A <- 40
        LD [first] <- A
        JMP show
head:   OUT GPU_CART_BANK, get_bankbyte(header)
        OUT GPU_CART_HI, get_highbyte(header)
        OUT GPU_CART_LO, get_lowbyte(header)
        OUT GPU_CMD, CMD_PRINTF
        RET
.ram
first:  db 0
col:    db 0
row:    db 0
left:   db 0
frame:  db 0
ticks:  db 0
key:    db 0
.data
header: db "INSTRUCTION   MY OP", 0
footer: db "MY = MICROCODE.TXT   OP = OPTIMAL   KEY", 0
)";

const char* const README_MICROCODE = R"(# %NAME%

An assembly program with its own microcode set. src/microcode.txt started
as the naive set, one section per instruction, one row of signals per
line. Edit a row, build, and the machine runs your rows. The IDE's
Microcode level shows them firing.

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
      put(dir / "src" / "main.asm", MAIN_ASM, name, out);
      break;
    case Kind::Microcode:
      fs::create_directories(dir / "assets", ec);
      put(dir / "README.md", README_MICROCODE, name, out);
      put(dir / "src" / "main.asm", CYCLES_ASM, name, out);
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
