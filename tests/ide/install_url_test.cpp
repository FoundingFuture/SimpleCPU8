// The address users install from. foundingfuture.com serves both scripts
// from /downloads/. The README and each script's usage lines spell it, so
// this holds the three to one address and leaves no placeholder.

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <string>

namespace fs = std::filesystem;

namespace {

const std::string DOWNLOADS = "https://foundingfuture.com/downloads/";

std::string readAll(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

TEST_SUITE("the install address") {
  TEST_CASE("install.sh, install.ps1 and the README name the downloads address") {
    const fs::path packaging = SC8_PACKAGING_DIR;
    const std::string sh = readAll(packaging / "install.sh");
    const std::string ps1 = readAll(packaging / "install.ps1");
    const std::string readme = readAll(packaging.parent_path() / "README.md");
    REQUIRE_FALSE(sh.empty());
    REQUIRE_FALSE(ps1.empty());
    REQUIRE_FALSE(readme.empty());
    CHECK(sh.find("curl -fsSL " + DOWNLOADS + "install.sh | sh") != std::string::npos);
    CHECK(ps1.find("irm " + DOWNLOADS + "install.ps1 | iex") != std::string::npos);
    CHECK(readme.find("curl -fsSL " + DOWNLOADS + "install.sh | sh") != std::string::npos);
    CHECK(readme.find("irm " + DOWNLOADS + "install.ps1 | iex") != std::string::npos);
    for (const std::string* text : {&sh, &ps1, &readme}) CHECK(text->find("<url>") == std::string::npos);
  }

  // Microsoft Defender stops `powershell -c "irm <url> | iex"` from cmd.exe
  // as Trojan:Win32/Commando.A!ml, and cmd says only "Access is denied".
  // The line typed into PowerShell runs.
  TEST_CASE("no instructions start PowerShell with the download on its command line") {
    const fs::path packaging = SC8_PACKAGING_DIR;
    for (const fs::path& p : {packaging / "install.ps1", packaging.parent_path() / "README.md"}) {
      CAPTURE(p.string());
      const std::string text = readAll(p);
      REQUIRE_FALSE(text.empty());
      CHECK(text.find("powershell -c") == std::string::npos);
      CHECK(text.find("powershell -Command") == std::string::npos);
    }
  }
}
