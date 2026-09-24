// A project folder into a ROM: the layouts, the four kinds simplecpu-make
// new lays out, and the cycle table a microcode project carries.

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "core/isa.h"
#include "project/project.h"

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
