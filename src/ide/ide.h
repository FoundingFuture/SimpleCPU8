// The IDE: one Computer, a source buffer, and the panes that look at both,
// arranged in levels. A level is a saved dock layout. Edit holds the
// editor, the messages and the manual. Run holds the screen and the
// debugger. Microcode holds the datapath and the rows. Switching level
// swaps the layout.
// docs/standalone.md, section "IDE levels", is the design.
#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "asm/asm.h"
#include "vm/audio.h"
#include "project/project.h"
#include "vm/computer.h"
#include "vm/display.h"
#include "vm/keys.h"

namespace sc8 {

enum class Level { Edit, Run, Microcode };

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

class Ide {
 public:
  Ide();

  // Open a ROM or a source file from the command line.
  void open(const std::string& path);

  void setLevel(Level level);
  Level level() const { return level_; }

  // The command line's --speed and --run: a label from the browser's
  // speed menu, then start running. False for a value the ladder lacks.
  bool setSpeed(const std::string& value);
  // With a BASIC program open and nothing else, --run means run it in
  // BASIC, which boots the interpreter first.
  void run() {
    if (openedBasic_) runInBasic();
    else setRunning(true);
  }

  // Called once per host frame, before rlImGuiBegin. Paces the machine,
  // feeds the keyboard and the audio. Renders the screen through the CRT
  // shader.
  void update();

  // Called once per host frame between rlImGuiBegin and rlImGuiEnd.
  void frame();

  DisplaySettings display;

 private:
  // The session.
  void assembleSource();
  void burnRom();
  void loadRomFile(const std::string& path);
  void loadCartridge(Cartridge cart, const std::string& what);
  void saveSource();
  void rebuildListing();
  void note(std::string message) { messages_.push_back(std::move(message)); }

  // Running.
  void setRunning(bool on);
  void pace();
  void stepInstruction();
  void stepMicro();
  void runOneFrame();
  void powerOn();
  void selectMicrocode(const std::string& name);
  void toggleBreakpoint(int instr);
  void pushBreakpoints();
  void typeIntoMachine();

  // The levels and their layouts.
  void buildLayout(Level level, unsigned dockspace);
  void menuBar();
  void shortcuts();

  // The panes. A pane shared by two levels takes the level's window name.
  void sourcePane();
  void basicPane();
  void projectPane();
  void openProject(const std::string& dir);
  void openProjectFile(const std::string& name);
  void saveProjectFile();
  void buildProject(bool run);
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

  std::string sourcePath_;
  std::string source_;
  std::string romPath_;
  Assembled assembled_;
  bool assembledOk_ = false;
  bool haveSource_ = true;  // false once a ROM without source is loaded
  std::vector<ListLine> listing_;
  std::vector<std::string> messages_;

  Level level_ = Level::Edit;
  bool layoutBuilt_[3] = {false, false, false};

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

  // The microcode pane.
  int selectedSection_ = 0;
  std::string customText_;
  std::vector<std::string> mcErrors_;
  bool mcEditing_ = false;

  // The manual pane.
  std::string manualFilter_;
  std::string manualPage_ = "LD";

  // The Project pane: a folder of .c, .h, .asm and .bas files, one open
  // in the editor at a time. Build hands the folder to the project
  // library, which is what simplecpu-make runs. The assembly it made goes
  // into the Source pane and the ROM into the machine.
  std::string projectDir_;
  std::vector<std::string> projectFiles_;
  std::string projectFile_;
  std::string projectText_;
  bool projectDirty_ = false;
  bool focusProject_ = false;  // bring the tab to the front next frame
  // Set once a project built, so Source assembles with its asset folders.
  std::optional<project::Layout> projectLayout_;

  // The BASIC pane. The editor and the interpreter's program memory are
  // two views of one program. Push writes the editor's text into the
  // memory the system page points at. While BASIC waits at READY, the
  // memory is read back each frame, so a line typed on the small screen
  // shows up in the editor. syncedText_ is the text of the last exchange:
  // when the editor still matches it, a change in the machine replaces
  // the editor's text without asking. When the editor has moved on, the
  // pane says so and offers Pull.
  std::string basicText_;
  std::string basicPath_;
  std::string basicSlot_ = "PROGRAM";
  std::string syncedText_;
  std::vector<uint8_t> machineProgram_;
  bool machineChanged_ = false;
  // Commands still go through the keyboard: RUN after a push, for one.
  std::string typing_;
  size_t typingPos_ = 0;
  // A push waits until the interpreter has booted and set SYS_PROG. This
  // is the text to push then, and the command to type after it.
  bool pushPending_ = false;
  std::string afterPush_;
  // Whether the cartridge in the slot is the built-in BASIC ROM.
  bool basicBooted_ = false;
  // The last file opened from the command line was a .bas.
  bool openedBasic_ = false;
  void bootBasic();
  void runInBasic();
  bool basicAtReady() const;
  bool pushProgram();
  void pullProgram();
  void syncBasic();
  std::vector<std::pair<std::string, std::string>> basicSlots_;
  bool basicDirty_ = false;  // slots changed and no ROM file to rewrite
};

}  // namespace sc8
