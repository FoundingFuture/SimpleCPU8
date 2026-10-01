// Where the IDE keeps its settings files.

#include <doctest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "ide/settings.h"

namespace fs = std::filesystem;
using sc8::Settings;

namespace {

void setEnv(const char* name, const std::string& value) {
#if defined(_WIN32)
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

// An empty value removes the variable on Windows, and unsetenv does it
// elsewhere.
void unsetEnv(const char* name) {
#if defined(_WIN32)
  _putenv_s(name, "");
#else
  unsetenv(name);
#endif
}

// Removes every variable the settings folder is found from, for one test.
struct NoHome {
  static constexpr const char* NAMES[] = {"APPDATA", "HOME", "XDG_CONFIG_HOME"};
  std::string saved[3];
  bool had[3] = {};
  NoHome() {
    for (int i = 0; i < 3; i++) {
      if (const char* v = std::getenv(NAMES[i])) {
        had[i] = true;
        saved[i] = v;
      }
      unsetEnv(NAMES[i]);
    }
  }
  ~NoHome() {
    for (int i = 0; i < 3; i++)
      if (had[i]) setEnv(NAMES[i], saved[i]);
  }
};

// Points the platform's home at a fresh folder for one test, so the test
// never creates or touches the real settings folder.
struct FakeHome {
  NoHome none;
  fs::path root = fs::temp_directory_path() / "sc8_settings_test";
  FakeHome() {
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
#if defined(_WIN32)
    setEnv("APPDATA", root.string());
#elif defined(__APPLE__)
    setEnv("HOME", root.string());
#else
    setEnv("XDG_CONFIG_HOME", root.string());
#endif
  }
  ~FakeHome() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
};

}  // namespace

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

  // A bare file name would be read from and written to the working
  // directory, so a machine without a home gets no paths at all.
  TEST_CASE("with no home, the folder and every file are empty paths") {
    NoHome none;
    CHECK(Settings::dir().empty());
    CHECK(Settings::settingsFile().empty());
    CHECK(Settings::layoutFile().empty());
    CHECK(Settings::displayFile().empty());
  }
}
