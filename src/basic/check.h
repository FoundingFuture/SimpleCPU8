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
// and CHECK it, again after each mistake from the line after it. The
// cartridge's own BASIC slots are left out, so its AUTORUN does not run.
Checked checkProgram(const Cartridge& interpreter, const std::string& text);

}  // namespace sc8::basic
