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
#include "core/cartridge.h"
#include "core/mcparse.h"
#include "ide/panes.h"

namespace fs = std::filesystem;

namespace sc8 {

namespace {

const char* DEFAULT_SOURCE = R"(; SimpleCPU-8: a countdown, then a halt.
        LD A <- 5
loop:   SUB A <- 1
        JZ done
        JMP loop
done:   LD [result] <- A
        HLT
.ram
result: db 0xEE
)";

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

Ide::Ide() : source_(DEFAULT_SOURCE) {
  source_.reserve(1 << 16);
  basicText_.reserve(1 << 14);
  customText_.reserve(1 << 15);
  audio_.start();
  assembleSource();
}

void Ide::open(const std::string& path) {
  if (fs::path(path).extension() == ".rom") {
    loadRomFile(path);
    return;
  }
  if (fs::path(path).extension() == ".bas") {
    std::ifstream in(path);
    if (!in) {
      note("cannot read " + path);
      return;
    }
    basicText_.assign(std::istreambuf_iterator<char>(in), {});
    basicPath_ = path;
    return;
  }
  std::ifstream in(path);
  if (!in) {
    note("cannot read " + path);
    return;
  }
  source_.assign(std::istreambuf_iterator<char>(in), {});
  source_.reserve(source_.size() + (1 << 16));
  sourcePath_ = path;
  assembleSource();
}

void Ide::setLevel(Level level) { level_ = level; }

bool Ide::setSpeed(const std::string& value) {
  for (size_t i = 0; i < std::size(LADDER); i++) {
    if (value == LADDER[i].value) {
      speed_ = static_cast<int>(i);
      return true;
    }
  }
  return false;
}

void Ide::loadRomFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    note("cannot read " + path);
    return;
  }
  std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(in), {});
  CartridgeResult r = decodeCartridge(bytes);
  if (!r.cartridge) {
    note(path + ": " + r.error);
    return;
  }
  romPath_ = path;
  loadCartridge(std::move(*r.cartridge), path);
}

// A cartridge from a file has no source, so the listing shows a
// disassembly. The slot list mirrors the ROM's BAS chunk for the BASIC
// pane. The machine's own cartridge is left alone once inserted.
void Ide::loadCartridge(Cartridge cart, const std::string& what) {
  setRunning(false);
  basicSlots_ = cart.basic;
  basicDirty_ = false;
  haveSource_ = false;
  computer_.insert(std::move(cart));
  pushBreakpoints();
  rebuildListing();
  note("loaded " + what + ", microcode " + computer_.microcodeName());
}

void Ide::assembleSource() {
  Assets assets;
  const fs::path dir = sourcePath_.empty() ? fs::current_path() : fs::path(sourcePath_).parent_path();
  assets.loadFile = [&](std::string_view name) -> std::optional<std::vector<uint8_t>> {
    std::ifstream f(dir / fs::path(name), std::ios::binary);
    if (!f) return std::nullopt;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
  };
  // A conversion that lost something is worth a line in the messages
  // pane. The browser's asset card carried the same note.
  std::vector<std::string> notes;
  assets.loadImage = [&](std::string_view name) {
    std::string n;
    auto img = loadImageFile(dir / fs::path(name), &n);
    if (!n.empty()) notes.push_back(std::string(name) + ": " + n);
    return img;
  };
  assets.loadSample = [&](std::string_view name) {
    std::string n;
    auto pcm = loadSampleFile(dir / fs::path(name), &n);
    if (!n.empty()) notes.push_back(std::string(name) + ": " + n);
    return pcm;
  };
  assembled_ = assemble(source_, &assets);
  assembledOk_ = assembled_.errors.empty();
  messages_ = std::move(notes);
  for (const AsmError& e : assembled_.errors) note("line " + std::to_string(e.line) + ": " + e.message);
  haveSource_ = true;
  if (assembledOk_) {
    setRunning(false);
    Cartridge c = assembled_.cartridge();
    c.microcode = computer_.microcodeName();
    c.basic = basicSlots_;
    computer_.insert(std::move(c));
    pushBreakpoints();
    note("assembled: " + std::to_string(assembled_.program.size()) + " instructions, " +
         std::to_string(assembled_.ramLength) + " bytes of .ram, " + std::to_string(assembled_.cart.size()) +
         " bytes of .data");
  }
  rebuildListing();
}

void Ide::burnRom() {
  if (!assembledOk_ || !haveSource_) {
    note("fix the assembly errors before burning");
    return;
  }
  fs::path out = sourcePath_.empty() ? fs::path("out.rom") : fs::path(sourcePath_).replace_extension(".rom");
  Cartridge c = assembled_.cartridge();
  c.microcode = computer_.microcodeName();
  c.basic = basicSlots_;
  std::vector<uint8_t> bytes = encodeCartridge(c);
  std::ofstream o(out, std::ios::binary);
  o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (o) {
    basicDirty_ = false;
    note("burned " + out.string() + " (" + std::to_string(bytes.size()) + " bytes)");
  } else {
    note("cannot write " + out.string());
  }
}

void Ide::saveSource() {
  if (sourcePath_.empty()) sourcePath_ = "main.asm";
  std::ofstream o(sourcePath_);
  o << source_;
  note(o ? "saved " + sourcePath_ : "cannot write " + sourcePath_);
}

// The listing model: one line per source line, or one per instruction
// when the ROM came without source. A .ram line that starts with a label
// of RAM kind gets the label. The pane offers it for the watch list.
void Ide::rebuildListing() {
  listing_.clear();
  if (!haveSource_) {
    const auto& prog = computer_.cartridge().program;
    for (size_t i = 0; i < prog.size(); i++) {
      listing_.push_back({disassemble(prog[i]), static_cast<int>(i), std::nullopt, ""});
    }
    return;
  }
  size_t pos = 0;
  int lineNo = 0;
  while (pos <= source_.size()) {
    const size_t nl = source_.find('\n', pos);
    const std::string text = source_.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = nl == std::string::npos ? source_.size() + 1 : nl + 1;
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
  pushBreakpoints();
  selectedSection_ = 0;
  // The trace speed needs readable rows, which the sealed set hides.
  if (!computer_.microcodeInspectable() && LADDER[static_cast<size_t>(speed_)].micro) speed_ = 1;
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
  static const ImGuiID IDS[3] = {ImHashStr("sc8-level-edit"), ImHashStr("sc8-level-run"),
                                 ImHashStr("sc8-level-microcode")};
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
    case Level::Edit:
      sourcePane();
      basicPane();
      messagesPane();
      manualPane();
      break;
    case Level::Run:
      screenPane();
      registersPane("Registers##run");
      memoryPane();
      breakpointsPane();
      listingPane("Listing##run");
      messagesPane();
      break;
    case Level::Microcode:
      datapathPane();
      flowPane();
      microcodePane();
      listingPane("Listing##micro");
      registersPane("Registers##micro");
      messagesPane();
      break;
  }
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
  ImGuiID left = dockspace, right, bottom, mid, midBottom, rightBottom;
  switch (level) {
    case Level::Edit:
      // The editor on the left with the messages under it. The manual on
      // the right, where a reader keeps it open.
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Right, 0.38f, &right, &left);
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.22f, &bottom, &left);
      ImGui::DockBuilderDockWindow("Source", left);
      ImGui::DockBuilderDockWindow("BASIC", left);
      ImGui::DockBuilderDockWindow("Messages", bottom);
      ImGui::DockBuilderDockWindow("Manual", right);
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
      ImGui::DockBuilderDockWindow("Messages", rightBottom);
      break;
    case Level::Microcode:
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
      ImGui::DockBuilderDockWindow("Messages", rightBottom);
      break;
  }
  ImGui::DockBuilderFinish(dockspace);
}

void Ide::menuBar() {
  if (!ImGui::BeginMainMenuBar()) return;
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("Assemble", "F7")) assembleSource();
    if (ImGui::MenuItem("Save source", "Ctrl+S")) saveSource();
    if (ImGui::MenuItem("Burn ROM", "F8")) burnRom();
    ImGui::Separator();
    if (ImGui::MenuItem("Boot the BASIC ROM")) {
      CartridgeResult r = decodeCartridge(std::vector<uint8_t>(basicRom().begin(), basicRom().end()));
      if (r.cartridge) {
        romPath_.clear();
        loadCartridge(std::move(*r.cartridge), "the BASIC ROM");
      } else {
        note("BASIC ROM: " + r.error);
      }
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Level")) {
    if (ImGui::MenuItem("Edit", "F1", level_ == Level::Edit)) setLevel(Level::Edit);
    if (ImGui::MenuItem("Run", "F2", level_ == Level::Run)) setLevel(Level::Run);
    if (ImGui::MenuItem("Microcode", "F3", level_ == Level::Microcode)) setLevel(Level::Microcode);
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
    ImGui::Separator();
    for (const char* name : {"@naive", "@optimal"}) {
      if (ImGui::MenuItem(name, nullptr, computer_.microcodeName() == name)) selectMicrocode(name);
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
  ImGui::EndMainMenuBar();
}

void Ide::shortcuts() {
  if (ImGui::IsKeyPressed(ImGuiKey_F1)) setLevel(Level::Edit);
  if (ImGui::IsKeyPressed(ImGuiKey_F2)) setLevel(Level::Run);
  if (ImGui::IsKeyPressed(ImGuiKey_F3)) setLevel(Level::Microcode);
  if (ImGui::IsKeyPressed(ImGuiKey_F7)) assembleSource();
  if (ImGui::IsKeyPressed(ImGuiKey_F8)) burnRom();
  if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
    if (ImGui::GetIO().KeyShift) powerOn();
    else setRunning(!running_);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F6)) runOneFrame();
  if (ImGui::IsKeyPressed(ImGuiKey_F10)) stepInstruction();
  if (ImGui::IsKeyPressed(ImGuiKey_F11)) stepMicro();
  if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) saveSource();
}

void Ide::messagesPane() {
  ImGui::Begin("Messages");
  for (const std::string& msg : messages_) ImGui::TextWrapped("%s", msg.c_str());
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
  ImGui::End();
}

}  // namespace sc8
