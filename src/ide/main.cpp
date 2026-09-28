// simplecpu-ide: the window around the Ide.
//
//   simplecpu-ide                 an empty session with a sample program
//   simplecpu-ide main.asm        open a source file
//   simplecpu-ide game.rom        open a ROM and browse it
//   simplecpu-ide game/assets/ship.png
//                                 open the project and the picture in the
//                                 sprite editor, or a .font in font mode
//   simplecpu-ide --font-view block
//                                 font mode's view: single, strip or block
//   simplecpu-ide --level run     start in a level: edit, run or microcode
//   simplecpu-ide --run           start running, --speed f60 picks the rate
//                                 (trace, 0.5, 2, 10, 60, 1000, 100000,
//                                 f30, f60, f120, max)
//   simplecpu-ide --screenshot shot.png
//                                 save the window after one second and quit
//
// F1, F2 and F3 switch the level. F5 runs and pauses, Shift+F5 powers on.
// F6 runs one frame, F7 assembles, F8 burns. F10 steps an instruction and
// F11 a microcycle.

#include <cstdio>
#include <optional>
#include <string>

#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"

#include <filesystem>

#include "ide/ide.h"
#include "ide/settings.h"

int main(int argc, char** argv) {
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
  InitWindow(1440, 900, "SimpleCPU-8 IDE");
  SetExitKey(KEY_NULL);
  SetTargetFPS(60);
  rlImGuiSetup(true);
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  // The layout is kept in the settings folder and written only by Save
  // layout in the Settings menu, never on every change.
  ImGui::GetIO().IniFilename = nullptr;

  {
    // The IDE registers the handler for its own entries in the layout
    // file, so the file is read after the IDE exists.
    sc8::Ide ide;
    {
      const std::filesystem::path layout = sc8::Settings::layoutFile();
      if (!layout.empty() && std::filesystem::exists(layout)) ImGui::LoadIniSettingsFromDisk(layout.string().c_str());
    }
    ide.applyWindow();
    ide.applyScale();
    std::string screenshot;
    bool run = false;
    // An opened project picks its own level. A --level on the command
    // line is applied after everything opened, so it wins.
    std::optional<sc8::Level> level;
    for (int i = 1; i < argc; i++) {
      const std::string a = argv[i];
      if (a == "--screenshot" && i + 1 < argc) {
        screenshot = argv[++i];
      } else if (a == "--level" && i + 1 < argc) {
        const std::string name = argv[++i];
        if (name == "basic") level = sc8::Level::Basic;
        else if (name == "project" || name == "edit") level = sc8::Level::Project;
        else if (name == "run") level = sc8::Level::Run;
        else if (name == "cpu" || name == "microcode") level = sc8::Level::Cpu;
        else std::fprintf(stderr, "unknown level %s: basic, project, run or cpu\n", name.c_str());
      } else if (a == "--speed" && i + 1 < argc) {
        if (!ide.setSpeed(argv[++i])) std::fprintf(stderr, "unknown speed %s\n", argv[i]);
      } else if (a == "--font-view" && i + 1 < argc) {
        if (!ide.setFontView(argv[++i])) std::fprintf(stderr, "unknown font view %s: single, strip or block\n", argv[i]);
      } else if (a == "--run") {
        run = true;
      } else {
        ide.open(a);
      }
    }
    ide.restoreSprite();
    if (level) ide.setLevel(*level);
    if (run) ide.run();

    int hostFrames = 0;
    while (!ide.done()) {
      // The close gesture asks about unsaved documents before it ends.
      // The question is asked in the IDE, so the full screen goes first.
      if (WindowShouldClose()) {
        ide.leaveScreenOnly();
        ide.requestQuit();
      }
      if (!screenshot.empty() && ++hostFrames == 60) {
        // raylib's TakeScreenshot drops the directory, so export by hand.
        Image shot = LoadImageFromScreen();
        ExportImage(shot, screenshot.c_str());
        UnloadImage(shot);
        break;
      }
      ide.update();
      // Full screen, the machine's picture is all there is to draw: the
      // panes and Dear ImGui are skipped until Escape.
      if (ide.screenOnly()) {
        BeginDrawing();
        ide.drawScreenOnly();
        EndDrawing();
        continue;
      }
      BeginDrawing();
      ClearBackground(Color{24, 24, 28, 255});
      rlImGuiBegin();
      ide.frame();
      rlImGuiEnd();
      EndDrawing();
    }
  }

  rlImGuiShutdown();
  CloseWindow();
  return 0;
}
