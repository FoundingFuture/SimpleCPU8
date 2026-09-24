#include "ide/ide.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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
    case OperandKind::Addr16: replace("addr16", "$" + hex(in.operand, 4)); break;
    case OperandKind::Target: text += " $" + hex(in.operand, 4); break;
    case OperandKind::Port: text += " $" + hex(in.operand >> 8, 2); break;
    case OperandKind::PortImm: text += " $" + hex(in.operand >> 8, 2) + ", $" + hex(in.operand & 0xff, 2); break;
  }
  return text;
}

}  // namespace

std::span<const Speed> speedLadder() { return LADDER; }

Ide::Ide() {
  customText_.reserve(1 << 15);
  audio_.start();
  newScratchProject();
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
  note("cannot open " + path + ": a .rom, a folder, or a .c, .h, .asm, .bas or microcode.txt");
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
  // The manual follows the level too: the CPU level opens the microcode
  // guide, the Run level the assembly guide. Project keeps the document's.
  if (level == Level::Cpu) {
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

void Ide::powerOn() {
  setRunning(false);
  computer_.powerOn();
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

void Ide::update() {
  // The keyboard belongs to the machine only while the screen pane has it
  // and the machine runs. Otherwise the editor owns the keys and the
  // machine sees everything released.
  if (screenHasKeys_ && running_) keyboard_.poll(computer_.input());
  else keyboard_.releaseAll(computer_.input());
  typeIntoMachine();
  if (running_) pace();
  syncBasic();
  // The chip renders on its own clock, so a tune plays on while the CPU
  // sits paused.
  computer_.pumpAudio(audio_);
  screen_.upload(computer_.frame());
  BeginTextureMode(panes::target());
  ClearBackground(BLACK);
  screen_.draw(0, 0, panes::PANE_SIDE, panes::PANE_SIDE, display);
  EndTextureMode();
}

// Each level owns a dockspace with a fixed id. imgui.ini keeps three
// layouts, so a rearrangement inside one level survives a switch. The
// hidden levels are submitted with KeepAliveOnly. That keeps their
// windows docked while they are not drawn.
void Ide::frame() {
  // The number in the id is the layout's version. A new pane bumps it, so
  // an imgui.ini from before the pane rebuilds the level once rather than
  // leaving the newcomer floating.
  static const ImGuiID IDS[3] = {ImHashStr("sc8-level-project-3"), ImHashStr("sc8-level-run-1"),
                                 ImHashStr("sc8-level-cpu-1")};
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const int active = static_cast<int>(level_);
  for (int i = 0; i < 3; i++) {
    if (i != active) ImGui::DockSpaceOverViewport(IDS[i], vp, ImGuiDockNodeFlags_KeepAliveOnly);
  }
  const ImGuiID dockspace = ImGui::DockSpaceOverViewport(IDS[active], vp, ImGuiDockNodeFlags_PassthruCentralNode);
  if (!layoutBuilt_[active]) {
    layoutBuilt_[active] = true;
    buildLayout(level_, dockspace);
  }
  menuBar();
  shortcuts();
  screenHasKeys_ = false;
  switch (level_) {
    case Level::Project:
      filesPane();
      editorPane();
      assemblyPane();
      messagesPane();
      screenPane();
      manualPane();
      break;
    case Level::Run:
      screenPane();
      registersPane("Registers##run");
      memoryPane();
      breakpointsPane();
      listingPane("Listing##run");
      messagesPane();
      manualPane();
      break;
    case Level::Cpu:
      datapathPane();
      flowPane();
      microcodePane();
      listingPane("Listing##micro");
      registersPane("Registers##micro");
      messagesPane();
      manualPane();
      break;
  }
  dialog_.draw();
  quitDialog();
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
    case Level::Project:
      // The files on the far left, the editor beside them with the
      // messages under it. The screen on the right, so a program's result
      // is seen without leaving the editor, and the manual under it.
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Right, 0.36f, &right, &left);
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Left, 0.26f, &files, &left);
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.22f, &bottom, &left);
      ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.5f, &rightBottom, &right);
      ImGui::DockBuilderDockWindow("Files", files);
      ImGui::DockBuilderDockWindow("Editor", left);
      ImGui::DockBuilderDockWindow("Assembly", left);
      ImGui::DockBuilderDockWindow("Messages", bottom);
      ImGui::DockBuilderDockWindow("Screen", right);
      ImGui::DockBuilderDockWindow("Manual", rightBottom);
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
      ImGui::DockBuilderDockWindow("Screen", mid);
      ImGui::DockBuilderDockWindow("Registers##run", midBottom);
      ImGui::DockBuilderDockWindow("Memory", right);
      ImGui::DockBuilderDockWindow("Manual", right);
      ImGui::DockBuilderDockWindow("Messages", rightBottom);
      break;
    case Level::Cpu:
      // The datapath large with the flow under it. The listing and the
      // registers on the left, the rows on the right.
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Right, 0.28f, &right, &left);
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Right, 0.62f, &mid, &left);
      ImGui::DockBuilderSplitNode(mid, ImGuiDir_Down, 0.40f, &midBottom, &mid);
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.34f, &bottom, &left);
      ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.20f, &rightBottom, &right);
      ImGui::DockBuilderDockWindow("Listing##micro", left);
      ImGui::DockBuilderDockWindow("Registers##micro", bottom);
      ImGui::DockBuilderDockWindow("Datapath", mid);
      ImGui::DockBuilderDockWindow("Flow", midBottom);
      ImGui::DockBuilderDockWindow("Microcode", right);
      ImGui::DockBuilderDockWindow("Manual", right);
      ImGui::DockBuilderDockWindow("Messages", rightBottom);
      break;
  }
  ImGui::DockBuilderFinish(dockspace);
}

void Ide::menuBar() {
  if (!ImGui::BeginMainMenuBar()) return;
  const fs::path startIn = projectDir_.empty() ? fs::current_path() : fs::path(projectDir_).parent_path();
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
    if (ImGui::MenuItem("Save", "Ctrl+S", false, activeDoc() != nullptr)) {
      if (projectDir_.empty()) {
        dialog_.open(FileDialog::Mode::OpenFolder, "Save the project: pick or make its folder", startIn, {},
                     [this](const fs::path& p) { saveProjectAs(p.string()); });
      } else if (Doc* d = activeDoc()) {
        saveDoc(*d);
      }
    }
    if (ImGui::MenuItem("Save all", "Ctrl+Shift+S")) {
      if (projectDir_.empty()) {
        dialog_.open(FileDialog::Mode::OpenFolder, "Save the project: pick or make its folder", startIn, {},
                     [this](const fs::path& p) { saveProjectAs(p.string()); });
      } else {
        saveAll();
      }
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
    if (ImGui::MenuItem("Project", "F1", level_ == Level::Project)) setLevel(Level::Project);
    if (ImGui::MenuItem("Run", "F2", level_ == Level::Run)) setLevel(Level::Run);
    if (ImGui::MenuItem("CPU", "F3", level_ == Level::Cpu)) setLevel(Level::Cpu);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Run")) {
    if (ImGui::MenuItem(running_ ? "Pause" : "Run", "F5")) setRunning(!running_);
    if (ImGui::MenuItem("Step instruction", "F10")) stepInstruction();
    if (ImGui::MenuItem("Step microcycle", "F11", false, computer_.microcodeInspectable())) stepMicro();
    if (ImGui::MenuItem("Run one frame", "F6")) runOneFrame();
    if (ImGui::MenuItem("Power on", "Shift+F5")) powerOn();
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
  if (ImGui::BeginMenu("Display")) {
    ImGui::MenuItem("CRT look", nullptr, &display.enabled);
    ImGui::SliderFloat("Scanlines", &display.scanlines, 0.0f, 1.0f);
    ImGui::SliderFloat("Curvature", &display.curvature, 0.0f, 1.0f);
    ImGui::SliderFloat("Blur", &display.blur, 0.0f, 1.0f);
    ImGui::SliderFloat("Bloom", &display.bloom, 0.0f, 1.0f);
    ImGui::SliderFloat("Vignette", &display.vignette, 0.0f, 1.0f);
    ImGui::MenuItem("Integer scale", nullptr, &display.integerScale);
    ImGui::EndMenu();
  }
  if (lockedMicrocode_) {
    ImGui::SameLine(ImGui::GetWindowWidth() - 180.0f);
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "microcode locked");
  }
  ImGui::EndMainMenuBar();
}

void Ide::shortcuts() {
  if (dialog_.isOpen() || askQuit_) return;
  const ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsKeyPressed(ImGuiKey_F1)) setLevel(Level::Project);
  if (ImGui::IsKeyPressed(ImGuiKey_F2)) setLevel(Level::Run);
  if (ImGui::IsKeyPressed(ImGuiKey_F3)) setLevel(Level::Cpu);
  if (ImGui::IsKeyPressed(ImGuiKey_F7)) buildProject(false);
  if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
    if (io.KeyShift) powerOn();
    else if (io.KeyCtrl) buildProject(true);
    else setRunning(!running_);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F6)) runOneFrame();
  if (ImGui::IsKeyPressed(ImGuiKey_F10)) stepInstruction();
  if (ImGui::IsKeyPressed(ImGuiKey_F11)) stepMicro();
  const bool cmd = io.KeyCtrl || io.KeySuper;
  if (cmd && ImGui::IsKeyPressed(ImGuiKey_S)) {
    if (projectDir_.empty()) {
      dialog_.open(FileDialog::Mode::OpenFolder, "Save the project: pick or make its folder", fs::current_path(), {},
                   [this](const fs::path& p) { saveProjectAs(p.string()); });
    } else if (io.KeyShift) {
      saveAll();
    } else if (Doc* d = activeDoc()) {
      saveDoc(*d);
    }
  }
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
    ImGui::Spacing();
    if (ImGui::Button("Save all and quit")) {
      if (projectDir_.empty()) {
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

void Ide::messagesPane() {
  ImGui::Begin("Messages");
  for (const std::string& msg : messages_) ImGui::TextWrapped("%s", msg.c_str());
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
  ImGui::End();
}

}  // namespace sc8
