#include "ide/settings.h"

#include <cstdlib>
#include <fstream>

namespace fs = std::filesystem;

namespace sc8 {

fs::path Settings::dir() {
  fs::path base;
#if defined(_WIN32)
  if (const char* appdata = std::getenv("APPDATA")) base = fs::path(appdata) / "SimpleCPU-8";
#elif defined(__APPLE__)
  if (const char* home = std::getenv("HOME")) base = fs::path(home) / "Library" / "Application Support" / "SimpleCPU-8";
#else
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) base = fs::path(xdg) / "simplecpu-8";
  else if (const char* home = std::getenv("HOME")) base = fs::path(home) / ".config" / "simplecpu-8";
#endif
  if (base.empty()) return base;
  std::error_code ec;
  fs::create_directories(base, ec);
  return base;
}

void Settings::load() {
  const fs::path file = settingsFile();
  if (file.empty()) return;
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    auto trim = [](std::string s) {
      while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t')) s.pop_back();
      size_t at = 0;
      while (at < s.size() && (s[at] == ' ' || s[at] == '\t')) at++;
      return s.substr(at);
    };
    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));
    if (key == "projects") projectsDir = value;
    else if (key == "scale") {
      const float f = std::strtof(value.c_str(), nullptr);
      if (f >= 0.5f && f <= 4.0f) uiScale = f;
    } else if (key == "format_asm") {
      formatAssembly = value != "0";
    }
  }
}

bool Settings::save() const {
  const fs::path file = settingsFile();
  if (file.empty()) return false;
  std::ofstream o(file);
  o << "# SimpleCPU-8 IDE settings\n";
  o << "projects = " << projectsDir << "\n";
  o << "scale = " << uiScale << "\n";
  o << "format_asm = " << (formatAssembly ? 1 : 0) << "\n";
  return static_cast<bool>(o);
}

}  // namespace sc8
