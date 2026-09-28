// A project folder into a ROM: the layouts, the four kinds simplecpu-make
// new lays out, and the cycle table a microcode project carries.

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "asm/asm.h"
#include "core/isa.h"
#include "project/project.h"
#if SC8_HAVE_BASIC
#include "basic/program.h"
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
