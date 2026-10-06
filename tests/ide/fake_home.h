// Environment helpers for the settings and install tests: a fake home
// folder, or none.
#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

namespace sc8test {

namespace fs = std::filesystem;

inline void setEnv(const char* name, const std::string& value) {
#if defined(_WIN32)
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

// An empty value removes the variable on Windows, and unsetenv does it
// elsewhere.
inline void unsetEnv(const char* name) {
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

}  // namespace sc8test
