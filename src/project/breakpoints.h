// Breakpoints on source lines. A build keeps where each source's lines sit
// in the assembly it made, in project.h's LineMap. stopAt turns a line of
// a file into the address the machine stops at. An Edit carries a
// breakpoint's line through a change to the text around it.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "asm/asm.h"
#include "project/project.h"

namespace sc8::project {

struct Stop {
  int line;  // the line that holds the code, at or after the one asked for
  int pc;    // its first instruction
};

// The first instruction of a file's line, or of the next line of the same
// file that has one. Nothing past the file's last instruction, and nothing
// for a file the build did not take in. Lines count from 1.
std::optional<Stop> stopAt(const LineMap& lines, const Assembled& assembled, const std::string& file, int line);

// What one edit did to a text's lines, from the text before and after it.
// The edit is the part between the longest common start and end.
class Edit {
 public:
  Edit(std::string_view before, std::string_view after);

  // Where a line of the text before sits in the text after, counting from
  // 1. A line the edit removed whole, newline and all, is nothing. A line
  // the edit cut into lands on the line where the edit starts.
  std::optional<int> follow(int line) const;

 private:
  bool same_ = true;
  // The lines the edit starts and ends in, and whether it starts or ends
  // at that line's first character.
  int first_ = 1;
  bool firstAtStart_ = false;
  int last_ = 1;
  bool lastAtStart_ = false;
  int delta_ = 0;  // lines added, less lines removed
};

}  // namespace sc8::project
