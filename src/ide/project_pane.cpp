// The project: its documents, each view's editor, the build, and the
// BASIC bridge between the active .bas document and the interpreter.
// project_column.cpp draws the project's files.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "imgui.h"
#include "imgui_internal.h"

#include "asm/format.h"
#include "basic/basic_rom.h"
#include "basic/lines.h"
#include "basic/program.h"
#include "core/cartridge.h"
#include "core/mcparse.h"
#include "ide/highlight.h"
#include "ide/ide.h"
#include "ide/panes.h"
#include "project/text_file.h"

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

// ---- the project

namespace {

// The installed examples in the settings folder. A build from the
// checkout reads them there too, so it shows what an install shows.
fs::path examplesRoot() { return Settings::examplesDir(); }

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
      // A libraries folder beside the examples holds libraries, not a project.
      if (!entry.is_directory() || entry.path().filename() == project::LIBRARIES_FOLDER) continue;
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
  forgetSourceBreaks();
  assetsStale_ = true;
  filesChanged_ = false;
  addDoc("main.asm", DEFAULT_SOURCE);
  docs_.back().dirty = false;
  settleActive();
  buildProject(false);
  viewForProject();
}

void Ide::addDoc(const std::string& name, const std::string& text) {
  Doc d;
  d.name = name;
  d.text = text;
  d.text.reserve(d.text.size() + (1 << 16));
  d.dirty = projectDir_.empty();
  docs_.push_back(std::move(d));
  sortDocs();
}

// The project's own documents by name, then the libraries by name.
void Ide::sortDocs() {
  std::sort(docs_.begin(), docs_.end(), [](const Doc& a, const Doc& b) {
    return a.library != b.library ? b.library : a.name < b.name;
  });
}

Doc* Ide::docNamed(const std::string& name) {
  for (Doc& d : docs_) {
    if (d.name == name) return &d;
  }
  return nullptr;
}

Doc* Ide::activeDocOf(View view) {
  Doc* d = docNamed(activeName_[static_cast<int>(view)]);
  return d && d->open ? d : nullptr;
}

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
      // A folder that holds the sources too, flat or a src/ with no
      // assets/ beside it, lists only what is not a source.
      const bool own = l.assets != l.sources;
      if (own ? e.path().filename().string().rfind('.', 0) == 0 : !flatAsset(e.path())) continue;
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
  assetsStale_ = true;
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
  assetsStale_ = true;
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
  docs_.erase(it);
  settleActive();
}

void Ide::activate(size_t index) {
  if (index >= docs_.size()) return;
  Doc& d = docs_[index];
  const std::optional<View> view = viewOfKind(docKindOf(d.name));
  if (!view) return;
  d.open = true;
  activeName_[static_cast<int>(*view)] = d.name;
  selectTab_[static_cast<int>(*view)] = d.name;
}

void Ide::openDoc(size_t index) {
  if (index >= docs_.size()) return;
  activate(index);
  if (const std::optional<View> view = viewOfKind(docKindOf(docs_[index].name))) setView(*view);
  focusAfterLayout_ = pane("Editor");
}

// A view whose active document is gone or closed takes another of its
// open documents: main.c, main.asm or autorun.bas when it has one, since
// that is where a reader starts, else the first by name.
void Ide::settleActive() {
  for (int v = 0; v < VIEW_COUNT; v++) {
    if (activeDocOf(static_cast<View>(v))) continue;
    activeName_[v].clear();
    for (const Doc& d : docs_) {
      if (!d.open || viewOfKind(docKindOf(d.name)) != static_cast<View>(v)) continue;
      const bool main = d.name == "main.c" || d.name == "main.asm" || d.name == "autorun.bas";
      if (activeName_[v].empty() || main) activeName_[v] = d.name;
    }
    selectTab_[v] = activeName_[v];
  }
}

bool Ide::newDoc(const std::string& name) {
  if (name.empty()) return false;
  if (docKindOf(name) == DocKind::Other) {
    note(name + ": a source is a .c, .h, .asm or .bas file, or microcode.txt");
    return false;
  }
  if (!docNamed(name)) {
    std::string text;
    if (name == "microcode.txt") text = "# your microcode set, started from the naive one\n\n" + serializeMicrocode(buildNaive());
    addDoc(name, text);
    docNamed(name)->dirty = true;
  }
  for (size_t i = 0; i < docs_.size(); i++) {
    if (docs_[i].name == name) openDoc(i);
  }
  return true;
}

project::Layout Ide::layout() const {
  return project::layoutOf(projectDir_.empty() ? fs::current_path() : fs::path(projectDir_));
}

std::vector<project::Source> Ide::sources() const {
  // A library is the libraries folder's, not the project's.
  std::vector<project::Source> out;
  for (const Doc& d : docs_) {
    if (!d.library) out.push_back({d.name, d.text});
  }
  return out;
}

void Ide::openProject(const std::string& given) {
  // The folder in its one spelling, whatever the command line or the
  // dialog wrote: its parent is then the folder it sits in.
  const std::string dir = project::normalFolder(fs::absolute(given)).string();
  const project::Layout l = project::layoutOf(dir);
  std::error_code ec;
  if (!fs::is_directory(l.sources, ec)) {
    note(dir + " is not a folder");
    return;
  }
  sprite_.close();
  projectDir_ = dir;
  example_ = insideExamples(projectDir_);
  projectTitle_ = l.name;
  romBase_.reset();
  romFiles_.clear();
  carriedProject_ = false;
  romPath_.clear();
  docs_.clear();
  forgetSourceBreaks();
  assetsStale_ = true;
  filesChanged_ = false;
  syncBases_.clear();
  for (const auto& entry : fs::directory_iterator(l.sources, ec)) {
    if (!entry.is_regular_file()) continue;
    const std::string name = entry.path().filename().string();
    if (docKindOf(name) == DocKind::Other) continue;
    if (auto t = readText(entry.path())) addDoc(name, *t);
  }
  for (Doc& d : docs_) d.dirty = false;
  if (docs_.empty()) note(l.sources.string() + " holds no source yet: New file in the project column adds one");
  settleActive();
  buildProject(false);
  note("opened project " + projectTitle_ + " in " + projectDir_);
  if (example_) note(projectTitle_ + " is an example: it stays as it is. File, Save project as keeps your copy.");
  viewForProject();
}

void Ide::viewForProject() {
  if (docs_.empty()) return;
  if (docNamed("microcode.txt")) {
    setView(View::Microcode);
    return;
  }
  for (const char* name : {"main.c", "main.asm", "autorun.bas"}) {
    if (!docNamed(name)) continue;
    setView(*viewOfKind(docKindOf(name)));
    return;
  }
  setView(*viewOfKind(docKindOf(docs_.front().name)));
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
  forgetSourceBreaks();
  assetsStale_ = true;
  filesChanged_ = false;
  syncBases_.clear();
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
  }
  settleActive();
  buildProject(false);
  note("opened ROM " + path + (carriedProject_ ? " with its project: " : " as a project: ") +
       std::to_string(docs_.size()) + " document(s)" + (carriedProject_ ? "" : ", the program as it is"));
  viewForProject();
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
  const fs::path root = project::normalFolder(fs::absolute(dir));
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
  if (!fs::exists(root / "README.md", ec)) writeText(root / "README.md", "# " + root.filename().string() + "\n");
  if (!fs::exists(root / ".gitignore", ec)) writeText(root / ".gitignore", "build/\n");
  projectDir_ = root.string();
  example_ = false;
  projectTitle_ = root.filename().string();
  assetsStale_ = true;
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
  // A library is saved into its own file in the libraries folder.
  if (doc.library) {
    if (writeText(doc.path, doc.text)) {
      doc.dirty = false;
      note("saved the library " + doc.path);
    } else {
      note("cannot write " + doc.path);
    }
    return;
  }
  if (refuseExample("saved")) return;
  if (projectDir_.empty()) {
    note("the project has no folder yet: use File, Save project as");
    return;
  }
  if (settings_.formatAssembly) layOut(doc);
  const project::Layout l = layout();
  std::error_code ec;
  fs::create_directories(l.sources, ec);
  if (writeText(l.sources / doc.name, doc.text)) {
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
  builtRom_.clear();
  lastBuildOk_ = false;
  project::Options o;
  o.libraries = libraryFolders();
  // A library is built from its file, so an edit in its tab goes there
  // first.
  for (Doc& d : docs_) {
    if (d.library && d.dirty) saveDoc(d);
  }
  project::Built built;
  if (example_) {
    // The open documents, with the example's own assets and README. The
    // ROM stays in memory, so nothing lands in the example's build/.
    built = project::build(layout(), sources(), o);
  } else if (!projectDir_.empty()) {
    saveAll();
    project::Written w = project::buildAndWrite(layout(), o);
    built = std::move(w.built);
    if (built.cartridge) {
      builtRom_ = w.rom.string();
      note("wrote " + w.rom.string());
    }
  } else {
    built = buildInMemory();
  }
  for (const std::string& n : built.notes) note(n);
  reportErrors(built.errors);
  librariesStale_ = true;
  if (!built.cartridge) return;
  lastBuildOk_ = true;
  insertBuilt(built, projectTitle_);
  if (run) {
    setRunning(true);
    showRunTab();
  }
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
  project::Options o;
  o.libraries = libraryFolders();
  project::Built built = project::buildSources(sources(), assets, o, projectTitle_, "",
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
  lineMap_ = std::move(built.lines);
  pcLines_ = project::PcLines(lineMap_, assembled_);
  stepLine_.clear();
  for (const project::SourceLine& line : pcLines_.lines()) {
    const DocKind kind = docKindOf(line.file);
    stepLine_.push_back((kind == DocKind::C || kind == DocKind::Header) && docNamed(line.file) ? 1 : 0);
  }
  builtTexts_.clear();
  for (const Doc& d : docs_) builtTexts_[d.name] = d.text;
  placeSourceBreaks();
  // A ROM with BASIC slots runs the interpreter: a BASIC or mixed project
  // puts it first, and an opened ROM with slots came from one.
  const bool hasBasic = !built.cartridge->basic.empty();
  const bool interpreter = hasBasic && (romBase_.has_value() || built.sources.empty() ||
                                        built.sources.front() == "the BASIC interpreter");
  loadCartridge(std::move(*built.cartridge), what);
  basicBooted_ = interpreter;
  refreshWatch();
  if (built.instructions > 0) {
    note("built " + what + ": " + std::to_string(built.instructions) + " instructions, " +
         std::to_string(built.ramBytes) + " bytes of RAM, " + std::to_string(built.dataBytes) + " bytes of data");
  }
}

void Ide::loadCartridge(Cartridge cart, const std::string& what) {
  setRunning(false);
  basicBooted_ = false;
  pushPending_ = false;
  // A new ROM or a fresh interpreter: the documents bring their programs
  // in, not the empty memory that wipes them.
  syncBases_.clear();
  shownEdit_ = -1;
  lastBasicTab_.clear();
  basicLines_.reset();
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

// ---- the panes

// A view's editor: one tab per open document of the view's kind, and
// under the tabs the active document. A tab's close button takes the tab
// away and leaves the document in the project.
void Ide::editorPane(View view) {
  const int v = static_cast<int>(view);
  if (!ImGui::Begin(paneName("Editor", view).c_str())) {
    ImGui::End();
    return;
  }
  // A tab asked for from elsewhere is selected by Dear ImGui one frame
  // later. Until then the tab it shows is not what the person picked.
  const bool pending = !selectTab_[v].empty();
  std::string picked;
  if (ImGui::BeginTabBar("docs", ImGuiTabBarFlags_FittingPolicyScroll)) {
    for (Doc& d : docs_) {
      if (!d.open || viewOfKind(docKindOf(d.name)) != view) continue;
      ImGuiTabItemFlags flags = d.dirty ? ImGuiTabItemFlags_UnsavedDocument : 0;
      if (pending && selectTab_[v] == d.name) flags |= ImGuiTabItemFlags_SetSelected;
      bool open = true;
      // The ### keeps one tab whatever the label says.
      const std::string label = d.name + (d.library ? " (library)" : "") + "###" + d.name;
      if (ImGui::BeginTabItem(label.c_str(), &open, flags)) {
        if (!pending) picked = d.name;
        ImGui::EndTabItem();
      }
      if (d.library && ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Edit library", nullptr, !d.readOnly)) d.readOnly = !d.readOnly;
        ImGui::EndPopup();
      }
      if (!open) d.open = false;
    }
    ImGui::EndTabBar();
    selectTab_[v].clear();
  }
  if (!picked.empty()) activeName_[v] = picked;
  if (!activeDocOf(view)) settleActive();
  Doc* d = activeDocOf(view);
  if (!d) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("no %s document is open. New file in the project column adds one.", viewLabel(view));
    ImGui::PopStyleColor();
    if (view == View::Microcode && !docNamed("microcode.txt")) {
      if (ImGui::SmallButton("Start microcode.txt from the naive set")) newDoc("microcode.txt");
    }
    ImGui::End();
    return;
  }
  // Each document is its own text box, with its own scroll and undo.
  ImGui::PushID(d->name.c_str());
  editorBody(*d);
  ImGui::PopID();
  ImGui::End();
}

// Save, the Run menu's items, and what a kind of document adds. The
// listing shows the same row without a document. Run and Step work
// there, and the buttons that act on a document are disabled.
void Ide::viewButtons(DocKind kind, Doc* doc) {
  ImGui::BeginDisabled(!doc || example_);
  if (ImGui::SmallButton("Save")) saveProject();
  ImGui::EndDisabled();
  // The Run menu's items for this view, a click away from the text.
  ImGui::SameLine();
  const bool going = runGoing();
  if (ImGui::SmallButton(going ? "Pause" : "Run")) {
    if (going) setRunning(false);
    else viewRun();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!doc);
  if (ImGui::SmallButton("Check")) viewCheck();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::SmallButton("Step")) viewStep();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s, F10", stepLabel());
  ImGui::BeginDisabled(!doc);
  if (kind == DocKind::Microcode) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Apply to machine")) applyProjectMicrocode(false);
    ImGui::SameLine();
    if (ImGui::SmallButton(lockedMicrocode_ ? "Unlock" : "Apply and lock")) {
      if (lockedMicrocode_) toggleMicrocodeLock();
      else applyProjectMicrocode(true);
    }
  } else if (kind == DocKind::Assembly) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Format") && doc) {
      if (layOut(*doc)) doc->dirty = true;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("mnemonics in one column, operands in the next, long labels on a line of their own");
    }
  }
  ImGui::EndDisabled();
}

// The active document with the buttons its kind needs. A .bas document
// has the interpreter beside it: Run in BASIC and the sync state.
void Ide::editorBody(Doc& doc) {
  Doc* d = &doc;
  const DocKind kind = docKindOf(d->name);
  viewButtons(kind, d);
  if (kind == DocKind::Basic) {
    ImGui::SameLine();
    if (d->readOnly) {
      ImGui::TextDisabled("a library, read only");
      ImGui::SameLine();
      if (ImGui::SmallButton("Edit library")) d->readOnly = false;
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s is shared by every project that includes it. Editing changes it for all of them.",
                          d->path.c_str());
      }
    } else if (basicAtReady() && inStep(*d)) {
      ImGui::TextDisabled("in step with the computer: LIST shows this text, and lines typed there show up here");
    } else if (basicBooted_ && computer_.machine().status == Status::Running) {
      ImGui::TextDisabled("a program is running: changes go across when it stops at READY");
    } else {
      ImGui::TextDisabled("Run boots the interpreter. From then on this text and the computer stay in step");
    }
    ImGui::SetItemTooltip("Enter at the end of a numbered line numbers the next one.\n"
                          "Enter on a bare number takes it away again.\n"
                          "%s-I or Shift+Enter opens a numbered line under the caret's line.\n"
                          "A line typed out of order moves to its place when the caret leaves it.\n"
                          "Its BASIC words turn to capitals then, as the computer stores them.",
                          ImGui::GetIO().ConfigMacOSXBehaviors ? "Cmd" : "Ctrl");
  } else if (kind == DocKind::Microcode) {
    ImGui::SameLine();
    ImGui::TextDisabled("the set the ROM carries. The panes beside it show its rows firing");
  } else if (kind == DocKind::C) {
    ImGui::SameLine();
    ImGui::TextDisabled("Check compiles every .c together. The Assembly tab shows the result");
  } else if (kind == DocKind::Assembly) {
    ImGui::SameLine();
    ImGui::TextDisabled(settings_.formatAssembly ? "laid out in columns on every save" : "Format lays it out in columns");
  }
  // The column before the text: the line the machine stands on while it
  // is paused, scrolled into view when it moves to another line.
  Gutter gutter;
  gutter.current = currentLine(*d);
  if (gutter.current == 0) {
    shownCurrent_ = 0;
  } else if (gutter.current != shownCurrent_ || d->name != shownCurrentDoc_) {
    gutter.reveal = gutter.current;
  }
  // A line the Breakpoints pane asked for wins over the machine's.
  const bool asked = revealLine_ > 0 && revealDoc_ == d->name;
  if (asked) gutter.reveal = revealLine_;
  if (kind == DocKind::Basic) {
    if (assistDoc_ != d->name) {
      basicAssist_.reset();
      assistDoc_ = d->name;
    }
    // A BASIC line carries its own number, so the column has none. A
    // breakpoint belongs to that number, and its dot to the row it is on.
    gutter.numbers = false;
    gutterMarks_.clear();
    const auto breaks = basicBreaks_.find(d->name);
    if (breaks != basicBreaks_.end()) {
      const std::vector<std::string> rows = basic::splitLines(d->text);
      for (size_t r = 0; r < rows.size(); r++) {
        const std::optional<int> n = basic::lineNumber(rows[r]);
        if (n && breaks->second.count(*n)) gutterMarks_.push_back(static_cast<int>(r) + 1);
      }
    }
    gutter.marks = &gutterMarks_;
    // A library stays as its file has it until Edit library.
    if (d->readOnly) {
      codeEditor("##doc", d->text, Syntax::Basic, ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_ReadOnly, nullptr,
                 nullptr, &gutter);
    } else if (codeEditor("##doc", d->text, Syntax::Basic, ImVec2(-1.0f, -1.0f),
                          ImGuiInputTextFlags_AllowTabInput | BasicAssist::FLAGS, BasicAssist::callback,
                          &basicAssist_, &gutter)) {
      d->dirty = true;
    }
    // Whenever the box is not being typed in, the text stands in number
    // order with its reserved words in capitals: a file opened out of
    // order, lines typed on the machine's screen, the editor just left.
    const bool resting = !ImGui::IsItemActive();
    if (resting && !d->readOnly && BasicAssist::sortText(d->text)) d->dirty = true;
    if (std::string n = basicAssist_.takeNote(); !n.empty()) note(n);
    if (gutter.clicked) toggleBasicBreak(*d, gutter.clicked);
    // A line that no longer exists loses its breakpoint, once the person
    // has left the box: a number being retyped is not a line removed.
    if (resting && breaks != basicBreaks_.end() && basicBreaks_.count(d->name)) {
      std::set<int> present;
      for (const std::string& row : basic::splitLines(d->text)) {
        if (const std::optional<int> n = basic::lineNumber(row)) present.insert(*n);
      }
      std::set<int>& lines = basicBreaks_[d->name];
      std::erase_if(lines, [&](int n) { return present.count(n) == 0; });
      if (lines.empty()) {
        basicBreaks_.erase(d->name);
        refreshWatch();
      }
    }
  } else {
    // A click on a number sets or clears a breakpoint.
    gutterMarks_.clear();
    if (auto sb = sourceBreaks_.find(d->name); sb != sourceBreaks_.end()) {
      for (const auto& [line, pc] : sb->second.lines) gutterMarks_.push_back(line);
    }
    gutter.marks = &gutterMarks_;
    if (codeEditor("##doc", d->text, syntaxOf(d->name), ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_AllowTabInput,
                   nullptr, nullptr, &gutter)) {
      d->dirty = true;
    }
    if (gutter.clicked) toggleSourceBreak(*d, gutter.clicked);
  }
  if (gutter.revealed) {
    shownCurrent_ = gutter.current;
    shownCurrentDoc_ = d->name;
    if (asked) revealLine_ = 0;
  }
}

// The assembly the last build handed the assembler: the compiler's
// output, then the project's own .asm files. Read only, since the
// sources are the documents.
void Ide::assemblyPane(const char* name) {
  ImGui::Begin(name);
  if (builtAssembly_.empty()) {
    ImGui::TextDisabled(romBase_ ? "an opened ROM has no assembly. The ASSEMBLY view lists its program"
                                 : "build the project to see the assembly it makes");
  } else {
    ImGui::TextDisabled("what the assembler saw at the last build, read only");
    codeEditor("##asm", builtAssembly_, Syntax::Assembly, ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_ReadOnly);
  }
  ImGui::End();
}

}  // namespace sc8
