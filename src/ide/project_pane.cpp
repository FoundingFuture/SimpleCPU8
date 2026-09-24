// The project: its documents, the Files and Editor panes, the build, and
// the BASIC bridge between the active .bas document and the interpreter.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "imgui.h"

#include "basic/basic_rom.h"
#include "basic/program.h"
#include "core/cartridge.h"
#include "core/mcparse.h"
#include "ide/ide.h"
#include "ide/panes.h"

namespace fs = std::filesystem;

namespace sc8 {

namespace {

const char* DEFAULT_SOURCE = R"(; SimpleCPU-8: a countdown, then a halt.
; This is the scratch project. Build runs it. Save the project to a
; folder to keep it, or open one of the examples.
        LD A <- 5
loop:   SUB A <- 1
        JZ done
        JMP loop
done:   LD [result] <- A
        HLT
.ram
result: db 0xEE
)";

std::optional<std::string> readText(const fs::path& p) {
  std::ifstream in(p);
  if (!in) return std::nullopt;
  return std::string(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

DocKind docKindOf(const std::string& name) {
  const std::string ext = fs::path(name).extension().string();
  if (name == "microcode.txt") return DocKind::Microcode;
  if (ext == ".c") return DocKind::C;
  if (ext == ".h") return DocKind::Header;
  if (ext == ".asm") return DocKind::Assembly;
  if (ext == ".bas") return DocKind::Basic;
  return DocKind::Other;
}

// ---- the project

void Ide::newScratchProject() {
  projectDir_.clear();
  projectTitle_ = "scratch";
  romBase_.reset();
  romFiles_.clear();
  carriedProject_ = false;
  romPath_.clear();
  docs_.clear();
  addDoc("main.asm", DEFAULT_SOURCE);
  docs_.back().dirty = false;
  activate(0);
  buildProject(false);
}

void Ide::addDoc(const std::string& name, const std::string& text) {
  Doc d;
  d.name = name;
  d.text = text;
  d.text.reserve(d.text.size() + (1 << 16));
  d.dirty = projectDir_.empty();
  docs_.push_back(std::move(d));
  std::sort(docs_.begin(), docs_.end(), [](const Doc& a, const Doc& b) { return a.name < b.name; });
}

Doc* Ide::activeDoc() { return active_ < docs_.size() ? &docs_[active_] : nullptr; }

// The manual follows the document: a .bas opens the BASIC guide, a .c
// the C guide, and so on. The Reference tab stays where the reader put it.
void Ide::activate(size_t index) {
  if (index >= docs_.size()) return;
  active_ = index;
  if (auto g = guideFor(docs_[index].name)) {
    guideView_.guide = *g;
    manualTab_ = static_cast<int>(*g);
  }
}

project::Layout Ide::layout() const {
  return project::layoutOf(projectDir_.empty() ? fs::current_path() : fs::path(projectDir_));
}

std::vector<project::Source> Ide::sources() const {
  std::vector<project::Source> out;
  for (const Doc& d : docs_) out.push_back({d.name, d.text});
  return out;
}

void Ide::openProject(const std::string& dir) {
  const project::Layout l = project::layoutOf(dir);
  std::error_code ec;
  if (!fs::is_directory(l.sources, ec)) {
    note(dir + " is not a folder");
    return;
  }
  projectDir_ = fs::absolute(dir).string();
  projectTitle_ = l.name;
  romBase_.reset();
  romFiles_.clear();
  carriedProject_ = false;
  romPath_.clear();
  docs_.clear();
  syncDoc_.clear();
  for (const auto& entry : fs::directory_iterator(l.sources, ec)) {
    if (!entry.is_regular_file()) continue;
    const std::string name = entry.path().filename().string();
    if (docKindOf(name) == DocKind::Other) continue;
    if (auto t = readText(entry.path())) addDoc(name, *t);
  }
  for (Doc& d : docs_) d.dirty = false;
  if (docs_.empty()) {
    note(l.sources.string() + " holds no source yet: add one with the field under the file list");
  } else {
    // main.c, main.asm or autorun.bas first, since that is where a
    // reader starts.
    size_t first = 0;
    for (size_t i = 0; i < docs_.size(); i++) {
      const std::string& n = docs_[i].name;
      if (n == "main.c" || n == "main.asm" || n == "autorun.bas") first = i;
    }
    activate(first);
  }
  focusFiles_ = true;
  buildProject(false);
  note("opened project " + projectTitle_ + " in " + projectDir_);
}

// A ROM is a project too: its BASIC slots and its own microcode set are
// documents, its program stays as it is. Saving the project to a folder
// writes those documents; the program cannot be written back as source.
void Ide::openRomAsProject(const std::string& path) {
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
  projectDir_.clear();
  romPath_ = fs::absolute(path).string();
  projectTitle_ = fs::path(path).stem().string();
  for (const auto& [k, v] : r.cartridge->meta) {
    if (k == "title" && !v.empty()) projectTitle_ = v;
  }
  docs_.clear();
  syncDoc_.clear();
  const project::Carried carry = project::carried(*r.cartridge);
  if (!carry.sources.empty()) {
    // The ROM carries its project: every source is a document, the
    // assets stay in the ROM for the build to read, and the program is
    // built again from the sources, listing and breakpoints included.
    for (const project::Source& src : carry.sources) addDoc(src.name, src.text);
    romFiles_ = carry.files;
    romBase_ = std::move(*r.cartridge);
    romBase_->sources.clear();
    for (Doc& d : docs_) d.dirty = false;
    size_t first = 0;
    for (size_t i = 0; i < docs_.size(); i++) {
      const std::string& n = docs_[i].name;
      if (n == "main.c" || n == "main.asm" || n == "autorun.bas") first = i;
    }
    activate(first);
    carriedProject_ = true;
  } else {
    for (const auto& [name, text] : r.cartridge->basic) {
      std::string file = name;
      for (char& c : file) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      addDoc(file + ".bas", text);
    }
    const std::string& mc = r.cartridge->microcode;
    if (mc != "@naive" && mc != "@optimal") addDoc("microcode.txt", mc);
    romFiles_.clear();
    romBase_ = std::move(*r.cartridge);
    carriedProject_ = false;
    for (Doc& d : docs_) d.dirty = false;
    if (!docs_.empty()) activate(0);
  }
  focusFiles_ = true;
  buildProject(false);
  note("opened ROM " + path + (carriedProject_ ? " with its project: " : " as a project: ") +
       std::to_string(docs_.size()) + " document(s)" + (carriedProject_ ? "" : ", the program as it is"));
}

void Ide::createProject(const std::string& dir, project::Kind kind) {
  project::Created c = project::create(dir, kind);
  if (!c.error.empty()) {
    note(c.error);
    return;
  }
  for (const fs::path& f : c.files) note("wrote " + f.string());
  openProject(dir);
}

// Save every document into a folder, in the larger layout, and make that
// folder the project. This is how a scratch project or an opened ROM
// gets a home.
void Ide::saveProjectAs(const std::string& dir) {
  const fs::path root = fs::absolute(dir);
  std::error_code ec;
  fs::create_directories(root / "src", ec);
  fs::create_directories(root / "assets", ec);
  if (!fs::exists(root / "README.md", ec)) std::ofstream(root / "README.md") << "# " << root.filename().string() << "\n";
  if (!fs::exists(root / ".gitignore", ec)) std::ofstream(root / ".gitignore") << "build/\n";
  projectDir_ = root.string();
  projectTitle_ = root.filename().string();
  for (Doc& d : docs_) saveDoc(d);
  for (const auto& [name, bytes] : romFiles_) {
    if (name.find("..") != std::string::npos) continue;
    const fs::path at = root / name;
    fs::create_directories(at.parent_path(), ec);
    std::ofstream o(at, std::ios::binary);
    o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (o) note("wrote " + at.string());
  }
  romFiles_.clear();
  if (romBase_ && !carriedProject_) {
    // The program has no source. The ROM itself goes beside the sources,
    // so the folder holds everything the project was opened from.
    std::vector<uint8_t> bytes = encodeCartridge(*romBase_);
    std::ofstream o(root / "build" / "base.rom", std::ios::binary);
    if (!o) {
      fs::create_directories(root / "build", ec);
      o.open(root / "build" / "base.rom", std::ios::binary);
    }
    o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    note("the ROM's program was written to build/base.rom; the sources are in src/");
  }
  romBase_.reset();
  carriedProject_ = false;
  note("project saved in " + projectDir_);
}

void Ide::saveDoc(Doc& doc) {
  if (projectDir_.empty()) {
    note("the project has no folder yet: use File, Save project as");
    return;
  }
  const project::Layout l = layout();
  std::error_code ec;
  fs::create_directories(l.sources, ec);
  std::ofstream o(l.sources / doc.name);
  o << doc.text;
  if (o) {
    doc.dirty = false;
    note("saved " + (l.sources / doc.name).string());
  } else {
    note("cannot write " + (l.sources / doc.name).string());
  }
}

void Ide::saveAll() {
  for (Doc& d : docs_) {
    if (d.dirty) saveDoc(d);
  }
}

// Save is the whole project: every changed document into the folder. A
// project with no folder yet asks for one first, and every document
// goes there.
void Ide::saveProject() {
  if (projectDir_.empty()) {
    const fs::path startIn = !settings_.projectsDir.empty() && fs::is_directory(settings_.projectsDir)
                                 ? fs::path(settings_.projectsDir)
                                 : fs::current_path();
    dialog_.open(FileDialog::Mode::OpenFolder, "Save the project: pick or make its folder", startIn, {},
                 [this](const fs::path& p) { saveProjectAs(p.string()); });
    return;
  }
  saveAll();
  if (!anyDirty()) note("project saved");
}

bool Ide::anyDirty() const {
  for (const Doc& d : docs_) {
    if (d.dirty) return true;
  }
  return false;
}

// ---- the build

// Build is what simplecpu-make does: every source into one ROM, which
// goes into the machine. A project with a folder is built from the
// folder, after its documents are saved, so the ROM lands in build/ as
// well. A project without one is built from the documents in memory.
void Ide::buildProject(bool run) {
  messages_.clear();
  project::Options o;
  project::Built built;
  if (!projectDir_.empty()) {
    saveAll();
    project::Written w = project::buildAndWrite(layout(), o);
    built = std::move(w.built);
    if (built.cartridge) note("wrote " + w.rom.string());
  } else {
    std::vector<std::string> notes;
    // A carried project reads its pictures and sounds from the ROM and
    // builds the program again; a bare ROM keeps its program.
    Cartridge carrier;
    carrier.sources = romFiles_;
    Assets assets = carriedProject_ ? project::loadersFrom(carrier, &notes) : project::loaders(layout(), &notes);
    built = project::buildSources(sources(), assets, o, projectTitle_, "",
                                  carriedProject_ ? std::optional<Cartridge>{} : romBase_);
    built.notes.insert(built.notes.begin(), notes.begin(), notes.end());
    if (built.cartridge) project::embed(*built.cartridge, sources(), romFiles_);
  }
  for (const std::string& n : built.notes) note(n);
  for (const std::string& e : built.errors) note(e);
  if (!built.cartridge) return;
  insertBuilt(built, projectTitle_);
  if (run) setRunning(true);
}

void Ide::insertBuilt(project::Built& built, const std::string& what) {
  builtAssembly_ = built.assembly;
  assembled_ = std::move(built.assembled);
  haveSource_ = !built.assembly.empty();
  // A ROM with BASIC slots runs the interpreter: a BASIC or mixed project
  // puts it first, and an opened ROM with slots came from one.
  const bool hasBasic = !built.cartridge->basic.empty();
  const bool interpreter = hasBasic && (romBase_.has_value() || built.sources.empty() ||
                                        built.sources.front() == "the BASIC interpreter");
  loadCartridge(std::move(*built.cartridge), what);
  basicBooted_ = interpreter;
  if (built.instructions > 0) {
    note("built " + what + ": " + std::to_string(built.instructions) + " instructions, " +
         std::to_string(built.ramBytes) + " bytes of RAM, " + std::to_string(built.dataBytes) + " bytes of data");
  }
}

void Ide::loadCartridge(Cartridge cart, const std::string& what) {
  setRunning(false);
  basicBooted_ = false;
  pushPending_ = false;
  machineChanged_ = false;
  machineProgram_.clear();
  computer_.insert(std::move(cart));
  applyLock();
  pushBreakpoints();
  rebuildListing();
  const std::string set = lockedMicrocode_ ? "locked " + computer_.microcodeName().substr(0, 8) : computer_.microcodeName();
  note("loaded " + what + ", microcode " + (set.size() > 12 ? "your own set" : set));
}

void Ide::burnRom(const std::string& path) {
  std::vector<uint8_t> bytes = encodeCartridge(computer_.cartridge());
  std::ofstream o(path, std::ios::binary);
  o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  note(o ? "burned " + path + " (" + std::to_string(bytes.size()) + " bytes)" : "cannot write " + path);
}

// ---- BASIC

// The built in interpreter alone, for a quick run of a .bas document
// without a build.
void Ide::bootBasic() {
  CartridgeResult r = decodeCartridge(std::vector<uint8_t>(basicRom().begin(), basicRom().end()));
  if (!r.cartridge) {
    note("BASIC ROM: " + r.error);
    return;
  }
  builtAssembly_.clear();
  haveSource_ = false;
  loadCartridge(std::move(*r.cartridge), "the BASIC ROM");
  basicBooted_ = true;
  // A fresh interpreter holds the empty program. Starting from it keeps
  // the boot from counting as a change to the document.
  machineProgram_ = {0, 0, 3};
}

// Run the active .bas document: boot the interpreter unless the machine
// already runs one, put the program in its memory and type RUN. The push
// waits for the boot, since the system page is empty until then.
void Ide::runInBasic() {
  Doc* d = activeDoc();
  if (!d || docKindOf(d->name) != DocKind::Basic) return;
  syncDoc_ = d->name;
  if (!basicBooted_ || computer_.machine().status != Status::Running) bootBasic();
  pushPending_ = true;
  afterPush_ = "RUN\n";
  setRunning(true);
}

bool Ide::basicAtReady() const {
  if (!basicBooted_ || computer_.machine().status != Status::Running) return false;
  const auto& ram = computer_.machine().ram;
  const int prog = (ram[basic::SYS_PROG] << 8) | ram[basic::SYS_PROG + 1];
  return prog != 0 && ram[basic::SYS_RUNNING] == 0 && typingPos_ >= typing_.size();
}

bool Ide::pushProgram() {
  if (!basicAtReady()) return false;
  Doc* d = nullptr;
  for (Doc& doc : docs_) {
    if (doc.name == syncDoc_) d = &doc;
  }
  if (!d) return false;
  auto& ram = computer_.machine().ram;
  const size_t prog = static_cast<size_t>((ram[basic::SYS_PROG] << 8) | ram[basic::SYS_PROG + 1]);
  std::vector<uint8_t> bytes = basic::encodeProgram(d->text);
  if (prog + bytes.size() > ram.size()) return false;
  std::copy(bytes.begin(), bytes.end(), ram.begin() + static_cast<std::ptrdiff_t>(prog));
  ram[basic::SYS_PROG_LEN] = static_cast<uint8_t>(bytes.size() >> 8);
  ram[basic::SYS_PROG_LEN + 1] = static_cast<uint8_t>(bytes.size() & 255);
  machineProgram_ = std::move(bytes);
  syncedText_ = d->text;
  machineChanged_ = false;
  return true;
}

void Ide::pullProgram() {
  for (Doc& doc : docs_) {
    if (doc.name != syncDoc_) continue;
    const std::string text = basic::decodeProgram(machineProgram_);
    if (text != doc.text) {
      doc.text = text;
      doc.text.reserve(doc.text.size() + (1 << 16));
      doc.dirty = true;
    }
    syncedText_ = text;
  }
  machineChanged_ = false;
}

// Once per frame. A pending push goes through as soon as BASIC is at
// READY. Otherwise the program memory is compared with the last exchange
// and a difference flows to the document, or waits for Pull when the
// document has edits of its own.
void Ide::syncBasic() {
  if (!basicAtReady()) return;
  if (pushPending_) {
    if (!pushProgram()) return;
    pushPending_ = false;
    typing_ = afterPush_;
    typingPos_ = 0;
    return;
  }
  const auto& ram = computer_.machine().ram;
  const size_t prog = static_cast<size_t>((ram[basic::SYS_PROG] << 8) | ram[basic::SYS_PROG + 1]);
  const size_t len = static_cast<size_t>((ram[basic::SYS_PROG_LEN] << 8) | ram[basic::SYS_PROG_LEN + 1]);
  if (len < 3 || len > basic::PROGRAM_MAX || prog + len > ram.size()) return;
  const auto first = ram.begin() + static_cast<std::ptrdiff_t>(prog);
  if (std::equal(first, first + static_cast<std::ptrdiff_t>(len), machineProgram_.begin(), machineProgram_.end())) return;
  machineProgram_.assign(first, first + static_cast<std::ptrdiff_t>(len));
  // With no document in step yet, the AUTORUN document is the natural
  // partner: a built BASIC project boots into it.
  if (syncDoc_.empty()) {
    for (const Doc& d : docs_) {
      if (d.name == "autorun.bas") syncDoc_ = d.name;
    }
    if (syncDoc_.empty()) return;
    syncedText_.clear();
    for (const Doc& d : docs_) {
      if (d.name == syncDoc_) syncedText_ = d.text;
    }
  }
  const Doc* d = nullptr;
  for (const Doc& doc : docs_) {
    if (doc.name == syncDoc_) d = &doc;
  }
  if (!d) return;
  if (d->text == syncedText_ || basic::encodeProgram(d->text) == machineProgram_) pullProgram();
  else machineChanged_ = true;
}

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

// ---- the panes

void Ide::filesPane() {
  if (focusFiles_) {
    ImGui::SetNextWindowFocus();
    focusFiles_ = false;
  }
  ImGui::Begin("Files");
  const std::string where = projectDir_.empty() ? (romBase_ ? "a ROM, not saved as a folder yet" : "not saved yet")
                                                : projectDir_;
  ImGui::TextWrapped("%s", projectTitle_.c_str());
  ImGui::TextDisabled("%s", where.c_str());
  if (ImGui::SmallButton("Build")) buildProject(false);
  ImGui::SameLine();
  if (ImGui::SmallButton("Build and run")) buildProject(true);
  if (ImGui::SmallButton("Save")) saveProject();
  ImGui::SameLine();
  if (ImGui::SmallButton("Boot BASIC")) bootBasic();
  ImGui::Separator();
  for (size_t i = 0; i < docs_.size(); i++) {
    const Doc& d = docs_[i];
    const std::string label = d.name + (d.dirty ? " *" : "");
    if (ImGui::Selectable(label.c_str(), i == active_)) activate(i);
  }
  ImGui::Spacing();
  ImGui::SetNextItemWidth(-1.0f);
  if (panes::inputLine("##new", newDocName_, "new file: name.c, .h, .asm, .bas, Enter",
                       ImGuiInputTextFlags_EnterReturnsTrue)) {
    const std::string n = newDocName_;
    if (!n.empty() && docKindOf(n) != DocKind::Other) {
      bool exists = false;
      for (const Doc& d : docs_) exists = exists || d.name == n;
      if (!exists) {
        std::string text;
        if (n == "microcode.txt") text = "# your microcode set, started from the naive one\n\n" + serializeMicrocode(buildNaive());
        addDoc(n, text);
        docs_.back().dirty = true;
        std::sort(docs_.begin(), docs_.end(), [](const Doc& a, const Doc& b) { return a.name < b.name; });
      }
      for (size_t i = 0; i < docs_.size(); i++) {
        if (docs_[i].name == n) activate(i);
      }
      newDocName_.clear();
    } else if (!n.empty()) {
      note(n + ": a source is a .c, .h, .asm or .bas file, or microcode.txt");
    }
  }
  if (romBase_ && !carriedProject_) {
    ImGui::Spacing();
    ImGui::TextWrapped("The ROM carries no project. Its program is not a document and its listing is a disassembly.");
  } else if (carriedProject_) {
    ImGui::Spacing();
    ImGui::TextWrapped("The project came out of the ROM. Save project as writes it to a folder.");
  }
  ImGui::End();
}

// The editor shows the active document with the buttons its kind needs.
// A .bas document has the interpreter beside it: Run in BASIC, the sync
// state and Pull.
void Ide::editorPane() {
  ImGui::Begin("Editor");
  Doc* d = activeDoc();
  if (!d) {
    ImGui::TextDisabled("no document: add one in the Files pane, or open a project");
    ImGui::End();
    return;
  }
  const DocKind kind = docKindOf(d->name);
  ImGui::Text("%s%s", d->name.c_str(), d->dirty ? " (unsaved)" : "");
  ImGui::SameLine();
  if (ImGui::SmallButton("Save")) saveProject();
  ImGui::SameLine();
  if (ImGui::SmallButton("Build")) buildProject(false);
  if (kind == DocKind::Basic) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Run in BASIC")) runInBasic();
    ImGui::SameLine();
    if (ImGui::SmallButton("Push to machine")) {
      syncDoc_ = d->name;
      if (!basicAtReady()) note("push waits until BASIC is at READY: boot it and press Run");
      pushPending_ = true;
      afterPush_.clear();
      if (!running_) setRunning(true);
    }
    ImGui::SameLine();
    if (machineChanged_ && syncDoc_ == d->name) {
      if (ImGui::SmallButton("Pull from machine")) pullProgram();
      ImGui::SameLine();
      ImGui::TextDisabled("the machine's program changed and this document has edits of its own");
    } else if (pushPending_) {
      ImGui::TextDisabled("waiting for READY");
    } else if (basicAtReady() && syncDoc_ == d->name) {
      ImGui::TextDisabled("in step with the machine: a line typed on the screen shows up here");
    } else {
      ImGui::TextDisabled("Run in BASIC boots the interpreter with this program. Build makes the whole ROM.");
    }
  } else if (kind == DocKind::Microcode) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Apply to machine")) {
      McParsed parsed = parseMicrocode(d->text);
      for (const McError& e : parsed.errors) note("microcode.txt:" + std::to_string(e.line) + ": " + e.message);
      if (parsed.errors.empty()) {
        customText_ = d->text;
        selectMicrocode(d->text);
        note("your microcode was applied and the machine restarted");
      }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(lockedMicrocode_ ? "Unlock" : "Apply and lock")) {
      if (lockedMicrocode_) {
        lockedMicrocode_.reset();
        note("microcode unlocked: the next build or ROM brings its own set");
      } else {
        McParsed parsed = parseMicrocode(d->text);
        for (const McError& e : parsed.errors) note("microcode.txt:" + std::to_string(e.line) + ": " + e.message);
        if (parsed.errors.empty()) {
          lockedMicrocode_ = d->text;
          customText_ = d->text;
          selectMicrocode(d->text);
          note("microcode locked: every project and ROM runs on this set until unlocked");
        }
      }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("the set the ROM carries; the CPU level shows its rows firing");
  } else if (kind == DocKind::C) {
    ImGui::SameLine();
    ImGui::TextDisabled("Build compiles every .c together; the Assembly pane shows the result");
  }
  if (panes::inputMultiline("##doc", d->text, ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_AllowTabInput)) d->dirty = true;
  ImGui::End();
}

// The assembly the last build handed the assembler: the compiler's
// output, then the project's own .asm files. Read only, since the
// sources are the documents.
void Ide::assemblyPane() {
  ImGui::Begin("Assembly");
  if (builtAssembly_.empty()) {
    ImGui::TextDisabled(romBase_ ? "an opened ROM has no assembly; the Run level lists its program"
                                 : "build the project to see the assembly it makes");
  } else {
    ImGui::TextDisabled("what the assembler saw at the last build, read only");
    panes::inputMultiline("##asm", builtAssembly_, ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_ReadOnly);
  }
  ImGui::End();
}

}  // namespace sc8
