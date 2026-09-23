// simplecpu-ide: the window around the Ide.
//
//   simplecpu-ide                 an empty session with a sample program
//   simplecpu-ide main.asm        open a source file
//   simplecpu-ide game.rom        open a ROM and browse it
//   simplecpu-ide --screenshot shot.png
//                                 save the window after one second and quit

#include <string>

#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"

#include "ide/ide.h"

int main(int argc, char** argv) {
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
  InitWindow(1440, 900, "SimpleCPU-8 IDE");
  SetExitKey(KEY_NULL);
  SetTargetFPS(60);
  rlImGuiSetup(true);
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  {
    sc8::Ide ide;
    std::string screenshot;
    for (int i = 1; i < argc; i++) {
      if (std::string(argv[i]) == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
      else ide.open(argv[i]);
    }

    int hostFrames = 0;
    while (!WindowShouldClose()) {
      if (!screenshot.empty() && ++hostFrames == 60) {
        TakeScreenshot(screenshot.c_str());
        break;
      }
      ide.update();
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
