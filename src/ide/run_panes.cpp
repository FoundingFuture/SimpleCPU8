// The Run level's panes. The screen, the registers with the run controls,
// memory with the watch list, the breakpoints and the listing. The
// Microcode level shows the listing and the registers too.
#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"

#include "ide/ide.h"
#include "ide/panes.h"

namespace sc8 {

using panes::hex;

namespace {

const ImVec4 ACCENT(0.35f, 0.85f, 0.4f, 1.0f);
const ImVec4 STOP(1.0f, 0.35f, 0.3f, 1.0f);
const ImVec4 LINK(0.55f, 0.75f, 1.0f, 1.0f);

// The four flags in the classic order, grey when clear, green when set.
void flagLetters(const Flags& f) {
  const std::pair<char, bool> letters[] = {{'N', f.n}, {'V', f.v}, {'Z', f.z}, {'C', f.c}};
  for (const auto& [c, on] : letters) {
    ImGui::SameLine(0.0f, 4.0f);
    if (on) ImGui::TextColored(ACCENT, "%c", c);
    else ImGui::TextDisabled("%c", c);
  }
}

// The zero page byte an address holds, or -1 off the end.
int wordAt(const Machine& m, int addr) {
  if (addr < 0 || addr + 1 >= RAM_SIZE) return -1;
  return (m.ram[static_cast<size_t>(addr)] << 8) | m.ram[static_cast<size_t>(addr + 1)];
}

}  // namespace

// ---- screen

void Ide::screenPane(const char* name) {
  ImGui::Begin(name);
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  // One line under the picture stays for the status.
  const float side = std::max(64.0f, std::min(avail.x, avail.y - ImGui::GetTextLineHeightWithSpacing()));
  // Center the square in the pane.
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - side) * 0.5f));
  // A render texture is stored upside down, so the source rectangle flips it.
  const Rectangle src{0, 0, static_cast<float>(panes::PANE_SIDE), -static_cast<float>(panes::PANE_SIDE)};
  rlImGuiImageRect(&panes::target().texture, static_cast<int>(side), static_cast<int>(side), src);
  // The machine gets the keyboard while the pane is focused or hovered.
  // Clicking the picture focuses the pane, so a click is how a player
  // takes the keys from the editor.
  screenHasKeys_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
                   ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
  if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ImGui::SetWindowFocus();
  const Machine& m = computer_.machine();
  if (m.status != Status::Running) {
    ImGui::TextColored(STOP, "%s%s", std::string(statusName(m.status)).c_str(),
                       m.crash ? (": " + m.crash->message).c_str() : "");
  } else if (screenHasKeys_ && running_) {
    ImGui::TextDisabled("the keyboard goes to the machine");
  } else {
    ImGui::TextDisabled("click the screen to give it the keyboard");
  }
  ImGui::End();
}

// ---- registers and the run controls

void Ide::runControls() {
  if (ImGui::Button(running_ ? "Pause" : "Run")) setRunning(!running_);
  ImGui::SameLine();
  if (ImGui::Button("Step")) stepInstruction();
  ImGui::SameLine();
  if (computer_.microcodeInspectable()) {
    if (ImGui::Button("Microstep")) stepMicro();
    ImGui::SameLine();
  }
  // Single click runs to the next frame tick. Double click latches the
  // fast-frame mode and lights the button.
  if (fastFrameLatch_) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.55f, 0.15f, 1.0f));
  const bool frameClicked = ImGui::Button("Frame");
  if (fastFrameLatch_) ImGui::PopStyleColor();
  if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
    fastFrameLatch_ = !fastFrameLatch_;
    setRunning(running_);
  } else if (frameClicked) {
    runOneFrame();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("run to the next frame tick. Double click latches fast-frame: a GPU_FRAME read may then push the "
                      "frame counter forward, so a poll loop falls through instead of spinning.%s",
                      fastFrameLatch_ ? "\nLatched." : "");
  }
  ImGui::SameLine();
  if (ImGui::Button("Power on")) powerOn();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(150);
  const auto ladder = speedLadder();
  if (ImGui::BeginCombo("##speed", ladder[static_cast<size_t>(speed_)].label)) {
    for (size_t i = 0; i < ladder.size(); i++) {
      const bool enabled = !ladder[i].micro || computer_.microcodeInspectable();
      if (!enabled) ImGui::BeginDisabled();
      if (ImGui::Selectable(ladder[i].label, speed_ == static_cast<int>(i))) {
        speed_ = static_cast<int>(i);
        if (running_) setRunning(true);
      }
      if (!enabled) ImGui::EndDisabled();
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("how fast the machine runs. The slow speeds are for watching.");
}

void Ide::registersPane(const char* name) {
  ImGui::Begin(name);
  registersBody();
  ImGui::End();
}

void Ide::registersBody() {
  runControls();
  ImGui::Separator();
  const Machine& m = computer_.machine();
  const std::string status(statusName(m.status));
  if (m.status == Status::Running) ImGui::TextColored(running_ ? ACCENT : ImVec4(1, 1, 1, 1), "%s", running_ ? "running" : "paused");
  else ImGui::TextColored(STOP, "%s", status.c_str());
  if (m.crash) ImGui::TextWrapped("%s", m.crash->message.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("microcode %s", computer_.microcodeInspectable() && computer_.microcodeName() != "@naive"
                                          ? "custom"
                                          : computer_.microcodeName().c_str());
  ImGui::Text("PC %s   A %s (%3d)   D1 %s   D2 %s   SP %s", hex(m.pc, 4).c_str(), hex(m.acc, 2).c_str(), m.acc,
              hex(m.d1, 4).c_str(), hex(m.d2, 4).c_str(), hex(m.sp, 4).c_str());
  ImGui::Text("flags ");
  flagLetters(m.flags);
  ImGui::SameLine(0.0f, 16.0f);
  ImGui::Text("latches  A %s  B %s   IR %s %s", hex(m.aLatch, 2).c_str(), hex(m.bLatch, 2).c_str(), hex(m.irOp, 2).c_str(),
              hex(m.irOperand, 4).c_str());
  ImGui::Text("cycles %llu   instructions %llu   frame %llu", static_cast<unsigned long long>(m.cycles),
              static_cast<unsigned long long>(m.instructions), static_cast<unsigned long long>(computer_.frameCounter()));
  if (m.lastBus) {
    static const char* KINDS[] = {"fetch", "ram read", "ram write", "stack read", "stack write", "io read", "io write"};
    ImGui::Text("bus  %s  %s <- %s", KINDS[static_cast<int>(m.lastBus->kind)], hex(m.lastBus->addr, 4).c_str(),
                hex(m.lastBus->data, 2).c_str());
  }
}

// ---- memory

// A dump of a byte array: sixteen bytes a row with their characters, the
// whole array behind a clipper, so 64K scrolls as fast as 256. hot marks
// the one address the last bus event touched, sp the stack pointer.
static void hexDump(const char* id, const uint8_t* bytes, int size, int& jumpTo, std::optional<int> hot,
                    std::optional<int> sp) {
  ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_Borders);
  const int rows = size / 16;
  if (jumpTo >= 0) {
    ImGui::SetScrollY(static_cast<float>(jumpTo / 16) * ImGui::GetTextLineHeightWithSpacing());
    jumpTo = -1;
  }
  ImGuiListClipper clipper;
  clipper.Begin(rows, ImGui::GetTextLineHeightWithSpacing());
  while (clipper.Step()) {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
      const int base = row * 16;
      ImGui::Text("%s:", hex(static_cast<unsigned>(base), 4).c_str());
      std::string chars;
      for (int col = 0; col < 16; col++) {
        const int addr = base + col;
        const uint8_t v = bytes[addr];
        ImGui::SameLine(0.0f, col == 8 ? 9.0f : 4.0f);
        const std::string s = hex(v, 2);
        if (hot && *hot == addr) ImGui::TextColored(ACCENT, "%s", s.c_str());
        else if (sp && *sp == addr) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", s.c_str());
        else ImGui::TextUnformatted(s.c_str());
        chars += (v >= 32 && v < 127) ? static_cast<char>(v) : '.';
      }
      ImGui::SameLine(0.0f, 12.0f);
      ImGui::TextDisabled("%s", chars.c_str());
    }
  }
  ImGui::EndChild();
}

// The whole 64K of data RAM, with the watch list above it. The address
// field jumps, the buttons jump to the places a reader wants most, and
// following the bus keeps the last access in view while stepping.
void Ide::memoryPane(const char* name) {
  ImGui::Begin(name);
  memoryBody();
  ImGui::End();
}

void Ide::memoryBody() {
  const Machine& m = computer_.machine();
  static char addrBuf[8] = "0000";
  ImGui::SetNextItemWidth(70);
  if (ImGui::InputText("go to", addrBuf, sizeof addrBuf,
                       ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
    memAddr_ = static_cast<int>(std::strtol(addrBuf, nullptr, 16)) & 0xfff0;
    memJump_ = memAddr_;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("zero page")) memJump_ = memAddr_ = 0;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("the first 256 bytes, the ones a one byte address reaches");
  ImGui::SameLine();
  if (ImGui::SmallButton("D1")) memJump_ = memAddr_ = m.d1 & 0xfff0;
  ImGui::SameLine();
  if (ImGui::SmallButton("D2")) memJump_ = memAddr_ = m.d2 & 0xfff0;
  ImGui::SameLine();
  if (ImGui::SmallButton("text screen")) memJump_ = memAddr_ = 0xFAC0;
  ImGui::SameLine();
  ImGui::Checkbox("follow the bus", &memFollow_);
  std::optional<int> hot;
  if (m.lastBus && (m.lastBus->kind == BusEvent::Kind::RamRead || m.lastBus->kind == BusEvent::Kind::RamWrite)) {
    hot = static_cast<int>(m.lastBus->addr);
    if (memFollow_ && !running_ && (*hot < memAddr_ || *hot >= memAddr_ + 256)) memJump_ = memAddr_ = *hot & 0xfff0;
  }

  if (!watched_.empty()) {
    ImGui::Separator();
    ImGui::TextDisabled("watch");
    std::string remove;
    for (const std::string& w : watched_) {
      auto it = assembled_.labels.find(w);
      if (it == assembled_.labels.end()) continue;
      const int addr = it->second.value;
      const int byte = m.ram[static_cast<size_t>(addr & 0xffff)];
      const int word = wordAt(m, addr);
      ImGui::PushID(w.c_str());
      if (ImGui::SmallButton("x")) remove = w;
      ImGui::SameLine();
      ImGui::Text("%-12s $%s  byte %s (%3d)  word %s", w.c_str(), hex(static_cast<unsigned>(addr), 4).c_str(),
                  hex(static_cast<unsigned>(byte), 2).c_str(), byte, word < 0 ? "----" : hex(static_cast<unsigned>(word), 4).c_str());
      ImGui::PopID();
    }
    if (!remove.empty()) watched_.erase(std::find(watched_.begin(), watched_.end(), remove));
  }
  ImGui::Separator();
  hexDump("hex", m.ram.data(), RAM_SIZE, memJump_, hot, std::nullopt);
}

// The stack: 4096 bytes, SP pointing at the next free cell and growing
// down, so the live part is at the bottom. Following SP keeps it in view.
void Ide::stackPane(const char* name) {
  ImGui::Begin(name);
  stackBody();
  ImGui::End();
}

void Ide::stackBody() {
  const Machine& m = computer_.machine();
  ImGui::Text("SP %s   %d bytes in use of %d", hex(m.sp, 4).c_str(), STACK_TOP - m.sp, STACK_SIZE);
  ImGui::SameLine();
  ImGui::Checkbox("follow SP", &stackFollow_);
  ImGui::SameLine();
  if (ImGui::SmallButton("top")) stackJump_ = STACK_SIZE - 256;
  std::optional<int> hot;
  if (m.lastBus && (m.lastBus->kind == BusEvent::Kind::StackRead || m.lastBus->kind == BusEvent::Kind::StackWrite)) {
    hot = static_cast<int>(m.lastBus->addr);
  }
  if (stackFollow_ && !running_) {
    const int want = std::max(0, (static_cast<int>(m.sp) & 0xfff0) - 64);
    if (want != stackShown_) stackJump_ = stackShown_ = want;
  }
  hexDump("stack", m.stack.data(), STACK_SIZE, stackJump_, hot, static_cast<int>(m.sp));
}

void Ide::breakpointsPane() {
  ImGui::Begin("Breakpoints");
  if (breakpoints_.empty()) {
    ImGui::TextDisabled("none. Click a line's margin in the listing to set one.");
  } else {
    if (ImGui::SmallButton("clear all")) {
      breakpoints_.clear();
      pushBreakpoints();
    }
    std::optional<uint16_t> remove;
    for (uint16_t pc : breakpoints_) {
      ImGui::PushID(pc);
      if (ImGui::SmallButton("x")) remove = pc;
      ImGui::SameLine();
      std::string text;
      for (const ListLine& l : listing_) {
        if (l.instr && *l.instr == pc) text = l.text;
      }
      ImGui::Text("%s  %s", hex(pc, 4).c_str(), text.c_str());
      ImGui::PopID();
    }
    if (remove) {
      breakpoints_.erase(*remove);
      pushBreakpoints();
    }
  }
  ImGui::End();
}

// ---- listing

// The assembled program, read-only, with the PC line highlighted. The
// gutter toggles a breakpoint, and a RAM label at the start of a line
// toggles its watch. One line at the top names the microcode set and
// nothing else of it.
void Ide::listingPane(const char* name) {
  ImGui::Begin(name);
  const Machine& m = computer_.machine();
  ImGui::TextDisabled("microcode: %s", computer_.microcodeInspectable() && computer_.microcodeName() != "@naive"
                                           ? "your own set"
                                           : computer_.microcodeName().c_str());
  ImGui::SameLine(0.0f, 20.0f);
  ImGui::Checkbox("follow PC", &followPc_);
  ImGui::Separator();
  ImGui::BeginChild("lines");
  const float lineH = ImGui::GetTextLineHeightWithSpacing();
  const int pcLine = static_cast<int>(m.pc);
  for (size_t i = 0; i < listing_.size(); i++) {
    const ListLine& line = listing_[i];
    ImGui::PushID(static_cast<int>(i));
    const bool isPc = line.instr && *line.instr == pcLine;
    const bool hasBp = line.instr && breakpoints_.count(static_cast<uint16_t>(*line.instr));
    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    if (isPc) {
      const ImU32 bg = computer_.hitBreakpoint ? IM_COL32(120, 40, 30, 255)
                       : m.status == Status::Crashed ? IM_COL32(120, 40, 30, 255)
                                                     : IM_COL32(30, 80, 45, 255);
      ImGui::GetWindowDrawList()->AddRectFilled(rowStart, ImVec2(rowStart.x + ImGui::GetContentRegionAvail().x, rowStart.y + lineH), bg);
      if (followPc_ && (running_ || shownPc_ != m.pc)) ImGui::SetScrollHereY(0.5f);
    }
    // The gutter: a dot for a breakpoint, a click toggles it.
    if (ImGui::InvisibleButton("bp", ImVec2(14.0f, lineH)) && line.instr) toggleBreakpoint(*line.instr);
    if (ImGui::IsItemHovered() && line.instr) ImGui::SetTooltip("breakpoint at $%s", hex(static_cast<unsigned>(*line.instr), 4).c_str());
    if (hasBp) {
      ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(rowStart.x + 7.0f, rowStart.y + lineH * 0.5f), 4.5f, IM_COL32(230, 70, 60, 255));
    } else if (ImGui::IsItemHovered() && line.instr) {
      ImGui::GetWindowDrawList()->AddCircle(ImVec2(rowStart.x + 7.0f, rowStart.y + lineH * 0.5f), 4.5f, IM_COL32(230, 70, 60, 160));
    }
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::TextDisabled("%3zu", i + 1);
    ImGui::SameLine(0.0f, 8.0f);
    // The address and the bytes. The instruction index is shown as the 16
    // bit value a JSR pushes. A .ram line shows the RAM address it defines.
    if (line.instr) {
      const auto& prog = computer_.cartridge().program;
      const auto idx = static_cast<size_t>(*line.instr);
      const std::string bytes = idx < prog.size() ? hex(prog[idx].op, 2) + " " + hex(prog[idx].operand, 4) : "";
      ImGui::TextDisabled("%s  %s", hex(static_cast<unsigned>(*line.instr), 4).c_str(), bytes.c_str());
    } else if (line.ramAddr) {
      ImGui::TextDisabled("$%s        ", hex(static_cast<unsigned>(*line.ramAddr), *line.ramAddr > 0xff ? 4 : 2).c_str());
    } else {
      ImGui::TextDisabled("             ");
    }
    ImGui::SameLine(0.0f, 10.0f);
    if (!line.label.empty()) {
      const bool watched = std::find(watched_.begin(), watched_.end(), line.label) != watched_.end();
      ImGui::TextColored(watched ? ACCENT : LINK, "%s", line.label.c_str());
      if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s: click to %s the watch list", line.label.c_str(), watched ? "leave" : "join");
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
          if (watched) watched_.erase(std::find(watched_.begin(), watched_.end(), line.label));
          else watched_.push_back(line.label);
        }
      }
      ImGui::SameLine(0.0f, 0.0f);
      ImGui::TextUnformatted(line.text.c_str() + line.label.size());
    } else {
      ImGui::TextUnformatted(line.text.empty() ? " " : line.text.c_str());
    }
    ImGui::PopID();
  }
  shownPc_ = m.pc;
  ImGui::EndChild();
  ImGui::End();
}

}  // namespace sc8
