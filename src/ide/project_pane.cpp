// The project: its documents, the Files and Editor panes, the build, and
// the BASIC bridge between the active .bas document and the interpreter.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "imgui.h"
#include "imgui_internal.h"

#include "asm/format.h"
#include "basic/basic_rom.h"
#include "basic/program.h"
#include "core/cartridge.h"
#include "core/mcparse.h"
#include "ide/highlight.h"
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

namespace {

// A microcode set's errors as the build writes errors, file:line: message.
std::vector<std::string> microcodeErrors(const McParsed& parsed) {
  std::vector<std::string> out;
  for (const McError& e : parsed.errors) out.push_back("microcode.txt:" + std::to_string(e.line) + ": " + e.message);
  return out;
}

// The checkout's examples/, which the build names. A distribution will
// name another folder.
fs::path examplesRoot() { return fs::path(SC8_EXAMPLES_DIR); }

// True when dir is inside examples/, or is it, however it is spelled.
bool insideExamples(const fs::path& dir) {
  std::error_code ec;
  const fs::path root = fs::weakly_canonical(examplesRoot(), ec);
  const fs::path at = fs::weakly_canonical(fs::absolute(dir, ec), ec);
  if (root.empty() || at.empty()) return false;
  auto r = root.begin();
  auto a = at.begin();
  for (; r != root.end(); ++r, ++a) {
    if (a == at.end() || *a != *r) return false;
  }
  return true;
}

// The README's first line without its hashes, or empty.
std::string readmeTitle(const fs::path& dir) {
  std::ifstream in(dir / "README.md");
  std::string first;
  if (!std::getline(in, first)) return "";
  while (!first.empty() && (first.front() == '#' || first.front() == ' ')) first.erase(first.begin());
  return first;
}

}  // namespace

void Ide::readExamples() {
  examples_.clear();
  std::error_code ec;
  for (const project::ExampleKind& k : project::EXAMPLE_KINDS) {
    ExampleGroup g{k.label, {}};
    for (const auto& entry : fs::directory_iterator(examplesRoot() / k.folder, ec)) {
      if (!entry.is_directory()) continue;
      g.items.push_back({entry.path().filename().string(), readmeTitle(entry.path()), entry.path().string()});
    }
    std::sort(g.items.begin(), g.items.end(), [](const Example& a, const Example& b) { return a.name < b.name; });
    if (!g.items.empty()) examples_.push_back(std::move(g));
  }
}

bool Ide::refuseExample(const std::string& what) {
  if (!example_) return false;
  note("not " + what + ": " + projectTitle_ + " is an example and stays as it is. File, Save project as keeps a "
       "copy of your own.");
  return true;
}

void Ide::newScratchProject() {
  // A sprite belongs to the project it came from, so it closes with it.
  sprite_.close();
  projectDir_.clear();
  example_ = false;
  projectTitle_ = "scratch";
  romBase_.reset();
  romFiles_.clear();
  carriedProject_ = false;
  romPath_.clear();
  docs_.clear();
  filesChanged_ = false;
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
void Ide::askRemove(const std::string& name, bool asset) {
  removing_ = name;
  removingAsset_ = asset;
}

namespace {

// A file in a flat folder that is an asset rather than the project's
// own: not a source, not the README, not a built ROM, not hidden.
bool flatAsset(const fs::path& p) {
  const std::string name = p.filename().string();
  if (name.empty() || name[0] == '.') return false;
  if (docKindOf(name) != DocKind::Other) return false;
  const std::string ext = p.extension().string();
  return name != "README.md" && ext != ".rom";
}

std::optional<std::vector<uint8_t>> readBytes(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return std::nullopt;
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

std::vector<Ide::AssetEntry> Ide::assetList() const {
  std::vector<AssetEntry> out;
  if (!projectDir_.empty()) {
    const project::Layout l = layout();
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(l.assets, ec)) {
      if (!e.is_regular_file(ec)) continue;
      if (l.nested ? e.path().filename().string().rfind('.', 0) == 0 : !flatAsset(e.path())) continue;
      out.push_back({e.path().filename().string(), e.file_size(ec)});
    }
  } else {
    for (const auto& [name, bytes] : romFiles_) {
      if (name.rfind("assets/", 0) == 0) out.push_back({name.substr(7), bytes.size()});
    }
  }
  std::sort(out.begin(), out.end(), [](const AssetEntry& a, const AssetEntry& b) { return a.name < b.name; });
  return out;
}

void Ide::addAsset(const std::string& path) {
  if (refuseExample("added")) return;
  const fs::path from(path);
  const std::string name = from.filename().string();
  for (const AssetEntry& a : assetList()) {
    if (a.name == name) {
      note(name + " is already an asset of the project: remove it first to replace it");
      return;
    }
  }
  if (docKindOf(name) != DocKind::Other) {
    note(name + " is a source file: add it with the field under the file list");
    return;
  }
  std::error_code ec;
  if (!projectDir_.empty()) {
    const fs::path dir = layout().assets;
    fs::create_directories(dir, ec);
    if (!fs::copy_file(from, dir / name, ec)) {
      note("cannot copy " + path + (ec ? ": " + ec.message() : ""));
      return;
    }
    note("added " + (dir / name).string());
    return;
  }
  auto bytes = readBytes(from);
  if (!bytes) {
    note("cannot read " + path);
    return;
  }
  romFiles_.push_back({"assets/" + name, std::move(*bytes)});
  filesChanged_ = true;
  note("added " + name + " to the project; the ROM carries it after the next save");
}

void Ide::removeAsset(const std::string& name) {
  if (refuseExample("removed")) return;
  if (sprite_.isOpen() && sprite_.name() == name) sprite_.close();
  if (!projectDir_.empty()) {
    const fs::path file = layout().assets / name;
    std::error_code ec;
    if (!fs::remove(file, ec)) {
      note("cannot delete " + file.string() + (ec ? ": " + ec.message() : ""));
      return;
    }
    note("deleted " + file.string());
    return;
  }
  const auto it = std::find_if(romFiles_.begin(), romFiles_.end(),
                               [&](const auto& f) { return f.first == "assets/" + name; });
  if (it == romFiles_.end()) return;
  romFiles_.erase(it);
  filesChanged_ = true;
  note(name + " left the project");
}

void Ide::removeDoc(const std::string& name) {
  const auto it = std::find_if(docs_.begin(), docs_.end(), [&](const Doc& d) { return d.name == name; });
  if (it == docs_.end()) return;
  // A document added to an example lives only in memory, and goes freely.
  std::error_code exists;
  if (fs::exists(layout().sources / name, exists) && refuseExample("removed")) return;
  if (!projectDir_.empty()) {
    const fs::path file = layout().sources / name;
    std::error_code ec;
    if (fs::exists(file, ec) && !fs::remove(file, ec)) {
      note("cannot delete " + file.string() + (ec ? ": " + ec.message() : ""));
      return;
    }
    note("deleted " + file.string());
  } else {
    note(name + " left the project");
    filesChanged_ = true;
  }
  const size_t index = static_cast<size_t>(it - docs_.begin());
  docs_.erase(it);
  if (syncDoc_ == name) syncDoc_.clear();
  if (active_ > index || active_ >= docs_.size()) active_ = docs_.empty() ? 0 : active_ - (active_ > 0 ? 1 : 0);
  if (!docs_.empty()) activate(active_);
}

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
  sprite_.close();
  projectDir_ = fs::absolute(dir).string();
  example_ = insideExamples(projectDir_);
  projectTitle_ = l.name;
  romBase_.reset();
  romFiles_.clear();
  carriedProject_ = false;
  romPath_.clear();
  docs_.clear();
  filesChanged_ = false;
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
  if (example_) note(projectTitle_ + " is an example: it stays as it is. File, Save project as keeps your copy.");
  levelForProject();
}

void Ide::levelForProject() {
  const bool allBasic = !docs_.empty() && std::all_of(docs_.begin(), docs_.end(), [](const Doc& d) {
    return docKindOf(d.name) == DocKind::Basic;
  });
  if (allBasic) setLevel(Level::Basic);
  else if (level_ == Level::Basic) setLevel(Level::Project);
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
  sprite_.close();
  projectDir_.clear();
  example_ = false;
  romPath_ = fs::absolute(path).string();
  projectTitle_ = fs::path(path).stem().string();
  for (const auto& [k, v] : r.cartridge->meta) {
    if (k == "title" && !v.empty()) projectTitle_ = v;
  }
  docs_.clear();
  filesChanged_ = false;
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
  levelForProject();
}

void Ide::createProject(const std::string& dir, project::Kind kind) {
  if (insideExamples(dir)) {
    note("not created: " + dir + " is inside the examples, which stay as they are");
    return;
  }
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
// gets a home. A project that has a folder already brings its README and
// its assets along: they are part of the project as much as the sources.
void Ide::saveProjectAs(const std::string& dir) {
  const fs::path root = fs::absolute(dir);
  if (insideExamples(root)) {
    note("not saved: " + root.string() + " is inside the examples, which stay as they are. Pick a folder of your own");
    return;
  }
  std::error_code ec;
  fs::create_directories(root / "src", ec);
  fs::create_directories(root / "assets", ec);
  if (!projectDir_.empty()) {
    const project::Created c = project::copyProjectFiles(layout(), root);
    for (const fs::path& f : c.files) note("wrote " + f.string());
    if (!c.error.empty()) {
      note(c.error + ". The project stays where it was.");
      return;
    }
  }
  if (!fs::exists(root / "README.md", ec)) std::ofstream(root / "README.md") << "# " << root.filename().string() << "\n";
  if (!fs::exists(root / ".gitignore", ec)) std::ofstream(root / ".gitignore") << "build/\n";
  projectDir_ = root.string();
  example_ = false;
  projectTitle_ = root.filename().string();
  for (Doc& d : docs_) saveDoc(d);
  if (sprite_.dirty()) sprite_.save();
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
  filesChanged_ = false;
  note("project saved in " + projectDir_);
}

bool Ide::layOut(Doc& doc) {
  if (docKindOf(doc.name) != DocKind::Assembly) return false;
  std::string laid = formatAssembly(doc.text);
  if (laid == doc.text) return false;
  doc.text = std::move(laid);
  // A text box that is being edited keeps its own copy of the text and
  // would write it back over the new one.
  if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetActiveID())) {
    state->ReloadUserBufAndKeepSelection();
  }
  return true;
}

void Ide::saveDoc(Doc& doc) {
  if (refuseExample("saved")) return;
  if (projectDir_.empty()) {
    note("the project has no folder yet: use File, Save project as");
    return;
  }
  if (settings_.formatAssembly) layOut(doc);
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
  // The sprite too, so a build reads the strip as it is drawn.
  if (sprite_.dirty()) sprite_.save();
}

// Save is the whole project: every changed document into the folder. A
// project with no folder yet asks for one first, and every document
// goes there.
std::string Ide::saveTarget() const {
  if (!projectDir_.empty()) return "Save project";
  if (!romPath_.empty()) return "Save into " + fs::path(romPath_).filename().string() + (romSaveConfirmed_ ? "" : "...");
  return "Save project...";
}

// The ROM is the project in one file, so saving writes the ROM again from
// the documents. A build error leaves the file as it was and the
// documents unsaved. The machine keeps running what it runs: Build puts
// the new ROM in.
bool Ide::saveIntoRom() {
  if (settings_.formatAssembly) {
    for (Doc& d : docs_) {
      if (d.dirty) layOut(d);
    }
  }
  project::Built built = buildInMemory();
  for (const std::string& n : built.notes) note(n);
  reportErrors(built.errors);
  if (!built.cartridge) {
    note("not saved: " + fs::path(romPath_).filename().string() + " is left as it was until the build succeeds");
    return false;
  }
  const std::vector<uint8_t> bytes = encodeCartridge(*built.cartridge);
  std::ofstream o(romPath_, std::ios::binary);
  o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!o) {
    note("cannot write " + romPath_);
    return false;
  }
  for (Doc& d : docs_) d.dirty = false;
  filesChanged_ = false;
  note("saved into " + romPath_ + " (" + std::to_string(bytes.size()) + " bytes)");
  return true;
}

void Ide::saveProject() {
  // An example's Save is disabled, and Ctrl+S says why.
  if (refuseExample("saved")) return;
  if (projectDir_.empty() && !romPath_.empty()) {
    if (romSaveConfirmed_) saveIntoRom();
    else askRomSave_ = true;
    return;
  }
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
  if (filesChanged_ || sprite_.dirty()) return true;
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
  if (example_) {
    // The open documents, with the example's own assets and README. The
    // ROM stays in memory, so nothing lands in the example's build/.
    built = project::build(layout(), sources(), o);
  } else if (!projectDir_.empty()) {
    saveAll();
    project::Written w = project::buildAndWrite(layout(), o);
    built = std::move(w.built);
    if (built.cartridge) note("wrote " + w.rom.string());
  } else {
    built = buildInMemory();
  }
  for (const std::string& n : built.notes) note(n);
  reportErrors(built.errors);
  if (!built.cartridge) return;
  insertBuilt(built, projectTitle_);
  if (run) setRunning(true);
}

project::Built Ide::buildInMemory() {
  // The sprite being drawn is one of the assets the build reads.
  if (sprite_.dirty()) sprite_.save();
  std::vector<std::string> notes;
  // A carried project reads its pictures and sounds from the ROM and
  // builds the program again; a bare ROM keeps its program.
  Cartridge carrier;
  carrier.sources = romFiles_;
  // A scratch project's assets live in memory too, so they come from the
  // same list a carried project reads.
  const bool inMemory = carriedProject_ || !romBase_;
  Assets assets = inMemory ? project::loadersFrom(carrier, &notes) : project::loaders(layout(), &notes);
  // The asset files by name, which a project with BASIC places on the
  // cartridge for STO_FIND.
  std::vector<std::string> assetNames;
  if (inMemory) {
    for (const auto& [name, bytes] : romFiles_) {
      if (name.starts_with("assets/")) assetNames.push_back(name.substr(7));
    }
  } else {
    for (const std::filesystem::path& p : project::assetFiles(layout())) assetNames.push_back(p.filename().string());
  }
  project::Built built = project::buildSources(sources(), assets, project::Options{}, projectTitle_, "",
                                               carriedProject_ ? std::optional<Cartridge>{} : romBase_, assetNames);
  built.notes.insert(built.notes.begin(), notes.begin(), notes.end());
  // A ROM without a project keeps its program, and its slots and
  // microcode are not a project that builds on its own. Carried in the
  // SRC chunk, they would open next time as one without the program.
  const bool bareRom = romBase_.has_value() && !carriedProject_;
  if (built.cartridge && !bareRom) project::embed(*built.cartridge, sources(), romFiles_);
  return built;
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
  // A new ROM or a fresh interpreter: the document is the partner that
  // brings its program in, not the empty memory that wipes it.
  syncDoc_.clear();
  syncBase_.clear();
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

void Ide::writeProgram(const std::vector<uint8_t>& bytes) {
  basic::storeProgram(computer_.machine().ram, bytes);
}

bool Ide::pushProgram() {
  if (!basicAtReady()) return false;
  Doc* d = nullptr;
  for (Doc& doc : docs_) {
    if (doc.name == syncDoc_) d = &doc;
  }
  if (!d) return false;
  std::vector<uint8_t> bytes = basic::encodeProgram(d->text);
  writeProgram(bytes);
  syncBase_ = std::move(bytes);
  return true;
}

void Ide::setDocText(Doc& doc, std::string text) {
  doc.text = std::move(text);
  if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetActiveID())) {
    state->ReloadUserBufAndKeepSelection();
  }
}

// The document in step with the interpreter: the .bas document in the
// editor, else the one last in step, else autorun.bas, which a built
// BASIC project boots into.
Doc* Ide::basicPartner() {
  Doc* active = activeDoc();
  if (active && docKindOf(active->name) == DocKind::Basic) return active;
  for (Doc& d : docs_) {
    if (d.name == syncDoc_) return &d;
  }
  for (Doc& d : docs_) {
    if (d.name == "autorun.bas") return &d;
  }
  return nullptr;
}

// Once per frame while BASIC waits at READY. A pending push from Run in
// BASIC goes first. Then the memory and the document are compared with
// the program they last agreed on: a side that moved carries the other
// along, and two sides that both moved are merged line by line.
void Ide::syncBasic() {
  if (!basicAtReady()) return;
  Doc* d = basicPartner();
  // A line BASIC would misread stops the exchange both ways until it is
  // changed, and a Run in BASIC waiting on it is dropped. Syncing the rest
  // would take the line out of the document at the next merge.
  const std::string why = d ? basic::refusal(d->text) : "";
  if (!why.empty()) {
    if (why != refusalNoted_) note(d->name + ": " + why);
    refusalNoted_ = why;
    pushPending_ = false;
    return;
  }
  refusalNoted_.clear();
  if (pushPending_) {
    if (d && syncDoc_.empty()) syncDoc_ = d->name;
    if (!pushProgram()) return;
    pushPending_ = false;
    typing_ = afterPush_;
    typingPos_ = 0;
    return;
  }
  if (!d) return;
  const auto& ram = computer_.machine().ram;
  const size_t prog = static_cast<size_t>((ram[basic::SYS_PROG] << 8) | ram[basic::SYS_PROG + 1]);
  const size_t len = static_cast<size_t>((ram[basic::SYS_PROG_LEN] << 8) | ram[basic::SYS_PROG_LEN + 1]);
  if (len < 3 || len > basic::PROGRAM_MAX || prog + len > ram.size()) return;
  const auto first = ram.begin() + static_cast<std::ptrdiff_t>(prog);
  const std::vector<uint8_t> machine(first, first + static_cast<std::ptrdiff_t>(len));
  // A document that just became the partner brings its program along.
  if (d->name != syncDoc_) {
    syncDoc_ = d->name;
    syncBase_ = machine;
  }
  const std::vector<uint8_t> doc = basic::encodeProgram(d->text);
  if (doc == machine) {
    syncBase_ = machine;
    return;
  }
  const bool docMoved = doc != syncBase_;
  const bool machineMoved = machine != syncBase_;
  const std::vector<uint8_t> result = !machineMoved ? doc
                                      : !docMoved   ? machine
                                                    : basic::mergePrograms(syncBase_, doc, machine);
  if (result != machine) writeProgram(result);
  if (result != doc) {
    setDocText(*d, basic::patchText(d->text, result));
    d->dirty = true;
    // NEW typed on the computer empties the document too. The file keeps
    // the old program until the next save, and the person should know.
    if (result.size() <= 3 && doc.size() > 3) {
      note("NEW on the computer emptied " + d->name + ". The file on disk keeps the old program until you save.");
    }
  }
  syncBase_ = result;
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

void Ide::filesPane(const char* name, bool* open) {
  if (focusFiles_) {
    ImGui::SetNextWindowFocus();
    focusFiles_ = false;
  }
  ImGui::Begin(name, open);
  const std::string where = projectDir_.empty() ? (romBase_ ? "a ROM, not saved as a folder yet" : "not saved yet")
                                                : projectDir_;
  ImGui::TextWrapped("%s", projectTitle_.c_str());
  ImGui::TextDisabled("%s", where.c_str());
  if (ImGui::SmallButton("Build")) buildProject(false);
  ImGui::SameLine();
  if (ImGui::SmallButton("Build and run")) buildProject(true);
  ImGui::BeginDisabled(example_);
  if (ImGui::SmallButton("Save")) saveProject();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::SmallButton("Boot BASIC")) bootBasic();
  ImGui::SameLine();
  if (activeDoc() && ImGui::SmallButton("Remove")) askRemove(activeDoc()->name);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("remove the selected file from the project; a right click on a file does too");
  ImGui::Separator();
  for (size_t i = 0; i < docs_.size(); i++) {
    const Doc& d = docs_[i];
    const std::string label = d.name + (d.dirty ? " *" : "");
    if (ImGui::Selectable(label.c_str(), i == active_)) activate(i);
    if (ImGui::BeginPopupContextItem()) {
      if (ImGui::MenuItem(("Remove " + d.name + "...").c_str())) askRemove(d.name);
      ImGui::EndPopup();
    }
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
  // The assets: pictures, sounds and any file the sources name.
  ImGui::Spacing();
  ImGui::SeparatorText("Assets");
  const std::vector<AssetEntry> assets = assetList();
  if (assets.empty()) ImGui::TextDisabled("none yet");
  for (const AssetEntry& a : assets) {
    ImGui::PushID(a.name.c_str());
    const bool picture = isDrawable(a.name);
    const bool isFontAsset = SpriteEditor::isFont(a.name);
    const char* editor = isFontAsset ? "font editor" : "sprite editor";
    const bool editing = sprite_.isOpen() && sprite_.name() == a.name;
    if (ImGui::Selectable(a.name.c_str(), editing, ImGuiSelectableFlags_AllowDoubleClick) && picture &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      openSprite(a.name);
    }
    if (ImGui::IsItemHovered() && isFontAsset) {
      const std::string stem = fs::path(a.name).stem().string();
      ImGui::SetTooltip("%s, %ju bytes. C names it as __font(\"%s\"), assembly as .font('%s'), BASIC as "
                        "LOADFONT %s. A double click opens it in the font editor.",
                        a.name.c_str(), a.bytes, a.name.c_str(), a.name.c_str(), stem.c_str());
    } else if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s, %ju bytes. C names it as __sprite(\"%s\"), __image, __sample or __file; "
                        "assembly as .sprite('%s'), .image, .sample or .file.%s",
                        a.name.c_str(), a.bytes, a.name.c_str(), a.name.c_str(),
                        picture ? " A double click opens it in the sprite editor." : "");
    }
    if (ImGui::BeginPopupContextItem()) {
      if (picture && ImGui::MenuItem((std::string("Edit in the ") + editor).c_str())) openSprite(a.name);
      if (ImGui::MenuItem(("Remove " + a.name + "...").c_str())) askRemove(a.name, true);
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }
  if (ImGui::SmallButton("Add asset...")) {
    dialog_.open(FileDialog::Mode::OpenFile, "Add a file to the project's assets", fs::current_path(), {},
                 [this](const fs::path& p) { addAsset(p.string()); });
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("copy a picture, a sound or any file into the project; a right click on an asset removes it");
  ImGui::SameLine();
  if (ImGui::SmallButton("New sprite...")) askNewSprite();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("draw a new sprite strip, saved as a PNG under Assets");
  {
    // Beside New sprite when the pane is wide enough, under it otherwise.
    const ImGuiStyle& st = ImGui::GetStyle();
    const float w = ImGui::CalcTextSize("New font...").x + st.FramePadding.x * 2.0f;
    if (ImGui::GetItemRectMax().x + st.ItemSpacing.x + w <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) {
      ImGui::SameLine();
    }
  }
  if (ImGui::SmallButton("New font...")) askNewFont_ = true;
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("a copy of the built-in font as a .font under Assets, opened in the font editor");
  }

  if (romBase_ && !carriedProject_) {
    ImGui::Spacing();
    ImGui::TextWrapped("The ROM carries no project. Its program is not a document and its listing is a disassembly.");
  } else if (carriedProject_) {
    ImGui::Spacing();
    ImGui::TextWrapped("The project came out of the ROM. Save writes it back into the ROM file. "
                       "Save project as writes it to a folder.");
  }
  ImGui::End();
}

// The editor shows the active document with the buttons its kind needs.
// A .bas document has the interpreter beside it: Run in BASIC, the sync
// state and Pull.
void Ide::editorPane(const char* name) {
  ImGui::Begin(name);
  Doc* d = activeDoc();
  if (!d) {
    ImGui::TextDisabled("no document: add one in the Files pane, or open a project");
    ImGui::End();
    return;
  }
  const DocKind kind = docKindOf(d->name);
  ImGui::Text("%s%s", d->name.c_str(), d->dirty ? " (unsaved)" : "");
  ImGui::SameLine();
  ImGui::BeginDisabled(example_);
  if (ImGui::SmallButton("Save")) saveProject();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::SmallButton("Build")) buildProject(false);
  if (kind == DocKind::Basic) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Run in BASIC")) runInBasic();
    ImGui::SameLine();
    if (basicAtReady() && syncDoc_ == d->name) {
      ImGui::TextDisabled("in step with the computer: LIST shows this text, and lines typed there show up here");
    } else if (basicBooted_ && computer_.machine().status == Status::Running) {
      ImGui::TextDisabled("a program is running: changes go across when it stops at READY");
    } else {
      ImGui::TextDisabled("Run in BASIC boots the interpreter; from then on this text and the computer stay in step");
    }
    ImGui::SetItemTooltip("Enter at the end of a numbered line numbers the next one.\n"
                          "Enter on a bare number takes it away again.\n"
                          "%s-I or Shift+Enter opens a numbered line under the caret's line.\n"
                          "A line typed out of order moves to its place when the caret leaves it.\n"
                          "Its BASIC words turn to capitals then, as the computer stores them.",
                          ImGui::GetIO().ConfigMacOSXBehaviors ? "Cmd" : "Ctrl");
  } else if (kind == DocKind::Microcode) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Apply to machine")) {
      McParsed parsed = parseMicrocode(d->text);
      reportErrors(microcodeErrors(parsed));
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
        reportErrors(microcodeErrors(parsed));
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
  } else if (kind == DocKind::Assembly) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Format")) {
      if (layOut(*d)) d->dirty = true;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("mnemonics in one column, operands in the next, long labels on a line of their own");
    }
    ImGui::SameLine();
    ImGui::TextDisabled(settings_.formatAssembly ? "laid out in columns on every save" : "Format lays it out in columns");
  }
  if (kind == DocKind::Basic) {
    if (assistDoc_ != d->name) {
      basicAssist_.reset();
      assistDoc_ = d->name;
    }
    if (codeEditor("##doc", d->text, Syntax::Basic, ImVec2(-1.0f, -1.0f),
                   ImGuiInputTextFlags_AllowTabInput | BasicAssist::FLAGS, BasicAssist::callback, &basicAssist_)) {
      d->dirty = true;
    }
    // Whenever the box is not being typed in, the text stands in number
    // order with its reserved words in capitals: a file opened out of
    // order, lines typed on the machine's screen, the editor just left.
    if (!ImGui::IsItemActive() && BasicAssist::sortText(d->text)) d->dirty = true;
    if (std::string n = basicAssist_.takeNote(); !n.empty()) note(n);
  } else if (codeEditor("##doc", d->text, syntaxOf(d->name), ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_AllowTabInput)) {
    d->dirty = true;
  }
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
    codeEditor("##asm", builtAssembly_, Syntax::Assembly, ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_ReadOnly);
  }
  ImGui::End();
}

}  // namespace sc8
