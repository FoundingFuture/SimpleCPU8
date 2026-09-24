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
  void run() { setRunning(true); }

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
  void cPane();
  void compileC();
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

  // The BASIC pane. Typing state for Push to RAM. The text goes into the
  // input device's key queue at the rate the machine drains it.
  // The C pane: one file, its headers read from beside it. The assembly
  // the compiler writes goes into the Source pane and is assembled there.
  std::string cText_;
  std::string cPath_;
  std::string basicText_;
  std::string basicPath_;
  std::string basicSlot_ = "PROGRAM";
  std::string typing_;
  size_t typingPos_ = 0;
  std::vector<std::pair<std::string, std::string>> basicSlots_;
  bool basicDirty_ = false;  // slots changed and no ROM file to rewrite
};

}  // namespace sc8
