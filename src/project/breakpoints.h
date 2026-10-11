// Breakpoints and steps on source lines. A build keeps where each
// source's lines sit in the assembly it made, in project.h's LineMap.
// stopAt turns a line of a file into the address the machine stops at,
// and PcLines turns an address back into its line. An Edit carries a
// breakpoint's line through a change to the text around it. BasicLines
// sees a BASIC line start from outside the machine. rowAt and RowBreaks
// are a breakpoint on a row of a microcode set.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <map>
#include <set>

#include "asm/asm.h"
#include "core/machine.h"
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

// A line of a source file, counted from 1.
struct SourceLine {
  std::string file;
  int line = 0;
};

// The source line every instruction of a build came from. A C statement's
// instructions all belong to the statement's line, up to the next line
// the compiler marked. Made once per build, read once per instruction
// while the IDE steps a line of C.
class PcLines {
 public:
  PcLines() = default;
  PcLines(const LineMap& lines, const Assembled& assembled);

  // The line an address belongs to. Nothing for an address past the
  // program, in a .org gap, or in code no source line made.
  const SourceLine* at(int pc) const;
  // The same as a place in lines(), or -1. Two addresses of one line
  // have one place, so a step compares places and no strings.
  int id(int pc) const;
  const std::vector<SourceLine>& lines() const { return lines_; }

 private:
  std::vector<int> index_;  // by address, a place in lines_ or -1
  std::vector<SourceLine> lines_;
};

// A BASIC line starting, read off the interpreter's system page. Call
// started() after every instruction the machine runs. breakpoints.cpp
// has the rule and why it holds.
class BasicLines {
 public:
  // The number of the line that started with the last instruction, or 0.
  // Its statements have not run yet.
  int started(std::span<const uint8_t> ram);
  // Forget what was seen, for a machine powered on again.
  void reset() { *this = BasicLines(); }

 private:
  // The last two values SYS_PC held. The interpreter may write the word
  // a byte at a time, so the value before a change can be half written.
  int last_[2] = {-1, -1};
  bool wasRunning_ = false;
  bool checking_ = false;
};

// A row of a microcode set's text: the section it belongs to, its place
// in the section counted from 0, and the line of the text it is on.
struct RowStop {
  std::string section;  // an instruction's name as the set spells it, or fetch
  int row = 0;
  int line = 0;
};

// Every row of a microcode set's text, in the text's order. A line that
// ends in a colon opens a section. Every other line with something on it
// before a # is a row of it.
std::vector<RowStop> rowLines(std::string_view text);

// The row on a line of a microcode set's text, or the next row after it.
// Nothing past the last row.
std::optional<RowStop> rowAt(std::string_view text, int line);

// Breakpoints on rows of microcode. The machine stops inside an
// instruction, before the row runs. A row of fetch stops every
// instruction. The sealed set has no rows to stop at. The caller asks
// Computer::microcodeInspectable first.
class RowBreaks {
 public:
  void set(std::vector<RowStop> rows);
  bool empty() const { return rows_.empty(); }

  // The instruction at the machine's PC has a row with a breakpoint.
  // Asked between two instructions, and it costs one table read.
  bool onNext(const Machine& m) const;

  // Run rows until the machine stands before the next row with a
  // breakpoint, in the instruction it is in or about to start. True when
  // it stands before one now, and stopped() names it. False when that
  // instruction has none left, and the machine has not moved.
  bool advance(Machine& m);
  const RowStop& stopped() const { return stopped_; }

 private:
  std::vector<RowStop> rows_;
  bool anyFetch_ = false;
  bool byOp_[256] = {};
  // The moment of the last stop: the instruction count and the row's
  // place in fetch plus instruction. A run that starts there goes on.
  uint64_t lastInstr_ = ~uint64_t{0};
  size_t lastPlace_ = 0;
  RowStop stopped_;
};

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
