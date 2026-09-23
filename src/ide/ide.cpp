#include "ide/ide.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "imgui.h"
#include "imgui_internal.h"
#include "raylib.h"
#include "rlImGui.h"

#include "core/cartridge.h"
#include "core/mcparse.h"

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

// One render target the screen pane shows through Dear ImGui. Drawing
// through the CRT shader has to happen in raylib's own pass, so the frame
// is rendered here first and the pane shows the result.
constexpr int PANE_SIDE = 768;

RenderTexture2D& target() {
  static RenderTexture2D rt = LoadRenderTexture(PANE_SIDE, PANE_SIDE);
  return rt;
}

int textCallback(ImGuiInputTextCallbackData* data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
    auto* s = static_cast<std::string*>(data->UserData);
    s->resize(static_cast<size_t>(data->BufTextLen));
    data->Buf = s->data();
  }
  return 0;
}

}  // namespace

Ide::Ide() : source_(DEFAULT_SOURCE) {
  source_.reserve(1 << 16);
  assembleSource();
}

void Ide::open(const std::string& path) {
  if (fs::path(path).extension() == ".rom") {
    loadRomFile(path);
    return;
  }
  std::ifstream in(path);
  if (!in) {
    messages_.push_back("cannot read " + path);
    return;
  }
  source_.assign(std::istreambuf_iterator<char>(in), {});
  source_.reserve(source_.size() + (1 << 16));
  sourcePath_ = path;
  assembleSource();
}

void Ide::loadRomFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    messages_.push_back("cannot read " + path);
    return;
  }
  std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(in), {});
  CartridgeResult r = decodeCartridge(bytes);
  if (!r.cartridge) {
    messages_.push_back(path + ": " + r.error);
    return;
  }
  romPath_ = path;
  computer_.insert(std::move(*r.cartridge));
  running_ = false;
  messages_.push_back("loaded " + path + ", microcode " + computer_.microcodeName());
}

void Ide::assembleSource() {
  Assets assets;
  const fs::path dir = sourcePath_.empty() ? fs::current_path() : fs::path(sourcePath_).parent_path();
  assets.loadFile = [&](std::string_view name) -> std::optional<std::vector<uint8_t>> {
    std::ifstream f(dir / fs::path(name), std::ios::binary);
    if (!f) return std::nullopt;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
  };
  assembled_ = assemble(source_, &assets);
  assembledOk_ = assembled_.errors.empty();
  messages_.clear();
  for (const AsmError& e : assembled_.errors) {
    messages_.push_back("line " + std::to_string(e.line) + ": " + e.message);
  }
  if (assembledOk_) {
    Cartridge c = assembled_.cartridge();
    c.microcode = computer_.microcodeName();
    computer_.insert(std::move(c));
    running_ = false;
    messages_.push_back("assembled: " + std::to_string(assembled_.program.size()) + " instructions, " +
                        std::to_string(assembled_.ramLength) + " bytes of .ram, " +
                        std::to_string(assembled_.cart.size()) + " bytes of .data");
  }
}

void Ide::burnRom() {
  if (!assembledOk_) {
    messages_.push_back("fix the assembly errors before burning");
    return;
  }
  fs::path out = sourcePath_.empty() ? fs::path("out.rom") : fs::path(sourcePath_).replace_extension(".rom");
  Cartridge c = assembled_.cartridge();
  c.microcode = computer_.microcodeName();
  std::vector<uint8_t> bytes = encodeCartridge(c);
  std::ofstream o(out, std::ios::binary);
  o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  messages_.push_back(o ? "burned " + out.string() + " (" + std::to_string(bytes.size()) + " bytes)"
                        : "cannot write " + out.string());
}

void Ide::update() {
  // Pace the machine like simplecpu does, then render the screen once.
  if (running_) {
    owed_ += static_cast<double>(GetFrameTime()) * fps_;
    int frames = 0;
    while (owed_ >= 1.0 && frames < 8) {
      if (!computer_.runFrame()) running_ = false;
      owed_ -= 1.0;
      frames++;
    }
    if (owed_ > 8.0) owed_ = 0.0;
  }
  screen_.upload(computer_.frame());
  BeginTextureMode(target());
  ClearBackground(BLACK);
  screen_.draw(0, 0, PANE_SIDE, PANE_SIDE, display);
  EndTextureMode();
}

// The first run has no imgui.ini, so the panes get a layout here: source
// on the left, the screen top right with the machine under it, the tools
// along the bottom. A saved layout wins on every later run.
void Ide::defaultLayout(unsigned dockspace) {
  ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace);
  if (!node || !node->IsLeafNode()) return;
  ImGui::DockBuilderRemoveNode(dockspace);
  ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);
  ImGuiID left, right, rightTop, rightBottom, bottom, bottomA, bottomB;
  ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Down, 0.28f, &bottom, &left);
  ImGui::DockBuilderSplitNode(left, ImGuiDir_Right, 0.42f, &right, &left);
  ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.38f, &rightBottom, &rightTop);
  ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.5f, &bottomB, &bottomA);
  ImGui::DockBuilderDockWindow("Source", left);
  ImGui::DockBuilderDockWindow("Screen", rightTop);
  ImGui::DockBuilderDockWindow("Machine", rightBottom);
  ImGui::DockBuilderDockWindow("Microcode", bottomA);
  ImGui::DockBuilderDockWindow("ROM", bottomB);
  ImGui::DockBuilderDockWindow("Messages", bottomB);
  ImGui::DockBuilderFinish(dockspace);
}

void Ide::frame() {
  const ImGuiID dockspace = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
  if (!layoutDone_) {
    layoutDone_ = true;
    defaultLayout(dockspace);
  }
  menuBar();
  sourcePane();
  machinePane();
  screenPane();
  microcodePane();
  romPane();
  messagesPane();
}

void Ide::menuBar() {
  if (!ImGui::BeginMainMenuBar()) return;
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("Assemble", "F7")) assembleSource();
    if (ImGui::MenuItem("Burn ROM", "F8")) burnRom();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Run")) {
    if (ImGui::MenuItem(running_ ? "Pause" : "Run", "F5")) running_ = !running_;
    if (ImGui::MenuItem("Step instruction", "F10")) computer_.machine().instructionStep();
    if (ImGui::MenuItem("Step microcycle", "F11")) computer_.machine().microStep();
    if (ImGui::MenuItem("Run one frame", "F6")) computer_.runFrame();
    if (ImGui::MenuItem("Power on", "Shift+F5")) {
      computer_.powerOn();
      running_ = false;
    }
    ImGui::Separator();
    for (const char* name : {"@naive", "@optimal"}) {
      if (ImGui::MenuItem(name, nullptr, computer_.microcodeName() == name)) {
        computer_.selectMicrocode(name);
        computer_.powerOn();
        running_ = false;
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
  ImGui::EndMainMenuBar();

  if (ImGui::IsKeyPressed(ImGuiKey_F7)) assembleSource();
  if (ImGui::IsKeyPressed(ImGuiKey_F8)) burnRom();
  if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
    if (ImGui::GetIO().KeyShift) {
      computer_.powerOn();
      running_ = false;
    } else {
      running_ = !running_;
    }
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F6)) computer_.runFrame();
  if (ImGui::IsKeyPressed(ImGuiKey_F10)) computer_.machine().instructionStep();
  if (ImGui::IsKeyPressed(ImGuiKey_F11)) computer_.machine().microStep();
}

void Ide::sourcePane() {
  ImGui::Begin("Source");
  ImGui::TextUnformatted(sourcePath_.empty() ? "(unsaved)" : sourcePath_.c_str());
  ImGui::SameLine();
  if (ImGui::SmallButton("Assemble")) assembleSource();
  ImGui::SameLine();
  if (ImGui::SmallButton("Burn ROM")) burnRom();
  const ImVec2 size(-1.0f, -1.0f);
  ImGui::InputTextMultiline("##source", source_.data(), source_.capacity() + 1, size,
                            ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput,
                            textCallback, &source_);
  ImGui::End();
}

void Ide::machinePane() {
  ImGui::Begin("Machine");
  const Machine& m = computer_.machine();
  ImGui::Text("status  %s", std::string(statusName(m.status)).c_str());
  if (m.crash) ImGui::TextWrapped("%s", m.crash->message.c_str());
  ImGui::Text("PC %04X   A %02X   D1 %04X   D2 %04X   SP %04X", m.pc, m.acc, m.d1, m.d2, m.sp);
  ImGui::Text("flags  %c %c %c %c", m.flags.n ? 'N' : 'n', m.flags.v ? 'V' : 'v', m.flags.z ? 'Z' : 'z',
              m.flags.c ? 'C' : 'c');
  ImGui::Text("latches  A %02X  B %02X   IR %02X %04X", m.aLatch, m.bLatch, m.irOp, m.irOperand);
  ImGui::Text("cycles %llu   instructions %llu   frame %llu", static_cast<unsigned long long>(m.cycles),
              static_cast<unsigned long long>(m.instructions), static_cast<unsigned long long>(computer_.frameCounter()));
  if (m.lastMicro && computer_.microcodeInspectable()) {
    ImGui::Text("micro  %s row %d: %s", m.lastMicro->op.c_str(), m.lastMicro->row,
                rowText(m.lastMicro->signals).c_str());
  } else if (m.lastMicro) {
    ImGui::TextDisabled("micro  %s (sealed)", m.lastMicro->op.c_str());
  }
  if (m.lastBus) {
    static const char* KINDS[] = {"fetch", "ram-read", "ram-write", "stack-read", "stack-write", "io-read", "io-write"};
    ImGui::Text("bus  %s %04X <- %02X", KINDS[static_cast<int>(m.lastBus->kind)], m.lastBus->addr, m.lastBus->data);
  }
  ImGui::Separator();
  if (ImGui::Button(running_ ? "Pause" : "Run")) running_ = !running_;
  ImGui::SameLine();
  if (ImGui::Button("Step")) computer_.machine().instructionStep();
  ImGui::SameLine();
  if (ImGui::Button("Microstep")) computer_.machine().microStep();
  ImGui::SameLine();
  if (ImGui::Button("Frame")) computer_.runFrame();
  ImGui::SameLine();
  if (ImGui::Button("Power on")) {
    computer_.powerOn();
    running_ = false;
  }
  ImGui::SliderInt("fps", &fps_, 1, 240);

  ImGui::Separator();
  ImGui::TextUnformatted("zero page");
  for (int row = 0; row < 16; row++) {
    std::string line;
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02X: ", row * 16);
    line += buf;
    for (int col = 0; col < 16; col++) {
      std::snprintf(buf, sizeof buf, "%02X ", m.ram[static_cast<size_t>(row * 16 + col)]);
      line += buf;
    }
    ImGui::TextUnformatted(line.c_str());
  }
  ImGui::End();
}

void Ide::screenPane() {
  ImGui::Begin("Screen");
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float side = std::max(64.0f, std::min(avail.x, avail.y));
  // A render texture is stored upside down, so the source rectangle flips it.
  const Rectangle src{0, 0, static_cast<float>(PANE_SIDE), -static_cast<float>(PANE_SIDE)};
  rlImGuiImageRect(&target().texture, static_cast<int>(side), static_cast<int>(side), src);
  ImGui::End();
}

void Ide::microcodePane() {
  ImGui::Begin("Microcode");
  ImGui::Text("set: %s", computer_.microcodeName().c_str());
  if (!computer_.microcodeInspectable()) {
    ImGui::TextWrapped("The optimal set is sealed. It runs and can be raced, never read. Select @naive or a "
                       "custom set to trace, step and edit rows.");
    ImGui::End();
    return;
  }
  const Machine& m = computer_.machine();
  const auto& sections = m.microcode().sections();
  const std::string current = m.lastMicro ? m.lastMicro->op : "";
  if (ImGui::BeginListBox("##sections", ImVec2(180, -1))) {
    for (size_t i = 0; i < sections.size(); i++) {
      const bool live = sections[i].name == current;
      if (ImGui::Selectable((sections[i].name + (live ? "  <" : "")).c_str(), selectedSection_ == static_cast<int>(i))) {
        selectedSection_ = static_cast<int>(i);
      }
    }
    ImGui::EndListBox();
  }
  ImGui::SameLine();
  ImGui::BeginGroup();
  if (selectedSection_ >= 0 && selectedSection_ < static_cast<int>(sections.size())) {
    const auto& s = sections[static_cast<size_t>(selectedSection_)];
    ImGui::Text("%s: %zu row%s", s.name.c_str(), s.rows.size(), s.rows.size() == 1 ? "" : "s");
    for (size_t r = 0; r < s.rows.size(); r++) {
      const bool live = m.lastMicro && m.lastMicro->op == s.name && m.lastMicro->row == static_cast<int>(r);
      if (live) ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%02zu  %s", r + 1, rowText(s.rows[r]).c_str());
      else ImGui::Text("%02zu  %s", r + 1, rowText(s.rows[r]).c_str());
    }
  }
  ImGui::EndGroup();
  ImGui::End();
}

void Ide::romPane() {
  ImGui::Begin("ROM");
  const Cartridge& c = computer_.cartridge();
  ImGui::TextUnformatted(romPath_.empty() ? "(from the source pane)" : romPath_.c_str());
  ImGui::Text("program %zu instructions", c.program.size());
  ImGui::Text("ram image %zu bytes", c.ram.size());
  ImGui::Text("data %zu bytes", c.data.size());
  ImGui::Text("microcode %s", c.microcode.empty() ? "@naive" : (c.microcode[0] == '@' ? c.microcode.c_str() : "custom"));
  for (const auto& [k, v] : c.meta) ImGui::Text("%s: %s", k.c_str(), v.c_str());
  if (!c.assets.empty() && ImGui::TreeNodeEx("assets", ImGuiTreeNodeFlags_DefaultOpen)) {
    for (const RomAsset& a : c.assets) ImGui::Text("%-8s %-20s at %6u  %u bytes", a.kind.c_str(), a.name.c_str(), a.offset, a.size);
    ImGui::TreePop();
  }
  if (!c.basic.empty() && ImGui::TreeNodeEx("BASIC programs", ImGuiTreeNodeFlags_DefaultOpen)) {
    for (const auto& [name, text] : c.basic) ImGui::Text("%-16s %zu bytes", name.c_str(), text.size());
    ImGui::TreePop();
  }
  ImGui::End();
}

void Ide::messagesPane() {
  ImGui::Begin("Messages");
  for (const std::string& msg : messages_) ImGui::TextWrapped("%s", msg.c_str());
  ImGui::End();
}

}  // namespace sc8
