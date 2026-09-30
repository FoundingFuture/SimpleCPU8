// The person's settings, kept where the platform keeps such things:
// Library/Application Support/SimpleCPU-8 on macOS, %APPDATA%\SimpleCPU-8
// on Windows, $XDG_CONFIG_HOME/simplecpu-8 or ~/.config/simplecpu-8
// elsewhere. settings.txt holds the values as key = value lines, and
// layout.ini holds the pane layout, written only on Save layout.
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
  static std::filesystem::path settingsFile() { return dir() / "settings.txt"; }
  static std::filesystem::path layoutFile() { return dir() / "layout.ini"; }

  // Read the file if there is one; a missing file leaves the defaults.
  void load();
  // Write the file. False when it could not be written.
  bool save() const;
};

}  // namespace sc8
