// The Run menu. Every view has the same items: Run, Check, Step, Pause
// and Stop. Each works at the level of code the view shows. The view's
// speed and the machine's microcode set come after them.
//
// A step of BASIC or C is a run the host ends. The machine runs flat out
// with a watch on it, and the watch says when the line is over. The
// machine itself steps instructions and rows only.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <string>

#include "imgui.h"

#include "basic/basic_rom.h"
#include "basic/check.h"
#include "basic/lines.h"
#include "basic/program.h"
#include "core/cartridge.h"
#include "core/mcparse.h"
#include "ide/ide.h"
#include "ide/panes.h"
#include "project/project.h"

namespace sc8 {

namespace {

// What Step does in each view, for the menu and the editor's button.
constexpr const char* STEP_LABELS[VIEW_COUNT] = {"Step one line", "Step one C line", "Step one instruction",
                                                 "Step one row"};

constexpr int KEY_BREAK = 27;  // K_BREAK in basic.h: Escape stops a program

// A document's name the way BASIC names a program: GAME.BAS.
std::string upperName(std::string name) {
  for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return name;
}

}  // namespace

const char* Ide::stepLabel() const { return STEP_LABELS[static_cast<int>(view_)]; }

bool Ide::basicMidProgram() const {
  if (!basicBooted_ || poweredOff_ || computer_.machine().status != Status::Running) return false;
  const auto& ram = computer_.machine().ram;
  return ram[basic::SYS_RUNNING] == 1 && !basic::atPrompt(ram);
}

bool Ide::needsBuild() const {
  if (!lastBuildOk_ || builtTexts_.size() != docs_.size()) return true;
  for (const Doc& d : docs_) {
    const auto built = builtTexts_.find(d.name);
    if (built == builtTexts_.end() || built->second != d.text) return true;
  }
  return false;
}

// Run: build if a document changed since the last build, then run. The
// BASIC view hands the interpreter the active program and types RUN. A
// BASIC program that was paused goes on from where it stopped.
void Ide::viewRun() {
  if (view_ == View::Basic && activeDocOf(View::Basic)) {
    if (basicMidProgram()) setRunning(true);
    else runInBasic();
    return;
  }
  if (needsBuild()) {
    buildProject(true);
    return;
  }
  if (poweredOff_ || computer_.machine().status != Status::Running) powerOn();
  setRunning(true);
}

// Check: build without running. The BASIC view stores the program and
// reports what CHECK reports, in RUN's words, and RUN is not typed. The
// MICROCODE view parses the set and puts every row through checkRow.
void Ide::viewCheck() {
  if (view_ == View::Microcode && docNamed("microcode.txt")) {
    checkMicrocodeDoc(*docNamed("microcode.txt"));
    return;
  }
  // BASIC that calls C or assembly by name needs the build to resolve
  // the names, and the build checks every .bas the same way.
  const bool basicAlone = std::all_of(docs_.begin(), docs_.end(), [](const Doc& d) {
    const DocKind kind = docKindOf(d.name);
    return kind == DocKind::Basic || kind == DocKind::Microcode;
  });
  if (view_ == View::Basic && basicAlone && activeDocOf(View::Basic)) {
    checkBasicDoc(*activeDocOf(View::Basic));
    return;
  }
  buildProject(false);
}

void Ide::checkBasicDoc(Doc& doc) {
  messages_.clear();
  if (const std::string why = basic::refusal(doc.text); !why.empty()) {
    reportErrors({doc.name + ": " + why});
    return;
  }
  // The program is stored by the exchange every frame makes at READY,
  // so LIST on the machine shows what was checked.
  librariesStale_ = true;
  CartridgeResult rom = decodeCartridge(std::vector<uint8_t>(basicRom().begin(), basicRom().end()));
  if (!rom.cartridge) {
    reportErrors({"BASIC ROM: " + rom.error});
    return;
  }
  // Every .bas of the project and every library it includes is a slot,
  // so the program's INCLUDE lines find their files.
  rom.cartridge->basic = basicSlots();
  // The check runs on a machine of its own, so the one on screen keeps
  // its picture and its program's state.
  const basic::Checked checked = basic::checkProgram(*rom.cartridge, doc.text, basic::fileNameOf(doc.name));
  const std::vector<std::string> rows = basic::splitLines(doc.text);
  std::vector<std::string> errors;
  for (const basic::Issue& issue : checked.issues) {
    int row = 0;
    for (size_t r = 0; r < rows.size(); r++) {
      if (issue.line > 0 && basic::lineNumber(rows[r]) == issue.line) row = static_cast<int>(r) + 1;
    }
    errors.push_back(doc.name + (row > 0 ? ":" + std::to_string(row) : "") + ": " + issue.message);
  }
  if (!checked.failure.empty()) errors.push_back(doc.name + ": " + checked.failure);
  reportErrors(errors);
  if (errors.empty()) {
    note(doc.name + ": CHECK found no mistake. RUN finds the ones only a value causes, a division by zero for one.");
    showMessages();
  }
}

void Ide::checkMicrocodeDoc(const Doc& doc) {
  messages_.clear();
  const project::MicrocodeCheck c = project::checkMicrocode(doc.text);
  std::vector<std::string> errors;
  for (const std::string& e : c.errors) errors.push_back(doc.name + ":" + e);
  for (const std::string& w : c.conflicts) errors.push_back(doc.name + ":" + w);
  reportErrors(errors);
  if (!c.errors.empty()) return;
  note(doc.name + ": " + std::to_string(c.sections) + " sections, " + std::to_string(c.rows) + " rows, " +
       (c.conflicts.empty() ? "no row breaks a rule" : std::to_string(c.conflicts.size()) + " row(s) break a rule"));
  showMessages();
}

// Step: one line of BASIC, one line of C, one instruction or one row.
void Ide::viewStep() {
  switch (view_) {
    case View::Basic: stepBasic(); break;
    case View::C: stepC(); break;
    case View::Assembly: stepInstruction(); break;
    case View::Microcode: stepMicro(); break;
  }
}

// Stop: the program ends and the next Run starts it from the top. A
// BASIC program gets the break key, as a person at the keyboard gives
// it, and BASIC goes back to READY. Any other program is powered on
// again and left paused, since a reset would keep its variables.
void Ide::viewStop() {
  if (basicMidProgram()) {
    computer_.input().pushKey(KEY_BREAK, false);
    computer_.input().pushKey(KEY_BREAK, true);
    setRunning(true);
    return;
  }
  if (basicBooted_) {
    setRunning(false);
    return;
  }
  powerOn();
}

// ---- stepping a line

void Ide::beginStep(std::function<bool()> until) {
  setRunning(true);
  stepUntil_ = std::move(until);
  stepDone_ = false;
  // A step is for watching, so the machine records what it does.
  computer_.setTrace(true);
  refreshWatch();
}

// The machine runs the short way with no watch. So one is set only for
// a step, a BASIC breakpoint or a row breakpoint the machine can meet.
void Ide::refreshWatch() {
  const bool lines = basicBooted_ && !basicBreaks_.empty();
  const bool rows = !rowBreaks_.empty() && computer_.microcodeInspectable();
  if (stepUntil_ || lines || rows) computer_.setWatch([this] { return watchHit(); });
  else computer_.setWatch(nullptr);
}

// Called by the machine's run loop after every instruction while a watch
// is set. True stops the run.
bool Ide::watchHit() {
  Machine& m = computer_.machine();
  bool stop = false;
  basicStarted_ = basicBooted_ ? basicLines_.started(m.ram) : 0;
  if (basicStarted_ != 0 && !basicBreaks_.empty()) {
    const std::string doc = runningBasicDoc();
    const auto lines = basicBreaks_.find(doc);
    if (lines != basicBreaks_.end() && lines->second.count(basicStarted_)) {
      watchNote_ = "Stopped at line " + std::to_string(basicStarted_) + " in " + upperName(doc);
      stop = true;
    }
  }
  // A row breakpoint in the instruction about to start. The watch runs
  // the machine on to the row, since the run loop stops between
  // instructions only.
  if (!rowBreaks_.empty() && computer_.microcodeInspectable() && rowBreaks_.onNext(m) && rowBreaks_.advance(m)) {
    watchNote_ = rowNote();
    stop = true;
  }
  if (stepUntil_ && stepUntil_()) {
    stepDone_ = true;
    stop = true;
  }
  return stop;
}

// One line of C: run until the address belongs to another line of a C
// document. A line of a library or of the startup code is run through,
// since the project has no document to show it in.
void Ide::stepC() {
  const Machine& m = computer_.machine();
  if (poweredOff_ || m.status != Status::Running) {
    note("the machine is not running: Run starts the program");
    return;
  }
  if (needsBuild()) note("the documents changed since the last build, so the step follows the program as it was built");
  const int from = pcLines_.id(m.pc);
  beginStep([this, from] {
    const int now = pcLines_.id(computer_.machine().pc);
    return now >= 0 && now != from && stepLine_[static_cast<size_t>(now)];
  });
}

// One line of BASIC: run until the next line starts, or the program
// ends. At READY the step runs the program first and stops at its first
// line. project/breakpoints.cpp says how a line's start is seen.
void Ide::stepBasic() {
  if (!basicMidProgram()) {
    if (!activeDocOf(View::Basic)) {
      note("no BASIC program is open or running, so there is no line to step");
      return;
    }
    runInBasic();
  }
  stepSawProgram_ = basicMidProgram();
  beginStep([this] {
    const auto& ram = computer_.machine().ram;
    if (basicStarted_ != 0) return true;
    if (ram[basic::SYS_RUNNING] == 1) stepSawProgram_ = true;
    return stepSawProgram_ && basic::atPrompt(ram);
  });
}

// Where the machine stands, in the words of the view's language.
std::string Ide::whereNote() {
  const Machine& m = computer_.machine();
  if (basicBooted_) {
    const auto& ram = m.ram;
    if (ram[basic::SYS_RUNNING] != 1) return "the program ended, BASIC is at READY";
    const int line = (ram[basic::SYS_ERR_LINE] << 8) | ram[basic::SYS_ERR_LINE + 1];
    const std::string doc = runningBasicDoc();
    return "line " + std::to_string(line) + (doc.empty() ? "" : " in " + upperName(doc));
  }
  if (const project::SourceLine* at = pcLines_.at(m.pc)) return at->file + ":" + std::to_string(at->line);
  return "PC " + panes::hex(m.pc, 4);
}

// The line of a document the machine stands on, counted from 1 in the
// document's text, or 0 when it stands on none of its lines. The editor
// marks it while the machine is paused.
int Ide::currentLine(const Doc& doc) {
  const Machine& m = computer_.machine();
  if (running_ || poweredOff_ || m.status != Status::Running) return 0;
  if (docKindOf(doc.name) == DocKind::Basic) {
    if (!basicMidProgram() || doc.name != runningBasicDoc()) return 0;
    const int line = (m.ram[basic::SYS_ERR_LINE] << 8) | m.ram[basic::SYS_ERR_LINE + 1];
    const std::vector<std::string> rows = basic::splitLines(doc.text);
    for (size_t r = 0; r < rows.size(); r++) {
      if (line > 0 && basic::lineNumber(rows[r]) == line) return static_cast<int>(r) + 1;
    }
    return 0;
  }
  if (basicBooted_) return 0;
  const project::SourceLine* at = pcLines_.at(m.pc);
  return at && at->file == doc.name ? at->line : 0;
}

void Ide::showWhere() {
  if (basicBooted_) {
    // The file the BASIC line is in comes forward, a library's too. The
    // IDE picked this tab, so no FILE goes to the machine for it.
    if (!basicMidProgram()) return;
    const std::string name = runningBasicDoc();
    for (size_t i = 0; i < docs_.size(); i++) {
      if (docs_[i].name != name) continue;
      if (activeName_[static_cast<int>(View::Basic)] != name || !docs_[i].open) activate(i);
      lastBasicTab_ = name;
    }
    return;
  }
  const project::SourceLine* at = pcLines_.at(computer_.machine().pc);
  if (!at) return;
  for (size_t i = 0; i < docs_.size(); i++) {
    if (docs_[i].name != at->file) continue;
    if (activeName_[static_cast<int>(view_)] != at->file && viewOfKind(docKindOf(at->file)) == view_) activate(i);
  }
}

// ---- the menu

void Ide::applyProjectMicrocode(bool lock) {
  Doc* set = docNamed("microcode.txt");
  if (!set) return;
  const project::MicrocodeCheck c = project::checkMicrocode(set->text);
  std::vector<std::string> errors;
  for (const std::string& e : c.errors) errors.push_back(set->name + ":" + e);
  reportErrors(errors);
  if (!errors.empty()) return;
  if (lock) lockedMicrocode_ = set->text;
  selectMicrocode(set->text);
  note(lock ? "microcode locked: every project and ROM runs on the project's set until unlocked"
            : "the project's microcode was applied and the machine restarted");
}

void Ide::toggleMicrocodeLock() {
  if (lockedMicrocode_) {
    lockedMicrocode_.reset();
    note("microcode unlocked: the next build or ROM brings its own set");
  } else {
    lockedMicrocode_ = computer_.microcodeName();
    note("microcode locked: every project and ROM runs on the set in the machine now");
  }
}

bool Ide::addBreak(const std::string& spec) {
  // A library's document is there once the libraries were looked up.
  if (librariesStale_) {
    librariesStale_ = false;
    refreshLibraries();
  }
  const size_t colon = spec.rfind(':');
  if (colon == std::string::npos) return false;
  const Doc* doc = docNamed(spec.substr(0, colon));
  const int line = std::atoi(spec.c_str() + colon + 1);
  if (!doc || line < 1) return false;
  if (docKindOf(doc->name) == DocKind::Basic) {
    basicBreaks_[doc->name].insert(line);
    refreshWatch();
  } else {
    toggleSourceBreak(*doc, line);
  }
  return true;
}

bool Ide::menuAction(const std::string& name) {
  if (name == "run") viewRun();
  else if (name == "check") viewCheck();
  else if (name == "step") viewStep();
  else if (name == "pause") setRunning(false);
  else if (name == "stop") viewStop();
  else return false;
  return true;
}

void Ide::runMenu() {
  if (!ImGui::BeginMenu("Run")) return;
  if (ImGui::MenuItem("Run", "F5", false, !running_)) viewRun();
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(view_ == View::Basic ? "hand BASIC the program in the editor and RUN it"
                                           : "build the project if a document changed, then run it");
  }
  if (ImGui::MenuItem("Check", "F7")) viewCheck();
  if (ImGui::IsItemHovered()) {
    static const char* const WHAT[VIEW_COUNT] = {
        "store the program and report what BASIC's CHECK finds, without running it",
        "build the project and show the messages, without running it",
        "build the project and show the messages, without running it",
        "parse microcode.txt and check every row against the row rules"};
    ImGui::SetTooltip("%s", WHAT[static_cast<int>(view_)]);
  }
  if (ImGui::MenuItem(stepLabel(), "F10")) viewStep();
  if (ImGui::MenuItem("Pause", "F5", false, running_)) setRunning(false);
  if (ImGui::MenuItem("Stop")) viewStop();
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(view_ == View::Basic ? "break the BASIC program, as Escape does on the machine"
                                           : "end the program: the machine is powered on again and waits for Run");
  }
  ImGui::Separator();
  const auto ladder = speedLadder();
  if (ImGui::BeginMenu("Speed")) {
    ImGui::TextDisabled("the %s view's speed", viewLabel(view_));
    for (size_t i = 0; i < ladder.size(); i++) {
      const bool enabled = !ladder[i].micro || computer_.microcodeInspectable();
      if (ImGui::MenuItem(ladder[i].label, nullptr, speedIndex() == static_cast<int>(i), enabled)) {
        pickSpeed(static_cast<int>(i));
      }
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Microcode")) {
    const std::string& name = computer_.microcodeName();
    const bool builtIn = name == "@naive" || name == "@optimal";
    const bool have = docNamed("microcode.txt") != nullptr;
    if (ImGui::MenuItem("project", nullptr, !builtIn, have)) applyProjectMicrocode(false);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip(have ? "the set in the project's microcode.txt"
                             : "the project has no microcode.txt. New file in the project column adds one");
    }
    if (ImGui::MenuItem("naive", nullptr, name == "@naive")) selectMicrocode("@naive");
    if (ImGui::MenuItem("optimal", nullptr, name == "@optimal")) selectMicrocode("@optimal");
    ImGui::Separator();
    if (ImGui::MenuItem("Lock the running set", nullptr, lockedMicrocode_.has_value())) toggleMicrocodeLock();
    ImGui::EndMenu();
  }
  ImGui::EndMenu();
}

}  // namespace sc8
