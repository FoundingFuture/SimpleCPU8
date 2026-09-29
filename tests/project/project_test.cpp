// A project folder into a ROM: the layouts, the four kinds simplecpu-make
// new lays out, and the cycle table a microcode project carries.

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

#include "asm/asm.h"
#include "core/isa.h"
#include "project/project.h"
#include "assets/assets.h"
#include "assets/font.h"
#include "assets/sprite.h"
#if SC8_HAVE_BASIC
#include "basic/program.h"
#include "devices/font.h"
#include "support/basic_session.h"
#endif

namespace fs = std::filesystem;
using namespace sc8;

namespace {

// A fresh temporary folder per test, removed when the test ends.
struct TempDir {
  fs::path path;
  TempDir() {
    path = fs::temp_directory_path() / fs::path("sc8-project-" + std::to_string(std::rand()));
    fs::remove_all(path);
  }
  ~TempDir() { fs::remove_all(path); }
};

void write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << text;
}

}  // namespace

// The IDE builds an example from its open documents, since it never writes
// them into the example's folder. Everything else comes from the folder.
TEST_SUITE("a build from sources in memory") {
  TEST_CASE("the folder's own sources build what the folder builds") {
    TempDir t;
    write(t.path / "src" / "main.asm", "        HLT\n.data\nship: .file('ship.bin')\n");
    write(t.path / "assets" / "ship.bin", "AB");
    write(t.path / "README.md", "# Ships\n");
    const project::Layout l = project::layoutOf(t.path);
    const project::Built disk = project::build(l, {});
    REQUIRE_MESSAGE(disk.cartridge, (disk.errors.empty() ? std::string() : disk.errors[0]));
    const project::Built mem =
        project::build(l, {{"main.asm", "        HLT\n.data\nship: .file('ship.bin')\n"}}, {});
    REQUIRE(mem.cartridge);
    CHECK(encodeCartridge(*mem.cartridge) == encodeCartridge(*disk.cartridge));
  }

  TEST_CASE("an edited source builds as edited, with the folder's title and assets") {
    TempDir t;
    write(t.path / "src" / "main.asm", "        HLT\n");
    write(t.path / "assets" / "ship.bin", "AB");
    write(t.path / "README.md", "# Ships\n");
    const project::Layout l = project::layoutOf(t.path);
    const project::Built mem = project::build(
        l, {{"main.asm", "        LD A <- 7\n        LD [r] <- A\n        HLT\n.ram\nr: ds 1\n"}}, {});
    REQUIRE_MESSAGE(mem.cartridge, (mem.errors.empty() ? std::string() : mem.errors[0]));
    CHECK_EQ(mem.instructions, 3u);
    bool titled = false;
    for (const auto& [k, v] : mem.cartridge->meta) titled = titled || (k == "title" && v == "Ships");
    CHECK(titled);
    bool carried = false;
    for (const auto& [name, bytes] : mem.cartridge->sources) carried = carried || name == "assets/ship.bin";
    CHECK(carried);
    // Nothing was written: the folder's source is as it was, and no ROM.
    std::ifstream in(t.path / "src" / "main.asm");
    CHECK_EQ(std::string(std::istreambuf_iterator<char>(in), {}), "        HLT\n");
    CHECK_FALSE(fs::exists(t.path / "build"));
  }
}

// Save as in the IDE writes the sources itself. The rest of a project,
// its README and its assets, comes along through copyProjectFiles.
TEST_SUITE("copying a project's other files") {
  TEST_CASE("a flat project's assets go into assets/, and its README with them") {
    TempDir from;
    TempDir to;
    write(from.path / "main.asm", "        HLT\n");
    write(from.path / "ship.png", "PNG bytes");
    write(from.path / "tune.wav", "WAV bytes");
    write(from.path / "README.md", "# Flat\nThe readme.\n");
    const project::Created c = project::copyProjectFiles(project::layoutOf(from.path), to.path);
    REQUIRE_MESSAGE(c.error.empty(), c.error);
    auto text = [](const fs::path& p) {
      std::ifstream in(p);
      return std::string(std::istreambuf_iterator<char>(in), {});
    };
    CHECK_EQ(text(to.path / "assets" / "ship.png"), "PNG bytes");
    CHECK_EQ(text(to.path / "assets" / "tune.wav"), "WAV bytes");
    CHECK_EQ(text(to.path / "README.md"), "# Flat\nThe readme.\n");
    // The sources are Save as's to write, not this.
    CHECK_FALSE(fs::exists(to.path / "main.asm"));
    CHECK_FALSE(fs::exists(to.path / "assets" / "main.asm"));
    CHECK_EQ(c.files.size(), 3u);
  }

  TEST_CASE("a project with src/ brings every file in its assets/") {
    TempDir from;
    TempDir to;
    write(from.path / "src" / "main.c", "int main(void) { return 0; }\n");
    write(from.path / "assets" / "ball.png", "ball");
    write(from.path / "assets" / "wall.font", "font");
    write(from.path / "README.md", "# Nested\n");
    const project::Created c = project::copyProjectFiles(project::layoutOf(from.path), to.path);
    REQUIRE_MESSAGE(c.error.empty(), c.error);
    CHECK(fs::exists(to.path / "assets" / "ball.png"));
    CHECK(fs::exists(to.path / "assets" / "wall.font"));
    CHECK(fs::exists(to.path / "README.md"));
    CHECK_FALSE(fs::exists(to.path / "src" / "main.c"));
    // The copy builds with the assets where the larger layout looks.
    write(to.path / "src" / "main.c", "int main(void) { return 0; }\n");
    const project::Layout l = project::layoutOf(to.path);
    CHECK_EQ(project::assetFiles(l).size(), 2u);
  }

  TEST_CASE("an asset of the same name in the target is replaced") {
    TempDir from;
    TempDir to;
    write(from.path / "src" / "main.asm", "        HLT\n");
    write(from.path / "assets" / "ship.png", "new");
    write(to.path / "assets" / "ship.png", "old");
    REQUIRE(project::copyProjectFiles(project::layoutOf(from.path), to.path).error.empty());
    std::ifstream in(to.path / "assets" / "ship.png");
    CHECK_EQ(std::string(std::istreambuf_iterator<char>(in), {}), "new");
  }

  TEST_CASE("a project saved as its own folder copies nothing") {
    TempDir t;
    write(t.path / "src" / "main.asm", "        HLT\n");
    write(t.path / "assets" / "ship.png", "ship");
    write(t.path / "README.md", "# Same\n");
    const project::Created c = project::copyProjectFiles(project::layoutOf(t.path), t.path);
    CHECK(c.error.empty());
    CHECK(c.files.empty());
    CHECK(fs::exists(t.path / "assets" / "ship.png"));
  }
}

TEST_SUITE("project layouts") {
  TEST_CASE("an empty path is the working directory, as a bare file name's parent is") {
    const project::Layout l = project::layoutOf(fs::path("pong.asm").parent_path());
    CHECK(l.name == fs::current_path().filename().string());
    CHECK(l.sources == (l.nested ? fs::path("src") : fs::path()));
  }

  TEST_CASE("a flat folder is its own sources, assets and build") {
    TempDir t;
    write(t.path / "main.c", "int main(void) { return 0; }\n");
    const project::Layout l = project::layoutOf(t.path);
    CHECK_FALSE(l.nested);
    CHECK(l.sources == t.path);
    CHECK(l.build == t.path);
    project::Written w = project::buildAndWrite(l, {});
    REQUIRE_MESSAGE(w.built.cartridge, (w.built.errors.empty() ? std::string() : w.built.errors[0]));
    CHECK(fs::exists(t.path / (l.name + ".rom")));
  }

  TEST_CASE("a folder with src/ builds into build/ and looks for assets in assets/") {
    TempDir t;
    write(t.path / "src" / "main.c", "int main(void) { return 0; }\n");
    write(t.path / "README.md", "# The Title\n");
    fs::create_directories(t.path / "assets");
    const project::Layout l = project::layoutOf(t.path);
    CHECK(l.nested);
    CHECK(l.assets == t.path / "assets");
    project::Written w = project::buildAndWrite(l, {});
    REQUIRE(w.built.cartridge);
    CHECK(w.rom == t.path / "build" / (l.name + ".rom"));
    bool titled = false;
    for (const auto& [k, v] : w.built.cartridge->meta) titled = titled || (k == "title" && v == "The Title");
    CHECK(titled);
  }

  TEST_CASE("an empty folder is refused with a message") {
    TempDir t;
    fs::create_directories(t.path / "src");
    project::Built b = project::build(project::layoutOf(t.path), {});
    CHECK_FALSE(b.cartridge);
    REQUIRE_EQ(b.errors.size(), 1u);
    CHECK(b.errors[0].find("no .c, .asm or .bas") != std::string::npos);
  }

  TEST_CASE("microcode.txt among the sources is the ROM's set") {
    TempDir t;
    write(t.path / "src" / "main.asm", "HLT\n");
    write(t.path / "src" / "microcode.txt", "fetch:\n  FETCH\nHLT:\n  HALT\n");
    project::Built b = project::build(project::layoutOf(t.path), {});
    REQUIRE_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
    CHECK(b.cartridge->microcode.find("HALT") != std::string::npos);
    // The cycle table rides along: one line per instruction, and a missing
    // microprogram shows as dashes rather than a number.
    CHECK(b.assembly.find("cycles_addr:") != std::string::npos);
    CHECK(b.assembly.find("db \"HLT             2  2\", 0") != std::string::npos);
    CHECK(b.assembly.find("db \"NOP            --  1\", 0") != std::string::npos);
  }

  TEST_CASE("a row that breaks a rule builds, with a warning naming its line") {
    TempDir t;
    write(t.path / "src" / "main.asm", "HLT\n");
    write(t.path / "src" / "microcode.txt", "fetch:\n  FETCH\n# a comment\nHLT:\n  HALT\n  RAM_TO_B, ADDR_D1, ADDR_D2\n");
    project::Built b = project::build(project::layoutOf(t.path), {});
    REQUIRE(b.cartridge);
    bool warned = false;
    for (const std::string& n : b.notes) {
      warned = warned || (n.find("microcode.txt:6: warning: HLT, row 2, breaks rule 8") != std::string::npos);
    }
    CHECK_MESSAGE(warned, (b.notes.empty() ? std::string("no notes") : b.notes.back()));
  }

  TEST_CASE("a new assembly project starts laid out in columns") {
    TempDir t;
    REQUIRE(project::create(t.path / "p", project::Kind::Assembly).error.empty());
    std::ifstream in(t.path / "p" / "src" / "main.asm");
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    CHECK(text.find("loop:   OUT    GPU_X_HI, 0") != std::string::npos);
  }

  TEST_CASE("a microcode.txt that does not parse names its line") {
    TempDir t;
    write(t.path / "src" / "main.asm", "HLT\n");
    write(t.path / "src" / "microcode.txt", "fetch:\n  NOT_A_SIGNAL\n");
    project::Built b = project::build(project::layoutOf(t.path), {});
    CHECK_FALSE(b.cartridge);
    REQUIRE_FALSE(b.errors.empty());
    CHECK(b.errors[0].find("microcode.txt:") != std::string::npos);
  }
}

TEST_SUITE("simplecpu-make new") {
  TEST_CASE("a C project builds") {
    TempDir t;
    project::Created c = project::create(t.path, project::Kind::C);
    REQUIRE(c.error.empty());
    CHECK(fs::exists(t.path / "src" / "main.c"));
    CHECK(fs::exists(t.path / "assets"));
    CHECK(fs::exists(t.path / ".gitignore"));
    project::Built b = project::build(project::layoutOf(t.path), {});
    CHECK_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
  }

  TEST_CASE("an assembly project builds") {
    TempDir t;
    REQUIRE(project::create(t.path, project::Kind::Assembly).error.empty());
    project::Built b = project::build(project::layoutOf(t.path), {});
    CHECK_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
    CHECK(b.cartridge->microcode == "@naive");
  }

  TEST_CASE("a microcode project builds with the naive set written out") {
    TempDir t;
    REQUIRE(project::create(t.path, project::Kind::Microcode).error.empty());
    CHECK(fs::exists(t.path / "src" / "microcode.txt"));
    project::Built b = project::build(project::layoutOf(t.path), {});
    REQUIRE_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
    CHECK(b.cartridge->microcode.find("fetch:") != std::string::npos);
    // One line per instruction in the table.
    size_t lines = 0;
    for (size_t at = b.assembly.find("\ncyc_"); at != std::string::npos; at = b.assembly.find("\ncyc_", at + 1)) lines++;
    CHECK(lines == ops().size());
  }

#if SC8_HAVE_BASIC
  TEST_CASE("a BASIC project is the interpreter with an AUTORUN slot") {
    TempDir t;
    REQUIRE(project::create(t.path, project::Kind::Basic).error.empty());
    project::Built b = project::build(project::layoutOf(t.path), {});
    REQUIRE_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
    REQUIRE_EQ(b.cartridge->basic.size(), 1u);
    CHECK(b.cartridge->basic[0].first == "AUTORUN");
    CHECK(b.cartridge->microcode == "@optimal");
    CHECK(b.instructions > 1000);
  }
#endif

  TEST_CASE("a folder that holds something is left alone") {
    TempDir t;
    write(t.path / "keep.txt", "mine\n");
    project::Created c = project::create(t.path, project::Kind::C);
    CHECK_FALSE(c.error.empty());
    CHECK_FALSE(fs::exists(t.path / "src"));
  }
}

#if SC8_HAVE_BASIC
TEST_SUITE("a project with BASIC and C") {
  const char* const DOUBLE_C = R"(#include <basicvars.h>
int calls;
void DOUBLE(void) { calls = calls + 1; basic_set('A', basic_get('A') * 2); }
void unused_but_kept(void) { calls = 0; }
)";

  TEST_CASE("is one program: the interpreter's main, the user's functions kept, the labels resolved") {
    TempDir t;
    write(t.path / "src" / "double.c", DOUBLE_C);
    write(t.path / "src" / "answer.asm", "ANSWER: LD A <- 42\n        RET\n");
    write(t.path / "src" / "autorun.bas",
          "10 A = 21\n20 CALL DOUBLE\n30 PRINT A\n40 JSR ANSWER : PRINT PEEK(4)\n50 IF A > 1 THEN JMP DOUBLE\n"
          "60 PRINT \"JSR DOUBLE\"\n70 REM JSR DOUBLE\n80 JSR unused_but_kept\n90 JSR A\n100 JSR PEEK(4)\n");
    project::Built b = project::build(project::layoutOf(t.path), {});
    REQUIRE_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
    CHECK(b.cartridge->microcode == "@optimal");
    // The interpreter's own main is the entry, and the user's C is there.
    CHECK(b.assembly.find("\nmain:") != std::string::npos);
    CHECK(b.assembly.find("\nDOUBLE:") != std::string::npos);
    CHECK(b.assembly.find("\nunused_but_kept:") != std::string::npos);
    CHECK(b.assembly.find("\nbasic_set:") != std::string::npos);
    CHECK(b.assembly.find("term_puts:") != std::string::npos);
    // The system page is kept once, at the size basic.h and the
    // interpreter's own build give it, so the compiler starts after it.
    const size_t sys = b.assembly.find("__sys:");
    REQUIRE(sys != std::string::npos);
    const std::string sysLine = b.assembly.substr(sys, b.assembly.find('\n', sys) - sys);
    CHECK_MESSAGE(sysLine.find("ds " + std::to_string(basic::SYSTEM_PAGE_SIZE)) != std::string::npos, sysLine);
    CHECK(b.assembly.find("__sys:", sys + 1) == std::string::npos);
    REQUIRE_EQ(b.cartridge->basic.size(), 1u);
    const std::string bas = b.cartridge->basic[0].second;
    // The slot DOUBLE landed on, read back off the assembly, is what the
    // program now says, and ANSWER is the assembly label after the C.
    const Assembled a = assemble(b.assembly);
    REQUIRE(a.errors.empty());
    const std::string slot = std::to_string(a.labels.at("DOUBLE").value);
    const std::string answer = std::to_string(a.labels.at("ANSWER").value);
    CHECK(a.labels.at("DOUBLE").value > 0);
    CHECK_MESSAGE(bas.find("20 CALL " + slot + "\n") != std::string::npos, bas);
    CHECK(bas.find("40 JSR " + answer + " : PRINT PEEK(4)") != std::string::npos);
    CHECK(bas.find("50 IF A > 1 THEN JMP " + slot + "\n") != std::string::npos);
    // A string, a REM, a variable and a function call are left alone.
    CHECK(bas.find("60 PRINT \"JSR DOUBLE\"") != std::string::npos);
    CHECK(bas.find("70 REM JSR DOUBLE") != std::string::npos);
    CHECK(bas.find("80 JSR unused_but_kept") == std::string::npos);
    CHECK(bas.find("90 JSR A\n") != std::string::npos);
    CHECK(bas.find("100 JSR PEEK(4)") != std::string::npos);
    bool interpreter = false;
    for (const std::string& s : b.sources) interpreter = interpreter || s == "the BASIC interpreter";
    CHECK(interpreter);
  }

  TEST_CASE("a main in the user's C is refused") {
    TempDir t;
    write(t.path / "src" / "main.c", "int main(void) { return 0; }\n");
    write(t.path / "src" / "autorun.bas", "10 PRINT 1\n");
    project::Built b = project::build(project::layoutOf(t.path), {});
    CHECK_FALSE(b.cartridge);
    REQUIRE_FALSE(b.errors.empty());
    CHECK_MESSAGE(b.errors[0].find("main.c:1: main belongs to the interpreter") != std::string::npos, b.errors[0]);
  }

  TEST_CASE("an unknown label names the file, the line and the name") {
    TempDir t;
    write(t.path / "src" / "double.c", DOUBLE_C);
    write(t.path / "src" / "autorun.bas", "10 PRINT 1\n20 CALL TRIPLE\n30 JMP QUAD\n");
    project::Built b = project::build(project::layoutOf(t.path), {});
    CHECK_FALSE(b.cartridge);
    REQUIRE_EQ(b.errors.size(), 2u);
    CHECK_MESSAGE(b.errors[0].find("autorun.bas:2: TRIPLE is not a label") != std::string::npos, b.errors[0]);
    CHECK_MESSAGE(b.errors[1].find("autorun.bas:3: QUAD is not a label") != std::string::npos, b.errors[1]);
  }

  TEST_CASE("a user basic.h is refused, because the interpreter's is included by that name") {
    TempDir t;
    write(t.path / "src" / "double.c", DOUBLE_C);
    write(t.path / "src" / "basic.h", "#define X 1\n");
    write(t.path / "src" / "autorun.bas", "10 PRINT 1\n");
    project::Built b = project::build(project::layoutOf(t.path), {});
    CHECK_FALSE(b.cartridge);
    REQUIRE_FALSE(b.errors.empty());
    CHECK(b.errors[0].find("basic.h is the interpreter's header") != std::string::npos);
  }

  TEST_CASE("a .bas with .asm and no .c is still the interpreter's assembly plus the driver") {
    TempDir t;
    write(t.path / "src" / "answer.asm", "ANSWER: LD A <- 42\n        RET\n");
    write(t.path / "src" / "autorun.bas", "10 CALL ANSWER\n20 PRINT PEEK(4)\n");
    project::Built b = project::build(project::layoutOf(t.path), {});
    REQUIRE_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
    CHECK(b.assembly.find("; Generated by the SimpleCPU-8 C compiler") != std::string::npos);
    CHECK(b.cartridge->basic[0].second.find("10 CALL ANSWER") == std::string::npos);
    CHECK(b.cartridge->basic[0].second.find("10 CALL ") != std::string::npos);
  }
}
#endif

TEST_SUITE("calls across languages") {
  // BASIC calls C and assembly by name, assembly calls C, and C calls
  // assembly. The build reads the BASIC and the assembly for the names
  // they call first, and those C functions are kept as reached.
  TEST_CASE("a C function only an assembly file calls is kept, and C calls the assembly") {
    TempDir t;
    write(t.path / "src" / "main.c",
          "int result;\n"
          "int twice(int n);\n"
          "int add_one(int n) { return n + 1; }\n"
          "int main(void) { result = twice(20); return 0; }\n");
    write(t.path / "src" / "twice.asm",
          "twice:  LD A <- [D3+1]\n"
          "        ADD A <- [D3+1]\n"
          "        LD [__ret+1] <- A\n"
          "        LD A <- 0\n"
          "        LD [__ret] <- A\n"
          "        LD D3 <- D3+2\n"
          "        RET\n"
          "unused_by_c:\n"
          "        JSR add_one\n"
          "        RET\n");
    project::Built b = project::build(project::layoutOf(t.path), {});
    REQUIRE_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
    CHECK(b.assembly.find("\nadd_one:") != std::string::npos);
  }

  TEST_CASE("with BASIC, a C function nothing calls is left out, and one BASIC calls is kept") {
    TempDir t;
    write(t.path / "src" / "autorun.bas", "10 CALL KEPT\n20 PRINT USR(1, ALSO, 2)\n");
    write(t.path / "src" / "funcs.c",
          "void KEPT(void) { }\n"
          "int ALSO(int x) { return x; }\n"
          "int DROPPED(void) { return 1; }\n");
    project::Built b = project::build(project::layoutOf(t.path), {});
    REQUIRE_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
    CHECK(b.assembly.find("\nKEPT:") != std::string::npos);
    CHECK(b.assembly.find("\nALSO:") != std::string::npos);
    CHECK(b.assembly.find("\nDROPPED:") == std::string::npos);
  }

  TEST_CASE("BASIC calling a static function is refused, with the fix") {
    TempDir t;
    write(t.path / "src" / "autorun.bas", "10 CALL HIDDEN\n");
    write(t.path / "src" / "funcs.c", "static void HIDDEN(void) { }\n");
    project::Built b = project::build(project::layoutOf(t.path), {});
    CHECK_FALSE(b.cartridge);
    REQUIRE_FALSE(b.errors.empty());
    CHECK(b.errors[0].find("HIDDEN is static") != std::string::npos);
    CHECK(b.errors[0].find("remove static") != std::string::npos);
  }
}

#if SC8_HAVE_BASIC
namespace {

// A two by two picture on the machine palette, as the sprite editor saves.
std::vector<uint8_t> pngBytes() {
  sprite::Strip s = sprite::blank(2, 2);
  s.frames[0].set(0, 0, 3);
  return sprite::encodePng(s, machinePalette());
}

// A short 8 bit mono WAV that miniaudio decodes.
std::vector<uint8_t> wavBytes() {
  const std::vector<uint8_t> pcm = {128, 200, 56, 128, 128, 200, 56, 128};
  std::vector<uint8_t> w;
  auto str = [&](const char* t) { w.insert(w.end(), t, t + 4); };
  auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) w.push_back(static_cast<uint8_t>(v >> (8 * i))); };
  auto u16 = [&](uint16_t v) { w.push_back(static_cast<uint8_t>(v & 0xff)); w.push_back(static_cast<uint8_t>(v >> 8)); };
  str("RIFF");
  u32(static_cast<uint32_t>(36 + pcm.size()));
  str("WAVE");
  str("fmt ");
  u32(16);
  u16(1);
  u16(1);
  u32(8000);
  u32(8000);
  u16(1);
  u16(8);
  str("data");
  u32(static_cast<uint32_t>(pcm.size()));
  w.insert(w.end(), pcm.begin(), pcm.end());
  return w;
}

void writeBytes(const fs::path& p, const std::vector<uint8_t>& bytes) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                           static_cast<std::streamsize>(bytes.size()));
}

const RomAsset* entry(const Cartridge& c, const std::string& name) {
  for (const RomAsset& a : c.assets) if (a.name == name) return &a;
  return nullptr;
}

// A BASIC project in memory, its assets named in `files`.
project::Built basicInMemory(const std::map<std::string, std::vector<uint8_t>>& files) {
  Assets assets;
  assets.loadFile = [files](std::string_view name) -> std::optional<std::vector<uint8_t>> {
    auto it = files.find(std::string(name));
    if (it == files.end()) return std::nullopt;
    return it->second;
  };
  std::vector<std::string> names;
  for (const auto& [n, b] : files) names.push_back(n);
  return project::buildSources({{"autorun.bas", "10 PRINT 1\n"}}, assets, {}, "t", "", std::nullopt, names);
}

}  // namespace

TEST_SUITE("assets on the cartridge by name") {
  TEST_CASE("a BASIC project places every file in assets/ and lists each by kind and stem") {
    TempDir t;
    write(t.path / "src" / "autorun.bas", "10 PRINT 1\n");
    const std::string fontText = font::write(font::builtIn());
    write(t.path / "assets" / "small.font", fontText);
    writeBytes(t.path / "assets" / "ship.png", pngBytes());
    writeBytes(t.path / "assets" / "boom.wav", wavBytes());
    project::Built b = project::build(project::layoutOf(t.path), {});
    REQUIRE_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
    const Cartridge& c = *b.cartridge;
    const RomAsset* f = entry(c, "small");
    const RomAsset* i = entry(c, "ship");
    const RomAsset* s = entry(c, "boom");
    REQUIRE(f);
    REQUIRE(i);
    REQUIRE(s);
    CHECK_EQ(f->kind, "font");
    CHECK_EQ(i->kind, "image");
    CHECK_EQ(s->kind, "sample");
    const std::vector<uint8_t> blob = font::blob(font::builtIn());
    REQUIRE_EQ(f->size, blob.size());
    CHECK(std::vector<uint8_t>(c.data.begin() + f->offset, c.data.begin() + f->offset + f->size) == blob);
    // The picture as .image places it: its pixels, one byte each.
    CHECK_EQ(i->size, 4u);
    CHECK_EQ(c.data[i->offset], 3);
  }

  TEST_CASE("a C project lists only what a directive placed, under its label") {
    TempDir t;
    write(t.path / "src" / "main.c", "__ROM const unsigned char pic[] = __image(\"ship.png\");\nint main(void) { return 0; }\n");
    writeBytes(t.path / "assets" / "ship.png", pngBytes());
    write(t.path / "assets" / "small.font", font::write(font::builtIn()));
    project::Built b = project::build(project::layoutOf(t.path), {});
    REQUIRE_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
    REQUIRE_EQ(b.cartridge->assets.size(), 1u);
    CHECK_EQ(b.cartridge->assets[0].kind, "image");
    CHECK_EQ(b.cartridge->assets[0].name, "pic");
  }

  TEST_CASE("a file a directive placed is not placed twice, and gets a second entry under its stem") {
    TempDir t;
    write(t.path / "src" / "autorun.bas", "10 PRINT 1\n");
    write(t.path / "src" / "fonts.asm", ".data\nsmallfont: .font('small.font')\n");
    write(t.path / "assets" / "small.font", font::write(font::builtIn()));
    project::Built b = project::build(project::layoutOf(t.path), {});
    REQUIRE_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
    const RomAsset* byLabel = entry(*b.cartridge, "smallfont");
    const RomAsset* byStem = entry(*b.cartridge, "small");
    REQUIRE(byLabel);
    REQUIRE(byStem);
    CHECK_EQ(byStem->offset, byLabel->offset);
    CHECK_EQ(byStem->size, byLabel->size);
    CHECK_EQ(b.cartridge->data.size(), static_cast<size_t>(byLabel->offset + byLabel->size));
  }

  TEST_CASE("two stems that differ only in case are refused, naming both files") {
    project::Built b = basicInMemory({{"small.font", std::vector<uint8_t>(0)}, {"Small.bin", {1, 2}}});
    CHECK_FALSE(b.cartridge);
    REQUIRE_FALSE(b.errors.empty());
    CHECK(b.errors[0].find("Small.bin") != std::string::npos);
    CHECK(b.errors[0].find("small.font") != std::string::npos);
  }

  TEST_CASE("a stem BASIC cannot write gets a note naming the file, and is still placed") {
    const std::string fontText = font::write(font::builtIn());
    const std::vector<uint8_t> fontBytes(fontText.begin(), fontText.end());
    project::Built b = basicInMemory({{"small-2.font", fontBytes}, {"averylongname.bin", {7}}, {"ok.bin", {8}}});
    REQUIRE_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
    std::string notes;
    for (const std::string& n : b.notes) notes += n + "\n";
    CHECK(notes.find("small-2.font") != std::string::npos);
    CHECK(notes.find("averylongname.bin") != std::string::npos);
    CHECK(notes.find("ok.bin") == std::string::npos);
    CHECK(entry(*b.cartridge, "small-2"));
    CHECK_EQ(entry(*b.cartridge, "ok")->kind, "file");
  }

  TEST_CASE("a BASIC and C project places its font and starts its heap stack below BASIC's screen") {
    TempDir t;
    write(t.path / "src" / "autorun.bas", "10 LOADFONT SMALL\n20 CALL DOUBLE\n");
    write(t.path / "src" / "double.c", "void DOUBLE(void) { }\n");
    write(t.path / "assets" / "small.font", font::write(font::builtIn()));
    project::Built b = project::build(project::layoutOf(t.path), {});
    REQUIRE_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
    const RomAsset* f = entry(*b.cartridge, "small");
    REQUIRE(f);
    CHECK_EQ(f->kind, "font");
    CHECK(b.assembly.find("LD D1 <- " + std::to_string(basic::SCREEN) + "\n        LD D3 <- D1") != std::string::npos);
    // LIST shows the name: it is not a label, so the build leaves it alone.
    CHECK(b.cartridge->basic[0].second.find("10 LOADFONT SMALL") != std::string::npos);
  }

  TEST_CASE("a .font that does not read stops the build at its line") {
    project::Built b = basicInMemory({{"bad.font", {'n', 'o', '\n'}}});
    CHECK_FALSE(b.cartridge);
    REQUIRE_FALSE(b.errors.empty());
    CHECK(b.errors[0].find("bad.font:1:") != std::string::npos);
  }
}

// examples/basic/font as simplecpu-make builds it, booted headless. The program
// waits for a key between its three steps, so each one is checked on
// screen before the next.
namespace {

// The first cell on the text screen that holds `code`, as column and row.
std::pair<int, int> cellOf(const testing::Session& s, int code) {
  const int cols = s.m->ram[basic::SYS_COLS], rows = s.m->ram[basic::SYS_ROWS];
  for (int i = 0; i < cols * rows; i++) {
    if (s.m->ram[static_cast<size_t>(basic::SCREEN + i)] == code) return {i % cols, i / cols};
  }
  return {-1, -1};
}

// Pixels in the text colour in the w by h cell at col, row of the frame.
int litInCell(const Gpu::Frame& f, std::pair<int, int> at, int w, int h, int fg) {
  int n = 0;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      if (f[static_cast<size_t>((at.second * h + y) * gpu::SCREEN_W + at.first * w + x)] == fg) n++;
    }
  }
  return n;
}

int bitsIn(uint8_t b) {
  int n = 0;
  for (; b; b = static_cast<uint8_t>(b & (b - 1))) n++;
  return n;
}

void pressKey(testing::Session& s) {
  s.pushKey(' ', false);
  s.runBudget(200000);
  s.pushKey(' ', true);
  s.runBudget(20000000);
}

}  // namespace

TEST_SUITE("examples/basic/font") {
  TEST_CASE("simplecpu-make builds it, and it runs its font, the built-in font, then 8 by 8 cells") {
    const fs::path example = fs::path(SC8_EXAMPLES_DIR) / project::EXAMPLE_KINDS[1].folder / "font";
    TempDir t;
    fs::create_directories(t.path);
    const fs::path rom = t.path / "font.rom";
    const std::string cmd = "\"" + std::string(SC8_SIMPLECPU_MAKE) + "\" \"" + example.string() + "\" -o \"" +
                            rom.string() + "\" > \"" + (t.path / "make.txt").string() + "\" 2>&1";
    REQUIRE_EQ(std::system(cmd.c_str()), 0);
    std::ifstream in(rom, std::ios::binary);
    const CartridgeResult r = decodeCartridge(std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {}));
    REQUIRE_MESSAGE(r.cartridge, r.error);

    std::ifstream fontIn(example / "assets" / "chunky.font");
    const font::ReadResult chunky = font::read(std::string(std::istreambuf_iterator<char>(fontIn), {}));
    REQUIRE_MESSAGE(chunky.font, chunky.error);
    REQUIRE_EQ(chunky.font->width, 8);
    REQUIRE_EQ(chunky.font->height, 8);

    testing::Session s(*r.cartridge);
    s.load();
    s.runBudget(20000000);

    // LOADFONT CHUNKY: the font's cell makes the grid 32 by 32, and the
    // heart at $80 is on screen in the font's own pixels.
    CHECK_EQ(s.m->ram[basic::SYS_COLS], 32);
    CHECK_EQ(s.m->ram[basic::SYS_ROWS], 32);
    const auto heart = cellOf(s, 0x80);
    REQUIRE_GE(heart.first, 0);
    int heartBits = 0;
    for (int row = 0; row < 8; row++) heartBits += bitsIn(chunky.font->glyphs[static_cast<size_t>(0x80 * 8 + row)]);
    CHECK_GT(heartBits, 0);
    CHECK_EQ(litInCell(s.gpu.composeFrame(), heart, 8, 8, s.gpu.textColor), heartBits);

    // LOADFONT alone: the built-in font at 6 by 8, 42 columns.
    pressKey(s);
    CHECK_EQ(s.m->ram[basic::SYS_COLS], 42);
    CHECK_EQ(s.m->ram[basic::SYS_ROWS], 32);
    int builtInT = 0;
    for (int row = 0; row < 8; row++) builtInT += bitsIn(glyphRow('T', row));
    const auto t6 = cellOf(s, 'T');
    REQUIRE_GE(t6.first, 0);
    CHECK_EQ(litInCell(s.gpu.composeFrame(), t6, 6, 8, s.gpu.textColor), builtInT);

    // SETTEXT 8, 8: the same built-in glyphs in 8 by 8 cells, 32 by 32.
    pressKey(s);
    CHECK_EQ(s.m->ram[basic::SYS_COLS], 32);
    CHECK_EQ(s.m->ram[basic::SYS_ROWS], 32);
    CHECK_EQ(s.gpu.glyphRowOf('T', 0), glyphRow('T', 0));
    const auto t8 = cellOf(s, 'T');
    REQUIRE_GE(t8.first, 0);
    CHECK_EQ(litInCell(s.gpu.composeFrame(), t8, 8, 8, s.gpu.textColor), builtInT);
    CHECK_EQ(s.m->status, Status::Running);
  }
}
#endif

// The Open example menu lists examples/ by these folders. So examples/
// holds exactly the folders EXAMPLE_KINDS names, and each example sits in
// the folder of its kind.
TEST_SUITE("the examples folder") {
  TEST_CASE("examples/ holds one folder for each kind, and nothing else as a folder") {
    std::set<std::string> folders;
    for (const auto& e : fs::directory_iterator(SC8_EXAMPLES_DIR)) {
      if (e.is_directory()) folders.insert(e.path().filename().string());
    }
    std::set<std::string> kinds;
    for (const project::ExampleKind& k : project::EXAMPLE_KINDS) kinds.insert(k.folder);
    CHECK(folders == kinds);
  }

  TEST_CASE("each example sits in the folder of its kind") {
    auto has = [](const fs::path& dir, const std::string& ext) {
      for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() == ext) return true;
      }
      return false;
    };
    size_t count = 0;
    for (const project::ExampleKind& k : project::EXAMPLE_KINDS) {
      for (const auto& e : fs::directory_iterator(fs::path(SC8_EXAMPLES_DIR) / k.folder)) {
        if (!e.is_directory()) continue;
        count++;
        const fs::path src = project::layoutOf(e.path()).sources;
        const bool micro = fs::exists(src / "microcode.txt");
        const bool bas = has(src, ".bas");
        const bool c = has(src, ".c");
        const bool assembly = has(src, ".asm");
        const std::string folder = k.folder;
        CAPTURE(e.path().string());
        if (folder == "microcode") CHECK(micro);
        if (folder == "basic") CHECK((bas && !micro));
        if (folder == "c") CHECK((c && !bas && !micro));
        if (folder == "assembly") CHECK((assembly && !c && !bas && !micro));
      }
    }
    CHECK(count > 0);
  }
}

