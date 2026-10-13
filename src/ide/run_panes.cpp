// The panes that watch the machine run. The screen, the registers with
// the run controls, memory with the watch list, the breakpoints and the
// listing. The ASSEMBLY view shows them all, the other views some.
#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"

#include "basic/lines.h"
#include "cc/layout.h"
#include "core/mcparse.h"
#include "ide/highlight.h"
#include "ide/ide.h"
#include "ide/listing.h"
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
  screenBody(std::max(64.0f, std::min(avail.x, avail.y - ImGui::GetTextLineHeightWithSpacing())));
  ImGui::End();
}

void Ide::screenBody(float side) {
  const float width = ImGui::GetContentRegionAvail().x;
  // Center the square in the pane.
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (width - side) * 0.5f));
  panes::screenTarget().show(side);
  // The machine gets the keyboard while the pane is focused or hovered.
  // Clicking the picture focuses the pane, so a click is how a player
  // takes the keys from the editor.
  screenHasKeys_ = screenHasKeys_ || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
                   ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
  if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ImGui::SetWindowFocus();
  if (ImGui::SmallButton("Full screen")) enterScreenOnly();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("the screen alone, full size. Esc comes back. F12");
  ImGui::SameLine();
  const Machine& m = computer_.machine();
  if (m.status != Status::Running) {
    ImGui::TextColored(STOP, "%s%s", std::string(statusName(m.status)).c_str(),
                       m.crash ? (": " + m.crash->message).c_str() : "");
  } else if (screenHasKeys_ && running_) {
    ImGui::TextDisabled("the keyboard goes to the machine");
  } else {
    ImGui::TextDisabled("click the screen to give it the keyboard");
  }
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
  if (ImGui::BeginCombo("##speed", ladder[static_cast<size_t>(speedIndex())].label)) {
    for (size_t i = 0; i < ladder.size(); i++) {
      const bool enabled = !ladder[i].micro || computer_.microcodeInspectable();
      if (!enabled) ImGui::BeginDisabled();
      if (ImGui::Selectable(ladder[i].label, speedIndex() == static_cast<int>(i))) {
        pickSpeed(static_cast<int>(i));
      }
      if (!enabled) ImGui::EndDisabled();
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("how fast the machine runs. The slow speeds are for watching.");
}

void Ide::registersPane(const char* name) {
  ImGui::Begin(name);
  runControls();
  ImGui::Separator();
  registersBody();
  ImGui::End();
}

void Ide::registersBody() {
  const Machine& m = computer_.machine();
  const std::string status(statusName(m.status));
  if (m.status == Status::Running) ImGui::TextColored(running_ ? ACCENT : ImVec4(1, 1, 1, 1), "%s", running_ ? "running" : "paused");
  else ImGui::TextColored(STOP, "%s", status.c_str());
  if (m.crash) ImGui::TextWrapped("%s", m.crash->message.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("microcode %s", computer_.microcodeInspectable() && computer_.microcodeName() != "@naive"
                                          ? "custom"
                                          : computer_.microcodeName().c_str());
  ImGui::Text("PC %s   A %s (%3d)   D1 %s   D2 %s   D3 %s   SP %s", hex(m.pc, 4).c_str(), hex(m.acc, 2).c_str(),
              m.acc, hex(m.d1, 4).c_str(), hex(m.d2, 4).c_str(), hex(m.d3, 4).c_str(), hex(m.sp, 4).c_str());
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
  if (ImGui::SmallButton("D3")) memJump_ = memAddr_ = m.d3 & 0xfff0;
  ImGui::SameLine();
  // The screen the GPU reads, wherever the program mapped it: $F000 for
  // BASIC, $FAC0 by the C convention. Before a program maps one, $FAC0.
  if (ImGui::SmallButton("text screen")) {
    const Gpu& g = computer_.gpu();
    memJump_ = memAddr_ = g.textMapped ? g.textBase : cc::C_STACK_TOP;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("the character screen text mode reads, where the program mapped it");
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
  ImGui::Text("SP %s   %d bytes in use of %d", hex(m.sp, 4).c_str(), m.stackTop() - m.sp, m.stackSize());
  ImGui::SameLine();
  ImGui::Checkbox("follow SP", &stackFollow_);
  ImGui::SameLine();
  if (ImGui::SmallButton("top")) stackJump_ = m.stackSize() - 256;
  std::optional<int> hot;
  if (m.lastBus && (m.lastBus->kind == BusEvent::Kind::StackRead || m.lastBus->kind == BusEvent::Kind::StackWrite)) {
    hot = static_cast<int>(m.lastBus->addr);
  }
  if (stackFollow_ && !running_) {
    const int want = std::max(0, (static_cast<int>(m.sp) & 0xfff0) - 64);
    if (want != stackShown_) stackJump_ = stackShown_ = want;
  }
  hexDump("stack", m.stack.data(), m.stackSize(), stackJump_, hot, static_cast<int>(m.sp));
}

// Every breakpoint with its document and line: the BASIC lines, the C
// and assembly lines, the rows of microcode, then the listing's own by
// address. A click on one shows the document and the line in its view.
void Ide::breakpointsPane(const char* pane) {
  ImGui::Begin(pane);
  if (breakpoints_.empty() && sourceBreaks_.empty() && basicBreaks_.empty()) {
    ImGui::TextDisabled("none. Click in the column before a line in the editor, or in a line's margin in the "
                        "listing, to set one.");
    ImGui::End();
    return;
  }
  if (ImGui::SmallButton("clear all")) {
    breakpoints_.clear();
    sourceBreaks_.clear();
    basicBreaks_.clear();
    pushBreakpoints();
    rebuildRowBreaks();
  }
  // What a click asked for, done after the lists are walked.
  std::optional<std::pair<std::string, int>> removeLine, removeBasic, show;
  for (const auto& [name, lines] : basicBreaks_) {
    const Doc* doc = docNamed(name);
    const std::vector<std::string> rows = doc ? basic::splitLines(doc->text) : std::vector<std::string>{};
    for (const int number : lines) {
      int row = 0;
      for (size_t r = 0; r < rows.size(); r++) {
        if (basic::lineNumber(rows[r]) == number) row = static_cast<int>(r) + 1;
      }
      const std::string where = name + " line " + std::to_string(number);
      ImGui::PushID(where.c_str());
      if (ImGui::SmallButton("x")) removeBasic = {name, number};
      ImGui::SameLine();
      if (ImGui::Selectable(where.c_str())) show = {name, row};
      ImGui::PopID();
    }
  }
  for (const auto& [name, sb] : sourceBreaks_) {
    const bool microcode = docKindOf(name) == DocKind::Microcode;
    for (const auto& [line, pc] : sb.lines) {
      const std::string where = name + ":" + std::to_string(line);
      std::string text;
      if (microcode) {
        const std::optional<project::RowStop> row = project::rowAt(sb.text, line);
        text = where + (row ? "  " + row->section + ", row " + std::to_string(row->row + 1) : "");
      } else if (pc) {
        text = hex(*pc, 4) + "  " + where;
      } else {
        text = "      " + where + "  (placed by the next build)";
      }
      ImGui::PushID(where.c_str());
      if (ImGui::SmallButton("x")) removeLine = {name, line};
      ImGui::SameLine();
      if (ImGui::Selectable(text.c_str())) show = {name, line};
      ImGui::PopID();
    }
  }
  if (removeBasic) {
    std::set<int>& lines = basicBreaks_[removeBasic->first];
    lines.erase(removeBasic->second);
    if (lines.empty()) basicBreaks_.erase(removeBasic->first);
    refreshWatch();
  }
  if (removeLine) {
    auto doc = sourceBreaks_.find(removeLine->first);
    doc->second.lines.erase(removeLine->second);
    if (doc->second.lines.empty()) sourceBreaks_.erase(doc);
    pushBreakpoints();
    rebuildRowBreaks();
  }
  if (show) selectLine(show->first, show->second);
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
  ImGui::End();
}

// ---- listing

namespace {

// What a line of a listing starts with: the bar behind the line the
// machine stands on, then the gutter. A dot there is a breakpoint, and a
// ring under the mouse says a click sets one. True for a click on a line
// that takes a breakpoint. The gutter stays the last item, for a tooltip.
bool lineGutter(bool current, ImU32 bar, bool scroll, bool marked, bool takes) {
  const float lineH = ImGui::GetTextLineHeightWithSpacing();
  const ImVec2 rowStart = ImGui::GetCursorScreenPos();
  if (current) {
    ImGui::GetWindowDrawList()->AddRectFilled(
        rowStart, ImVec2(rowStart.x + ImGui::GetContentRegionAvail().x, rowStart.y + lineH), bar);
    if (scroll) ImGui::SetScrollHereY(0.5f);
  }
  const bool clicked = ImGui::InvisibleButton("bp", ImVec2(14.0f, lineH)) && takes;
  const ImVec2 dot(rowStart.x + 7.0f, rowStart.y + lineH * 0.5f);
  if (marked) ImGui::GetWindowDrawList()->AddCircleFilled(dot, 4.5f, IM_COL32(230, 70, 60, 255));
  else if (takes && ImGui::IsItemHovered()) ImGui::GetWindowDrawList()->AddCircle(dot, 4.5f, IM_COL32(230, 70, 60, 160));
  return clicked;
}

}  // namespace

// The Listing tab. In the MICROCODE view it holds one of four listings,
// and a row of choices under the buttons picks it.
void Ide::listingPane(const char* name) {
  ImGui::Begin(name);
  // Where the listing shares the editor's tab area it has the editor's
  // buttons, so a run or a step goes on from either tab.
  if (runTab(view_)) viewButtons(kindOfView(view_), nullptr);
  switch (view_ == View::Microcode ? chooseListing() : Listing::Assembly) {
    case Listing::Microcode: rowsListing(); break;
    case Listing::Assembly: assemblyListing(); break;
    case Listing::C: sourceListing(Listing::C); break;
    case Listing::Basic: sourceListing(Listing::Basic); break;
  }
  ImGui::End();
}

Listings Ide::listingsHere() const {
  Listings have;
  have.microcode = computer_.microcodeInspectable();
  for (const Doc& d : docs_) {
    const DocKind kind = docKindOf(d.name);
    have.c = have.c || kind == DocKind::C;
    have.basic = have.basic || kind == DocKind::Basic;
  }
  return have;
}

bool Ide::setListing(const std::string& key) {
  const std::optional<Listing> listing = listingNamed(key);
  if (listing) listingChoice_ = *listing;
  return listing.has_value();
}

// The row of choices, and the listing it leaves on screen. A choice the
// program has no listing for is grey and says why.
Listing Ide::chooseListing() {
  const Listings have = listingsHere();
  const Listing shown = listingShown(listingChoice_, have);
  for (int i = 0; i < LISTING_COUNT; i++) {
    const Listing l = static_cast<Listing>(i);
    const bool here = hasListing(have, l);
    if (i > 0) ImGui::SameLine();
    ImGui::BeginDisabled(!here);
    if (ImGui::RadioButton(listingLabel(l), l == shown)) listingChoice_ = l;
    ImGui::EndDisabled();
    if (!here && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", listingMissing(l));
  }
  return shown;
}

// The line every listing has over its lines. It names the microcode set
// and shows nothing else of it. Then comes the switch that keeps the
// machine's place in view.
void Ide::listingHead(const char* follow) {
  ImGui::TextDisabled("microcode: %s", computer_.microcodeInspectable() && computer_.microcodeName() != "@naive"
                                           ? "your own set"
                                           : computer_.microcodeName().c_str());
  ImGui::SameLine(0.0f, 20.0f);
  ImGui::Checkbox(follow, &followPc_);
}

// Red where the machine stopped at a breakpoint or crashed, else green.
unsigned Ide::standingBar() const {
  const bool stopped = computer_.hitBreakpoint || computer_.machine().status == Status::Crashed;
  return stopped ? IM_COL32(120, 40, 30, 255) : IM_COL32(30, 80, 45, 255);
}

// The assembled program, read-only, with the PC line highlighted. The
// gutter toggles a breakpoint, and a RAM label at the start of a line
// toggles its watch.
void Ide::assemblyListing() {
  const Machine& m = computer_.machine();
  listingHead("follow PC");
  ImGui::Separator();
  // Behind the editor's tab the lines are not laid out, so the scroll to
  // the PC waits for the frame that shows them.
  const bool shown = ImGui::BeginChild("lines");
  const int pcLine = static_cast<int>(m.pc);
  const ImU32 bar = standingBar();
  for (size_t i = 0; i < listing_.size(); i++) {
    const ListLine& line = listing_[i];
    ImGui::PushID(static_cast<int>(i));
    const bool isPc = line.instr && *line.instr == pcLine;
    const bool hasBp = line.instr && breaksAt(static_cast<uint16_t>(*line.instr));
    const bool scroll = followPc_ && (running_ || shownPc_ != m.pc);
    // The gutter: a dot for a breakpoint, a click toggles it.
    if (lineGutter(isPc, bar, scroll, hasBp, line.instr.has_value())) toggleBreakpoint(*line.instr);
    if (ImGui::IsItemHovered() && line.instr) ImGui::SetTooltip("breakpoint at $%s", hex(static_cast<unsigned>(*line.instr), 4).c_str());
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
      // The rest of the line after the label: its colon, then the code.
      std::string_view rest = std::string_view(line.text).substr(line.label.size());
      if (!rest.empty() && rest.front() == ':') {
        ImGui::TextUnformatted(":");
        ImGui::SameLine(0.0f, 0.0f);
        rest.remove_prefix(1);
      }
      codeLine(Syntax::Assembly, rest);
    } else {
      codeLine(Syntax::Assembly, line.text);
    }
    ImGui::PopID();
  }
  if (shown) shownPc_ = m.pc;
  ImGui::EndChild();
}

// The set the machine runs, written out as text: a line per section and
// per row. A custom set's name is its text, so the name says when the
// lines are stale.
const Ide::RowsText& Ide::rowsText() {
  const std::string& name = computer_.microcodeName();
  if (rowsText_.made && rowsText_.of == name) return rowsText_;
  rowsText_ = {};
  rowsText_.made = true;
  rowsText_.of = name;
  const std::string text = serializeMicrocode(computer_.machine().microcode());
  rowsText_.rows = project::rowLines(text);
  for (size_t at = 0; at < text.size();) {
    size_t nl = text.find('\n', at);
    if (nl == std::string::npos) nl = text.size();
    rowsText_.lines.push_back(text.substr(at, nl - at));
    at = nl + 1;
  }
  rowsText_.rowAt.assign(rowsText_.lines.size(), -1);
  for (size_t r = 0; r < rowsText_.rows.size(); r++) {
    rowsText_.rowAt[static_cast<size_t>(rowsText_.rows[r].line - 1)] = static_cast<int>(r);
  }
  return rowsText_;
}

// A click in the rows' gutter. A row breakpoint is a line of the
// project's microcode document, so the row is looked up there.
void Ide::toggleListedRow(const project::RowStop& row) {
  const std::string what = "row " + std::to_string(row.row + 1) + " of " + row.section;
  for (const Doc& d : docs_) {
    if (docKindOf(d.name) != DocKind::Microcode) continue;
    const std::optional<int> line = project::lineOfRow(project::rowLines(d.text), row.section, row.row);
    if (line) toggleRowBreak(d, *line);
    else note(d.name + " has no " + what + ", so no breakpoint");
    return;
  }
  note("no breakpoint on " + what + ": a row breakpoint is kept in the project's microcode set, and this project "
       "has none. The editor beside this tab starts one");
}

// The microcode set's rows. The row that ran last is highlighted, the
// one the Execution pane and the datapath show. A machine that ran
// without a record has no such row: a row breakpoint stops it that way.
// Then the row it stands before is outlined.
void Ide::rowsListing() {
  const Machine& m = computer_.machine();
  const RowsText& set = rowsText();
  listingHead("follow the row");
  auto lineOf = [&](const std::string& section, int row) {
    return project::lineOfRow(set.rows, section, row).value_or(0);
  };
  const int live = m.lastMicro ? lineOf(m.lastMicro->op, m.lastMicro->row) : 0;
  const std::optional<project::RowStop> next = m.lastMicro ? std::nullopt : project::rowBefore(m);
  const int before = next ? lineOf(next->section, next->row) : 0;
  if (!m.lastMicro) {
    ImGui::SameLine(0.0f, 20.0f);
    ImGui::TextDisabled(!m.trace() ? "rows are not recorded at this speed"
                        : before   ? "the machine stands before the outlined row"
                                   : "no row recorded yet: Step runs one");
  }
  ImGui::Separator();
  const ListingSpot spot{Listing::Microcode, "", live ? live : before};
  // The breakpoints, as lines of this text.
  std::vector<int> marks;
  for (const project::RowStop& r : rowMarks_) {
    if (const std::optional<int> line = project::lineOfRow(set.rows, r.section, r.row)) marks.push_back(*line);
  }
  const bool shown = ImGui::BeginChild("rows");
  const ImU32 bar = standingBar();
  const bool scroll = followPc_ && (running_ || !(shownSpot_ == spot));
  std::optional<project::RowStop> clicked;
  for (size_t i = 0; i < set.lines.size(); i++) {
    const int lineNo = static_cast<int>(i) + 1;
    const int row = set.rowAt[i];
    ImGui::PushID(lineNo);
    const bool marked = std::find(marks.begin(), marks.end(), lineNo) != marks.end();
    if (lineNo == before) {
      const ImVec2 at = ImGui::GetCursorScreenPos();
      const ImVec2 end(at.x + ImGui::GetContentRegionAvail().x, at.y + ImGui::GetTextLineHeightWithSpacing());
      ImGui::GetWindowDrawList()->AddRect(at, end, IM_COL32(60, 160, 90, 255));
      if (scroll) ImGui::SetScrollHereY(0.5f);
    }
    if (lineGutter(lineNo == live, bar, scroll, marked, row >= 0)) clicked = set.rows[static_cast<size_t>(row)];
    if (ImGui::IsItemHovered() && row >= 0) {
      const project::RowStop& r = set.rows[static_cast<size_t>(row)];
      ImGui::SetTooltip("breakpoint before row %d of %s", r.row + 1, r.section.c_str());
    }
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::TextDisabled("%4d", lineNo);
    ImGui::SameLine(0.0f, 8.0f);
    const std::string& text = set.lines[i];
    if (row >= 0) ImGui::TextUnformatted(text.c_str());
    else if (text.empty()) ImGui::TextUnformatted(" ");
    else ImGui::TextColored(LINK, "%s", text.c_str());
    ImGui::PopID();
  }
  if (shown) shownSpot_ = spot;
  ImGui::EndChild();
  if (clicked) toggleListedRow(*clicked);
}

// A C or a BASIC document as the program's source, read-only. The line
// the machine stands on is highlighted, while it runs too. The document
// is the one the machine stands in, and stays when the machine leaves
// the project's own code.
void Ide::sourceListing(Listing which) {
  const bool basic = which == Listing::Basic;
  const Machine& m = computer_.machine();
  std::vector<std::string> names;
  for (const Doc& d : docs_) {
    const DocKind kind = docKindOf(d.name);
    if (basic ? kind == DocKind::Basic : kind == DocKind::C || kind == DocKind::Header) names.push_back(d.name);
  }
  std::string standing;
  if (!poweredOff_ && m.status == Status::Running) {
    if (basic) standing = basicMidProgram() ? runningBasicDoc() : "";
    else if (const project::SourceLine* at = basicBooted_ ? nullptr : pcLines_.at(m.pc)) standing = at->file;
  }
  std::string& last = listingDoc_[basic ? 1 : 0];
  last = listingDoc(standing, last, names);
  const Doc* doc = docNamed(last);
  listingHead("follow the line");
  ImGui::SameLine(0.0f, 20.0f);
  ImGui::TextDisabled("%s", last.c_str());
  ImGui::Separator();
  if (!doc) return;
  const int current = standingLine(*doc);
  const ListingSpot spot{which, last, current};
  const std::set<int>* basicMarks = nullptr;
  const SourceBreaks* sourceMarks = nullptr;
  if (basic) {
    if (const auto it = basicBreaks_.find(doc->name); it != basicBreaks_.end()) basicMarks = &it->second;
  } else if (const auto it = sourceBreaks_.find(doc->name); it != sourceBreaks_.end()) {
    sourceMarks = &it->second;
  }
  const bool shown = ImGui::BeginChild("source");
  const ImU32 bar = standingBar();
  const bool scroll = followPc_ && (running_ || !(shownSpot_ == spot));
  const Syntax syntax = syntaxOf(doc->name);
  const std::string_view text = doc->text;
  int clicked = 0;
  int lineNo = 0;
  for (size_t at = 0; at < text.size() || lineNo == 0;) {
    size_t nl = text.find('\n', at);
    if (nl == std::string_view::npos) nl = text.size();
    const std::string_view line = text.substr(at, nl - at);
    at = nl + 1;
    lineNo++;
    ImGui::PushID(lineNo);
    // A BASIC breakpoint belongs to the number its line carries.
    const std::optional<int> number = basic ? basic::lineNumber(line) : std::nullopt;
    const bool marked = basic ? basicMarks && number && basicMarks->count(*number)
                              : sourceMarks && sourceMarks->lines.count(lineNo);
    if (lineGutter(lineNo == current, bar, scroll, marked, !basic || number.has_value())) clicked = lineNo;
    ImGui::SameLine(0.0f, 4.0f);
    // A BASIC line carries its own number, so the column has none.
    if (!basic) {
      ImGui::TextDisabled("%4d", lineNo);
      ImGui::SameLine(0.0f, 8.0f);
    }
    codeLine(syntax, line);
    ImGui::PopID();
  }
  if (shown) shownSpot_ = spot;
  ImGui::EndChild();
  if (clicked && basic) toggleBasicBreak(*doc, clicked);
  else if (clicked) toggleSourceBreak(*doc, clicked);
}

}  // namespace sc8
