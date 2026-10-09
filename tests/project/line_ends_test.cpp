// A source with CR LF line ends builds what the same source with LF
// builds, and its errors name the same lines. The tools write LF on every
// platform, but a file saved by a Windows editor carries CR LF. A text
// file written in text mode on Windows gets CR LF, so the file tests here
// fail there and pass elsewhere.

#include <doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>
#include <vector>

#include "asm/asm.h"
#include "basic/program.h"
#include "cc/cc.h"
#include "core/cartridge.h"
#include "core/microcode.h"
#include "core/mcparse.h"
#include "project/project.h"
#include "project/text_file.h"

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

std::string readExact(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

std::vector<uint8_t> bytesOf(const std::string& s) { return {s.begin(), s.end()}; }

// A PNG's signature holds CR LF, which a file kept byte for byte keeps.
const std::string PNG_BYTES = std::string("\x89PNG\r\n\x1a\n", 8) + "\r\nnot a picture\r\n";

const std::vector<uint8_t>* carried(const Cartridge& c, const std::string& name) {
  for (const auto& [n, bytes] : c.sources) {
    if (n == name) return &bytes;
  }
  return nullptr;
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

  TEST_CASE("writeText writes LF on every platform") {
    Folder f("write");
    fs::create_directories(f.path);
    REQUIRE(writeText(f.path / "a.txt", "one\r\ntwo\nthree\r\n"));
    CHECK(readExact(f.path / "a.txt") == "one\ntwo\nthree\n");
  }

  TEST_CASE("a CR LF project and its LF copy build the same ROM, every chunk included") {
    Folder lf("lf"), cr("cr");
    const std::string readme = "# Line ends\nA test.\n";
    const std::string microcode = "# The naive set.\n" + serializeMicrocode(buildNaive());
    for (const auto& [dir, conv] : {std::pair{lf.path, false}, std::pair{cr.path, true}}) {
      auto text = [&](const std::string& t) { return conv ? crlf(t) : t; };
      writeExact(dir / "README.md", text(readme));
      writeExact(dir / "src" / "main.asm", text(ASM));
      writeExact(dir / "src" / "microcode.txt", text(microcode));
      writeExact(dir / "assets" / "pic.png", PNG_BYTES);
    }
    const project::Built a = project::build(project::layoutOf(lf.path), {});
    const project::Built b = project::build(project::layoutOf(cr.path), {});
    REQUIRE_MESSAGE(a.cartridge, (a.errors.empty() ? std::string() : a.errors[0]));
    REQUIRE_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
    CHECK(encodeCartridge(*b.cartridge, false) == encodeCartridge(*a.cartridge, false));
    const std::vector<uint8_t>* png = carried(*b.cartridge, "assets/pic.png");
    REQUIRE(png);
    CHECK(*png == bytesOf(PNG_BYTES));
    const std::vector<uint8_t>* asmText = carried(*b.cartridge, "main.asm");
    REQUIRE(asmText);
    CHECK(*asmText == bytesOf(ASM));
  }

  TEST_CASE("simplecpu-asm burns the same ROM from CR LF files as from LF ones") {
    Folder lf("lf"), cr("cr");
    const std::string microcode = "# The naive set.\n" + serializeMicrocode(buildNaive());
    for (const auto& [dir, conv] : {std::pair{lf.path, false}, std::pair{cr.path, true}}) {
      auto text = [&](const std::string& t) { return conv ? crlf(t) : t; };
      writeExact(dir / "main.asm", text(ASM));
      writeExact(dir / "demo.bas", text("10 PRINT \"HI\"\n"));
      writeExact(dir / "microcode.txt", text(microcode));
      const std::string cmd = std::string("\"") + SC8_SIMPLECPU_ASM + "\" \"" + (dir / "main.asm").string() +
                              "\" --microcode \"" + (dir / "microcode.txt").string() + "\" --bas \"DEMO=" +
                              (dir / "demo.bas").string() + "\" -o \"" + (dir / "out.rom").string() + "\"";
      REQUIRE(std::system(cmd.c_str()) == 0);
    }
    const std::string a = readExact(lf.path / "out.rom");
    const std::string b = readExact(cr.path / "out.rom");
    CHECK_FALSE(a.empty());
    CHECK(b == a);
  }

#if SC8_HAVE_BASIC
  TEST_CASE("a CR LF BASIC project and its LF copy build the same ROM, slots included") {
    Folder lf("lf"), cr("cr");
    const std::string autorun = "10 PRINT \"HI\"\n20 END\n";
    const std::string more = "10 PRINT \"MORE\"\n";
    for (const auto& [dir, conv] : {std::pair{lf.path, false}, std::pair{cr.path, true}}) {
      auto text = [&](const std::string& t) { return conv ? crlf(t) : t; };
      writeExact(dir / "README.md", text("# Slots\n"));
      writeExact(dir / "src" / "autorun.bas", text(autorun));
      writeExact(dir / "src" / "more.bas", text(more));
    }
    const project::Built a = project::build(project::layoutOf(lf.path), {});
    const project::Built b = project::build(project::layoutOf(cr.path), {});
    REQUIRE_MESSAGE(a.cartridge, (a.errors.empty() ? std::string() : a.errors[0]));
    REQUIRE_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
    CHECK(b.cartridge->basic == a.cartridge->basic);
    CHECK(encodeCartridge(*b.cartridge, false) == encodeCartridge(*a.cartridge, false));
  }
#endif

  TEST_CASE("unpack writes the text files LF and the other files byte for byte") {
    Cartridge rom;
    rom.sources = {{"main.asm", bytesOf(crlf(ASM))},
                   {"README.md", bytesOf(crlf("# Unpacked\n"))},
                   {"assets/pic.png", bytesOf(PNG_BYTES)}};
    Folder f("unpack");
    const project::Created c = project::unpack(rom, f.path);
    REQUIRE_MESSAGE(c.error.empty(), c.error);
    CHECK(readExact(f.path / "src" / "main.asm") == ASM);
    CHECK(readExact(f.path / "README.md") == "# Unpacked\n");
    CHECK(readExact(f.path / ".gitignore") == "build/\n");
    CHECK(readExact(f.path / "assets" / "pic.png") == PNG_BYTES);
  }

  TEST_CASE("create() writes LF for every kind") {
    for (const project::Kind kind :
         {project::Kind::C, project::Kind::Basic, project::Kind::Assembly, project::Kind::Microcode}) {
      Folder f("create");
      const project::Created c = project::create(f.path, kind);
      REQUIRE_MESSAGE(c.error.empty(), c.error);
      REQUIRE_FALSE(c.files.empty());
      for (const fs::path& p : c.files) CHECK_MESSAGE(readExact(p).find('\r') == std::string::npos, p.string());
    }
  }

  TEST_CASE("simplecpu-make's assembly listing is LF") {
    Folder f("listing");
    writeExact(f.path / "src" / "main.asm", crlf(ASM));
    project::Options opts;
    opts.keepAsm = true;
    const project::Written w = project::buildAndWrite(project::layoutOf(f.path), opts);
    REQUIRE_MESSAGE(!w.rom.empty(), (w.built.errors.empty() ? std::string() : w.built.errors[0]));
    const std::string listing = readExact(f.path / "build" / (project::layoutOf(f.path).name + ".asm"));
    CHECK_FALSE(listing.empty());
    CHECK(listing.find('\r') == std::string::npos);
  }
}
