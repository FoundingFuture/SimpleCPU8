// The IDE: one Computer, one project, and the panes that look at both,
// arranged in levels. A level is a saved dock layout. Project holds the
// files, the screen, the messages and the manual. Run holds the screen
// and the debugger. CPU holds the datapath and the microcode rows.
// Switching level swaps the layout.
//
// There is always a project. It is a folder of sources (.c, .h, .asm,
// .bas, microcode.txt) and assets, or an opened ROM (its slots and its
// microcode as documents, its program as it is), or the scratch project
// the IDE starts with until it is saved somewhere. Build turns the
// project into a ROM and puts it in the machine: C first, then the
// assembler over everything, then the ROM. A project with BASIC boots
// into the interpreter.
// docs/standalone.md, section "IDE levels", is the design.
#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "asm/asm.h"
#include "ide/filedialog.h"
#include "ide/guide.h"
#include "project/project.h"
#include "vm/audio.h"
#include "vm/computer.h"
#include "vm/display.h"
#include "vm/keys.h"

namespace sc8 {

enum class Level { Project, Run, Cpu };

// The speed ladder from the browser's speed menu: the trace speed, six
// instruction rates, three frame-locked rates, then MAX.
struct Speed {
  const char* label;
  const char* value;  // the browser's menu value, and the --speed spelling
  double ips;  // per second. 0 for MAX and the frame-locked rates
  int fps;     // frame-locked rate, 0 otherwise
  bool micro;  // one microcode row per tick
};

std::span<const Speed> speedLadder();

// One line of the listing pane: the source line, or one instruction of a
// ROM that came without source.
struct ListLine {
  std::string text;
  std::optional<int> instr;    // program index, for a code line
  std::optional<int> ramAddr;  // the RAM address a .ram line defines
  std::string label;           // a RAM label opening the line
};

// One document of the project: a source by its file name, its text and
// whether the text differs from the disk. Kind follows the extension.
enum class DocKind { C, Header, Assembly, Basic, Microcode, Other };
DocKind docKindOf(const std::string& name);

struct Doc {
  std::string name;
  std::string text;
  bool dirty = false;
};

class Ide {
 public:
  Ide();

  // Open a ROM, a project folder or a source file (which opens its
  // project) from the command line.
  void open(const std::string& path);

  void setLevel(Level level);
  Level level() const { return level_; }

  // The command line's --speed and --run: a label from the browser's
  // speed menu, then start running. False for a value the ladder lacks.
  bool setSpeed(const std::string& value);
  // --run: build the project and run it. A .bas opened from the command
  // line runs in BASIC instead, the quick way.
  void run();

  // Called once per host frame, before rlImGuiBegin. Paces the machine,
  // feeds the keyboard and the audio. Renders the screen through the CRT
  // shader.
  void update();

  // Called once per host frame between rlImGuiBegin and rlImGuiEnd.
  void frame();

  // The window's close gesture. With unsaved documents the IDE asks
  // first; done() turns true once the person answered or nothing needed
  // saving.
  void requestQuit();
  bool done() const { return done_; }

  DisplaySettings display;

 private:
  // The project.
  void newScratchProject();
  void openProject(const std::string& dir);
  void openRomAsProject(const std::string& path);
  void createProject(const std::string& dir, project::Kind kind);
  void saveProjectAs(const std::string& dir);
  void saveDoc(Doc& doc);
  void saveAll();
  bool anyDirty() const;
  void activate(size_t index);
  Doc* activeDoc();
  void addDoc(const std::string& name, const std::string& text);
  void buildProject(bool run);
  void insertBuilt(project::Built& built, const std::string& what);
  void loadCartridge(Cartridge cart, const std::string& what);
  void burnRom(const std::string& path);
  void rebuildListing();
  void note(std::string message) { messages_.push_back(std::move(message)); }
  std::vector<project::Source> sources() const;
  project::Layout layout() const;

  // Running.
  void setRunning(bool on);
  void pace();
  void stepInstruction();
  void stepMicro();
  void runOneFrame();
  void powerOn();
  void selectMicrocode(const std::string& name);
  void applyLock();
  void toggleBreakpoint(int instr);
  void pushBreakpoints();
  void typeIntoMachine();

  // BASIC: the active .bas document and the interpreter's program memory
  // are two views of one program. Push writes the text where the system
  // page points. While BASIC waits at READY, the memory is read back each
  // frame, so a line typed on the small screen shows up in the document.
  void bootBasic();
  void runInBasic();
  bool basicAtReady() const;
  bool pushProgram();
  void pullProgram();
  void syncBasic();

  // The levels and their layouts.
  void buildLayout(Level level, unsigned dockspace);
  void menuBar();
  void shortcuts();
  void quitDialog();

  // The panes. A pane shared by two levels takes the level's window name.
  void filesPane();
  void editorPane();
  void assemblyPane();
  void messagesPane();
  void manualPane();
  void screenPane();
  void registersPane(const char* name);
  void runControls();
  void memoryPane();
  void breakpointsPane();
  void listingPane(const char* name);
  void datapathPane();
  void flowPane();
  void microcodePane();

  Computer computer_;
  Display screen_;
  Audio audio_;
  Keyboard keyboard_;
  FileDialog dialog_;

  // The project. projectDir_ is empty for a ROM project and for the
  // scratch project. romBase_ holds an opened ROM, whose program the
  // build keeps.
  std::string projectDir_;
  std::string projectTitle_ = "scratch";
  std::optional<Cartridge> romBase_;
  std::string romPath_;  // the ROM file opened, for a burn to the same place
  std::vector<Doc> docs_;
  size_t active_ = 0;
  bool focusFiles_ = false;
  std::string newDocName_;
  bool openedBasic_ = false;   // the command line opened a .bas
  std::string builtAssembly_;  // what the assembler saw at the last build
  Assembled assembled_;
  bool haveSource_ = false;  // the listing has source lines
  std::vector<ListLine> listing_;
  std::vector<std::string> messages_;

  Level level_ = Level::Project;
  bool layoutBuilt_[3] = {false, false, false};
  bool done_ = false;
  bool askQuit_ = false;

  bool running_ = false;
  int speed_ = 8;  // index into the ladder, f60 by default
  double owed_ = 0.0;
  double tick_ = 0.0;
  bool fastFrameLatch_ = false;
  std::set<uint16_t> breakpoints_;
  bool screenHasKeys_ = false;
  bool followPc_ = true;
  uint16_t shownPc_ = 0xffff;  // the PC the listing last scrolled to

  // The memory pane.
  int memAddr_ = 0;
  std::vector<std::string> watched_;

  // The microcode pane. lockedMicrocode_ holds a set the machine keeps
  // whatever a project or ROM brings.
  int selectedSection_ = 0;
  std::string customText_;
  std::vector<std::string> mcErrors_;
  bool mcEditing_ = false;
  std::optional<std::string> lockedMicrocode_;

  // The manual pane.
  std::string manualFilter_;
  std::string manualPage_ = "LD";

  // BASIC sync state. syncDoc_ names the document in step with the
  // machine, syncedText_ its text at the last exchange. When the document
  // still matches it, a change in the machine replaces the text; when it
  // has moved on, the pane says so and offers Pull.
  std::string syncDoc_;
  std::string syncedText_;
  std::vector<uint8_t> machineProgram_;
  bool machineChanged_ = false;
  bool basicBooted_ = false;  // the machine runs the interpreter
  // Commands still go through the keyboard: RUN after a push, for one.
  std::string typing_;
  size_t typingPos_ = 0;
  // A push waits until the interpreter has booted and set SYS_PROG. This
  // is the command to type after it.
  bool pushPending_ = false;
  std::string afterPush_;

  // The Microcode pane's execution view (micro_panes.cpp). Off, the pane
  // walks the current instruction's rows, fetch first. On, it browses
  // every section of the set.
  bool mcBrowse_ = false;

  // The Manual pane's guide tab: which guide, its filter and its scroll.
  GuideView guideView_;
  int manualTab_ = -1;  // a tab to select next frame, -1 for none
};

}  // namespace sc8
