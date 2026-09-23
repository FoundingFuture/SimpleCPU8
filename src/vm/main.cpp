// simplecpu: the virtual computer as a standalone program.
//
//   simplecpu --rom game.rom               boot the ROM
//   simplecpu --rom game.rom --fps 60      pace the GPU frame counter to 60 Hz
//   simplecpu --rom game.rom --max         run flat out, the host decides
//   simplecpu --basic                      boot into the BASIC interpreter
//   simplecpu --basic hello.bas            boot BASIC and load the program
//   simplecpu --basic hello.bas --run      load it and run it
//   simplecpu --rom x.rom --type 'RUN\n'    type text into the machine after
//                                          boot; \n is Enter
//   simplecpu --rom game.rom --microcode naive
//   simplecpu --rom game.rom --crt 0.6     CRT look at 60 percent
//   simplecpu --rom game.rom --no-crt      the plain scaled picture
//   simplecpu --rom game.rom --scale 3     window at 3 x 256
//   simplecpu --rom game.rom --screenshot shot.png
//                                          save the window after one second and quit
//
// Keys while running: F1 toggles the CRT look, F2 and F3 turn it down and
// up. F5 powers on again, F11 goes fullscreen. Quitting is the operating
// system's own gesture: Command-Q on macOS, Alt-F4 or the close button
// elsewhere. No key is taken from the machine, so Escape and Ctrl-C reach
// BASIC, which uses both to break a running program.

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "raylib.h"

#include "core/cartridge.h"
#include "vm/audio.h"
#include "vm/computer.h"
#include "vm/display.h"

#if SC8_HAVE_BASIC
#include "basic/basic_rom.h"
#endif
#include "vm/keys.h"

using namespace sc8;

namespace {

int usage() {
  std::fprintf(stderr,
               "usage: simplecpu --rom FILE.rom [--fps N | --max] [--microcode naive|optimal]\n"
               "                 [--crt S | --no-crt] [--scale N] [--title T]\n"
               "       simplecpu --basic [FILE.bas [--run]]\n"
               "       ... [--type TEXT] [--screenshot FILE.png]\n");
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

// A slot name from a file name: the stem, uppercase, letters and digits,
// at most 16, which is what the storage device accepts.
std::string slotNameFor(const std::string& path) {
  std::string stem = std::filesystem::path(path).stem().string();
  std::string out;
  for (char c : stem) {
    if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (out.size() == 16) break;
  }
  return out.empty() ? "PROGRAM" : out;
}

std::string romTitle(const Cartridge& c, const std::string& fallback) {
  for (const auto& [k, v] : c.meta) {
    if (k == "title") return v;
  }
  return fallback;
}

}  // namespace

int main(int argc, char** argv) {
  std::string romPath, basicPath, microcode, title, screenshot, typedExtra;
  bool basic = false, maxSpeed = false, runBasic = false;
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
    else if (a == "--run") runBasic = true;
    else if (a == "--type") {
      std::string t = next();
      std::string out;
      for (size_t k = 0; k < t.size(); k++) {
        if (t[k] == '\\' && k + 1 < t.size() && t[k + 1] == 'n') {
          out += '\n';
          k++;
        } else {
          out += t[k];
        }
      }
      typedExtra += out;
    }
    else if (a == "--microcode") microcode = next();
    else if (a == "--crt") display.setStrength(std::stof(next()));
    else if (a == "--no-crt") display.enabled = false;
    else if (a == "--scale") scale = std::stoi(next());
    else if (a == "--title") title = next();
    else if (a == "--screenshot") screenshot = next();
    else return usage();
  }
  if (romPath.empty() && !basic) return usage();
  if (!romPath.empty() && basic) {
    std::fprintf(stderr, "simplecpu: --basic boots its own ROM, so it takes no --rom\n");
    return 2;
  }

  std::optional<Cartridge> cart;
  std::string typed;
  if (basic) {
#if SC8_HAVE_BASIC
    std::vector<uint8_t> bytes(basicRom().begin(), basicRom().end());
    CartridgeResult r = decodeCartridge(bytes);
    if (!r.cartridge) {
      std::fprintf(stderr, "simplecpu: the built-in BASIC ROM is damaged: %s\n", r.error.c_str());
      return 1;
    }
    cart = std::move(r.cartridge);
    if (!basicPath.empty()) {
      // The program goes into a slot of the BASIC cartridge and the
      // interpreter is told to load it, the same way a person would.
      std::ifstream in(basicPath);
      if (!in) {
        std::fprintf(stderr, "cannot read %s\n", basicPath.c_str());
        return 1;
      }
      std::string text(std::istreambuf_iterator<char>(in), {});
      std::string slot = slotNameFor(basicPath);
      cart->basic.emplace_back(slot, text);
      typed = "!LOAD \"" + slot + "\"\n";
      if (runBasic) typed += "RUN\n";
    }
#else
    std::fprintf(stderr, "simplecpu: this build has no BASIC ROM (built with SC8_BUILD_TOOLS off)\n");
    return 1;
#endif
  } else {
    cart = loadRom(romPath);
    if (!cart) return 1;
  }

  Computer computer;
  if (!microcode.empty()) {
    for (const std::string& e : computer.selectMicrocode(microcode)) std::fprintf(stderr, "microcode: %s\n", e.c_str());
    cart->microcode.clear();  // the command line wins over the ROM's choice
  }
  computer.insert(std::move(*cart));
  computer.setRomPath(romPath);
  typed += typedExtra;
  if (!typed.empty()) computer.typeText(typed);
  if (title.empty()) title = basic ? "BASIC" : romTitle(computer.cartridge(), romPath);

  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
  InitWindow(SCREEN_W * scale, SCREEN_H * scale, ("SimpleCPU-8: " + title).c_str());
  // Every key reaches the machine. Quitting belongs to the window.
  SetExitKey(KEY_NULL);
  SetTargetFPS(fps > 0 ? fps : 60);

  // The display owns GL objects, so it is destroyed inside this block,
  // before CloseWindow takes the context away.
  {
    Audio audio;
    audio.start();
    Display screen;
    if (!screen.shaderReady()) std::fprintf(stderr, "simplecpu: the CRT shader did not compile, showing the plain picture\n");

    double owed = 0.0;
    float strength = 0.5f;
    while (!WindowShouldClose()) {
      // One and a half seconds of wall clock, so the machine has run a
      // while whatever the host's frame rate is.
      if (!screenshot.empty() && GetTime() > 1.5) {
        // raylib's TakeScreenshot drops the directory, so export by hand.
        Image shot = LoadImageFromScreen();
        ExportImage(shot, screenshot.c_str());
        UnloadImage(shot);
        break;
      }
      if (IsKeyPressed(KEY_F1)) display.enabled = !display.enabled;
      if (IsKeyPressed(KEY_F2)) display.setStrength(strength = std::max(0.0f, strength - 0.1f));
      if (IsKeyPressed(KEY_F3)) display.setStrength(strength = std::min(1.0f, strength + 0.1f));
      if (IsKeyPressed(KEY_F5)) computer.powerOn();
      if (IsKeyPressed(KEY_F11)) ToggleFullscreen();
      pollKeyboard(computer.input());
      computer.pumpTyping();

      // Frame-locked pacing, the worker's rule. Owed frames come from real
      // elapsed time times the target rate, capped at about a second of
      // catch-up. A game then animates the same on any host. MAX runs the
      // core flat out on a wall clock budget per host frame instead.
      if (maxSpeed) {
        const double until = GetTime() + 0.012;
        while (GetTime() < until && computer.runInstructions(20000)) {
        }
      } else {
        owed += static_cast<double>(GetFrameTime()) * fps;
        if (owed > fps) owed = fps;
        while (owed >= 1.0) {
          const uint64_t done = computer.runToNextFrame();
          owed -= 1.0;
          if (computer.machine().status != Status::Running) break;
          if (done == 0) break;  // nothing advanced: avoid a busy spin
        }
      }
      // The chip renders on its own clock, so a tune plays on while the CPU
      // sits halted.
      computer.pumpAudio(audio);

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
  }

  CloseWindow();
  return 0;
}
