// The Edit level's panes: the source editor, the BASIC editor and the
// manual. Plus the shared helpers of panes.h.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "imgui.h"
#include "raylib.h"

#include "core/cartridge.h"
#include "ide/ide.h"

#include "assets/assets.h"
#include "basic/program.h"
#include "cc/cc.h"
#include "ide/manual.h"
#include "ide/panes.h"

namespace fs = std::filesystem;

namespace sc8 {

namespace panes {

RenderTexture2D& target() {
  static RenderTexture2D rt = LoadRenderTexture(PANE_SIDE, PANE_SIDE);
  return rt;
}

namespace {

int resizeCallback(ImGuiInputTextCallbackData* data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
    auto* s = static_cast<std::string*>(data->UserData);
    s->resize(static_cast<size_t>(data->BufTextLen));
    data->Buf = s->data();
  }
  return 0;
}

}  // namespace

bool inputMultiline(const char* id, std::string& text, ImVec2 size, ImGuiInputTextFlags flags) {
  if (text.capacity() < text.size() + 256) text.reserve(text.size() + 4096);
  return ImGui::InputTextMultiline(id, text.data(), text.capacity() + 1, size,
                                   flags | ImGuiInputTextFlags_CallbackResize, resizeCallback, &text);
}

bool inputLine(const char* label, std::string& text, const char* hint, ImGuiInputTextFlags flags) {
  if (text.capacity() < text.size() + 64) text.reserve(text.size() + 256);
  return ImGui::InputTextWithHint(label, hint, text.data(), text.capacity() + 1,
                                  flags | ImGuiInputTextFlags_CallbackResize, resizeCallback, &text);
}

std::string hex(unsigned v, int digits) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%0*X", digits, v);
  return buf;
}

ImU32 rgb(unsigned c, int alpha) {
  return IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, alpha);
}

}  // namespace panes

using panes::hex;

// ---- source

void Ide::sourcePane() {
  ImGui::Begin("Source");
  ImGui::TextUnformatted(sourcePath_.empty() ? "(unsaved)" : sourcePath_.c_str());
  ImGui::SameLine();
  if (ImGui::SmallButton("Assemble")) assembleSource();
  ImGui::SameLine();
  if (ImGui::SmallButton("Save")) saveSource();
  ImGui::SameLine();
  if (ImGui::SmallButton("Burn ROM")) burnRom();
  panes::inputMultiline("##source", source_, ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_AllowTabInput);
  ImGui::End();
}

// ---- C

// ---- the project

void Ide::openProject(const std::string& dir) {
  projectDir_ = dir;
  focusProject_ = true;
  projectFiles_.clear();
  projectFile_.clear();
  projectText_.clear();
  projectDirty_ = false;
  const project::Layout l = project::layoutOf(dir);
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(l.sources, ec)) {
    if (!entry.is_regular_file()) continue;
    const std::string ext = entry.path().extension().string();
    if (ext == ".c" || ext == ".h" || ext == ".asm" || ext == ".bas") projectFiles_.push_back(entry.path().filename().string());
  }
  std::sort(projectFiles_.begin(), projectFiles_.end());
  if (projectFiles_.empty()) {
    note(l.sources.string() + " holds no .c, .h, .asm or .bas file");
    return;
  }
  // main.c first when there is one, since that is where a reader starts.
  const auto main = std::find(projectFiles_.begin(), projectFiles_.end(), "main.c");
  openProjectFile(main != projectFiles_.end() ? *main : projectFiles_.front());
}

void Ide::openProjectFile(const std::string& name) {
  if (projectDirty_) saveProjectFile();
  const project::Layout l = project::layoutOf(projectDir_);
  std::ifstream in(l.sources / name);
  if (!in) {
    note("cannot read " + (l.sources / name).string());
    return;
  }
  projectText_.assign(std::istreambuf_iterator<char>(in), {});
  projectText_.reserve(projectText_.size() + (1 << 16));
  projectFile_ = name;
  projectDirty_ = false;
  if (std::find(projectFiles_.begin(), projectFiles_.end(), name) == projectFiles_.end()) {
    projectFiles_.push_back(name);
    std::sort(projectFiles_.begin(), projectFiles_.end());
  }
}

void Ide::saveProjectFile() {
  if (projectFile_.empty()) return;
  const project::Layout l = project::layoutOf(projectDir_);
  std::ofstream o(l.sources / projectFile_);
  o << projectText_;
  note(o ? "saved " + projectFile_ : "cannot write " + (l.sources / projectFile_).string());
  projectDirty_ = !o;
}

// Build is what simplecpu-make does: every source in the folder into one
// ROM, written to the build folder. The assembly it made lands in the
// Source pane and is assembled there, so the listing, the breakpoints and
// the machine all come from it and the generated code can be read.
void Ide::buildProject(bool run) {
  if (projectDir_.empty()) {
    note("open a project folder first");
    return;
  }
  if (projectDirty_) saveProjectFile();
  const project::Layout l = project::layoutOf(projectDir_);
  project::Options o;
  project::Written w = project::buildAndWrite(l, o);
  messages_.clear();
  for (const std::string& n : w.built.notes) note(n);
  for (const std::string& e : w.built.errors) note(e);
  if (!w.built.cartridge) return;
  projectLayout_ = l;
  source_ = w.built.assembly;
  source_.reserve(source_.size() + (1 << 16));
  sourcePath_ = (l.build / (l.name + ".asm")).string();
  assembleSource();
  romPath_ = w.rom.string();
  note("built " + w.rom.string() + ": " + std::to_string(w.built.instructions) + " instructions, " +
       std::to_string(w.built.ramBytes) + " bytes of RAM, " + std::to_string(w.built.dataBytes) + " bytes of data");
  if (run && assembledOk_) setRunning(true);
}

void Ide::projectPane() {
  if (focusProject_) {
    ImGui::SetNextWindowFocus();
    focusProject_ = false;
  }
  ImGui::Begin("Project");
  ImGui::SetNextItemWidth(300);
  panes::inputLine("folder", projectDir_, "a folder of .c and .h files, or one with src/");
  ImGui::SameLine();
  if (ImGui::SmallButton("Open")) openProject(projectDir_);
  ImGui::SameLine();
  if (ImGui::SmallButton("Build")) buildProject(false);
  ImGui::SameLine();
  if (ImGui::SmallButton("Build and Run")) buildProject(true);
  ImGui::SameLine();
  ImGui::TextDisabled("every .c, .asm and .bas in the folder goes into one ROM in build/");
  // A fresh project in the folder named above, of one of the four kinds,
  // the way simplecpu-make new lays it out. It opens at once.
  ImGui::TextDisabled("new project in that folder:");
  const std::pair<const char*, project::Kind> KINDS[] = {
      {"C", project::Kind::C}, {"BASIC", project::Kind::Basic},
      {"Assembly", project::Kind::Assembly}, {"Microcode", project::Kind::Microcode}};
  for (const auto& [label, kind] : KINDS) {
    ImGui::SameLine();
    if (ImGui::SmallButton(label)) {
      if (projectDir_.empty()) {
        note("type the folder for the new project first");
      } else {
        project::Created c = project::create(projectDir_, kind);
        if (!c.error.empty()) {
          note(c.error);
        } else {
          for (const fs::path& f : c.files) note("wrote " + f.string());
          openProject(projectDir_);
          buildProject(false);
        }
      }
    }
  }

  ImGui::BeginChild("##files", ImVec2(150.0f, -1.0f), ImGuiChildFlags_Borders);
  for (const std::string& f : projectFiles_) {
    if (ImGui::Selectable(f.c_str(), f == projectFile_)) openProjectFile(f);
  }
  ImGui::Spacing();
  static char newName[64] = "";
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::InputTextWithHint("##new", "new file, Enter", newName, sizeof newName, ImGuiInputTextFlags_EnterReturnsTrue)) {
    const std::string n = newName;
    if (!n.empty() && !projectDir_.empty()) {
      const project::Layout l = project::layoutOf(projectDir_);
      std::error_code ec;
      fs::create_directories(l.sources, ec);
      if (!fs::exists(l.sources / n)) std::ofstream(l.sources / n) << "";
      openProjectFile(n);
      newName[0] = 0;
    }
  }
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##edit", ImVec2(-1.0f, -1.0f));
  if (projectFile_.empty()) {
    ImGui::TextDisabled("no file open");
  } else {
    ImGui::Text("%s%s", projectFile_.c_str(), projectDirty_ ? " (unsaved)" : "");
    ImGui::SameLine();
    if (ImGui::SmallButton("Save")) saveProjectFile();
    if (panes::inputMultiline("##ptext", projectText_, ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_AllowTabInput)) {
      projectDirty_ = true;
    }
  }
  ImGui::EndChild();
  ImGui::End();
}

// ---- BASIC

// Commands for the interpreter go through the key queue, the way a person
// types them. The queue holds 64 events and a key is two. The pump waits
// above 32 queued. Programs do not go this way: pushProgram writes them
// into the interpreter's memory.
void Ide::typeIntoMachine() {
  InputBus& in = computer_.input();
  while (typingPos_ < typing_.size() && in.queued() <= 32) {
    const char c = typing_[typingPos_++];
    if (c == '\r') continue;
    const int code = c == '\n' ? 13 : std::toupper(static_cast<unsigned char>(c));
    if (code < 1 || code > 127) continue;
    in.pushKey(static_cast<uint8_t>(code), false);
    in.pushKey(static_cast<uint8_t>(code), true);
  }
}

void Ide::basicPane() {
  ImGui::Begin("BASIC");
  ImGui::SetNextItemWidth(260);
  panes::inputLine("file", basicPath_, "path of a .bas file");
  ImGui::SameLine();
  if (ImGui::SmallButton("Load")) {
    std::ifstream in(basicPath_);
    if (in) {
      basicText_.assign(std::istreambuf_iterator<char>(in), {});
      note("loaded " + basicPath_);
    } else {
      note("cannot read " + basicPath_);
    }
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Save")) {
    if (basicPath_.empty()) basicPath_ = "program.bas";
    std::ofstream o(basicPath_);
    o << basicText_;
    note(o ? "saved " + basicPath_ : "cannot write " + basicPath_);
  }

  ImGui::SetNextItemWidth(160);
  panes::inputLine("slot", basicSlot_, "slot name in the ROM");
  ImGui::SameLine();
  if (ImGui::SmallButton("Save into ROM")) {
    // Add or replace the named slot. A session that came from a .rom
    // file rewrites the file. Anything else waits for the next burn.
    auto it = std::find_if(basicSlots_.begin(), basicSlots_.end(), [&](const auto& s) { return s.first == basicSlot_; });
    if (it == basicSlots_.end()) basicSlots_.emplace_back(basicSlot_, basicText_);
    else it->second = basicText_;
    if (!romPath_.empty()) {
      Cartridge c = computer_.cartridge();
      c.basic = basicSlots_;
      std::vector<uint8_t> bytes = encodeCartridge(c);
      std::ofstream o(romPath_, std::ios::binary);
      o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
      note(o ? "saved slot " + basicSlot_ + " into " + romPath_ : "cannot write " + romPath_);
      basicDirty_ = !o;
    } else {
      basicDirty_ = true;
      note("slot " + basicSlot_ + " saved, it goes into the ROM at the next burn");
    }
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Run in BASIC")) runInBasic();
  ImGui::SameLine();
  if (ImGui::SmallButton("Push to machine")) {
    if (!basicAtReady()) note("push waits until BASIC is at READY: boot it and press Run");
    pushPending_ = true;
    afterPush_.clear();
    if (!running_) setRunning(true);
  }
  ImGui::SameLine();
  if (machineChanged_) {
    if (ImGui::SmallButton("Pull from machine")) pullProgram();
    ImGui::SameLine();
    ImGui::TextDisabled("the program in the machine changed and the editor has edits of its own");
  } else if (pushPending_) {
    ImGui::TextDisabled("waiting for READY");
  } else if (basicAtReady()) {
    ImGui::TextDisabled("in step with the machine: a line typed on the screen shows up here");
  } else {
    ImGui::TextDisabled("Run boots BASIC, puts the program in its memory and runs it.");
  }
  if (!basicSlots_.empty()) {
    std::string slots = basicDirty_ ? "slots (unburned): " : "slots: ";
    for (size_t i = 0; i < basicSlots_.size(); i++) slots += (i ? ", " : "") + basicSlots_[i].first;
    ImGui::TextDisabled("%s", slots.c_str());
  }
  panes::inputMultiline("##basic", basicText_, ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_AllowTabInput);
  ImGui::End();
}

// ---- manual

namespace {

// The reference pages beside the instruction pages. Each is one entry in
// the manual's list and one page on the right.
struct RefPage {
  const char* id;
  const char* title;
};

const RefPage REF_PAGES[] = {
    {"gpuref", "GPU reference"},
    {"audio", "Audio reference"},
    {"acpref", "Coprocessor reference"},
    {"input", "Input reference"},
};

bool prefix(const std::string& s, const std::string& p) {
  if (p.size() > s.size()) return false;
  for (size_t i = 0; i < p.size(); i++) {
    if (std::toupper(static_cast<unsigned char>(s[i])) != std::toupper(static_cast<unsigned char>(p[i]))) return false;
  }
  return true;
}

void heading(const char* text) {
  ImGui::Spacing();
  ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "%s", text);
  ImGui::Separator();
}

void refTable(const char* id, const char* nameHead, const char* valueHead, const std::vector<manual::RefRow>& rows,
              int digits = 2) {
  if (!ImGui::BeginTable(id, 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) return;
  ImGui::TableSetupColumn(nameHead, ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn(valueHead, ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn("meaning", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableHeadersRow();
  for (const manual::RefRow& r : rows) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(r.name.c_str());
    ImGui::TableNextColumn();
    ImGui::Text("$%s", hex(static_cast<unsigned>(r.value), digits).c_str());
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", r.meaning.c_str());
  }
  ImGui::EndTable();
}

void instrPage(const manual::InstrPage& page, const Computer& computer) {
  ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "%s", page.usage.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("%s", page.title.c_str());
  ImGui::Separator();
  ImGui::TextDisabled("Parameters");
  ImGui::TextWrapped("%s", page.params.c_str());
  ImGui::TextDisabled("Flags affected");
  ImGui::TextWrapped("%s", page.flags.list.c_str());
  if (ImGui::BeginTable("flags", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("flag", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch);
    const std::pair<const char*, const std::string*> rows[] = {
        {"N", &page.flags.n}, {"V", &page.flags.v}, {"Z", &page.flags.z}, {"C", &page.flags.c}};
    for (const auto& [name, text] : rows) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      const bool off = *text == "not affected";
      if (off) ImGui::TextDisabled("%s", name);
      else ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.4f, 1.0f), "%s", name);
      ImGui::TableNextColumn();
      if (off) ImGui::TextDisabled("%s", text->c_str());
      else ImGui::TextWrapped("%s", text->c_str());
    }
    ImGui::EndTable();
  }
  ImGui::TextDisabled("Registers involved");
  ImGui::TextWrapped("%s", page.registers.c_str());
  ImGui::Spacing();
  ImGui::TextWrapped("%s", page.description.c_str());
  ImGui::Spacing();
  // The loaded set gets a column of its own when it is the user's. The
  // cost of a custom row is then read off the same table.
  const bool custom = computer.microcodeInspectable() && computer.microcodeName() != "@naive";
  if (ImGui::BeginTable("shapes", custom ? 5 : 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn("shape");
    ImGui::TableSetupColumn("opcode");
    ImGui::TableSetupColumn("cycles naive");
    ImGui::TableSetupColumn("cycles optimal");
    if (custom) ImGui::TableSetupColumn("cycles yours");
    ImGui::TableHeadersRow();
    for (const manual::InstrShape& s : page.shapes) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(s.name.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("$%s", hex(s.op, 2).c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%d", s.naive);
      ImGui::TableNextColumn();
      ImGui::Text("%d", s.optimal);
      if (custom) {
        ImGui::TableNextColumn();
        const int c = manual::cyclesUnder(computer.machine().microcode(), s.name);
        if (c < 0) ImGui::TextDisabled("no rows");
        else ImGui::Text("%d", c);
      }
    }
    ImGui::EndTable();
  }
  static const int fetchNaive = static_cast<int>(buildNaive().get("fetch")->size());
  static const int fetchOptimal = static_cast<int>(buildOptimal().get("fetch")->size());
  ImGui::TextDisabled("Cycles include the fetch. Naive fetch is %d cycle, optimal fetch is %d (it steps PC during the fetch).",
                      fetchNaive, fetchOptimal);
}

void gpuRefPage() {
  heading("GPU reference");
  ImGui::TextWrapped("Every port and every command the GPU understands. The recipe is always the same: set some "
                     "latches, then write one command byte to GPU_CMD. All names are built-in assembler constants.");
  heading("Ports");
  refTable("gpuports", "port", "addr", manual::gpuPorts());
  heading("Argument names");
  ImGui::TextWrapped("A data port means whatever the running command says it means, so there are many names for "
                     "seven ports. Write the name that says the job: OUT GPU_RADIUS, 20 and OUT GPU_DATA0, 20 are the "
                     "same instruction, and only one of them says what 20 is.");
  if (ImGui::BeginTable("aliases", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("port", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("addr", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("meaning", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (const manual::RefRow& r : manual::gpuAliases()) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(r.name.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("GPU_DATA%d", r.value - 2);
      ImGui::TableNextColumn();
      ImGui::Text("$%s", hex(static_cast<unsigned>(r.value), 2).c_str());
      ImGui::TableNextColumn();
      ImGui::TextWrapped("%s", r.meaning.c_str());
    }
    ImGui::EndTable();
  }
  heading("The command modifier");
  ImGui::TextWrapped("The data ports clear after every command. GPU_CMD_MOD changes that. It is a mode, not a one-shot: "
                     "the bits hold until you write the port again, so write 0 back when you are done.");
  refTable("mods", "bit", "value", manual::gpuMods(), 2);
  heading("Commands");
  ImGui::TextWrapped("Write the byte to GPU_CMD. The arguments column names the ports in the order they sit. No GPU "
                     "command touches the CPU's N V Z C: what a command answers, it answers on a data port, and the "
                     "leaves column says which. IN sets no CPU flag, so AND the value before a JZ.");
  if (ImGui::BeginTable("gpucmds", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("command", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("byte", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("arguments", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("leaves", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("meaning", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableHeadersRow();
    for (const manual::GpuCmdRow& r : manual::gpuCommands()) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(r.name.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("$%s", hex(static_cast<unsigned>(r.value), 2).c_str());
      ImGui::TableNextColumn();
      ImGui::TextWrapped("%s", r.args.c_str());
      ImGui::TableNextColumn();
      ImGui::TextWrapped("%s", r.leaves.c_str());
      ImGui::TableNextColumn();
      ImGui::TextWrapped("%s", r.meaning.c_str());
    }
    ImGui::EndTable();
  }
}

void audioRefPage() {
  heading("Audio reference");
  ImGui::TextWrapped("The audio chip is a device on the IO bus, like the GPU. Eight tracks, eight players each, 64 "
                     "voices. It reads the same cartridge the GPU reads and renders on its own real-time clock, so a "
                     "tune keeps playing while the CPU is paused.");
  heading("Ports");
  refTable("apuports", "port", "addr", manual::apuPorts());
  heading("Commands");
  ImGui::TextWrapped("Set the latches first, then write the byte to APU_CMD. No audio command touches the CPU's flags "
                     "and none answers on a port. APU_STATUS is 1 while a tune plays, APU_VOICES counts the voices.");
  refTable("apucmds", "command", "byte", manual::apuCommands());
}

void acpRefPage() {
  heading("Coprocessor reference");
  ImGui::TextWrapped("The ACP is the arithmetic coprocessor. One command reads two operands and writes one result "
                     "through a block of data RAM the program nominates, in one cycle. Point the block at some RAM, say "
                     "the type and the shape, then write one command byte.");
  heading("Ports");
  refTable("acpports", "port", "addr", manual::acpPorts());
  heading("Element types");
  ImGui::TextWrapped("Write one to ACP_FMT. It sets the result type as well, so ACP_RFMT is only needed when the two "
                     "differ, which is how a conversion is asked for.");
  refTable("acpfmts", "type", "value", manual::acpFormats());
  heading("Commands");
  ImGui::TextWrapped("r is ACP_ROWS, c is ACP_COLS and n is ACP_COLS_B. A dash means the operand is not read and takes "
                     "no room. The types column is what the command accepts: anything else sets ACP_BADFMT. The raises "
                     "column is what a command raises beyond ACP_ZERO, ACP_NEGATIVE, ACP_NAN and the does-not-fit sense "
                     "of ACP_OVERFLOW, which every command sets from its result.");
  if (ImGui::BeginTable("acpcmds", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("command", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("byte", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("A", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("B", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("R", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("types", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("also raises", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("what it does", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableHeadersRow();
    for (const manual::AcpCmdRow& r : manual::acpCommands()) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(r.name.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("$%s", hex(static_cast<unsigned>(r.value), 2).c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(r.a.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(r.b.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(r.r.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(r.types.c_str());
      ImGui::TableNextColumn();
      if (r.raises == "the usual") ImGui::TextDisabled("%s", r.raises.c_str());
      else ImGui::TextWrapped("%s", r.raises.c_str());
      ImGui::TableNextColumn();
      ImGui::TextWrapped("%s", r.what.c_str());
    }
    ImGui::EndTable();
  }
  heading("Flags");
  ImGui::TextWrapped("IN ACP_FLAGS reads them. Every command replaces every bit. IN sets no CPU flags, so AND the bit "
                     "you want before you branch.");
  refTable("acpflags", "flag", "bit", manual::acpFlags());
}

void inputRefPage() {
  heading("Input reference");
  ImGui::TextWrapped("The controller is a device on the bus. IO_CONTROLLER is the joystick as one byte, a level: read "
                     "it, then test a bit with AND A <- BTN_FIRE. IO_KEY pops one key event: printable keys are "
                     "uppercase ASCII, Enter is 13, arrows are 17 to 20, and the top bit marks a release. Ctrl with a "
                     "letter is a control code, 1 to 26.");
  heading("Ports");
  refTable("inports", "port", "addr", manual::inputPorts());
  heading("Buttons");
  refTable("btns", "button", "bit", manual::inputButtons());
  heading("Modifiers");
  refTable("mods", "modifier", "bit", manual::inputMods());
}

}  // namespace

// The manual: a row of tabs, one per guide, then a Reference tab. That
// tab holds the instruction pages and the device references. It keeps
// its own filter box, the list of pages on the left and the page on the
// right. Typing a prefix filters the mnemonics, as the browser did.
// manualTab_ selects a tab from elsewhere, for the guide of an open
// file. The tabs stay, so the person can always pick another.
void Ide::manualPane() {
  ImGui::Begin("Manual");
  if (ImGui::BeginTabBar("manualtabs")) {
    static const struct { const char* label; Guide guide; } GUIDE_TABS[] = {
        {"BASIC", Guide::Basic}, {"C", Guide::C}, {"Assembly", Guide::Assembly}, {"Microcode", Guide::Microcode}};
    for (int i = 0; i < GUIDE_COUNT; i++) {
      const ImGuiTabItemFlags flags = manualTab_ == i ? ImGuiTabItemFlags_SetSelected : 0;
      if (ImGui::BeginTabItem(GUIDE_TABS[i].label, nullptr, flags)) {
        if (guideView_.guide != GUIDE_TABS[i].guide) {
          guideView_.guide = GUIDE_TABS[i].guide;
          guideView_.chapter = -1;
        }
        drawGuide(guideView_);
        ImGui::EndTabItem();
      }
    }
    const ImGuiTabItemFlags refFlags = manualTab_ == GUIDE_COUNT ? ImGuiTabItemFlags_SetSelected : 0;
    if (ImGui::BeginTabItem("Reference", nullptr, refFlags)) {
      ImGui::SetNextItemWidth(-1.0f);
      panes::inputLine("##filter", manualFilter_, "filter: type the start of a mnemonic");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("type the start of a mnemonic to filter the list");
      const auto& pages = manual::instructionPages();
      ImGui::BeginChild("list", ImVec2(150, 0), ImGuiChildFlags_Borders);
      ImGui::TextDisabled("Instructions");
      for (const manual::InstrPage& p : pages) {
        if (!manualFilter_.empty() && !prefix(p.mnemonic, manualFilter_)) continue;
        if (ImGui::Selectable(p.mnemonic.c_str(), manualPage_ == p.mnemonic)) manualPage_ = p.mnemonic;
      }
      ImGui::Spacing();
      ImGui::TextDisabled("Devices");
      for (const RefPage& r : REF_PAGES) {
        if (!manualFilter_.empty() && !prefix(r.title, manualFilter_)) continue;
        if (ImGui::Selectable(r.title, manualPage_ == r.id)) manualPage_ = r.id;
      }
      ImGui::EndChild();
      ImGui::SameLine();
      ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_Borders);
      bool shown = false;
      for (const manual::InstrPage& p : pages) {
        if (p.mnemonic == manualPage_) {
          instrPage(p, computer_);
          shown = true;
        }
      }
      if (!shown) {
        if (manualPage_ == "gpuref") gpuRefPage();
        else if (manualPage_ == "audio") audioRefPage();
        else if (manualPage_ == "acpref") acpRefPage();
        else if (manualPage_ == "input") inputRefPage();
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  manualTab_ = -1;
  ImGui::End();
}

}  // namespace sc8
