// A source with CR LF line ends builds what the same source with LF
// builds, and its errors name the same lines. The tools write LF on every
// platform, but a file saved by a Windows editor carries CR LF.

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>
#include <vector>

#include "asm/asm.h"
#include "basic/program.h"
#include "cc/cc.h"
#include "core/cartridge.h"
#include "project/project.h"

namespace fs = std::filesystem;
using namespace sc8;

namespace {

std::string crlf(const std::string& text) {
  std::string out;
  for (const char c : text) {
    if (c == '\n') out += '\r';
    out += c;
  }
  return out;
}

std::vector<uint8_t> romOf(const Assembled& a) { return encodeCartridge(a.cartridge(), false); }

std::vector<int> errorLines(const Assembled& a) {
  std::vector<int> out;
  for (const AsmError& e : a.errors) out.push_back(e.line);
  return out;
}

// Binary, so the bytes on disk are the line ends given on every platform.
void writeExact(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

// The errors with the folder taken out, so two folders' errors compare.
std::vector<std::string> inFolder(std::vector<std::string> errors, const fs::path& root) {
  for (std::string& e : errors)
    if (const size_t at = e.find(root.string()); at != std::string::npos) e.erase(at, root.string().size());
  return errors;
}

struct Folder {
  fs::path path;
  explicit Folder(const std::string& name) {
    path = fs::temp_directory_path() / fs::path("sc8-line-ends-" + name + "-" + std::to_string(std::rand()));
    fs::remove_all(path);
  }
  ~Folder() { fs::remove_all(path); }
};

const std::string ASM = R"(; Sum two numbers.
start:  LD A <- [a]     ; the first
        ADD A <- [b]
        LD [sum] <- A
        HLT
.ram
a:      db 5
b:      db 3
sum:    db 0
)";

const std::string ASM_ERRORS = R"(        LD A <- [a]
        FOO
        LD A <- [nowhere]
.ram
a:      db 1
)";

const std::string C = R"(// Twice a number, through a macro over two lines.
#define TWICE(x) \
  ((x) * 2)

int main(void) {
  int a = TWICE(3);
  return a;
}
)";

const std::string C_ERRORS = R"(int main(void) {
  int a = 1;
  int b = ;
  return a;
}
)";

}  // namespace

TEST_SUITE("CR LF line ends") {
  TEST_CASE("the assembler builds the same ROM") {
    const Assembled lf = assemble(ASM);
    const Assembled cr = assemble(crlf(ASM));
    REQUIRE(lf.errors.empty());
    CHECK(cr.errors.empty());
    CHECK(romOf(cr) == romOf(lf));
  }

  TEST_CASE("the assembler's errors name the same lines") {
    const Assembled lf = assemble(ASM_ERRORS);
    const Assembled cr = assemble(crlf(ASM_ERRORS));
    REQUIRE(errorLines(lf) == std::vector<int>{2, 3});
    CHECK(errorLines(cr) == errorLines(lf));
  }

  TEST_CASE("the C compiler writes the same assembly, which builds the same ROM") {
    const CcResult lf = compile({{"main.c", C}});
    const CcResult cr = compile({{"main.c", crlf(C)}});
    REQUIRE(lf.errors.empty());
    CHECK(cr.errors.empty());
    CHECK(cr.assembly == lf.assembly);
    CHECK(romOf(assemble(cr.assembly)) == romOf(assemble(lf.assembly)));
  }

  TEST_CASE("the C compiler's errors name the same lines") {
    const CcResult lf = compile({{"main.c", C_ERRORS}});
    const CcResult cr = compile({{"main.c", crlf(C_ERRORS)}});
    REQUIRE_FALSE(lf.errors.empty());
    CHECK(lf.errors[0].find("main.c:3") != std::string::npos);
    CHECK(cr.errors == lf.errors);
  }

  TEST_CASE("a BASIC program encodes to the same bytes") {
    const std::string text = "10 PRINT \"HI\"\n20 GOTO 10\n";
    CHECK(basic::encodeProgram(crlf(text)) == basic::encodeProgram(text));
  }

  // The SRC chunk carries each file as it is on disk, so the ROMs differ
  // there. Everything the program runs from is the same.
  TEST_CASE("a project builds the same program, under the same title") {
    Folder lf("lf"), cr("cr");
    const std::string readme = "# Line ends\nA test.\n";
    writeExact(lf.path / "README.md", readme);
    writeExact(lf.path / "src" / "main.asm", ASM);
    writeExact(cr.path / "README.md", crlf(readme));
    writeExact(cr.path / "src" / "main.asm", crlf(ASM));
    const project::Built a = project::build(project::layoutOf(lf.path), {});
    const project::Built b = project::build(project::layoutOf(cr.path), {});
    REQUIRE_MESSAGE(a.cartridge, (a.errors.empty() ? std::string() : a.errors[0]));
    REQUIRE_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
    CHECK(b.cartridge->program == a.cartridge->program);
    CHECK(b.cartridge->ram == a.cartridge->ram);
    CHECK(b.cartridge->data == a.cartridge->data);
    CHECK(b.cartridge->microcode == a.cartridge->microcode);
    CHECK(b.cartridge->meta == a.cartridge->meta);
  }

  TEST_CASE("a project's C errors name the same lines") {
    Folder lf("lf"), cr("cr");
    writeExact(lf.path / "src" / "main.c", C_ERRORS);
    writeExact(cr.path / "src" / "main.c", crlf(C_ERRORS));
    const project::Built a = project::build(project::layoutOf(lf.path), {});
    const project::Built b = project::build(project::layoutOf(cr.path), {});
    REQUIRE_FALSE(a.errors.empty());
    CHECK(inFolder(b.errors, cr.path) == inFolder(a.errors, lf.path));
  }

#if SC8_HAVE_BASIC
  TEST_CASE("a project's BASIC errors name the same lines") {
    Folder lf("lf"), cr("cr");
    const std::string bas = "10 PRINT \"HI\"\n20 PRIMT 5\n";
    writeExact(lf.path / "src" / "autorun.bas", bas);
    writeExact(cr.path / "src" / "autorun.bas", crlf(bas));
    const project::Built a = project::build(project::layoutOf(lf.path), {});
    const project::Built b = project::build(project::layoutOf(cr.path), {});
    REQUIRE_FALSE(a.errors.empty());
    CHECK(a.errors[0].find("autorun.bas:2") != std::string::npos);
    CHECK(inFolder(b.errors, cr.path) == inFolder(a.errors, lf.path));
  }
#endif
}
