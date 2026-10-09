// Where the IDE keeps its settings files.

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "fake_home.h"
#include "ide/settings.h"

namespace fs = std::filesystem;
using sc8::Settings;
using sc8test::FakeHome;
using sc8test::NoHome;

TEST_SUITE("the settings folder") {
  TEST_CASE("the folder sits under the platform's home and is created") {
    FakeHome home;
    const fs::path dir = Settings::dir();
    REQUIRE_FALSE(dir.empty());
    CHECK(fs::is_directory(dir));
    CHECK(dir.string().rfind(home.root.string(), 0) == 0);
  }

  TEST_CASE("each file has its name in the settings folder") {
    FakeHome home;
    const fs::path dir = Settings::dir();
    CHECK(Settings::settingsFile() == dir / "settings.txt");
    CHECK(Settings::layoutFile() == dir / "layout.ini");
    CHECK(Settings::displayFile() == dir / "display.txt");
  }

  // The installers copy the examples here, and File, Open example reads
  // them from here. install.sh and install.ps1 spell the same name.
  TEST_CASE("the examples sit in the settings folder") {
    FakeHome home;
    CHECK(Settings::examplesDir() == Settings::dir() / "examples");
  }

  // A bare file name would be read from and written to the working
  // directory, so a machine without a home gets no paths at all.
  TEST_CASE("with no home, the folder and every file are empty paths") {
    NoHome none;
    CHECK(Settings::dir().empty());
    CHECK(Settings::settingsFile().empty());
    CHECK(Settings::layoutFile().empty());
    CHECK(Settings::displayFile().empty());
    CHECK(Settings::examplesDir().empty());
  }

  // A text-mode stream on Windows writes CR LF. This fails there alone.
  TEST_CASE("settings.txt has LF line ends") {
    FakeHome home;
    Settings s;
    s.projectsDir = "projects";
    REQUIRE(s.save());
    std::ifstream in(Settings::settingsFile(), std::ios::binary);
    const std::string text(std::istreambuf_iterator<char>(in), {});
    CHECK(text.find("projects = projects\n") != std::string::npos);
    CHECK(text.find('\r') == std::string::npos);
  }
}
