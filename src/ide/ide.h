// The IDE: one Computer, one project, and the panes that look at both,
// arranged in four views, one per language (ide/view.h). A view is a
// saved dock layout around the editor of its language, and every view
// runs the machine. The project column sits left of the views, outside
// them, and stays when the view changes.
//
// There is always a project. It is a folder of sources (.c, .h, .asm,
// .bas, microcode.txt) and assets, or an opened ROM (its slots and its
// microcode as documents, its program as it is), or the scratch project
// the IDE starts with until it is saved somewhere. Build turns the
// project into a ROM and puts it in the machine: C first, then the
// assembler over everything, then the ROM. A project with BASIC boots
// into the interpreter.
// docs/standalone.md, section "IDE levels", describes the levels the
// views replaced.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "asm/asm.h"
#include "basic/program.h"
#include "ide/basic_assist.h"
#include "ide/filedialog.h"
#include "ide/guide.h"
#include "ide/listing.h"
#include "ide/settings.h"
#include "ide/sprite_editor.h"
#include "ide/view.h"
#include "project/breakpoints.h"
#include "project/project.h"
#include "vm/audio.h"
#include "vm/computer.h"
#include "vm/display.h"
#include "vm/keys.h"

namespace sc8 {

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
// whether the text differs from the disk. Its kind follows its name
// (ide/view.h). `open` says its tab shows in its view's editor. A closed
// document stays in the project, and the project column opens it again.
// A library is a .bas file of the libraries folder that a program of the
// project INCLUDEs. It is no source of the project: `path` is its own
// file, and it is read only until the person chooses Edit library.
struct Doc {
  std::string name;
  std::string text;
  bool dirty = false;
  bool open = true;
  bool library = false;
  bool readOnly = false;
  std::string path;
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
  // The command line's --listing, the MICROCODE view's choice: microcode,
  // assembly, c or basic. False for any other name.
  bool setListing(const std::string& key);

  void setView(View view);
  View view() const { return view_; }

  // The command line's --speed and --run: a label from the browser's
  // speed menu, then start running. False for a value the ladder lacks.
  // The command line's speed holds in every view.
  bool setSpeed(const std::string& value);
  // --run: build the project and run it. A .bas opened from the command
  // line runs in BASIC instead, the quick way.
  void run();
  // --do: one of the Run menu's items by name, in the view on screen:
  // run, check, step, pause or stop. False for any other name.
  bool menuAction(const std::string& name);
  // --break: a breakpoint on a document's line, written name:line. A .bas
  // takes its BASIC line number. False when the document is not open.
  bool addBreak(const std::string& spec);

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
  // Nothing on screen changes without input: the machine is not running,
  // the sound chip is silent and no sprite preview is playing. The host
  // loop then sleeps until the next event, see vm/redraw.h.
  bool idle() const;

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
  // The project column's list of assets, never read on every frame. A project
  // opening, an asset the IDE writes or removes, and the window coming
  // back to the front each read the folder again.
  std::vector<AssetEntry> shownAssets_;
  bool assetsStale_ = true;
  bool windowFocused_ = true;
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
  // Make a document the one its view's editor shows, without leaving the
  // view on screen. openDoc also shows its tab and switches to its view.
  void activate(size_t index);
  void openDoc(size_t index);
  // The document the view on screen edits, and any view's.
  Doc* activeDoc() { return activeDocOf(view_); }
  Doc* activeDocOf(View view);
  Doc* docNamed(const std::string& name);
  void settleActive();
  // A new document of the project, opened in its view. False with a note
  // for a name that is not a source or is taken.
  bool newDoc(const std::string& name);
  void addDoc(const std::string& name, const std::string& text);
  void buildProject(bool run);
  void insertBuilt(project::Built& built, const std::string& what);
  void loadCartridge(Cartridge cart, const std::string& what);
  void burnRom(const std::string& path);
  void rebuildListing();
  void note(std::string message) { messages_.push_back({std::move(message), false}); }
  // A build's or a microcode set's errors: each on a red line in Messages,
  // and the error tone once, when Preferences has it on.
  void reportErrors(const std::vector<std::string>& errors);
  void showMessages();
  // The editor's tab area: the listing shares it in two views, and a run
  // or a step brings the listing to the front.
  void showRunTab();
  std::string editorTab() const;
  static void bringForward(const std::string& name);
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

  // The Run menu (run_menu.cpp). Each item works at the level of the
  // view on screen: a line of BASIC, a line of C, an instruction, a row.
  void runMenu();
  void viewRun();
  void viewCheck();
  void viewStep();
  void viewStop();
  const char* stepLabel() const;
  // A document changed since the last build, or the last build failed.
  bool needsBuild() const;
  bool lastBuildOk_ = false;
  // BASIC runs a program now, and is not waiting at its prompt.
  bool basicMidProgram() const;
  void checkBasicDoc(Doc& doc);
  void checkMicrocodeDoc(const Doc& doc);
  // The Microcode submenu: the project's microcode.txt into the machine,
  // and the lock that keeps the running set.
  void applyProjectMicrocode(bool lock);
  void toggleMicrocodeLock();
  // A step of BASIC or C: the machine runs flat out until stepUntil_
  // answers true, asked after every instruction. Pause ends it early.
  void stepC();
  void stepBasic();
  void beginStep(std::function<bool()> until);
  void refreshWatch();
  bool watchHit();
  std::function<bool()> stepUntil_;
  bool stepDone_ = false;
  bool stepSawProgram_ = false;  // a BASIC step saw the program running
  // The last build's source line per address, and which of those lines
  // are lines of a C document, where a C step may stop.
  project::PcLines pcLines_;
  std::vector<char> stepLine_;
  // The BASIC line a watch saw start with the last instruction, or 0.
  project::BasicLines basicLines_;
  int basicStarted_ = 0;
  // Where the machine stands, in the words of the view's language.
  std::string whereNote();
  int currentLine(const Doc& doc);
  int standingLine(const Doc& doc);
  // The current line the editor last scrolled into view, and its document.
  int shownCurrent_ = 0;
  std::string shownCurrentDoc_;
  // Bring the document the machine stands in to the front of its editor.
  void showWhere();
  // A breakpoint set in the listing's margin. One on the address of an
  // editor breakpoint removes that one.
  void toggleBreakpoint(int instr);
  // The machine stops at the listing's addresses and at every editor
  // breakpoint the last build placed.
  void pushBreakpoints();
  bool breaksAt(uint16_t pc) const;
  // What Messages says when the machine stops at a breakpoint.
  std::string stopNote() const;
  // Breakpoints set in the editor's number column, by document. A line
  // maps to the address the last build gave it, or to nothing until a
  // build does. The lines refer to `text` and follow every edit made to it.
  struct SourceBreaks {
    std::string text;
    std::map<int, std::optional<uint16_t>> lines;
  };
  std::map<std::string, SourceBreaks> sourceBreaks_;
  project::LineMap lineMap_;                       // the last build's
  std::map<std::string, std::string> builtTexts_;  // each document as the last build read it
  std::vector<int> gutterMarks_;                   // the active document's breakpoint lines
  void toggleSourceBreak(const Doc& doc, int line);
  // A breakpoint on a row of microcode.txt. Its line lives in
  // sourceBreaks_ with no address and follows edits as the others do.
  // rowBreaks_ holds the rows those lines name, as the machine meets them.
  void toggleRowBreak(const Doc& doc, int line);
  void rebuildRowBreaks();
  project::RowBreaks rowBreaks_;
  std::vector<project::RowStop> rowMarks_;  // the same rows, for the listing's dots
  std::string rowNote() const;
  // Breakpoints on BASIC lines, by document and line number. The watch
  // stops the machine when such a line starts. A line that leaves the
  // document takes its breakpoint along. project/breakpoints.cpp has
  // the rule.
  std::map<std::string, std::set<int>> basicBreaks_;
  void toggleBasicBreak(const Doc& doc, int row);
  // The document whose program BASIC runs: the one in step, or the
  // AUTORUN a built project boots into.
  std::string runningBasicDoc() const;
  // What a watch stopped the machine for, for Messages.
  std::string watchNote_;
  // A document's line to show, asked for by the Breakpoints pane.
  void selectLine(const std::string& doc, int line);
  std::string revealDoc_;
  int revealLine_ = 0;
  // Move each document's breakpoints with the edits made since they were
  // placed. A line the edits removed loses its breakpoint.
  void followEdits();
  // After a build: each editor breakpoint to the address its line, or the
  // next line with code, now has.
  void placeSourceBreaks();
  // A new project, ROM or scratch: no document keeps a breakpoint.
  void forgetSourceBreaks();
  void typeIntoMachine();

  // BASIC (basic_files.cpp): a .bas document and its file in the
  // interpreter's store are two views of one program, a tab per file,
  // kept in step both ways while BASIC waits at READY. An edit in a
  // document is written into the store, so LIST shows it. A line typed
  // on the small screen appears in the document. When both sides changed
  // a file since the last exchange, its lines merge by number and the
  // document wins a line both changed. While a program runs, nothing
  // moves. The changes go across at the next READY.
  void bootBasic();
  void runInBasic();
  bool basicAtReady() const;
  basic::Store machineStore() const;
  bool writeStore(const basic::Store& store);
  bool pushMain();
  // The document Run runs as the main file, and the document a file of
  // the store is, by its name.
  Doc* mainBasicDoc();
  Doc* docOfFile(const std::string& file);
  bool inStep(const Doc& doc) const;
  void syncBasic();
  // The IDE as the storage device's host: LOAD, INCLUDE and FILE find
  // the project's documents, the libraries and files by path, and a SAVE
  // goes into the project.
  void hostStorage();
  void takeSaves();
  std::vector<std::pair<std::string, std::string>> savedSlots_;
  // The folders an INCLUDE finds a library in, and the libraries the
  // project's programs include as read only documents.
  std::vector<std::filesystem::path> libraryFolders() const;
  void refreshLibraries();
  // The list of libraries is brought up to date at the start of the next
  // frame. A pane holds a document while it draws, and a refresh may
  // move every document.
  bool librariesStale_ = true;
  // The slots a check of a .bas needs: every .bas of the project, then
  // every library the INCLUDE chain reaches, read from its file.
  BasicSlots basicSlots();
  void sortDocs();
  // Replace a document's text. The editor, when it holds the text,
  // reloads it rather than writing its own copy back.
  void setDocText(Doc& doc, std::string text);

  // The views and their layouts.
  bool buildLayout(View view, unsigned dockspace, float width, float height);
  // A pane's window name in a view: the base name and the view's key.
  // A pane two views show is two windows and never moves between them.
  static std::string paneName(const char* base, View view);
  std::string pane(const char* base) const { return paneName(base, view_); }
  void menuBar();
  // The four tab buttons at the left of the menu bar.
  void viewTabs();
  void shortcuts();
  void quitDialog();
  void settingsDialog();

  // The project column (project_column.cpp): a fixed column at the left
  // of the window, under the menu bar, drawn before the view's dockspace.
  // It is not a docked pane, so no view owns it and no layout loses it.
  // Open, it is a tree of the project. Collapsed, it is a narrow bar.
  // Draws it and answers its width.
  float projectColumn();
  void columnTree();
  void columnBar();
  void toggleColumn();
  void newFileDialog();
  // Show the assembly the last build made: the C view's Assembly tab for
  // a project with C, the ASSEMBLY view's listing otherwise.
  void showAssembly();
  // The column's width while open, in pixels at UI size 1. Save layout
  // keeps it with the open state.
  float columnWidth_ = COLUMN_WIDTH;
  static constexpr float COLUMN_WIDTH = 230.0f;
  static constexpr float COLUMN_MIN = 140.0f;
  static constexpr float COLUMN_MAX = 640.0f;
  // settings.txt had a column line, so the layout file's does not apply.
  bool columnFromSettings_ = false;
  bool askNewFile_ = false;

  // The panes. A pane two views show takes a window name per view.
  // editorPane is a view's editor: one tab per open document of its
  // kind, and under the tabs the active document with its buttons.
  void editorPane(View view);
  void editorBody(Doc& doc);
  // The buttons over a view's text. The listing passes no document.
  void viewButtons(DocKind kind, Doc* doc);
  void assemblyPane(const char* name);
  void messagesPane(const char* name);
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
  // The MICROCODE view's CPU pane: the run controls on top, then the datapath,
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
  void breakpointsPane(const char* pane);
  void listingPane(const char* name);
  // The MICROCODE view's Listing tab shows one listing of four. The
  // choice is kept, and a listing the program lacks gives way while it
  // is missing (ide/listing.h).
  Listing listingChoice_ = Listing::Microcode;
  Listings listingsHere() const;
  Listing chooseListing();
  void listingHead(const char* follow);
  unsigned standingBar() const;
  void assemblyListing();
  void rowsListing();
  void sourceListing(Listing which);
  // The running set as text, a line per section and per row.
  struct RowsText {
    bool made = false;
    std::string of;  // the set's name when the lines were made
    std::vector<std::string> lines;
    std::vector<project::RowStop> rows;
    std::vector<int> rowAt;  // per line, a place in rows or -1
  };
  RowsText rowsText_;
  const RowsText& rowsText();
  void toggleListedRow(const project::RowStop& row);
  // The document the C listing and the BASIC listing showed last.
  std::string listingDoc_[2];
  // The place a listing last scrolled to. The assembly keeps shownPc_.
  struct ListingSpot {
    Listing listing = Listing::Assembly;
    std::string doc;
    int line = -1;
    bool operator==(const ListingSpot&) const = default;
  };
  ListingSpot shownSpot_;
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
  // settings folder's examples/ by project::EXAMPLE_KINDS. A kind with no example
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
  std::string builtRom_;  // the ROM file the last build wrote, or empty
  std::vector<Doc> docs_;
  // Each view's active document by name, and a tab its editor selects at
  // the next frame.
  std::string activeName_[VIEW_COUNT];
  std::string selectTab_[VIEW_COUNT];
  std::string newDocName_;
  bool openedBasic_ = false;   // the command line opened a .bas
  std::string builtAssembly_;  // what the assembler saw at the last build
  Assembled assembled_;
  bool haveSource_ = false;  // the listing has source lines
  std::vector<ListLine> listing_;
  struct Message {
    std::string text;
    bool error;  // a build error, drawn on red
  };
  std::vector<Message> messages_;

  View view_ = View::Assembly;
  bool layoutBuilt_[VIEW_COUNT] = {false, false, false, false};
  // A view shown for the first time opens its own guide. After that the
  // view keeps the guide its reader picked.
  bool viewShown_[VIEW_COUNT] = {false, false, false, false};
  // Windows to focus, one a frame, before focusAfterLayout_: the tabs a
  // fresh layout shows in front.
  std::vector<std::string> focusQueue_;
  // After a project opens: the MICROCODE view for a project with a
  // microcode.txt, else the view of the document a reader starts in.
  void viewForProject();
  std::string focusAfterLayout_;  // a window to bring to the front once
  std::string frontTab_;          // a tab to bring to the front, after those
  bool done_ = false;
  bool askQuit_ = false;

  bool running_ = false;
  bool freshRun_ = false;  // the next paced frame is a run's first
  // Each view's speed, an index into the ladder. view.h has the defaults.
  int speed_[VIEW_COUNT] = {0, 0, 0, 0};
  // The speed the view on screen runs at. The trace speed needs readable
  // rows, so under the sealed set it answers the slowest instruction rate.
  int speedIndex() const;
  void pickSpeed(int index);
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

  // The Execution pane. lockedMicrocode_ holds a set the machine keeps
  // whatever a project or ROM brings.
  int selectedSection_ = 0;
  std::optional<std::string> lockedMicrocode_;

  // The manual pane.
  std::string manualFilter_;
  std::string manualPage_ = "LD";

  // BASIC sync state: by file name, the records the document and the
  // store agreed on at the last exchange. A file with an entry is in
  // step. shownEdit_ is the current file the IDE last saw, and
  // lastBasicTab_ the tab it last saw in the BASIC view's editor.
  std::map<std::string, std::vector<uint8_t>> syncBases_;
  int shownEdit_ = -1;
  std::string lastBasicTab_;
  // The editor's line numbering for .bas documents, and the document it
  // last watched.
  BasicAssist basicAssist_;
  std::string assistDoc_;
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

  // The Manual pane's guide tab, per view: which guide, its filter and
  // its scroll.
  GuideView guideView_[VIEW_COUNT];
  int manualTab_ = -1;  // a tab to select next frame, -1 for none
};

}  // namespace sc8
