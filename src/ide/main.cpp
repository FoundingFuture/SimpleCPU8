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
//   simplecpu-ide --view c        start in a view: basic, c, assembly or
//                                 microcode
//   simplecpu-ide --listing c     the MICROCODE view's listing: microcode,
//                                 assembly, c or basic
//   simplecpu-ide --level run     the same by the name of the level the
//                                 view replaced, for the screenshot scripts:
//                                 basic is basic, project is c, run is
//                                 assembly, cpu is microcode
//   simplecpu-ide --run           start running, --speed f60 picks the rate
//                                 (trace, 0.5, 2, 10, 60, 1000, 100000,
//                                 f30, f60, f120, max)
//   simplecpu-ide --screenshot shot.png
//                                 save the window after one second and quit
//   simplecpu-ide --break main.c:12
//                                 a breakpoint on a document's line, as a
//                                 click in the editor's column sets it. A
//                                 .bas takes the BASIC line number
//   simplecpu-ide --do run,step,step
//                                 the Run menu's items by name, one every
//                                 quarter second: run, check, step, pause,
//                                 stop. With --screenshot, the picture is
//                                 taken half a second after the last one
//
// F1 to F4 pick the view. F5 runs and pauses, Shift+F5 powers on.
// F6 runs one frame, F7 checks, F8 burns. F10 steps at the view's level
// and F11 a microcycle.

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"

#include <filesystem>

#include "ide/ide.h"
#include "ide/indent_keys.h"
#include "ide/settings.h"
#include "vm/redraw.h"

int main(int argc, char** argv) {
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
  InitWindow(1440, 900, "SimpleCPU-8 IDE");
  SetExitKey(KEY_NULL);
  SetTargetFPS(60);
  rlImGuiSetup(true);
  sc8::IndentKeys::wideTabs();
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  // The layout is kept in the settings folder and written only by Save
  // layout in the Settings menu, never on every change.
  ImGui::GetIO().IniFilename = nullptr;
  // DESIGN: the text cursor does not blink. An idle IDE sleeps until
  // input, see vm/redraw.h, and a blink would need a frame twice a second.
  ImGui::GetIO().ConfigInputTextCursorBlink = false;

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
    // --do: the Run menu's items still to do, and the frame for the next.
    std::vector<std::string> script;
    // --break: set once everything opened, since a document must exist.
    std::vector<std::string> breaks;
    int scriptAt = 15;
    int shotAt = 60;
    // An opened project picks its own view. A --view on the command line
    // is applied after everything opened, so it wins.
    std::optional<sc8::View> view;
    for (int i = 1; i < argc; i++) {
      const std::string a = argv[i];
      if (a == "--screenshot" && i + 1 < argc) {
        screenshot = argv[++i];
      } else if (a == "--view" && i + 1 < argc) {
        const std::string name = argv[++i];
        view = sc8::viewNamed(name);
        if (!view) std::fprintf(stderr, "unknown view %s: basic, c, assembly or microcode\n", name.c_str());
      } else if (a == "--level" && i + 1 < argc) {
        const std::string name = argv[++i];
        view = sc8::viewOfLevel(name);
        if (!view) {
          std::fprintf(stderr,
                       "unknown level %s: --level takes the old names basic, project, run or cpu, for the views "
                       "basic, c, assembly and microcode. --view takes the new ones.\n",
                       name.c_str());
        }
      } else if (a == "--speed" && i + 1 < argc) {
        if (!ide.setSpeed(argv[++i])) std::fprintf(stderr, "unknown speed %s\n", argv[i]);
      } else if (a == "--font-view" && i + 1 < argc) {
        if (!ide.setFontView(argv[++i])) std::fprintf(stderr, "unknown font view %s: single, strip or block\n", argv[i]);
      } else if (a == "--listing" && i + 1 < argc) {
        if (!ide.setListing(argv[++i])) {
          std::fprintf(stderr, "unknown listing %s: microcode, assembly, c or basic\n", argv[i]);
        }
      } else if (a == "--break" && i + 1 < argc) {
        breaks.push_back(argv[++i]);
      } else if (a == "--do" && i + 1 < argc) {
        const std::string list = argv[++i];
        for (size_t at = 0; at <= list.size();) {
          size_t comma = list.find(',', at);
          if (comma == std::string::npos) comma = list.size();
          if (comma > at) script.push_back(list.substr(at, comma - at));
          at = comma + 1;
        }
      } else if (a == "--run") {
        run = true;
      } else {
        ide.open(a);
      }
    }
    ide.restoreSprite();
    if (view) ide.setView(*view);
    for (const std::string& b : breaks) {
      if (!ide.addBreak(b)) std::fprintf(stderr, "no breakpoint at %s: name a document and a line, main.c:12\n", b.c_str());
    }
    if (run) ide.run();

    int hostFrames = 0;
    sc8::RedrawPacer pacer;
    // An idle IDE sleeps in EndDrawing until the next input event. The
    // screenshot switch counts frames, so it never sleeps.
    auto paceRedraw = [&] {
      if (pacer.waitAfterFrame(screenshot.empty() && script.empty() && ide.idle())) EnableEventWaiting();
      else DisableEventWaiting();
    };
    while (!ide.done()) {
      // The close gesture asks about unsaved documents before it ends.
      // The question is asked in the IDE, so the full screen goes first.
      if (WindowShouldClose()) {
        ide.leaveScreenOnly();
        ide.requestQuit();
      }
      hostFrames++;
      if (!script.empty() && hostFrames >= scriptAt) {
        if (!ide.menuAction(script.front())) {
          std::fprintf(stderr, "unknown --do item %s: run, check, step, pause or stop\n", script.front().c_str());
        }
        script.erase(script.begin());
        scriptAt = hostFrames + 15;
        shotAt = std::max(shotAt, hostFrames + 30);
      }
      if (!screenshot.empty() && hostFrames >= shotAt && script.empty()) {
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
        paceRedraw();
        EndDrawing();
        continue;
      }
      BeginDrawing();
      ClearBackground(Color{24, 24, 28, 255});
      rlImGuiBegin();
      ide.frame();
      rlImGuiEnd();
      paceRedraw();
      EndDrawing();
    }
  }

  rlImGuiShutdown();
  CloseWindow();
  return 0;
}
