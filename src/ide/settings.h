// The person's settings, kept where the platform keeps such things:
// Library/Application Support/SimpleCPU-8 on macOS, %APPDATA%\SimpleCPU-8
// on Windows, $XDG_CONFIG_HOME/simplecpu-8 or ~/.config/simplecpu-8
// elsewhere. settings.txt holds the values as key = value lines,
// layout.ini holds the pane layout, written only on Save layout, and
// display.txt holds the screen's look.
#pragma once

#include <filesystem>
#include <string>

namespace sc8 {

struct Settings {
  std::string projectsDir;  // where New project and Open project start
  float uiScale = 1.0f;     // every font and spacing times this
  bool formatAssembly = true;  // lay out .asm documents when they are saved
  bool errorSound = true;      // a tone when a build or a microcode set fails

  // The folder, created when missing. Empty when no home is known.
  static std::filesystem::path dir();
  // Each file in the folder, or an empty path when there is no folder.
  static std::filesystem::path settingsFile() { return file("settings.txt"); }
  static std::filesystem::path layoutFile() { return file("layout.ini"); }
  static std::filesystem::path displayFile() { return file("display.txt"); }

  // Read the file if there is one; a missing file leaves the defaults.
  void load();
  // Write the file. False when it could not be written.
  bool save() const;

 private:
  // DESIGN: never a bare name. dir() / name with an empty dir() is a
  // relative path, which would put the file in the working directory.
  static std::filesystem::path file(const char* name);
};

}  // namespace sc8
