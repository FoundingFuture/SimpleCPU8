// The licence texts ./d puts in each archive's LICENSES folder: the
// repository's own LICENSE, and each library's file as FetchContent brought
// it at the pinned tag. A preset that does not fetch a library skips it.

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
  TEST_CASE("SimpleCPU-8 is under the MIT licence of Founding Future") {
    const std::string text = readAll(SC8_LICENSE_FILE);
    CHECK(text.rfind("MIT License\n\nCopyright (c) 2026 Founding Future\n", 0) == 0);
    CHECK(text.find("Permission is hereby granted, free of charge, to any person obtaining a copy") !=
          std::string::npos);
  }

  TEST_CASE("the folder's README names every file") {
    const std::string readme = readAll(fs::path(SC8_PACKAGING_DIR) / "licenses" / "README.txt");
    CHECK(readme.find("simplecpu-8.txt") != std::string::npos);
    for (const Licence& l : LICENCES) {
      CAPTURE(l.file);
      CHECK(readme.find(l.file) != std::string::npos);
    }
  }

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
