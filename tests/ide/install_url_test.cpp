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
}
