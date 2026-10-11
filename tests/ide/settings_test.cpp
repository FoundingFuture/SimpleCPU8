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

  // The project column is open until its toggle collapses it, and the
  // next start finds it as it was left.
  TEST_CASE("the project column's state is kept, open by default") {
    FakeHome home;
    Settings fresh;
    fresh.load();
    CHECK(fresh.columnOpen);
    CHECK_FALSE(fresh.columnKnown);
    fresh.columnOpen = false;
    REQUIRE(fresh.save());
    Settings next;
    next.load();
    CHECK_FALSE(next.columnOpen);
    CHECK(next.columnKnown);
  }

  // The libraries BASIC programs INCLUDE live in one folder, which
  // Preferences may name. Unset, it is empty here and the project
  // library names the default.
  TEST_CASE("the libraries folder is kept, and is empty until it is set") {
    FakeHome home;
    Settings s;
    CHECK(s.librariesDir.empty());
    s.librariesDir = (home.root / "elsewhere").string();
    REQUIRE(s.save());
    Settings next;
    next.load();
    CHECK(next.librariesDir == s.librariesDir);
  }

  // A folder typed with a separator after it is the same folder. The
  // parent of `projects/` taken as spelled is `projects`, so the IDE
  // keeps both folders in the one spelling the rest of it reads.
  TEST_CASE("the projects and libraries folders are kept in one spelling") {
    FakeHome home;
    const fs::path projects = "/home/me/projects";
    const fs::path libraries = "/home/me/libs";
    Settings s;
    s.projectsDir = "/home/me/projects/";
    s.librariesDir = "/home/me/other/../libs/.";
    REQUIRE(s.save());
    // What Preferences stored is what the session goes on with.
    CHECK(fs::path(s.projectsDir) == projects);
    CHECK(fs::path(s.librariesDir) == libraries);
    Settings next;
    next.load();
    CHECK(fs::path(next.projectsDir) == projects);
    CHECK(fs::path(next.librariesDir) == libraries);
    // A file somebody wrote by hand is read the same way.
    {
      std::ofstream out(Settings::settingsFile(), std::ios::binary);
      out << "projects = /home/me/projects/./\nlibraries = \n";
    }
    Settings hand;
    hand.load();
    CHECK(fs::path(hand.projectsDir) == projects);
    CHECK(hand.librariesDir.empty());
  }

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
