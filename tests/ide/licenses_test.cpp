// The licence texts ./d puts in each archive's LICENSES folder. Each is
// the library's own file, as FetchContent brought it at the pinned tag.
// A preset that does not fetch a library skips its comparison.

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <string>

#include "project/text_file.h"

namespace fs = std::filesystem;

namespace {

std::string readAll(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return sc8::lf(std::string(std::istreambuf_iterator<char>(in), {}));
}

struct Licence {
  const char* file;
  const char* fetched;
};

const Licence LICENCES[] = {
    {"raylib.txt", SC8_LICENSE_RAYLIB},       {"miniaudio.txt", SC8_LICENSE_MINIAUDIO},
    {"imgui.txt", SC8_LICENSE_IMGUI},         {"doctest.txt", SC8_LICENSE_DOCTEST},
    {"stb.txt", SC8_LICENSE_STB},
};

}  // namespace

TEST_SUITE("the licence texts") {
  TEST_CASE("each library's text is in packaging/licenses") {
    const fs::path dir = fs::path(SC8_PACKAGING_DIR) / "licenses";
    for (const Licence& l : LICENCES) {
      CAPTURE(l.file);
      REQUIRE(fs::is_regular_file(dir / l.file));
      CHECK_FALSE(readAll(dir / l.file).empty());
    }
    CHECK(fs::is_regular_file(dir / "README.txt"));
  }

  TEST_CASE("each text is the fetched library's own") {
    const fs::path dir = fs::path(SC8_PACKAGING_DIR) / "licenses";
    for (const Licence& l : LICENCES) {
      CAPTURE(l.file);
      if (std::string(l.fetched).empty() || !fs::is_regular_file(l.fetched)) continue;
      CHECK(readAll(dir / l.file) == readAll(l.fetched));
    }
  }
}
