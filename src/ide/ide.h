// The IDE: one Computer, a source buffer, and the panes that look at both.
#pragma once

#include <string>
#include <vector>

#include "asm/asm.h"
#include "vm/computer.h"
#include "vm/display.h"

namespace sc8 {

class Ide {
 public:
  Ide();

  // Open a ROM or a source file from the command line.
  void open(const std::string& path);

  // Called once per host frame, before rlImGuiBegin: paces the machine and
  // renders the screen through the CRT shader.
  void update();

  // Called once per host frame between rlImGuiBegin and rlImGuiEnd.
  void frame();

  DisplaySettings display;

 private:
  void assembleSource();
  void burnRom();
  void loadRomFile(const std::string& path);

  void defaultLayout(unsigned dockspace);
  void menuBar();
  void sourcePane();
  void machinePane();
  void screenPane();
  void microcodePane();
  void romPane();
  void messagesPane();

  Computer computer_;
  Display screen_;
  std::string sourcePath_;
  std::string source_;
  std::string romPath_;
  Assembled assembled_;
  bool assembledOk_ = false;
  bool running_ = false;
  int fps_ = 60;
  double owed_ = 0.0;
  std::vector<std::string> messages_;
  int selectedSection_ = 0;
  bool layoutDone_ = false;
};

}  // namespace sc8
