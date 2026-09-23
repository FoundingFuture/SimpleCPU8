// The Microcode level's panes: the datapath schema, the flow view and the
// rows with their editor. The seal is respected here: on @optimal the rows
// stay hidden and the lanes light from the bus events alone.
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>

#include "imgui.h"

#include "core/mcparse.h"
#include "ide/datapath.h"
#include "ide/ide.h"
#include "ide/panes.h"

namespace sc8 {

using panes::hex;

namespace {

const ImVec4 ACCENT(0.35f, 0.85f, 0.4f, 1.0f);
const ImVec4 DIM(0.5f, 0.5f, 0.5f, 1.0f);

const char* SEALED = "The optimal set is sealed. It runs and can be raced, never read. Select @naive or your own set to "
                     "trace, step and edit rows.";

// The boxes and wires the last bus event names. What the machine reports
// even under the sealed set, so the schema shows where the bytes went.
void busLights(const BusEvent& bus, std::vector<std::string_view>& boxes, std::vector<std::string_view>& wires) {
  switch (bus.kind) {
    case BusEvent::Kind::Fetch: boxes = {"PROG", "IR", "PC"}; wires = {"pc-prog", "prog-ir"}; break;
    case BusEvent::Kind::RamRead:
    case BusEvent::Kind::RamWrite: boxes = {"RAM", "EA"}; wires = {"ea-ram"}; break;
    case BusEvent::Kind::StackRead:
    case BusEvent::Kind::StackWrite: boxes = {"STACK", "SP"}; wires = {"sp-stack"}; break;
    case BusEvent::Kind::IoRead:
    case BusEvent::Kind::IoWrite: boxes = {"IO"}; wires = {}; break;
  }
}

// The live value each box shows. The EA adder is stateless: its value is
// the address it computed this cycle.
std::string boxValue(std::string_view id, const Machine& m) {
  if (id == "EA") return m.lastMicro && m.lastMicro->ea ? hex(*m.lastMicro->ea, 4) : "----";
  if (id == "IR") return hex(m.irOp, 2) + " " + hex(m.irOperand, 4);
  if (id == "PC") return hex(m.pc, 4);
  if (id == "D1") return hex(m.d1, 4);
  if (id == "D2") return hex(m.d2, 4);
  if (id == "SP") return hex(m.sp, 4);
  if (id == "A") return hex(m.aLatch, 2);
  if (id == "B") return hex(m.bLatch, 2);
  if (id == "ACC") return hex(m.acc, 2);
  return "";
}

}  // namespace

// ---- datapath

void Ide::datapathPane() {
  ImGui::Begin("Datapath");
  const Machine& m = computer_.machine();
  const bool inspectable = computer_.microcodeInspectable();

  // Colors by row position: the first signal touching a box colors it,
  // the rest become dots on its top edge. Wires take the first signal's
  // color that drives them. The bus event lights in a neutral tone under
  // whatever the signals say.
  std::map<std::string_view, std::vector<ImU32>> boxColors;
  std::map<std::string_view, ImU32> wireColor;
  const ImU32 busColor = IM_COL32(200, 200, 210, 255);
  if (m.lastBus) {
    std::vector<std::string_view> boxes, wires;
    busLights(*m.lastBus, boxes, wires);
    for (std::string_view b : boxes) boxColors[b].push_back(busColor);
    for (std::string_view w : wires) wireColor[w] = busColor;
  }
  if (m.lastMicro && inspectable) {
    const Row& row = m.lastMicro->signals;
    for (size_t i = 0; i < row.size(); i++) {
      const ImU32 c = panes::rgb(dp::SIG_COLORS[i % 8]);
      for (std::string_view comp : dp::componentsFor(row[i])) {
        auto& list = boxColors[comp];
        // A bus light on the same box gives way to the signal's color.
        if (!list.empty() && list[0] == busColor) list.clear();
        if (std::find(list.begin(), list.end(), c) == list.end()) list.push_back(c);
      }
      for (std::string_view w : dp::wiresFor(row[i])) {
        auto it = wireColor.find(w);
        if (it == wireColor.end() || it->second == busColor) wireColor[w] = c;
      }
    }
  }

  // The drawing space is 410 x 240, scaled to the pane.
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float rowH = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
  const float scale = std::max(0.5f, std::min(avail.x / dp::VIEW_W, (avail.y - rowH) / dp::VIEW_H));
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  auto at = [&](float x, float y) { return ImVec2(origin.x + x * scale, origin.y + y * scale); };
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImU32 wireOff = IM_COL32(70, 72, 80, 255);
  const ImU32 boxFill = IM_COL32(38, 40, 46, 255);
  const ImU32 boxEdge = IM_COL32(110, 112, 120, 255);
  const ImU32 text = IM_COL32(230, 230, 235, 255);

  // Wires live behind the boxes. Active ones draw after the inactive
  // ones, so they sit on top of them.
  for (int pass = 0; pass < 2; pass++) {
    for (const dp::Wire& w : dp::wires()) {
      auto it = wireColor.find(w.id);
      const bool on = it != wireColor.end();
      if (on != (pass == 1)) continue;
      std::vector<ImVec2> pts;
      for (const auto& p : w.pts) pts.push_back(at(p[0], p[1]));
      dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), on ? it->second : wireOff, ImDrawFlags_None, on ? 2.5f * scale : 1.2f * scale);
    }
  }
  const dp::Card* hovered = nullptr;
  for (const dp::Box& b : dp::boxes()) {
    const ImVec2 p0 = at(b.x, b.y), p1 = at(b.x + b.w, b.y + b.h);
    auto it = boxColors.find(b.id);
    const bool on = it != boxColors.end();
    dl->AddRectFilled(p0, p1, boxFill, 5.0f * scale);
    dl->AddRect(p0, p1, on ? it->second[0] : boxEdge, 5.0f * scale, 0, on ? 2.5f : 1.0f);
    dl->AddText(ImVec2(p0.x + 6.0f * scale, p0.y + 4.0f * scale), on ? it->second[0] : text, b.label.data(), b.label.data() + b.label.size());
    if (on) {
      for (size_t k = 1; k < it->second.size(); k++) {
        dl->AddCircleFilled(ImVec2(p0.x + (12.0f + 10.0f * static_cast<float>(k - 1)) * scale, p0.y), 3.5f * scale, it->second[k]);
      }
    }
    if (b.value) {
      if (b.id == "FLAGS") {
        // Flags always show all four letters, N V Z C, grey when clear,
        // green when set.
        const std::pair<const char*, bool> letters[] = {{"N", m.flags.n}, {"V", m.flags.v}, {"Z", m.flags.z}, {"C", m.flags.c}};
        float x = p1.x - 6.0f * scale;
        for (int k = 3; k >= 0; k--) {
          const ImVec2 sz = ImGui::CalcTextSize(letters[k].first);
          x -= sz.x;
          dl->AddText(ImVec2(x, p1.y - 8.0f * scale - sz.y), letters[k].second ? IM_COL32(90, 217, 100, 255) : IM_COL32(120, 120, 125, 255), letters[k].first);
          x -= 3.0f;
        }
      } else {
        const std::string v = boxValue(b.id, m);
        const ImVec2 sz = ImGui::CalcTextSize(v.c_str());
        dl->AddText(ImVec2(p1.x - 6.0f * scale - sz.x, p1.y - 8.0f * scale - sz.y), text, v.c_str());
      }
    }
    if (ImGui::IsMouseHoveringRect(p0, p1)) hovered = dp::cardFor(b.id);
  }
  ImGui::Dummy(ImVec2(dp::VIEW_W * scale, dp::VIEW_H * scale));
  if (hovered) {
    ImGui::BeginTooltip();
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "%s", std::string(hovered->title).c_str());
    ImGui::TextDisabled("%s", std::string(hovered->kind).c_str());
    ImGui::PushTextWrapPos(420.0f);
    ImGui::TextUnformatted(hovered->what.data(), hovered->what.data() + hovered->what.size());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }

  // The executed row as colored chips under the picture.
  if (!m.lastMicro) {
    ImGui::TextDisabled("no microcycle yet: press Microstep or Step");
  } else if (!inspectable) {
    ImGui::TextDisabled("%s: the set is sealed, the lanes light from the bus alone", m.lastMicro->op.c_str());
  } else {
    const Row& row = m.lastMicro->signals;
    for (size_t i = 0; i < row.size(); i++) {
      if (i) ImGui::SameLine();
      const unsigned c = dp::SIG_COLORS[i % 8];
      ImGui::TextColored(ImVec4(static_cast<float>((c >> 16) & 255) / 255.0f, static_cast<float>((c >> 8) & 255) / 255.0f,
                                static_cast<float>(c & 255) / 255.0f, 1.0f),
                         "%s", std::string(signalName(row[i])).c_str());
    }
  }
  ImGui::End();
}

// ---- flow

// The whole instruction as one flow. Reveal follows execution: during
// fetch only the fetch rows show, numbered 00. Inside an instruction its
// rows show as 01..N under a done FETCH line, so the fetch does not
// vanish. The executed row is highlighted.
void Ide::flowPane() {
  ImGui::Begin("Flow");
  const Machine& m = computer_.machine();
  if (!computer_.microcodeInspectable()) {
    ImGui::TextWrapped("%s", SEALED);
    ImGui::End();
    return;
  }
  if (!m.lastMicro) {
    ImGui::TextDisabled("no microcycle yet");
    ImGui::End();
    return;
  }
  const Microcode& set = m.microcode();
  auto line = [&](const char* number, const Row& row, bool active, bool done) {
    const char* mark = active ? ">" : done ? "v" : " ";
    const std::string text = std::string(number) + " " + mark + " " + rowText(row);
    if (active) ImGui::TextColored(ACCENT, "%s", text.c_str());
    else if (done) ImGui::TextColored(DIM, "%s", text.c_str());
    else ImGui::TextUnformatted(text.c_str());
  };
  if (m.lastMicro->op == "fetch") {
    ImGui::TextDisabled("fetch");
    const Rows* rows = set.get("fetch");
    for (size_t i = 0; rows && i < rows->size(); i++) {
      const auto r = static_cast<int>(i);
      line("00", (*rows)[i], r == m.lastMicro->row, r < m.lastMicro->row);
    }
    ImGui::End();
    return;
  }
  const std::string& op = m.lastMicro->op;
  const Rows* rows = set.get(op);
  ImGui::TextDisabled("%s", op.c_str());
  ImGui::TextColored(DIM, "00 v FETCH -> %s", op.c_str());
  for (size_t i = 0; rows && i < rows->size(); i++) {
    const auto r = static_cast<int>(i);
    const std::string num = (i + 1 < 10 ? "0" : "") + std::to_string(i + 1);
    line(num.c_str(), (*rows)[i], r == m.lastMicro->row, r < m.lastMicro->row);
  }
  ImGui::End();
}

// ---- rows

// The sets to pick from, then the sections of the loaded set with the
// live row marked. The editor for a set of your own holds the whole text
// in the file format. Apply runs the parser and shows its errors.
void Ide::microcodePane() {
  ImGui::Begin("Microcode");
  const std::string& name = computer_.microcodeName();
  const bool custom = name != "@naive" && name != "@optimal";
  if (ImGui::RadioButton("@naive", name == "@naive")) selectMicrocode("@naive");
  ImGui::SameLine();
  if (ImGui::RadioButton("@optimal", name == "@optimal")) selectMicrocode("@optimal");
  ImGui::SameLine();
  if (ImGui::RadioButton("my own", custom)) {
    // A set of your own starts as a copy of the naive one.
    if (customText_.empty()) customText_ = serializeMicrocode(buildNaive());
    selectMicrocode(customText_);
  }
  if (!computer_.microcodeInspectable()) {
    ImGui::TextWrapped("%s", SEALED);
    ImGui::End();
    return;
  }
  ImGui::SameLine(0.0f, 20.0f);
  if (ImGui::SmallButton(mcEditing_ ? "close the editor" : "Edit rows...")) {
    if (!mcEditing_ && customText_.empty()) customText_ = serializeMicrocode(computer_.machine().microcode());
    mcEditing_ = !mcEditing_;
  }
  ImGui::Separator();

  if (mcEditing_) {
    if (ImGui::SmallButton("Apply")) {
      // Applying makes the set yours whichever radio was on, and restarts
      // the machine, as swapping microcode always does.
      selectMicrocode(customText_);
      if (mcErrors_.empty()) note("your microcode was applied and the machine restarted");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset to naive")) customText_ = serializeMicrocode(buildNaive());
    ImGui::SameLine();
    ImGui::TextDisabled("one section per instruction, one row per cycle, at most %d rows", ROW_CAP);
    for (const std::string& e : mcErrors_) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s", e.c_str());
    panes::inputMultiline("##mctext", customText_, ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_AllowTabInput);
    ImGui::End();
    return;
  }

  const Machine& m = computer_.machine();
  const auto& sections = m.microcode().sections();
  const std::string current = m.lastMicro ? m.lastMicro->op : "";
  if (ImGui::BeginListBox("##sections", ImVec2(170, -1))) {
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
      if (live) ImGui::TextColored(ACCENT, "%02zu  %s", r + 1, rowText(s.rows[r]).c_str());
      else ImGui::Text("%02zu  %s", r + 1, rowText(s.rows[r]).c_str());
    }
  }
  ImGui::EndGroup();
  ImGui::End();
}

}  // namespace sc8
