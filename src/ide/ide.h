// The IDE: one Computer, one project, and the panes that look at both,
// arranged in levels. A level is a saved dock layout. BASIC holds the
// editor, the screen and the manual only. Project holds the files, the
// editor, the screen, the messages and the manual. Run holds the screen
// and the debugger. CPU holds the datapath, the flow and the microcode
// rows, with a side pane for the screen, the manual and the state.
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
#include "ide/basic_assist.h"
#include "ide/filedialog.h"
#include "ide/guide.h"
#include "ide/settings.h"
#include "ide/sprite_editor.h"
#include "project/project.h"
#include "vm/audio.h"
#include "vm/computer.h"
#include "vm/display.h"
#include "vm/keys.h"

namespace sc8 {

// BASIC is the plain level: the editor, the screen and the manual, for a
// person writing BASIC who needs nothing else in view. Project adds the
// files, the assembly and the messages. Run adds the debugger. CPU shows
// the inside of the processor.
enum class Level { Basic, Project, Run, Cpu };
constexpr int LEVEL_COUNT = 4;

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
  // project) from the command line. A picture or a .font opens its project
  // and then the asset in the sprite editor.
  void open(const std::string& path);
  // The command line's --font-view: single, strip or block. False for any
  // other name.
  bool setFontView(const std::string& name);

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

  // Settings: the projects folder and the UI scale, from the platform's
  // settings folder. applyScale sizes every font and spacing.
  Settings& settings() { return settings_; }
  void applyScale();

  // The machine's screen alone, full screen. The IDE's panes are not drawn
  // at all meanwhile, so the host spends nothing on them. Escape comes
  // back, and it does not reach the machine: Ctrl-C still breaks a BASIC
  // program. The machine runs on and plays on as it did.
  // Put the window where the layout file had it. Called once after the
  // layout file is read. A place off every monitor attached now is left
  // alone, so a window never opens out of sight.
  void applyWindow();
  void restoreSprite();
  // The IDE window itself fills the monitor, borderless. Not the same as
  // the machine's full screen, which shows the picture alone.
  void toggleWindowFill();
  bool windowFilled() const;

  void enterScreenOnly();
  void leaveScreenOnly();
  bool screenOnly() const { return screenOnly_; }
  // Draw the screen-only frame: the picture square and centred on black.
  void drawScreenOnly();

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
  // Lay an .asm document out in assembler columns (asm/format.h). True
  // when the text changed. The editor, when it holds the text, reloads it.
  bool layOut(Doc& doc);
  void saveAll();
  void saveProject();
  // A project opened from a ROM saves into that ROM: the documents are
  // built, and the ROM file is written again with the project inside.
  bool saveIntoRom();
  // Save on a ROM project asks first: the ROM is not a folder, and a save
  // burns the documents into it. The person can burn, save the project
  // as a folder instead, or stop. romSaveQuits_ says the question came
  // from Save and quit. romSaveConfirmed_ is "don't ask again" for the
  // session.
  void romSaveDialog();
  // Removing a document asks first. In a folder project the file is
  // deleted from the disk: the folder is the project, so a file left in
  // src/ would still be built. In a ROM or scratch project the document
  // leaves the project, and the ROM keeps it until the next save.
  void askRemove(const std::string& name, bool asset = false);
  void removeDialog();
  void removeDoc(const std::string& name);
  std::string removing_;  // the document the dialog asks about, or empty
  // A ROM or scratch project gained or lost a file since it was saved.
  // The documents left are not dirty, but the project is.
  bool filesChanged_ = false;
  bool removingAsset_ = false;  // the dialog asks about an asset, not a document

  // Assets: pictures, sounds and any other file the sources name. A folder
  // project keeps them in assets/, or beside the sources in the flat
  // layout. A ROM or scratch project keeps them in memory as assets/name,
  // and the ROM carries them.
  struct AssetEntry {
    std::string name;
    uintmax_t bytes = 0;
  };
  std::vector<AssetEntry> assetList() const;
  void addAsset(const std::string& path);
  void removeAsset(const std::string& name);
  // An asset's bytes, and a write of new ones, for the sprite editor. In a
  // ROM or scratch project the write marks the project changed.
  std::optional<std::vector<uint8_t>> readAsset(const std::string& name) const;
  bool writeAsset(const std::string& name, const std::vector<uint8_t>& bytes);

  // The sprite editor (sprite_pane.cpp). It opens on a double click of a
  // picture or a .font under Assets, or from New sprite or New font.
  // Opening another asset over unsaved changes asks first; pendingSprite_
  // holds what to open after.
  void spritePane();
  void openSprite(const std::string& name);
  void askNewSprite();
  static bool isPicture(const std::string& name);
  // A picture or a .font: an asset the sprite editor opens.
  static bool isDrawable(const std::string& name);
  void newSpriteDialog();
  // New font writes the built-in font as a new .font asset and opens it.
  void newFontDialog();
  void spriteSwitchDialog();
  // Render the sprite preview through the display, before the panes draw.
  void spritePreview();
  SpriteEditor sprite_;
  Display previewScreen_;
  bool spriteVisible_ = false;
  // Frames left in which the sprite panes ask for focus. A fresh layout
  // hands focus to every window it docks, so one frame is not enough.
  int focusSprite_ = 0;
  bool askNewSprite_ = false;
  std::string newSpriteName_ = "sprite.png";
  int newSpriteW_ = 16, newSpriteH_ = 16, newSpriteFrames_ = 1;
  std::optional<std::string> pendingSprite_;  // an asset to open, or "" for a new sprite
  bool askNewFont_ = false;
  std::string newFontName_ = "font.font";
  bool askSpriteSwitch_ = false;
  std::vector<uint8_t> previewPixels_;
  // The display settings live in display.txt beside settings.txt.
  void loadDisplay();
  void saveDisplay();
  bool askRomSave_ = false;
  bool romSaveQuits_ = false;
  bool romSaveConfirmed_ = false;
  // The Save menu item's label: into the folder, the ROM, or a new folder.
  std::string saveTarget() const;
  // Build the documents in memory, as a ROM project or the scratch
  // project does.
  project::Built buildInMemory();
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
  void reset();
  void powerOff();
  bool poweredOff_ = false;  // the machine is off: black screen, nothing runs
  bool screenOnly_ = false;
  // The window to go back to: its size and its place.
  int windowedW_ = 0, windowedH_ = 0;
  int windowedX_ = 0, windowedY_ = 0;
  bool filledBeforeScreen_ = false;  // the IDE window filled the monitor before F12
  // The window's last plain place: not maximized, not filling the
  // monitor, not in the system's full screen. trackWindow notes it every
  // frame, and Save layout keeps it, so the next session has a window to
  // come back to from any of those.
  int plainX_ = 0, plainY_ = 0, plainW_ = 0, plainH_ = 0;
  void trackWindow();
  bool nativeFullscreen() const;
  bool nativeBeforeScreen_ = false;  // F12 came from the system's full screen
  int pendingNative_ = 0;  // frames until the saved system full screen is entered
  void statusMenu();
  void selectMicrocode(const std::string& name);
  void applyLock();
  void toggleBreakpoint(int instr);
  void pushBreakpoints();
  void typeIntoMachine();

  // BASIC: a .bas document and the interpreter's program memory are two
  // views of one program, kept in step both ways while BASIC waits at
  // READY. An edit in the document is written into the memory, so LIST
  // shows it. A line typed on the small screen appears in the document.
  // When both sides changed since the last exchange, the lines merge by
  // number and the document wins a line both changed. While a program
  // runs, nothing moves; the changes go across at the next READY.
  void bootBasic();
  void runInBasic();
  bool basicAtReady() const;
  bool pushProgram();
  void writeProgram(const std::vector<uint8_t>& bytes);
  Doc* basicPartner();
  void syncBasic();
  // Replace a document's text. The editor, when it holds the text,
  // reloads it rather than writing its own copy back.
  void setDocText(Doc& doc, std::string text);

  // The levels and their layouts.
  void buildLayout(Level level, unsigned dockspace);
  void menuBar();
  void shortcuts();
  void quitDialog();
  void settingsDialog();

  // The panes. A pane shared by two levels takes the level's window name.
  void filesPane(const char* name, bool* open = nullptr);
  void editorPane(const char* name);
  void assemblyPane();
  void messagesPane(const char* name, bool* open = nullptr);
  void manualPane(const char* name);
  void manualBody();
  void screenPane(const char* name);
  // The picture as a square, side pixels wide, centred in the width
  // there is, with the status line under it.
  void screenBody(float side);
  void registersPane(const char* name);
  void registersBody();
  void memoryBody();
  void stackBody();
  void datapathBody();
  void flowBody();
  // The CPU level's one view: the run controls on top, then the datapath,
  // the flow, the registers, the memory and the stack as sections that
  // open and close. The side pane beside it holds the screen, the
  // registers, the memory, the stack and the manual the same way.
  void cpuPane();
  void sidePane();
  // Registers, memory and stack show in one of the two at a time.
  // Opening one in a pane folds it in the other.
  enum class Place { Cpu, Side };
  enum Section : size_t { SecDatapath, SecFlow, SecRegisters, SecMemory, SecStack, SecScreen, SecManual, SEC_COUNT };
  struct SectionState {
    bool open;
    Place place;
  };
  static constexpr SectionState SECTION_DEFAULTS[SEC_COUNT] = {
      {true, Place::Cpu}, {true, Place::Cpu},  {true, Place::Cpu}, {true, Place::Cpu},
      {true, Place::Cpu}, {true, Place::Side}, {true, Place::Side}};
  SectionState sections_[SEC_COUNT] = {};
  // Draw a section's header in a pane. True when its body shows there.
  bool sectionHeader(Section s, Place here);
  // The side pane's screen section height in pixels, picture and status
  // line together. 0 fits the picture to the pane's width. A grip under
  // the section drags it, a double click on the grip goes back to 0.
  float sideScreenHeight_ = 0.0f;
  void sideScreen();
  // The section states travel in the layout file, so Save layout keeps
  // them and Reset layout brings the defaults back.
  void registerLayoutHandler();
  // The IDE window in the layout file: where it was, how big, and whether
  // it was maximized or filled the monitor. Save layout writes it; the
  // next start reads it and applyWindow puts the window back.
  struct WindowPlace {
    bool known = false;
    int x = 0, y = 0, w = 0, h = 0;
    int monitor = 0;
    bool maximized = false;
    bool fullscreen = false;  // filled the monitor, borderless
    bool native = false;      // the system's own full screen (macOS)
  };
  WindowPlace savedWindow_;
  // The sprite editor in the layout file: shown or not, the sprite it
  // had open, and its view. restoreSprite reopens that sprite after the
  // command line opened a project, when the project has it.
  std::string savedSprite_;
  WindowPlace windowNow() const;
  void resetSections();  // also the side screen's height
  void runControls();
  void memoryPane(const char* name);
  void stackPane(const char* name);
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
  Settings settings_;
  bool askSettings_ = false;
  Settings editing_;  // the dialog's copy until Save

  // The project. projectDir_ is empty for a ROM project and for the
  // scratch project. romBase_ holds an opened ROM, whose program the
  // build keeps.
  std::string projectDir_;
  std::string projectTitle_ = "scratch";
  std::optional<Cartridge> romBase_;
  // The other files a ROM carried (assets, README), kept for the build
  // and for Save project as. carriedProject_ says the ROM had sources.
  std::vector<std::pair<std::string, std::vector<uint8_t>>> romFiles_;
  bool carriedProject_ = false;
  // The open project is one of the examples, which is never written: Save
  // is Save project as, and a build stays in memory.
  bool example_ = false;
  // The examples File, Open example lists, read once at start from the
  // checkout's examples/ by project::EXAMPLE_KINDS. A kind with no example
  // has no group.
  struct Example {
    std::string name;
    std::string title;  // the README's first line
    std::string dir;
  };
  struct ExampleGroup {
    std::string label;
    std::vector<Example> items;
  };
  std::vector<ExampleGroup> examples_;
  void readExamples();
  // Refuse a write into an example, with a note naming what was refused.
  bool refuseExample(const std::string& what);
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
  bool layoutBuilt_[LEVEL_COUNT] = {false, false, false, false};
  // The BASIC level keeps the files and the messages out of sight until
  // the Level menu shows them.
  bool basicShowsFiles_ = false;
  bool basicShowsMessages_ = false;
  // After a project opens: the BASIC level for a project of .bas files
  // only, the Project level for anything else.
  void levelForProject();
  std::string focusAfterLayout_;  // a window to bring to the front once
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

  // The memory and stack panes. A jump is a row to scroll to next frame.
  int memAddr_ = 0;
  int memJump_ = -1;
  bool memFollow_ = true;
  int stackJump_ = STACK_SIZE - 256;
  int stackShown_ = -1;
  bool stackFollow_ = true;
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
  // machine, syncBase_ the program both agreed on at the last exchange.
  std::string syncDoc_;
  // The editor's line numbering for .bas documents, and the document it
  // last watched.
  BasicAssist basicAssist_;
  std::string assistDoc_;
  std::vector<uint8_t> syncBase_;
  bool basicBooted_ = false;  // the machine runs the interpreter
  // Commands still go through the keyboard: RUN after a push, for one.
  std::string typing_;
  size_t typingPos_ = 0;
  // A push waits until the interpreter has booted and set SYS_PROG. This
  // is the command to type after it.
  bool pushPending_ = false;
  std::string afterPush_;
  // The refusal last put in Messages, so a document that stays refused
  // is named once rather than every frame.
  std::string refusalNoted_;

  // The Microcode pane's execution view (micro_panes.cpp). Off, the pane
  // walks the current instruction's rows, fetch first. On, it browses
  // every section of the set.
  bool mcBrowse_ = false;

  // The Manual pane's guide tab: which guide, its filter and its scroll.
  GuideView guideView_;
  int manualTab_ = -1;  // a tab to select next frame, -1 for none
};

}  // namespace sc8
