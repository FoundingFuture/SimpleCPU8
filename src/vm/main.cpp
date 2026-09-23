// simplecpu: the virtual computer as a standalone program.
//
//   simplecpu --rom game.rom               boot the ROM
//   simplecpu --rom game.rom --fps 60      pace the GPU frame counter to 60 Hz
//   simplecpu --rom game.rom --max         run flat out, the host decides
//   simplecpu --basic                      boot into the BASIC interpreter
//   simplecpu --basic hello.bas            boot BASIC and load the program
//   simplecpu --rom game.rom --microcode naive
//   simplecpu --rom game.rom --crt 0.6     CRT look at 60 percent
//   simplecpu --rom game.rom --no-crt      the plain scaled picture
//   simplecpu --rom game.rom --scale 3     window at 3 x 256
//   simplecpu --rom game.rom --screenshot shot.png
//                                          save the window after one second and quit
//
// Keys while running: F1 toggles the CRT look, F2 and F3 turn it down and
// up, F5 resets, F11 fullscreen, Escape quits.

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "raylib.h"

#include "core/cartridge.h"
#include "vm/audio.h"
#include "vm/computer.h"
#include "vm/display.h"

using namespace sc8;

namespace {

int usage() {
  std::fprintf(stderr,
               "usage: simplecpu --rom FILE.rom [--fps N | --max] [--microcode naive|optimal]\n"
               "                 [--crt S | --no-crt] [--scale N] [--title T]\n"
               "       simplecpu --basic [FILE.bas]\n");
  return 2;
}

std::optional<Cartridge> loadRom(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot read %s\n", path.c_str());
    return std::nullopt;
  }
  std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(in), {});
  CartridgeResult r = decodeCartridge(bytes);
  if (!r.cartridge) std::fprintf(stderr, "%s: %s\n", path.c_str(), r.error.c_str());
  return r.cartridge;
}

std::string romTitle(const Cartridge& c, const std::string& fallback) {
  for (const auto& [k, v] : c.meta) {
    if (k == "title") return v;
  }
  return fallback;
}

}  // namespace

int main(int argc, char** argv) {
  std::string romPath, basicPath, microcode, title, screenshot;
  bool basic = false, maxSpeed = false;
  int fps = 60, scale = 3;
  DisplaySettings display;

  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s needs a value\n", a.c_str());
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--rom") romPath = next();
    else if (a == "--basic") {
      basic = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') basicPath = argv[++i];
    } else if (a == "--fps") fps = std::stoi(next());
    else if (a == "--max") maxSpeed = true;
    else if (a == "--microcode") microcode = next();
    else if (a == "--crt") display.setStrength(std::stof(next()));
    else if (a == "--no-crt") display.enabled = false;
    else if (a == "--scale") scale = std::stoi(next());
    else if (a == "--title") title = next();
    else if (a == "--screenshot") screenshot = next();
    else return usage();
  }
  if (romPath.empty() && !basic) return usage();
  if (basic) {
    std::fprintf(stderr,
                 "simplecpu: the BASIC ROM is not built yet. It needs the C compiler and the GPU,\n"
                 "which are the next ports. Boot a ROM with --rom for now.\n");
    return 1;
  }

  std::optional<Cartridge> cart = loadRom(romPath);
  if (!cart) return 1;

  Computer computer;
  if (!microcode.empty()) {
    for (const std::string& e : computer.selectMicrocode(microcode)) std::fprintf(stderr, "microcode: %s\n", e.c_str());
    cart->microcode.clear();  // the command line wins over the ROM's choice
  }
  computer.insert(std::move(*cart));
  if (title.empty()) title = romTitle(computer.cartridge(), romPath);

  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
  InitWindow(SCREEN_W * scale, SCREEN_H * scale, ("SimpleCPU-8: " + title).c_str());
  SetExitKey(KEY_ESCAPE);
  SetTargetFPS(fps > 0 ? fps : 60);

  Audio audio;
  audio.start();
  Display screen;
  if (!screen.shaderReady()) std::fprintf(stderr, "simplecpu: the CRT shader did not compile, showing the plain picture\n");

  double owed = 0.0;
  float strength = 0.5f;
  int hostFrames = 0;
  while (!WindowShouldClose()) {
    if (!screenshot.empty() && ++hostFrames == 60) {
      TakeScreenshot(screenshot.c_str());
      break;
    }
    if (IsKeyPressed(KEY_F1)) display.enabled = !display.enabled;
    if (IsKeyPressed(KEY_F2)) display.setStrength(strength = std::max(0.0f, strength - 0.1f));
    if (IsKeyPressed(KEY_F3)) display.setStrength(strength = std::min(1.0f, strength + 0.1f));
    if (IsKeyPressed(KEY_F5)) computer.powerOn();
    if (IsKeyPressed(KEY_F11)) ToggleFullscreen();

    // Frame-locked pacing: owed frames come from real elapsed time times
    // the target rate, so a game animates the same on any host. MAX runs
    // the core flat out on a wall clock budget instead.
    if (maxSpeed) {
      const double until = GetTime() + 0.012;
      while (GetTime() < until && computer.runFrame()) {
      }
    } else {
      owed += static_cast<double>(GetFrameTime()) * fps;
      int frames = 0;
      while (owed >= 1.0 && frames < 8) {
        computer.runFrame();
        owed -= 1.0;
        frames++;
      }
      if (owed > 8.0) owed = 0.0;  // a stall is forgiven, not caught up
    }

    screen.upload(computer.frame());
    BeginDrawing();
    ClearBackground(BLACK);
    screen.draw(0, 0, GetScreenWidth(), GetScreenHeight(), display);
    const Machine& m = computer.machine();
    if (m.status != Status::Running) {
      const std::string text = std::string(statusName(m.status)) + (m.crash ? ": " + m.crash->message : "");
      DrawText(text.c_str(), 12, GetScreenHeight() - 28, 20, RAYWHITE);
    }
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
