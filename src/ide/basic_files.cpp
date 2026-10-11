// The BASIC bridge. A .bas document and its file in the interpreter's
// store are two views of one program, a tab per file. While BASIC waits
// at READY the two are kept in step both ways, file by file. The IDE is
// also the storage device's host. It answers LOAD, INCLUDE and FILE with
// the project's documents, the libraries and files by path. It puts a
// SAVE into the project.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

#include "imgui.h"
#include "imgui_internal.h"

#include "basic/basic_rom.h"
#include "basic/program.h"
#include "core/cartridge.h"
#include "ide/ide.h"
#include "project/project.h"
#include "project/text_file.h"

namespace fs = std::filesystem;

namespace sc8 {

namespace {

std::string lowerOf(std::string text) {
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

// A name with a slash or a dot in it is a path, as the storage device
// reads one.
bool isPath(const std::string& name) { return name.find_first_of("/\\.") != std::string::npos; }

}  // namespace

// ---- the libraries

// Preferences' folder, or the default in the projects folder, then the
// folder libraries beside the project's own folder. For a project kept
// in the projects folder the two are one.
std::vector<fs::path> Ide::libraryFolders() const {
  std::vector<fs::path> out;
  const fs::path set = settings_.librariesDir.empty() ? project::defaultLibraries(settings_.projectsDir)
                                                      : fs::path(settings_.librariesDir);
  if (!set.empty()) out.push_back(set);
  if (!projectDir_.empty()) {
    const fs::path beside = project::defaultLibraries(fs::path(projectDir_).parent_path());
    if (std::find(out.begin(), out.end(), beside) == out.end()) out.push_back(beside);
  }
  return out;
}

Doc* Ide::docOfFile(const std::string& file) {
  // A project's own file wins over a library of the same name.
  for (const bool library : {false, true}) {
    for (Doc& d : docs_) {
      if (d.library == library && docKindOf(d.name) == DocKind::Basic && basic::fileNameOf(d.name) == file) return &d;
    }
  }
  return nullptr;
}

// The libraries the project's programs INCLUDE, as documents: read only,
// after the project's own. A library INCLUDEs others, so the list is
// walked until it stops growing. One nobody includes any more goes,
// unless it holds an edit that is not saved.
void Ide::refreshLibraries() {
  const std::vector<fs::path> folders = libraryFolders();
  std::set<std::string> wanted;
  std::vector<std::string> queue;
  for (const Doc& d : docs_) {
    if (!d.library && docKindOf(d.name) == DocKind::Basic) {
      for (const std::string& n : project::includesOf(d.text)) queue.push_back(n);
    }
  }
  bool changed = false;
  for (size_t i = 0; i < queue.size(); i++) {
    const std::string name = queue[i];
    if (wanted.count(name)) continue;
    Doc* have = docOfFile(name);
    if (have && !have->library) continue;
    std::string text;
    if (have) {
      text = have->text;
    } else {
      const fs::path file = project::libraryFile(folders, name);
      const std::optional<std::string> read = file.empty() ? std::nullopt : project::readProgramFile(file);
      if (!read) continue;
      text = *read;
      Doc lib;
      lib.name = lowerOf(file.filename().string());
      lib.text = text;
      lib.text.reserve(text.size() + (1 << 16));
      lib.library = true;
      lib.readOnly = true;
      lib.open = false;
      lib.path = file.string();
      docs_.push_back(std::move(lib));
      changed = true;
    }
    wanted.insert(name);
    for (const std::string& n : project::includesOf(text)) queue.push_back(n);
  }
  const size_t before = docs_.size();
  std::erase_if(docs_, [&](const Doc& d) { return d.library && !d.dirty && !wanted.count(basic::fileNameOf(d.name)); });
  if (changed || docs_.size() != before) {
    sortDocs();
    settleActive();
  }
}

BasicSlots Ide::basicSlots() {
  BasicSlots slots;
  std::vector<std::string> queue;
  for (const Doc& d : docs_) {
    if (d.library || docKindOf(d.name) != DocKind::Basic) continue;
    slots.emplace_back(basic::fileNameOf(d.name), d.text);
    for (const std::string& n : project::includesOf(d.text)) queue.push_back(n);
  }
  const std::vector<fs::path> folders = libraryFolders();
  for (size_t i = 0; i < queue.size(); i++) {
    const std::string name = queue[i];
    if (std::any_of(slots.begin(), slots.end(), [&](const auto& s) { return s.first == name; })) continue;
    // An open library's tab may hold an edit its file does not have yet.
    const Doc* open = docOfFile(name);
    const fs::path file = project::libraryFile(folders, name);
    const std::optional<std::string> text = open ? open->text : file.empty() ? std::nullopt : project::readProgramFile(file);
    if (!text) continue;
    slots.emplace_back(name, *text);
    for (const std::string& n : project::includesOf(*text)) queue.push_back(n);
  }
  return slots;
}

// ---- the storage device's host

void Ide::hostStorage() {
  Storage::Host host;
  host.find = [this](const std::string& name) -> std::optional<std::string> {
    if (isPath(name)) {
      // A path is read from the disk, against the project's folder.
      // DESIGN: the program in the machine names the path, and it may be
      // a ROM somebody else wrote. readProgramFile reads a .bas file of
      // a slot's size at most, so the machine cannot ask for any file.
      const fs::path base = projectDir_.empty() ? fs::current_path() : fs::path(projectDir_);
      const fs::path file = fs::path(name).is_absolute() ? fs::path(name) : base / name;
      if (std::optional<std::string> text = project::readProgramFile(file)) return text;
      // No such file: the device tries the slot the path's stem names.
      return std::nullopt;
    }
    std::string file = name;
    for (char& c : file) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    // The project's own document first, as it stands in the editor, then
    // a library. The cartridge's slots come after both, in the device.
    if (const Doc* d = docOfFile(file)) return d->text;
    const fs::path library = project::libraryFile(libraryFolders(), file);
    return library.empty() ? std::nullopt : project::readProgramFile(library);
  };
  // The machine is mid instruction here, so the SAVE waits for the frame.
  host.saved = [this](const std::string& name, const std::string& text) { savedSlots_.emplace_back(name, text); };
  computer_.storage().setHost(std::move(host));
}

// A SAVE on the machine goes into the document of that name. Any other
// name is a new source of the project. So a file loaded by path is the
// project's once saved.
//
// DESIGN: the program in the machine names the slot, and it may be a ROM
// somebody else wrote. A name becomes a file only when it is a file's
// name in BASIC, letters and digits. A library's file is written only
// when its tab has Edit library on, the gate that keeps a project from
// changing a library by accident. Otherwise the cartridge's slot holds
// what was saved, and nothing on the disk changes.
void Ide::takeSaves() {
  if (savedSlots_.empty()) return;
  std::vector<std::pair<std::string, std::string>> saves;
  saves.swap(savedSlots_);
  for (const auto& [slot, text] : saves) {
    if (!basic::isFileName(slot)) {
      note("SAVE \"" + slot + "\" is in the cartridge only: a file's name is letters and digits, 16 at most");
      continue;
    }
    const std::string file = basic::fileNameOf(slot);
    Doc* d = docOfFile(file);
    const bool library = d ? d->library : !project::libraryFile(libraryFolders(), file).empty();
    if (library && (!d || d->readOnly)) {
      note("SAVE \"" + file + "\" is in the cartridge only: " + lowerOf(file) +
           ".bas is a library, and its file changes once Edit library is on in its tab");
      continue;
    }
    if (!d) {
      addDoc(lowerOf(file) + ".bas", "");
      d = docOfFile(file);
      if (!d) continue;
      note("SAVE put " + d->name + " into the project");
    }
    setDocText(*d, text);
    d->dirty = true;
    // The machine holds what the document holds now.
    if (d->library || (!projectDir_.empty() && !example_)) saveDoc(*d);
  }
  refreshLibraries();
}

// ---- running

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
  pcLines_ = {};
  stepLine_.clear();
  loadCartridge(std::move(*r.cartridge), "the BASIC ROM");
  basicBooted_ = true;
  refreshWatch();
}

// The program Run runs: the document in the BASIC view's editor. When
// that is a library, or a file another of the project's programs
// INCLUDEs, the project's main program runs: autorun.bas, else its first
// .bas. RUN runs the main file, and an included file is not one.
Doc* Ide::mainBasicDoc() {
  Doc* active = activeDocOf(View::Basic);
  if (!active) return nullptr;
  const std::string file = basic::fileNameOf(active->name);
  bool included = active->library;
  for (const Doc& d : docs_) {
    if (&d == active || docKindOf(d.name) != DocKind::Basic) continue;
    const std::vector<std::string> names = project::includesOf(d.text);
    included = included || std::find(names.begin(), names.end(), file) != names.end();
  }
  if (!included) return active;
  if (Doc* autorun = docNamed("autorun.bas")) return autorun;
  for (Doc& d : docs_) {
    if (!d.library && docKindOf(d.name) == DocKind::Basic && &d != active) return &d;
  }
  return active;
}

// Run the BASIC program: boot the interpreter unless the machine already
// runs one, put the main program in its store and type RUN. The push
// waits for the boot, since the system page is empty until then.
void Ide::runInBasic() {
  if (!mainBasicDoc()) return;
  librariesStale_ = true;
  if (!basicBooted_ || computer_.machine().status != Status::Running) bootBasic();
  pushPending_ = true;
  afterPush_ = "RUN\n";
  setRunning(true);
}

// BASIC's own word that it waits at its prompt. No program running is not
// enough: AUTORUN, LOAD and a typed line store lines with none running.
// A program read or written between two of them came out mixed.
bool Ide::basicAtReady() const {
  if (!basicBooted_ || computer_.machine().status != Status::Running) return false;
  return basic::atPrompt(computer_.machine().ram) && typingPos_ >= typing_.size();
}

// The store as the interpreter holds it now, or one with no file when
// the system page does not describe one.
basic::Store Ide::machineStore() const {
  const auto& ram = computer_.machine().ram;
  const size_t prog = static_cast<size_t>((ram[basic::SYS_PROG] << 8) | ram[basic::SYS_PROG + 1]);
  const size_t len = static_cast<size_t>((ram[basic::SYS_PROG_LEN] << 8) | ram[basic::SYS_PROG_LEN + 1]);
  if (len < basic::EMPTY_PROGRAM || len > basic::PROGRAM_MAX || prog + len > ram.size()) return {};
  return basic::readStore(std::span<const uint8_t>(ram.data() + prog, len));
}

// A store into the interpreter's memory. False, with a note, when it is
// larger than the interpreter's buffer.
bool Ide::writeStore(const basic::Store& store) {
  const std::vector<uint8_t> bytes = basic::writeStore(store);
  if (bytes.size() >= basic::PROGRAM_MAX) {
    const std::string why = "the BASIC files are larger than the computer's program memory, 16384 bytes, so the "
                            "editor's changes stay in the editor";
    if (why != refusalNoted_) note(why);
    refusalNoted_ = why;
    return false;
  }
  basic::storeProgram(computer_.machine().ram, bytes);
  return true;
}

// Run's push: the store holds the main program alone, as the main file.
// The files it INCLUDEs come in when RUN runs, fresh from the documents.
bool Ide::pushMain() {
  Doc* main = mainBasicDoc();
  if (!main || !basicAtReady()) return false;
  const std::string name = basic::fileNameOf(main->name);
  basic::FileTable files{{name}};
  basic::Store store;
  store.files.push_back({name, true, basic::encodeLines(main->text, files, 0)});
  store.adopt(files);
  if (!writeStore(store)) return false;
  auto& ram = computer_.machine().ram;
  ram[basic::SYS_EDIT] = 0;
  ram[basic::SYS_FILE + 1] = 0;
  syncBases_.clear();
  syncBases_[name] = store.files[0].lines;
  shownEdit_ = 0;
  return true;
}

void Ide::setDocText(Doc& doc, std::string text) {
  doc.text = std::move(text);
  if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetActiveID())) {
    state->ReloadUserBufAndKeepSelection();
  }
}

bool Ide::inStep(const Doc& doc) const { return syncBases_.count(basic::fileNameOf(doc.name)) != 0; }

// Once per frame while BASIC waits at READY. A pending push from Run goes
// first. Then every file in the store that has a document is compared
// with the records the two last agreed on. A side that moved carries the
// other along. Two sides that both moved are merged line by line.
// The current file and the active tab are one thing. FILE on the machine
// brings its tab to the front, and a tab the person picks sends FILE.
void Ide::syncBasic() {
  takeSaves();
  if (!basicAtReady()) return;
  if (pushPending_) {
    const Doc* main = mainBasicDoc();
    const std::string why = main ? basic::refusal(main->text) : "";
    if (!why.empty()) {
      if (why != refusalNoted_) note(main->name + ": " + why);
      refusalNoted_ = why;
      pushPending_ = false;
      return;
    }
    if (!pushMain()) return;
    pushPending_ = false;
    typing_ = afterPush_;
    typingPos_ = 0;
    return;
  }
  basic::Store store = machineStore();
  if (store.files.empty()) return;
  basic::FileTable files = store.table();
  bool changed = false;
  auto& ram = computer_.machine().ram;

  // A plain interpreter's main file has no name of the project's. The
  // document in the editor becomes that file, and brings its program.
  Doc* tab = activeDocOf(View::Basic);
  if (store.files[0].name == "MAIN" && !docOfFile("MAIN") && tab && !tab->library &&
      files.find(basic::fileNameOf(tab->name)) < 0) {
    store.files[0].name = basic::fileNameOf(tab->name);
    files = store.table();
    changed = true;
  }

  // FILE or LOAD on the machine: the current file's tab comes forward.
  const int edit = ram[basic::SYS_EDIT];
  if (edit != shownEdit_ && static_cast<size_t>(edit) < store.files.size()) {
    shownEdit_ = edit;
    if (Doc* d = docOfFile(store.files[static_cast<size_t>(edit)].name)) {
      for (size_t i = 0; i < docs_.size(); i++) {
        if (&docs_[i] == d) activate(i);
      }
      lastBasicTab_ = d->name;
      tab = d;
    }
  }
  // A tab the person picked: its file goes into the store when it is not
  // there, and FILE makes it the current one.
  if (tab && tab->name != lastBasicTab_) {
    lastBasicTab_ = tab->name;
    const std::string want = basic::fileNameOf(tab->name);
    int at = files.find(want);
    if (at < 0) at = files.add(want);
    if (at < 0) {
      note(tab->name + " is not on the computer: its store holds " + std::to_string(basic::FILE_MAX) +
           " files and has that many. NEW ALL on the computer empties it.");
    } else {
      store.adopt(files);
      basic::StoredFile& file = store.files[static_cast<size_t>(at)];
      if (!file.loaded) {
        file.loaded = true;
        file.lines = basic::encodeLines(tab->text, files, at);
        store.adopt(files);
        syncBases_[want] = file.lines;
        changed = true;
      }
      if (at != edit) {
        typing_ = "FILE " + want + "\n";
        typingPos_ = 0;
      }
    }
  }

  for (size_t f = 0; f < store.files.size(); f++) {
    basic::StoredFile& file = store.files[f];
    if (!file.loaded) continue;
    Doc* d = docOfFile(file.name);
    // A library's tab is read only until Edit library. It shows the
    // library's file. A change on the machine stays on the machine until
    // SAVE writes that file.
    if (!d || d->readOnly) continue;
    // A line BASIC would misread stops the exchange for its file both
    // ways until it is changed. Syncing the rest would take the line out
    // of the document at the next merge.
    const std::string why = basic::refusal(d->text);
    if (!why.empty()) {
      if (why != refusalNoted_) note(d->name + ": " + why);
      refusalNoted_ = why;
      continue;
    }
    // A file the document meets for the first time: the document brings
    // its program along, as the store's records are what both agree on.
    auto base = syncBases_.find(file.name);
    if (base == syncBases_.end()) base = syncBases_.emplace(file.name, file.lines).first;
    const size_t linesBefore = file.lines.size();
    const basic::Synced out = basic::syncLines(base->second, d->text, file.lines, files, static_cast<int>(f));
    if (out.textChanged) {
      // NEW typed on the computer empties the document too. The file on
      // disk keeps the old program until the next save.
      if (out.lines.empty() && linesBefore == 0 && !d->text.empty()) {
        note("NEW on the computer emptied " + d->name + ". The file on disk keeps the old program until you save.");
      }
      setDocText(*d, out.text);
      d->dirty = true;
    }
    if (out.storeChanged) {
      file.lines = out.lines;
      changed = true;
    }
    base->second = out.lines;
  }
  // A file that left the store has no agreement to keep.
  std::erase_if(syncBases_, [&](const auto& kv) {
    const int at = files.find(kv.first);
    return at < 0 || static_cast<size_t>(at) >= store.files.size() || !store.files[static_cast<size_t>(at)].loaded;
  });
  if (changed) {
    store.adopt(files);
    if (writeStore(store)) refusalNoted_.clear();
  }
}

// Commands for the interpreter go through the key queue, the way a person
// types them. The queue holds 64 events and a key is two. The pump waits
// above 32 queued. Programs do not go this way: the store is written
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

}  // namespace sc8
