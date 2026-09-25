#include "ide/ide.h"

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "imgui.h"
#include "imgui_internal.h"
#include "raylib.h"
#include "rlImGui.h"

#include "assets/assets.h"
#include "basic/basic_rom.h"
#include "basic/program.h"
#include "core/cartridge.h"
#include "core/mcparse.h"
#include "ide/native_window.h"
#include "ide/panes.h"

namespace fs = std::filesystem;

namespace sc8 {

namespace {

// The ladder of the browser's speed menu. Slow speeds stretch the tick so
// the label is literally true: at 0.5 the machine executes one instruction
// every two seconds.
const Speed LADDER[] = {
    {"trace microcode", "trace", 3, 0, true},
    {"0.5 instr/s", "0.5", 0.5, 0, false},
    {"2 instr/s", "2", 2, 0, false},
    {"10 instr/s", "10", 10, 0, false},
    {"60 instr/s", "60", 60, 0, false},
    {"1k instr/s", "1000", 1000, 0, false},
    {"100k instr/s", "100000", 100000, 0, false},
    {"30 fps", "f30", 0, 30, false},
    {"60 fps", "f60", 0, 60, false},
    {"120 fps", "f120", 0, 120, false},
    {"MAX", "max", 0, 0, false},
};

// The worker ignored the fast-frame latch above this rate, where real
// frames arrive on their own.
constexpr double FAST_FRAME_MAX_IPS = 1000;

// Whether a run at this speed keeps the machine's record of what it did.
// The browser's trace.ts is not in the reference tree. This is the rule
// its worker comments describe. The watchable half of the ladder traces.
// The fast half runs lean, and the screen is what those speeds exist for.
bool tracedRun(const Speed& s) {
  if (s.micro) return true;
  return s.ips > 0 && s.ips <= FAST_FRAME_MAX_IPS;
}

using panes::hex;

// The disassembly of one instruction for a ROM that came without source.
// The canonical name with its operand placeholder filled in.
std::string disassemble(const Instr& in) {
  const OpDef* def = opByCode(in.op);
  if (!def) return "?? $" + hex(in.op, 2) + " $" + hex(in.operand, 4);
  std::string text(def->name);
  auto replace = [&](std::string_view word, const std::string& with) {
    const size_t at = text.find(word);
    if (at == std::string::npos) return false;
    text.replace(at, word.size(), with);
    return true;
  };
  switch (def->operand) {
    case OperandKind::None: break;
    case OperandKind::Imm8: replace("imm8", std::to_string(in.operand & 0xff)); break;
    case OperandKind::Addr8: replace("addr8", "$" + hex(in.operand & 0xff, 2)); break;
    case OperandKind::Disp8:
      if (!replace("disp8", std::to_string(in.operand & 0xff))) replace("n]", std::to_string(in.operand & 0xff) + "]");
      break;
    case OperandKind::Imm16: replace("imm16", "$" + hex(in.operand, 4)); break;
    case OperandKind::Off16: {
      const int off = in.operand >= 0x8000 ? in.operand - 0x10000 : in.operand;
      // Nothing added reads back the way it is written: a copy, or the
      // TST of a register against itself.
      if (off == 0 && text.size() == 13 && text.substr(3, 2) == text.substr(9, 2)) return "TST " + text.substr(3, 2);
      replace("+n", off == 0 ? std::string() : off < 0 ? std::to_string(off) : "+" + std::to_string(off));
      break;
    }
    case OperandKind::Addr16: replace("addr16", "$" + hex(in.operand, 4)); break;
    case OperandKind::Target: text += " $" + hex(in.operand, 4); break;
    case OperandKind::Port: text += " $" + hex(in.operand >> 8, 2); break;
    case OperandKind::PortImm: text += " $" + hex(in.operand >> 8, 2) + ", $" + hex(in.operand & 0xff, 2); break;
  }
  return text;
}

}  // namespace

std::span<const Speed> speedLadder() { return LADDER; }

Ide::Ide()
    : sprite_(SpriteEditor::Host{
          [this](const std::string& n) { return readAsset(n); },
          [this](const std::string& n, const std::vector<uint8_t>& b) { return writeAsset(n, b); },
          [this](std::string m) { note(std::move(m)); }}) {
  resetSections();
  registerLayoutHandler();
  customText_.reserve(1 << 15);
  settings_.load();
  loadDisplay();
  audio_.start();
  newScratchProject();
}

// The sections of the CPU view and the side pane, as one entry in the
// layout file:
//
//   [SC8Sections][State]
//   Memory=1,side
//
//   ScreenHeight=320
//
// A line names a section, whether it is open and where it shows. The
// last line is the side pane's screen height, 0 to fit the width.
void Ide::registerLayoutHandler() {
  static const char* const KEYS[SEC_COUNT] = {"Datapath", "Flow", "Registers", "Memory", "Stack", "Screen", "Manual"};
  ImGuiSettingsHandler h;
  h.TypeName = "SC8Sections";
  h.TypeHash = ImHashStr("SC8Sections");
  h.UserData = this;
  h.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, const char*) -> void* { return handler->UserData; };
  h.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* text) {
    Ide* ide = static_cast<Ide*>(entry);
    const std::string line = text;
    const size_t eq = line.find('=');
    if (eq == std::string::npos || eq + 1 >= line.size()) return;
    if (line.compare(0, eq, "ScreenHeight") == 0) {
      ide->sideScreenHeight_ = std::max(0.0f, std::strtof(line.c_str() + eq + 1, nullptr));
      return;
    }
    for (size_t i = 0; i < SEC_COUNT; i++) {
      if (line.compare(0, eq, KEYS[i]) != 0 || std::string_view(KEYS[i]).size() != eq) continue;
      SectionState& st = ide->sections_[i];
      st.open = line[eq + 1] == '1';
      if (line.find(",side", eq) != std::string::npos) st.place = Place::Side;
      else if (line.find(",cpu", eq) != std::string::npos) st.place = Place::Cpu;
    }
  };
  h.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out) {
    const Ide* ide = static_cast<const Ide*>(handler->UserData);
    out->appendf("[%s][State]\n", handler->TypeName);
    for (size_t i = 0; i < SEC_COUNT; i++) {
      const SectionState& st = ide->sections_[i];
      out->appendf("%s=%d,%s\n", KEYS[i], st.open ? 1 : 0, st.place == Place::Side ? "side" : "cpu");
    }
    out->appendf("ScreenHeight=%d\n", static_cast<int>(ide->sideScreenHeight_));
    out->append("\n");
  };
  ImGui::AddSettingsHandler(&h);

  // The window, as one entry:
  //
  //   [SC8Window][State]
  //   Pos=120,80
  //   Size=1440,900
  //   Monitor=0
  //   Maximized=0
  //   Fullscreen=0
  //   NativeFullscreen=0
  //
  // Pos and Size are the plain window, the one to come back to from a
  // maximized or full screen one.
  ImGuiSettingsHandler w;
  w.TypeName = "SC8Window";
  w.TypeHash = ImHashStr("SC8Window");
  w.UserData = this;
  w.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, const char*) -> void* { return handler->UserData; };
  w.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* text) {
    WindowPlace& p = static_cast<Ide*>(entry)->savedWindow_;
    int a = 0, b = 0;
    if (std::sscanf(text, "Pos=%d,%d", &a, &b) == 2) {
      p.x = a;
      p.y = b;
    } else if (std::sscanf(text, "Size=%d,%d", &a, &b) == 2) {
      p.w = a;
      p.h = b;
      p.known = a > 0 && b > 0;
    } else if (std::sscanf(text, "Monitor=%d", &a) == 1) {
      p.monitor = a;
    } else if (std::sscanf(text, "Maximized=%d", &a) == 1) {
      p.maximized = a != 0;
    } else if (std::sscanf(text, "Fullscreen=%d", &a) == 1) {
      p.fullscreen = a != 0;
    } else if (std::sscanf(text, "NativeFullscreen=%d", &a) == 1) {
      p.native = a != 0;
    }
  };
  w.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out) {
    const WindowPlace p = static_cast<const Ide*>(handler->UserData)->windowNow();
    out->appendf("[%s][State]\n", handler->TypeName);
    out->appendf("Pos=%d,%d\nSize=%d,%d\nMonitor=%d\n", p.x, p.y, p.w, p.h, p.monitor);
    out->appendf("Maximized=%d\nFullscreen=%d\nNativeFullscreen=%d\n\n", p.maximized ? 1 : 0,
                 p.fullscreen ? 1 : 0, p.native ? 1 : 0);
  };
  ImGui::AddSettingsHandler(&w);

  // The sprite editor, as one entry: Visible and Open, then the editor's
  // own view lines.
  ImGuiSettingsHandler sp;
  sp.TypeName = "SC8Sprite";
  sp.TypeHash = ImHashStr("SC8Sprite");
  sp.UserData = this;
  sp.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, const char*) -> void* { return handler->UserData; };
  sp.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* text) {
    Ide* ide = static_cast<Ide*>(entry);
    const std::string line = text;
    if (line.rfind("Visible=", 0) == 0) ide->spriteVisible_ = line.size() > 8 && line[8] == '1';
    else if (line.rfind("Open=", 0) == 0) ide->savedSprite_ = line.substr(5);
    else ide->sprite_.setView(line);
  };
  sp.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out) {
    const Ide* ide = static_cast<const Ide*>(handler->UserData);
    out->appendf("[%s][State]\n", handler->TypeName);
    out->appendf("Visible=%d\n", ide->spriteVisible_ ? 1 : 0);
    if (ide->sprite_.isOpen()) out->appendf("Open=%s\n", ide->sprite_.name().c_str());
    out->append(ide->sprite_.viewText().c_str());
    out->append("\n");
  };
  ImGui::AddSettingsHandler(&sp);
}

void Ide::restoreSprite() {
  if (!spriteVisible_ || savedSprite_.empty() || sprite_.isOpen()) return;
  for (const AssetEntry& a : assetList()) {
    if (a.name == savedSprite_) {
      openSprite(savedSprite_);
      return;
    }
  }
}

Ide::WindowPlace Ide::windowNow() const {
  WindowPlace p;
  p.known = true;
  if (screenOnly_) {
    // Full screen for the machine is a state of the moment, so the window
    // it came from is what is kept.
    p.fullscreen = filledBeforeScreen_;
    p.native = nativeBeforeScreen_;
  } else {
    p.maximized = IsWindowMaximized();
    p.fullscreen = windowFilled();
    p.native = nativeFullscreen();
  }
  if (plainW_ > 0) {
    p.x = plainX_;
    p.y = plainY_;
    p.w = plainW_;
    p.h = plainH_;
  } else {
    const Vector2 at = GetWindowPosition();
    p.x = static_cast<int>(at.x);
    p.y = static_cast<int>(at.y);
    p.w = GetScreenWidth();
    p.h = GetScreenHeight();
  }
  p.monitor = GetCurrentMonitor();
  return p;
}

bool Ide::windowFilled() const { return IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE); }

bool Ide::nativeFullscreen() const { return native::isFullscreen(GetWindowHandle()); }

void Ide::toggleWindowFill() {
  // The system's full screen and the borderless fill do not stack.
  if (nativeFullscreen()) native::toggleFullscreen(GetWindowHandle());
  ToggleBorderlessWindowed();
}

void Ide::trackWindow() {
  if (pendingNative_ > 0 && --pendingNative_ == 0 && !nativeFullscreen()) {
    native::toggleFullscreen(GetWindowHandle());
  }
  if (screenOnly_ || windowFilled() || nativeFullscreen() || IsWindowMaximized() || IsWindowMinimized()) return;
  const Vector2 at = GetWindowPosition();
  plainX_ = static_cast<int>(at.x);
  plainY_ = static_cast<int>(at.y);
  plainW_ = GetScreenWidth();
  plainH_ = GetScreenHeight();
}

void Ide::applyWindow() {
  const WindowPlace& p = savedWindow_;
  if (!p.known) return;
  // The monitor it was on may be gone. Keep the place only when a good
  // part of the title bar lands on a monitor attached now.
  bool visible = false;
  for (int m = 0; m < GetMonitorCount(); m++) {
    const Vector2 o = GetMonitorPosition(m);
    const int mx = static_cast<int>(o.x), my = static_cast<int>(o.y);
    const int mw = GetMonitorWidth(m), mh = GetMonitorHeight(m);
    if (p.x + 80 > mx && p.x + 80 < mx + mw && p.y + 10 > my && p.y + 10 < my + mh) visible = true;
  }
  if (visible) {
    SetWindowSize(std::max(p.w, 640), std::max(p.h, 400));
    SetWindowPosition(p.x, p.y);
  } else if (p.monitor >= 0 && p.monitor < GetMonitorCount()) {
    SetWindowMonitor(p.monitor);
  }
  plainX_ = p.x;
  plainY_ = p.y;
  plainW_ = p.w;
  plainH_ = p.h;
  // The system's full screen animates, and Cocoa ignores the request
  // before the window is on screen, so it waits for a few frames.
  if (p.native) pendingNative_ = 3;
  else if (p.fullscreen && !windowFilled()) toggleWindowFill();
  else if (p.maximized) MaximizeWindow();
}

void Ide::resetSections() {
  std::copy(std::begin(SECTION_DEFAULTS), std::end(SECTION_DEFAULTS), std::begin(sections_));
  sideScreenHeight_ = 0.0f;
}

// A ROM opens as a project. A folder opens as a project. A source file
// opens the project it sits in: its folder, or the folder above a src/
// directory, and becomes the active document.
void Ide::open(const std::string& path) {
  const std::string ext = fs::path(path).extension().string();
  openedBasic_ = ext == ".bas";
  if (ext == ".rom") {
    openRomAsProject(path);
    return;
  }
  if (fs::is_directory(path)) {
    openProject(path);
    return;
  }
  if (docKindOf(fs::path(path).filename().string()) != DocKind::Other) {
    fs::path dir = fs::absolute(path).parent_path();
    if (dir.filename() == "src") dir = dir.parent_path();
    openProject(dir.string());
    const std::string name = fs::path(path).filename().string();
    for (size_t i = 0; i < docs_.size(); i++) {
      if (docs_[i].name == name) activate(i);
    }
    return;
  }
  if (isPicture(path)) {
    // A picture opens its project, found from the assets folder it sits
    // in, and then the picture in the sprite editor.
    const fs::path file = fs::absolute(path);
    fs::path dir = file.parent_path();
    if (dir.filename() == "assets") dir = dir.parent_path();
    openProject(dir.string());
    setLevel(Level::Project);
    openSprite(file.filename().string());
    if (sprite_.isOpen()) focusAfterLayout_ = "###Sprite";
    return;
  }
  note("cannot open " + path + ": a .rom, a folder, a picture, or a .c, .h, .asm, .bas or microcode.txt");
}

void Ide::run() {
  if (openedBasic_ && activeDoc() && docKindOf(activeDoc()->name) == DocKind::Basic && !projectDir_.empty()) {
    // The whole project built when it opened. Its AUTORUN is running
    // already when the .bas was that one; any other runs the quick way.
    if (activeDoc()->name == "autorun.bas") setRunning(true);
    else runInBasic();
    return;
  }
  setRunning(true);
}

void Ide::setLevel(Level level) {
  level_ = level;
  // The manual follows the level too: the BASIC level opens the BASIC
  // guide, the CPU level the microcode guide, the Run level the assembly
  // guide. Project keeps the document's.
  if (level == Level::Basic) {
    guideView_.guide = Guide::Basic;
    manualTab_ = static_cast<int>(Guide::Basic);
  } else if (level == Level::Cpu) {
    guideView_.guide = Guide::Microcode;
    manualTab_ = static_cast<int>(Guide::Microcode);
  } else if (level == Level::Run) {
    guideView_.guide = Guide::Assembly;
    manualTab_ = static_cast<int>(Guide::Assembly);
  } else if (activeDoc()) {
    activate(active_);
  }
}

bool Ide::setSpeed(const std::string& value) {
  for (size_t i = 0; i < std::size(LADDER); i++) {
    if (value == LADDER[i].value) {
      speed_ = static_cast<int>(i);
      return true;
    }
  }
  return false;
}

void Ide::requestQuit() {
  if (anyDirty()) askQuit_ = true;
  else done_ = true;
}

// The listing model: one line per line of the built assembly, or one per
// instruction when the ROM came without source. A .ram line that starts
// with a label of RAM kind gets the label. The pane offers it for the
// watch list.
void Ide::rebuildListing() {
  listing_.clear();
  if (!haveSource_) {
    const auto& prog = computer_.cartridge().program;
    for (size_t i = 0; i < prog.size(); i++) {
      if (prog[i] == UNLOADED_SLOT) continue;
      listing_.push_back({disassemble(prog[i]), static_cast<int>(i), std::nullopt, ""});
    }
    return;
  }
  const std::string& source = builtAssembly_;
  size_t pos = 0;
  int lineNo = 0;
  while (pos <= source.size()) {
    const size_t nl = source.find('\n', pos);
    const std::string text = source.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = nl == std::string::npos ? source.size() + 1 : nl + 1;
    lineNo++;
    ListLine line{text, std::nullopt, std::nullopt, ""};
    if (auto it = assembled_.lineToInstr.find(lineNo); it != assembled_.lineToInstr.end()) line.instr = it->second;
    if (auto it = assembled_.ramLineAddr.find(lineNo); it != assembled_.ramLineAddr.end()) {
      line.ramAddr = it->second;
      const size_t colon = text.find(':');
      if (colon != std::string::npos) {
        std::string name = text.substr(0, colon);
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
        auto lab = assembled_.labels.find(name);
        if (lab != assembled_.labels.end() && lab->second.kind == Label::Kind::Ram) line.label = name;
      }
    }
    listing_.push_back(std::move(line));
  }
}

// ---- running

void Ide::setRunning(bool on) {
  running_ = on;
  owed_ = 0.0;
  tick_ = 0.0;
  const Speed& s = LADDER[static_cast<size_t>(speed_)];
  if (on) {
    computer_.setTrace(tracedRun(s));
    // Real frames drive the frame-locked rates and MAX. The trace speed
    // is for watching. So the latch applies to paced runs only.
    computer_.setFastFrame(fastFrameLatch_ && !s.micro && s.fps == 0 && s.ips > 0 && s.ips <= FAST_FRAME_MAX_IPS);
  } else {
    // Back to stepping: the machine records again, and the latch applies
    // whatever the last run speed. Stepping is watching.
    computer_.setTrace(true);
    computer_.setFastFrame(fastFrameLatch_);
  }
}

// One host frame of running, the worker's four starters folded into the
// frame loop. Each branch stops the run when the machine stopped. A
// breakpoint stops it too.
void Ide::pace() {
  const Speed& s = LADDER[static_cast<size_t>(speed_)];
  const double dt = static_cast<double>(GetFrameTime());
  auto stopped = [&](bool exhausted) {
    if (!exhausted && !computer_.hitBreakpoint) return false;
    setRunning(false);
    if (computer_.hitBreakpoint) note("stopped at breakpoint, PC " + hex(computer_.machine().pc, 4));
    return true;
  };
  if (s.micro) {
    // One row per tick, so every row is drawn, never skipped.
    tick_ += dt;
    const double period = 1.0 / s.ips;
    if (tick_ < period) return;
    tick_ = 0.0;
    const uint64_t done = computer_.runMicroBudget(1);
    stopped(done < 1);
  } else if (s.fps > 0) {
    // Owed frames come from real elapsed time times the target rate,
    // capped at about a second of catch-up.
    owed_ += dt * s.fps;
    if (owed_ > s.fps) owed_ = s.fps;
    while (owed_ >= 1.0) {
      const uint64_t done = computer_.runToNextFrame();
      owed_ -= 1.0;
      if (stopped(computer_.machine().status != Status::Running)) return;
      if (done == 0) break;  // nothing advanced: avoid a busy spin
    }
  } else if (s.ips <= 0) {
    // MAX: no counting at all, a wall clock budget inside the host frame.
    const double until = GetTime() + 0.012;
    constexpr uint64_t CHUNK = 20000;
    while (GetTime() < until) {
      const uint64_t done = computer_.runBudget(CHUNK);
      if (stopped(done < CHUNK)) return;
    }
  } else {
    owed_ += dt * s.ips;
    if (owed_ > s.ips) owed_ = s.ips;
    const auto n = static_cast<uint64_t>(std::floor(owed_));
    if (n == 0) return;
    owed_ -= static_cast<double>(n);
    const uint64_t done = computer_.runBudget(n);
    stopped(done < n);
  }
}

void Ide::stepInstruction() {
  setRunning(false);
  computer_.machine().instructionStep();
}

void Ide::stepMicro() {
  setRunning(false);
  if (computer_.microcodeInspectable()) computer_.machine().microStep();
  else note("the optimal set is sealed: it runs whole instructions only");
}

void Ide::runOneFrame() {
  setRunning(false);
  computer_.runToNextFrame();
  if (computer_.hitBreakpoint) note("stopped at breakpoint, PC " + hex(computer_.machine().pc, 4));
}

// Power on reloads the RAM image and starts from the first slot, the
// cold start. Reset restarts the CPU and keeps RAM, the warm one. Power
// off stops everything and blanks the screen until the next power on.
void Ide::powerOn() {
  setRunning(false);
  poweredOff_ = false;
  computer_.powerOn();
  applyLock();
  pushBreakpoints();
  // A restarted interpreter starts with an empty program; the document
  // is the partner that brings it back, not the other way round.
  syncDoc_.clear();
}

void Ide::reset() {
  setRunning(false);
  poweredOff_ = false;
  computer_.reset();
  pushBreakpoints();
  syncDoc_.clear();
}

void Ide::powerOff() {
  setRunning(false);
  poweredOff_ = true;
}

// The machine's state at the right of the menu bar, and the power
// controls under it: one place for what is otherwise a button in a pane.
void Ide::statusMenu() {
  const Machine& m = computer_.machine();
  std::string label;
  ImVec4 color(1.0f, 1.0f, 1.0f, 1.0f);
  if (poweredOff_) {
    label = "CPU: off";
    color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
  } else if (m.status == Status::Crashed) {
    label = "CPU: crashed, " + (m.crash ? m.crash->message : std::string("no message"));
    color = ImVec4(1.0f, 0.4f, 0.35f, 1.0f);
  } else if (m.status == Status::Halted) {
    label = "CPU: halted";
    color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
  } else if (running_) {
    label = "CPU: running";
    color = ImVec4(0.35f, 0.85f, 0.4f, 1.0f);
  } else {
    label = "CPU: paused";
  }
  const float width = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 4.0f;
  ImGui::SameLine(ImGui::GetWindowWidth() - width - 8.0f);
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  const bool open = ImGui::BeginMenu(label.c_str());
  ImGui::PopStyleColor();
  if (!open) return;
  const bool live = !poweredOff_ && m.status == Status::Running;
  if (ImGui::MenuItem(running_ ? "Pause" : "Run", "F5", false, live)) setRunning(!running_);
  if (ImGui::MenuItem("Step instruction", "F10", false, live)) stepInstruction();
  ImGui::Separator();
  if (ImGui::MenuItem("Reset", nullptr, false, !poweredOff_)) reset();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("restart the CPU at the first slot, RAM as it is");
  if (ImGui::MenuItem("Reboot", "Shift+F5")) powerOn();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("power on again: the RAM image reloaded, everything fresh");
  if (poweredOff_) {
    if (ImGui::MenuItem("Power on")) powerOn();
  } else {
    if (ImGui::MenuItem("Power off")) powerOff();
  }
  ImGui::EndMenu();
}

void Ide::selectMicrocode(const std::string& name) {
  setRunning(false);
  mcErrors_ = computer_.selectMicrocode(name);
  if (!mcErrors_.empty()) {
    note("microcode: " + std::to_string(mcErrors_.size()) + " error(s), the set was not applied");
    return;
  }
  // A locked set follows the choice: the lock is on what runs now.
  if (lockedMicrocode_) lockedMicrocode_ = name;
  pushBreakpoints();
  selectedSection_ = 0;
  // The trace speed needs readable rows, which the sealed set hides.
  if (!computer_.microcodeInspectable() && LADDER[static_cast<size_t>(speed_)].micro) speed_ = 1;
}

// After every insert: a locked set replaces whatever the cartridge
// brought, so the machine keeps running the person's own microcode.
void Ide::applyLock() {
  if (!lockedMicrocode_) return;
  if (computer_.microcodeName() == *lockedMicrocode_) return;
  mcErrors_ = computer_.selectMicrocode(*lockedMicrocode_);
  if (!mcErrors_.empty()) note("the locked microcode set no longer parses; the cartridge's own set runs");
}

void Ide::toggleBreakpoint(int instr) {
  const auto pc = static_cast<uint16_t>(instr);
  if (!breakpoints_.erase(pc)) breakpoints_.insert(pc);
  pushBreakpoints();
}

// The computer holds a new machine after every power on. The set lives
// here and is pushed after each one.
void Ide::pushBreakpoints() {
  computer_.setBreakpoints(std::vector<uint16_t>(breakpoints_.begin(), breakpoints_.end()));
}

// ---- the host frame

void Ide::enterScreenOnly() {
  if (screenOnly_) return;
  screenOnly_ = true;
  // A window that fills the monitor goes back to a plain window first,
  // so the machine's full screen and the way back start from one state.
  filledBeforeScreen_ = windowFilled();
  nativeBeforeScreen_ = nativeFullscreen();
  // In the system's full screen the window already covers the display:
  // the picture alone is drawn over it, and nothing moves.
  if (nativeBeforeScreen_) {
    if (!running_ && !poweredOff_) setRunning(true);
    return;
  }
  if (filledBeforeScreen_) toggleWindowFill();
  windowedW_ = GetScreenWidth();
  windowedH_ = GetScreenHeight();
  const Vector2 at = GetWindowPosition();
  windowedX_ = static_cast<int>(at.x);
  windowedY_ = static_cast<int>(at.y);
  const int m = GetCurrentMonitor();
  SetWindowSize(GetMonitorWidth(m), GetMonitorHeight(m));
  ToggleFullscreen();
  // Nothing can be pressed on the full screen but the machine's keys, so
  // a paused machine starts: the picture is what the person came for.
  if (!running_ && !poweredOff_) setRunning(true);
}

void Ide::leaveScreenOnly() {
  if (!screenOnly_) return;
  screenOnly_ = false;
  if (nativeBeforeScreen_) {
    ImGui::GetIO().ClearInputKeys();
    return;
  }
  if (IsWindowFullscreen()) ToggleFullscreen();
  SetWindowSize(windowedW_, windowedH_);
  SetWindowPosition(windowedX_, windowedY_);
  if (filledBeforeScreen_) toggleWindowFill();
  // Dear ImGui saw no frames while the screen was full, so it still holds
  // the keys as they were when it went: F12 down, which would read as held
  // and repeat straight back into full screen.
  ImGui::GetIO().ClearInputKeys();
}

void Ide::drawScreenOnly() {
  ClearBackground(BLACK);
  if (poweredOff_) return;
  const int w = GetScreenWidth();
  const int h = GetScreenHeight();
  const int side = std::min(w, h);
  screen_.setOutputDensity(GetWindowScaleDPI().x);
  screen_.upload(computer_.frame());
  screen_.draw((w - side) / 2, (h - side) / 2, side, side, display);
}

void Ide::update() {
  trackWindow();
  // Escape leaves the full screen before the keyboard is read, so the
  // machine never sees it.
  if (screenOnly_ && IsKeyPressed(KEY_ESCAPE)) leaveScreenOnly();
  // The keyboard belongs to the machine only while the screen pane has it,
  // or the screen fills the display, and the machine runs. Otherwise the
  // editor owns the keys and the machine sees everything released.
  if ((screenHasKeys_ || screenOnly_) && running_ && !poweredOff_) keyboard_.poll(computer_.input());
  else keyboard_.releaseAll(computer_.input());
  if (!poweredOff_) {
    typeIntoMachine();
    if (running_) pace();
    syncBasic();
  }
  // The chip renders on its own clock, so a tune plays on while the CPU
  // sits paused.
  computer_.pumpAudio(audio_);
  // The pane's texture is for the panes, which are not drawn full screen.
  if (screenOnly_) return;
  panes::PaneTarget& t = panes::screenTarget();
  BeginTextureMode(t.texture());
  ClearBackground(BLACK);
  screen_.setOutputDensity(1.0f);
  if (sprite_.previewOnScreen()) {
    // The sprite editor's preview plays on the Screen pane instead.
    sprite_.previewPixels(GetTime(), previewPixels_);
    screen_.upload(previewPixels_);
    screen_.draw(0, 0, t.side(), t.side(), display);
  } else if (!poweredOff_) {
    screen_.upload(computer_.frame());
    screen_.draw(0, 0, t.side(), t.side(), display);
  }
  EndTextureMode();
  spritePreview();
}

// Each level owns a dockspace with a fixed id. imgui.ini keeps three
// layouts, so a rearrangement inside one level survives a switch. The
// hidden levels are submitted with KeepAliveOnly. That keeps their
// windows docked while they are not drawn.
void Ide::frame() {
  // The number in the id is the layout's version. A new pane bumps it, so
  // an imgui.ini from before the pane rebuilds the level once rather than
  // leaving the newcomer floating.
  static const ImGuiID IDS[LEVEL_COUNT] = {ImHashStr("sc8-level-basic-1"), ImHashStr("sc8-level-project-4"),
                                           ImHashStr("sc8-level-run-2"), ImHashStr("sc8-level-cpu-4")};
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const int active = static_cast<int>(level_);
  for (int i = 0; i < LEVEL_COUNT; i++) {
    if (i != active) ImGui::DockSpaceOverViewport(IDS[i], vp, ImGuiDockNodeFlags_KeepAliveOnly);
  }
  const ImGuiID dockspace = ImGui::DockSpaceOverViewport(IDS[active], vp, ImGuiDockNodeFlags_PassthruCentralNode);
  if (!layoutBuilt_[active]) {
    layoutBuilt_[active] = true;
    buildLayout(level_, dockspace);
    // Which tab is in front is decided by focus, and every window of a
    // fresh layout asks for it. The one a person wants first wins here.
    if (focusAfterLayout_.empty())
      focusAfterLayout_ = level_ == Level::Basic   ? "Editor##basic"
                        : level_ == Level::Project ? "Editor"
                        : level_ == Level::Run     ? "Memory##run"
                                                   : "Microcode";
  } else if (!focusAfterLayout_.empty()) {
    ImGui::SetWindowFocus(focusAfterLayout_.c_str());
    focusAfterLayout_.clear();
  }
  menuBar();
  shortcuts();
  screenHasKeys_ = false;
  switch (level_) {
    case Level::Basic:
      editorPane("Editor##basic");
      screenPane("Screen##basic");
      manualPane("Manual##basic");
      if (basicShowsFiles_) filesPane("Files##basic", &basicShowsFiles_);
      if (basicShowsMessages_) messagesPane("Messages##basic", &basicShowsMessages_);
      break;
    case Level::Project:
      filesPane("Files");
      editorPane("Editor");
      if (spriteVisible_) spritePane();
      assemblyPane();
      messagesPane("Messages");
      screenPane("Screen##project");
      manualPane("Manual##project");
      break;
    case Level::Run:
      screenPane("Screen##run");
      registersPane("Registers##run");
      memoryPane("Memory##run");
      stackPane("Stack##run");
      breakpointsPane();
      listingPane("Listing##run");
      messagesPane("Messages");
      manualPane("Manual##run");
      break;
    case Level::Cpu:
      cpuPane();
      microcodePane();
      listingPane("Listing##micro");
      messagesPane("Messages");
      sidePane();
      break;
  }
  // A folder dialog opened from Preferences is drawn inside that popup,
  // so the two modals stack. Drawn here as well, it would open at the top
  // level and close Preferences, and the two would close each other.
  if (!askSettings_) dialog_.draw();
  quitDialog();
  romSaveDialog();
  removeDialog();
  settingsDialog();
  newSpriteDialog();
  spriteSwitchDialog();
}

// The first run of a level has no saved layout, so the panes get one
// here. A saved layout wins on every later run: the node then has splits
// and is left alone.
void Ide::buildLayout(Level level, unsigned dockspace) {
  ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace);
  if (!node || !node->IsLeafNode()) return;
  ImGui::DockBuilderRemoveNode(dockspace);
  ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);
  ImGuiID left = dockspace, right, bottom, mid, midBottom, rightBottom, files;
  switch (level) {
    case Level::Basic:
      // The editor large on the left. The screen on the right with the
      // manual under it. The files and the messages, when the Level menu
      // shows them, open as tabs beside the editor and the manual.
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Right, 0.42f, &right, &left);
      ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.5f, &rightBottom, &right);
      ImGui::DockBuilderDockWindow("Files##basic", left);
      ImGui::DockBuilderDockWindow("Editor##basic", left);
      ImGui::DockBuilderDockWindow("Screen##basic", right);
      ImGui::DockBuilderDockWindow("Messages##basic", rightBottom);
      ImGui::DockBuilderDockWindow("Manual##basic", rightBottom);
      break;
    case Level::Project:
      // The files on the far left, the editor beside them with the
      // messages under it. The screen on the right, so a program's result
      // is seen without leaving the editor, and the manual under it.
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Right, 0.36f, &right, &left);
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Left, 0.26f, &files, &left);
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.22f, &bottom, &left);
      ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.5f, &rightBottom, &right);
      ImGui::DockBuilderDockWindow("Files", files);
      ImGui::DockBuilderDockWindow("Sprite", left);
      ImGui::DockBuilderDockWindow("Assembly", left);
      ImGui::DockBuilderDockWindow("Editor", left);
      ImGui::DockBuilderDockWindow("Messages", bottom);
      ImGui::DockBuilderDockWindow("Screen##project", right);
      ImGui::DockBuilderDockWindow("Manual##project", rightBottom);
      ImGui::DockBuilderDockWindow("Sprite preview", rightBottom);
      break;
    case Level::Run:
      // The screen large in the middle, the registers and the run
      // controls under it. The listing on the left, memory on the right.
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Right, 0.24f, &right, &left);
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Right, 0.60f, &mid, &left);
      ImGui::DockBuilderSplitNode(mid, ImGuiDir_Down, 0.26f, &midBottom, &mid);
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.30f, &bottom, &left);
      ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.30f, &rightBottom, &right);
      ImGui::DockBuilderDockWindow("Listing##run", left);
      ImGui::DockBuilderDockWindow("Breakpoints", bottom);
      ImGui::DockBuilderDockWindow("Screen##run", mid);
      ImGui::DockBuilderDockWindow("Registers##run", midBottom);
      // The last window docked into a node is the tab in front.
      ImGui::DockBuilderDockWindow("Manual##run", right);
      ImGui::DockBuilderDockWindow("Stack##run", right);
      ImGui::DockBuilderDockWindow("Memory##run", right);
      ImGui::DockBuilderDockWindow("Messages", rightBottom);
      break;
    case Level::Cpu:
      // The CPU view large in the middle, its sections stacked. The
      // listing on the left. On the right the microcode rows and the
      // messages on top, the side pane with the screen and the manual
      // under them.
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Right, 0.28f, &right, &left);
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Right, 0.62f, &mid, &left);
      ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.60f, &rightBottom, &right);
      ImGui::DockBuilderDockWindow("Listing##micro", left);
      ImGui::DockBuilderDockWindow("CPU", mid);
      ImGui::DockBuilderDockWindow("Messages", right);
      ImGui::DockBuilderDockWindow("Microcode", right);
      ImGui::DockBuilderDockWindow("Side##cpu", rightBottom);
      break;
  }
  ImGui::DockBuilderFinish(dockspace);
}

void Ide::menuBar() {
  if (!ImGui::BeginMainMenuBar()) return;
  const fs::path startIn = !settings_.projectsDir.empty() && fs::is_directory(settings_.projectsDir)
                               ? fs::path(settings_.projectsDir)
                           : projectDir_.empty() ? fs::current_path()
                                                 : fs::path(projectDir_).parent_path();
  if (ImGui::BeginMenu("File")) {
    if (ImGui::BeginMenu("New project")) {
      const std::pair<const char*, project::Kind> KINDS[] = {
          {"BASIC", project::Kind::Basic}, {"C", project::Kind::C},
          {"Assembly", project::Kind::Assembly}, {"Microcode", project::Kind::Microcode}};
      for (const auto& [label, kind] : KINDS) {
        if (ImGui::MenuItem(label)) {
          const project::Kind k = kind;
          dialog_.open(FileDialog::Mode::OpenFolder, std::string("New ") + label + " project: pick or make its folder",
                       startIn, {}, [this, k](const fs::path& p) { createProject(p.string(), k); });
        }
      }
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Open project folder...", "Ctrl+O")) {
      dialog_.open(FileDialog::Mode::OpenFolder, "Open a project folder", startIn, {},
                   [this](const fs::path& p) { openProject(p.string()); });
    }
    if (ImGui::MenuItem("Open ROM...")) {
      dialog_.open(FileDialog::Mode::OpenFile, "Open a ROM", startIn, {".rom"},
                   [this](const fs::path& p) { openRomAsProject(p.string()); });
    }
    if (ImGui::MenuItem("Open source file...")) {
      dialog_.open(FileDialog::Mode::OpenFile, "Open a source file, which opens its project", startIn,
                   {".c", ".h", ".asm", ".bas", ".txt"}, [this](const fs::path& p) { open(p.string()); });
    }
    ImGui::Separator();
    const std::string saveLabel = saveTarget();
    if (ImGui::MenuItem(saveLabel.c_str(), "Ctrl+S")) saveProject();
    if (ImGui::MenuItem("Remove file from project...", nullptr, false, activeDoc() != nullptr)) {
      askRemove(activeDoc()->name);
    }
    if (ImGui::MenuItem("Save project as...")) {
      dialog_.open(FileDialog::Mode::OpenFolder, "Save the project as: pick or make its folder", startIn, {},
                   [this](const fs::path& p) { saveProjectAs(p.string()); });
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Build", "F7")) buildProject(false);
    if (ImGui::MenuItem("Build and run", "Ctrl+F5")) buildProject(true);
    if (ImGui::MenuItem("Burn ROM as...", "F8")) {
      dialog_.open(FileDialog::Mode::SaveFile, "Burn the machine's ROM to a file",
                   romPath_.empty() ? startIn : fs::path(romPath_).parent_path(), {".rom"},
                   [this](const fs::path& p) { burnRom(p.string()); },
                   romPath_.empty() ? projectTitle_ + ".rom" : fs::path(romPath_).filename().string());
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Boot the BASIC ROM")) bootBasic();
    ImGui::Separator();
    if (ImGui::MenuItem("Quit", "Cmd+Q")) requestQuit();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Level")) {
    if (ImGui::MenuItem("BASIC", "F1", level_ == Level::Basic)) setLevel(Level::Basic);
    if (ImGui::MenuItem("Project", "F2", level_ == Level::Project)) setLevel(Level::Project);
    if (ImGui::MenuItem("Run", "F3", level_ == Level::Run)) setLevel(Level::Run);
    if (ImGui::MenuItem("CPU", "F4", level_ == Level::Cpu)) setLevel(Level::Cpu);
    if (level_ == Level::Project) {
      ImGui::Separator();
      if (ImGui::MenuItem("Sprite editor", nullptr, spriteVisible_)) {
        spriteVisible_ = !spriteVisible_;
        focusSprite_ = spriteVisible_ ? 6 : 0;
      }
    }
    if (level_ == Level::Basic) {
      ImGui::Separator();
      if (ImGui::MenuItem("Show files", nullptr, basicShowsFiles_)) basicShowsFiles_ = !basicShowsFiles_;
      if (ImGui::MenuItem("Show messages", nullptr, basicShowsMessages_)) basicShowsMessages_ = !basicShowsMessages_;
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Run")) {
    if (ImGui::MenuItem(running_ ? "Pause" : "Run", "F5")) setRunning(!running_);
    if (ImGui::MenuItem("Step instruction", "F10")) stepInstruction();
    if (ImGui::MenuItem("Step microcycle", "F11", false, computer_.microcodeInspectable())) stepMicro();
    if (ImGui::MenuItem("Run one frame", "F6")) runOneFrame();
    if (ImGui::MenuItem("Reset")) reset();
    if (ImGui::MenuItem("Reboot", "Shift+F5")) powerOn();
    ImGui::Separator();
    if (ImGui::BeginMenu("Speed")) {
      for (size_t i = 0; i < std::size(LADDER); i++) {
        const bool enabled = !LADDER[i].micro || computer_.microcodeInspectable();
        if (ImGui::MenuItem(LADDER[i].label, nullptr, speed_ == static_cast<int>(i), enabled)) {
          speed_ = static_cast<int>(i);
          if (running_) setRunning(true);
        }
      }
      ImGui::EndMenu();
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Microcode")) {
    for (const char* name : {"@naive", "@optimal"}) {
      if (ImGui::MenuItem(name, nullptr, computer_.microcodeName() == name)) selectMicrocode(name);
    }
    const bool own = computer_.microcodeName() != "@naive" && computer_.microcodeName() != "@optimal";
    ImGui::MenuItem("your own set", nullptr, own, false);
    ImGui::Separator();
    if (ImGui::MenuItem("Lock the running set", nullptr, lockedMicrocode_.has_value())) {
      if (lockedMicrocode_) {
        lockedMicrocode_.reset();
        note("microcode unlocked: the next build or ROM brings its own set");
      } else {
        lockedMicrocode_ = computer_.microcodeName();
        note("microcode locked: every project and ROM runs on the set in the machine now");
      }
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Settings")) {
    if (ImGui::MenuItem("Preferences...")) {
      editing_ = settings_;
      askSettings_ = true;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Save layout")) {
      const fs::path f = Settings::layoutFile();
      if (f.empty()) {
        note("no settings folder is known on this machine, so the layout cannot be saved");
      } else {
        ImGui::SaveIniSettingsToDisk(f.string().c_str());
        note("layout saved to " + f.string());
      }
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("the panes as they are now, for every level, and the window's place and size, kept for "
                        "the next start");
    }
    if (ImGui::MenuItem("Reset layout")) {
      std::error_code ec;
      fs::remove(Settings::layoutFile(), ec);
      ImGui::ClearIniSettings();
      resetSections();
      for (bool& b : layoutBuilt_) b = false;
      note("layout reset to the built in one");
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Display")) {
    bool touched = ImGui::MenuItem("Display effect on", nullptr, &display.enabled);
    ImGui::SeparatorText("Effect");
    for (int i = 0; i < EFFECT_COUNT; i++) {
      const auto e = static_cast<Effect>(i);
      if (ImGui::MenuItem(effectName(e), nullptr, display.effect == e)) {
        display.usePreset(e);
        display.enabled = true;
        touched = true;
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", effectAbout(e));
    }
    ImGui::SeparatorText("Strengths");
    bool any = false;
    for (int k = 0; k < KNOB_COUNT; k++) {
      const auto knob = static_cast<Knob>(k);
      if (!effectUses(display.effect, knob)) continue;
      any = true;
      touched |= ImGui::SliderFloat(knobName(knob), &display.knob(knob), 0.0f, 1.0f);
    }
    if (!any) ImGui::TextDisabled("this effect has none");
    if (ImGui::MenuItem("Back to the effect's preset")) {
      display.usePreset(display.effect);
      touched = true;
    }
    ImGui::Separator();
    touched |= ImGui::MenuItem("Integer scale", nullptr, &display.integerScale);
    if (touched) saveDisplay();
    ImGui::Separator();
    if (ImGui::MenuItem("Full screen (Esc returns)", "F12")) enterScreenOnly();
    if (ImGui::MenuItem("IDE fills the monitor", nullptr, windowFilled())) toggleWindowFill();
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("the IDE window without a border over the whole monitor; Save layout keeps it");
    }
    ImGui::EndMenu();
  }
  if (lockedMicrocode_) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "  microcode locked");
  }
  statusMenu();
  ImGui::EndMainMenuBar();
}

void Ide::shortcuts() {
  if (dialog_.isOpen() || askQuit_) return;
  const ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsKeyPressed(ImGuiKey_F1)) setLevel(Level::Basic);
  if (ImGui::IsKeyPressed(ImGuiKey_F2)) setLevel(Level::Project);
  if (ImGui::IsKeyPressed(ImGuiKey_F3)) setLevel(Level::Run);
  if (ImGui::IsKeyPressed(ImGuiKey_F4)) setLevel(Level::Cpu);
  if (ImGui::IsKeyPressed(ImGuiKey_F7)) buildProject(false);
  if (ImGui::IsKeyPressed(ImGuiKey_F12)) enterScreenOnly();
  if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
    if (io.KeyShift) powerOn();
    else if (io.KeyCtrl) buildProject(true);
    else setRunning(!running_);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F6)) runOneFrame();
  if (ImGui::IsKeyPressed(ImGuiKey_F10)) stepInstruction();
  if (ImGui::IsKeyPressed(ImGuiKey_F11)) stepMicro();
  const bool cmd = io.KeyCtrl || io.KeySuper;
  if (cmd && ImGui::IsKeyPressed(ImGuiKey_S)) saveProject();
  if (cmd && ImGui::IsKeyPressed(ImGuiKey_O)) {
    dialog_.open(FileDialog::Mode::OpenFolder, "Open a project folder", fs::current_path(), {},
                 [this](const fs::path& p) { openProject(p.string()); });
  }
}

// The close gesture with unsaved documents: save them all and quit, quit
// as things are, or stay. A project with no folder cannot save without
// a folder, so its save opens the folder dialog first.
void Ide::quitDialog() {
  if (!askQuit_) return;
  ImGui::OpenPopup("Unsaved changes");
  if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("These documents have unsaved changes:");
    for (const Doc& d : docs_) {
      if (d.dirty) ImGui::BulletText("%s", d.name.c_str());
    }
    if (filesChanged_) {
      ImGui::BulletText("files added to or removed from the project");
    }
    if (sprite_.dirty()) ImGui::BulletText("the sprite %s", sprite_.name().c_str());
    ImGui::Spacing();
    if (ImGui::Button("Save and quit")) {
      if (projectDir_.empty() && !romPath_.empty()) {
        askQuit_ = false;
        ImGui::CloseCurrentPopup();
        if (romSaveConfirmed_) {
          done_ = saveIntoRom();
          if (!done_) note("the ROM could not be saved, so the IDE stays open");
        } else {
          askRomSave_ = true;
          romSaveQuits_ = true;
        }
      } else if (projectDir_.empty()) {
        askQuit_ = false;
        ImGui::CloseCurrentPopup();
        dialog_.open(FileDialog::Mode::OpenFolder, "Save the project: pick or make its folder", fs::current_path(), {},
                     [this](const fs::path& p) {
                       saveProjectAs(p.string());
                       done_ = !anyDirty();
                     });
      } else {
        saveAll();
        done_ = !anyDirty();
        if (!done_) note("a document could not be saved, so the IDE stays open");
        askQuit_ = false;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Quit without saving")) {
      done_ = true;
      askQuit_ = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      askQuit_ = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void Ide::removeDialog() {
  if (removing_.empty()) return;
  ImGui::OpenPopup("Remove a file");
  if (!ImGui::BeginPopupModal("Remove a file", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  if (!projectDir_.empty()) {
    const fs::path file = (removingAsset_ ? layout().assets : layout().sources) / removing_;
    ImGui::Text("Remove %s from the project?", removing_.c_str());
    ImGui::Text("This deletes the file from the disk, and it cannot be undone:");
    ImGui::TextDisabled("%s", file.string().c_str());
  } else {
    ImGui::Text("Remove %s from the project?", removing_.c_str());
    ImGui::TextDisabled(romPath_.empty() ? "It is not saved anywhere yet, so its text is gone."
                                         : "The ROM file keeps it until you save the project.");
  }
  ImGui::Spacing();
  if (ImGui::Button(projectDir_.empty() ? "Remove" : "Delete the file")) {
    const std::string name = removing_;
    removing_.clear();
    ImGui::CloseCurrentPopup();
    if (removingAsset_) removeAsset(name);
    else removeDoc(name);
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    removing_.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void Ide::romSaveDialog() {
  if (!askRomSave_) return;
  ImGui::OpenPopup("Save into the ROM?");
  if (!ImGui::BeginPopupModal("Save into the ROM?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  const std::string rom = fs::path(romPath_).filename().string();
  ImGui::Text("This project is not a folder. It lives in the ROM file %s.", rom.c_str());
  ImGui::Text("Saving builds the documents and burns them into that file, replacing what it holds.");
  ImGui::TextDisabled("%s", romPath_.c_str());
  ImGui::Spacing();
  ImGui::Checkbox("Don't ask again until the IDE closes", &romSaveConfirmed_);
  ImGui::Spacing();
  auto close = [this]() {
    askRomSave_ = false;
    ImGui::CloseCurrentPopup();
  };
  const bool quits = romSaveQuits_;
  if (ImGui::Button(("Burn into " + rom).c_str())) {
    close();
    romSaveQuits_ = false;
    const bool saved = saveIntoRom();
    if (quits) {
      done_ = saved;
      if (!saved) note("the ROM could not be saved, so the IDE stays open");
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Save as a project folder...")) {
    close();
    romSaveQuits_ = false;
    romSaveConfirmed_ = false;
    const fs::path startIn = !settings_.projectsDir.empty() && fs::is_directory(settings_.projectsDir)
                                 ? fs::path(settings_.projectsDir)
                                 : fs::current_path();
    dialog_.open(FileDialog::Mode::OpenFolder, "Save the project as: pick or make its folder", startIn, {},
                 [this, quits](const fs::path& p) {
                   saveProjectAs(p.string());
                   if (quits) done_ = !anyDirty();
                 });
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    close();
    romSaveQuits_ = false;
    romSaveConfirmed_ = false;
  }
  ImGui::EndPopup();
}

// The style is scaled from a copy taken at startup, so a second apply
// never compounds the first. FontScaleMain rasterizes the fonts at the
// new size, which is what keeps them sharp.
void Ide::applyScale() {
  static const ImGuiStyle base = ImGui::GetStyle();
  ImGuiStyle& style = ImGui::GetStyle();
  const float scale = std::clamp(settings_.uiScale, 0.5f, 4.0f);
  style = base;
  style.ScaleAllSizes(scale);
  style.FontScaleMain = scale;
}

void Ide::settingsDialog() {
  if (!askSettings_) return;
  ImGui::OpenPopup("Preferences");
  ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal("Preferences", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  ImGui::TextDisabled("kept in %s", Settings::dir().empty() ? "(no settings folder on this machine)"
                                                             : Settings::dir().string().c_str());
  ImGui::Spacing();
  ImGui::TextUnformatted("Projects folder");
  ImGui::SetNextItemWidth(400.0f);
  panes::inputLine("##projects", editing_.projectsDir, "where New project and Open project start");
  ImGui::SameLine();
  if (ImGui::SmallButton("Browse...")) {
    dialog_.open(FileDialog::Mode::OpenFolder, "The folder your projects live in",
                 editing_.projectsDir.empty() ? fs::current_path() : fs::path(editing_.projectsDir), {},
                 [this](const fs::path& p) { editing_.projectsDir = p.string(); });
  }
  ImGui::Spacing();
  ImGui::TextUnformatted("UI size");
  ImGui::SetNextItemWidth(400.0f);
  if (ImGui::SliderFloat("##scale", &editing_.uiScale, 0.75f, 2.5f, "%.2f x")) {
    // Applied as the slider moves, so the size is seen; Cancel puts it back.
    settings_.uiScale = editing_.uiScale;
    applyScale();
  }
  ImGui::Spacing();
  ImGui::Checkbox("Lay out assembly in columns on save", &editing_.formatAssembly);
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("mnemonics in one column, operands in the next, long labels on a line of their own");
  }
  ImGui::Spacing();
  if (ImGui::Button("Save", ImVec2(90, 0))) {
    settings_ = editing_;
    applyScale();
    if (!settings_.save()) note("the settings could not be written");
    else note("settings saved to " + Settings::settingsFile().string());
    askSettings_ = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(90, 0))) {
    settings_.load();
    applyScale();
    askSettings_ = false;
    ImGui::CloseCurrentPopup();
  }
  // The folder dialog is drawn above this one while it is open.
  dialog_.draw();
  ImGui::EndPopup();
}

void Ide::messagesPane(const char* name, bool* open) {
  ImGui::Begin(name, open);
  for (const std::string& msg : messages_) ImGui::TextWrapped("%s", msg.c_str());
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
  ImGui::End();
}

}  // namespace sc8
