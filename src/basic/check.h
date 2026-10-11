// The project build's check of a .bas file. BASIC's own CHECK command
// reads the program on a machine with no window. A build then reports
// what RUN would report from the text, in RUN's words. run.c's rt_check
// says what CHECK reads and what it leaves to RUN.
#pragma once

#include <string>
#include <vector>

#include "core/cartridge.h"

namespace sc8::basic {

struct Issue {
  int line = 0;         // the BASIC line number, 0 when BASIC named none
  std::string message;  // BASIC's message without its "? "
};

struct Checked {
  std::vector<Issue> issues;  // in line order, one per line at most
  std::string failure;        // why the check could not finish, or empty
};

// Boot `interpreter`, a cartridge with BASIC's program, LOAD `text` into it
// and CHECK it, again after each mistake from the line after it. The text
// is the slot `name`, the file's own name in the store, so a line that
// names its own file finds it. The cartridge's other slots stay for the
// program to INCLUDE. Its AUTORUN is left out, so it does not run at
// power on, and a program of that name is checked under another.
Checked checkProgram(const Cartridge& interpreter, const std::string& text, const std::string& name = "");

}  // namespace sc8::basic
