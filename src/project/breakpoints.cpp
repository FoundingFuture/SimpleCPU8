#include "project/breakpoints.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <iterator>
#include <map>

#include "basic/program.h"
#include "core/isa.h"

namespace sc8::project {

namespace {

int newlines(std::string_view text) { return static_cast<int>(std::count(text.begin(), text.end(), '\n')); }

// The first slot an assembly line became. Assembled::lineToInstr keeps the
// last one, and a line can make more than one. A .org gap's slots hold
// line 0, which no source line is.
std::optional<int> firstSlot(const Assembled& a, int asmLine) {
  for (size_t i = 0; i < a.instrToLine.size(); i++) {
    if (a.instrToLine[i] == asmLine) return static_cast<int>(i);
  }
  return std::nullopt;
}

// The first assembly line in [from, end) that holds an instruction.
std::optional<int> firstCode(const Assembled& a, int from, int end) {
  auto it = a.lineToInstr.lower_bound(from);
  if (it == a.lineToInstr.end() || it->first >= end) return std::nullopt;
  return it->first;
}

}  // namespace

std::optional<Stop> stopAt(const LineMap& lines, const Assembled& assembled, const std::string& file, int line) {
  if (line < 1) return std::nullopt;
  // A file's lines run from its span's .code line to the next span's.
  auto spanOf = [&](const std::string& name) -> std::optional<std::pair<int, int>> {
    for (size_t i = 0; i < lines.spans.size(); i++) {
      if (lines.spans[i].file != name) continue;
      const int end = i + 1 < lines.spans.size() ? lines.spans[i + 1].firstLine : INT_MAX;
      return std::pair{lines.spans[i].firstLine, end};
    }
    return std::nullopt;
  };

  if (auto c = lines.cLines.find(file); c != lines.cLines.end()) {
    const auto span = spanOf("generated.asm");
    if (!span) return std::nullopt;
    // A line whose first instruction is also the next marked line's made
    // no code of its own, as a declaration does. The later line holds it.
    std::optional<Stop> found;
    for (auto it = c->second.lower_bound(line); it != c->second.end(); ++it) {
      const auto code = firstCode(assembled, span->first + it->second, span->second);
      if (!code) break;
      const auto pc = firstSlot(assembled, *code);
      if (!pc) break;
      if (found && found->pc != *pc) break;
      found = Stop{it->first, *pc};
    }
    return found;
  }

  const auto span = spanOf(file);
  if (!span) return std::nullopt;
  const auto code = firstCode(assembled, span->first + line, span->second);
  if (!code) return std::nullopt;
  const auto pc = firstSlot(assembled, *code);
  if (!pc) return std::nullopt;
  return Stop{*code - span->first, *pc};
}

PcLines::PcLines(const LineMap& lines, const Assembled& assembled) {
  // The compiler's marks by the line of the whole assembly they sit on.
  // The mark at or before an instruction's line names its C line.
  std::map<int, SourceLine> marks;
  int generatedFirst = 0;
  int generatedEnd = 0;
  for (size_t i = 0; i < lines.spans.size(); i++) {
    if (lines.spans[i].file != "generated.asm") continue;
    generatedFirst = lines.spans[i].firstLine;
    generatedEnd = i + 1 < lines.spans.size() ? lines.spans[i + 1].firstLine : INT_MAX;
  }
  for (const auto& [file, byLine] : lines.cLines) {
    for (const auto& [line, asmLine] : byLine) marks[generatedFirst + asmLine] = SourceLine{file, line};
  }
  std::map<std::pair<std::string, int>, int> known;
  index_.assign(assembled.instrToLine.size(), -1);
  for (size_t pc = 0; pc < assembled.instrToLine.size(); pc++) {
    const int asmLine = assembled.instrToLine[pc];
    if (asmLine <= 0) continue;
    SourceLine found;
    if (generatedFirst > 0 && asmLine > generatedFirst && asmLine < generatedEnd) {
      auto mark = marks.upper_bound(asmLine);
      if (mark == marks.begin()) continue;
      found = std::prev(mark)->second;
    } else {
      const LineMap::Span* span = nullptr;
      for (const LineMap::Span& s : lines.spans) {
        if (s.firstLine <= asmLine) span = &s;
      }
      if (!span) continue;
      found = SourceLine{span->file, asmLine - span->firstLine};
    }
    const auto [slot, fresh] = known.try_emplace({found.file, found.line}, static_cast<int>(lines_.size()));
    if (fresh) lines_.push_back(found);
    index_[pc] = slot->second;
  }
}

int PcLines::id(int pc) const {
  if (pc < 0 || static_cast<size_t>(pc) >= index_.size()) return -1;
  return index_[static_cast<size_t>(pc)];
}

const SourceLine* PcLines::at(int pc) const {
  const int i = id(pc);
  return i < 0 ? nullptr : &lines_[static_cast<size_t>(i)];
}

// DESIGN: a BASIC step and a BASIC breakpoint are the host watching
// memory. The interpreter has no hook for them and the program is not
// changed, so the machine never lies about what it runs.
//
// RUN's loop does three things for every line, in rt_run of run.c. It
// writes the line's number to SYS_ERR_LINE. It moves SYS_PC from the
// line's offset to the offset of the line after it. Then it runs the
// line's statements. So a line starts at the instruction that leaves
// SYS_PC one record past the offset it held. That record's number is in
// SYS_ERR_LINE then. A jump also moves SYS_PC, but SYS_ERR_LINE still
// names the line that jumped. So a jump to the line after the next is
// not a start. SYS_PC alone does not say which line runs: while a line runs it
// already holds the next line's offset.
//
// CHECK walks the lines the same way without running them. It sets
// SYS_RUNNING after it wrote the first line's number, where RUN and a
// line typed at the prompt set it with SYS_ERR_LINE at 0. So a stretch
// of SYS_RUNNING that began with a line number is CHECK reading, and no
// line in it starts.
//
// The watch costs a few byte reads per instruction, so it holds at every
// speed, MAX included.
int BasicLines::started(std::span<const uint8_t> ram) {
  auto word = [&](size_t at) { return at + 1 < ram.size() ? (ram[at] << 8) | ram[at + 1] : 0; };
  const bool running = ram[basic::SYS_RUNNING] == 1;
  if (running && !wasRunning_) checking_ = word(basic::SYS_ERR_LINE) != 0;
  wasRunning_ = running;
  const int pc = word(basic::SYS_PC);
  if (pc == last_[0]) return 0;
  int line = 0;
  if (running && !checking_) {
    const size_t prog = static_cast<size_t>(word(basic::SYS_PROG));
    for (const int from : last_) {
      if (from < 0 || prog + static_cast<size_t>(from) + 2 >= ram.size()) continue;
      const size_t record = prog + static_cast<size_t>(from);
      const int number = word(record);
      if (number == 0 || pc != from + ram[record + 2] || word(basic::SYS_ERR_LINE) != number) continue;
      line = number;
    }
  }
  last_[1] = last_[0];
  last_[0] = pc;
  return line;
}

std::vector<RowStop> rowLines(std::string_view text) {
  std::vector<RowStop> out;
  std::string section;
  int row = 0;
  int lineNo = 0;
  for (size_t at = 0; at <= text.size();) {
    size_t nl = text.find('\n', at);
    if (nl == std::string_view::npos) nl = text.size();
    std::string_view line = text.substr(at, nl - at);
    at = nl + 1;
    lineNo++;
    line = line.substr(0, line.find('#'));
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.remove_suffix(1);
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.remove_prefix(1);
    if (line.empty()) continue;
    if (line.back() == ':') {
      section = std::string(line.substr(0, line.size() - 1));
      row = 0;
    } else {
      out.push_back({section, row++, lineNo});
    }
  }
  return out;
}

std::optional<RowStop> rowAt(std::string_view text, int line) {
  if (line < 1) return std::nullopt;
  for (RowStop& r : rowLines(text)) {
    if (r.line >= line) return std::move(r);
  }
  return std::nullopt;
}

void RowBreaks::set(std::vector<RowStop> rows) {
  rows_ = std::move(rows);
  anyFetch_ = false;
  std::fill(std::begin(byOp_), std::end(byOp_), false);
  for (const RowStop& r : rows_) {
    if (r.section == "fetch") anyFetch_ = true;
    else if (const OpDef* def = opByName(r.section)) byOp_[def->op] = true;
  }
}

bool RowBreaks::onNext(const Machine& m) const {
  if (rows_.empty()) return false;
  if (anyFetch_) return true;
  return m.pc < m.program.size() && byOp_[m.program[m.pc].op];
}

bool RowBreaks::advance(Machine& m) {
  if (rows_.empty() || m.status != Status::Running) return false;
  // The instruction the sequencer is in, or the one it starts next.
  const Machine::SequencerPos pos = m.sequencer();
  const bool between = pos.section < 0;
  const size_t at = between ? m.pc : m.lastInstrPc;
  if (at >= m.program.size()) return false;
  const OpDef* def = opByCode(m.program[at].op);
  const Rows* fetch = m.microcode().get("fetch");
  const Rows* own = def ? m.microcode().get(def->name) : nullptr;
  const size_t fetchRows = fetch ? fetch->size() : 0;
  // A row's place counts the fetch rows first, then the instruction's.
  const size_t here = between ? 0 : pos.inFetch ? pos.rowIndex : fetchRows + pos.rowIndex;
  const bool stoppedHere = lastInstr_ == m.instructions && lastPlace_ == here;
  const RowStop* next = nullptr;
  size_t nextPlace = 0;
  for (const RowStop& r : rows_) {
    size_t place = 0;
    if (r.section == "fetch") {
      if (static_cast<size_t>(r.row) >= fetchRows) continue;
      place = static_cast<size_t>(r.row);
    } else {
      if (!def || !own || r.section != def->name || static_cast<size_t>(r.row) >= own->size()) continue;
      place = fetchRows + static_cast<size_t>(r.row);
    }
    if (place < here || (place == here && stoppedHere)) continue;
    if (!next || place < nextPlace) {
      next = &r;
      nextPlace = place;
    }
  }
  if (!next) return false;
  for (size_t i = here; i < nextPlace; i++) {
    if (!m.microStep()) return false;
  }
  lastInstr_ = m.instructions;
  lastPlace_ = nextPlace;
  stopped_ = *next;
  return true;
}

Edit::Edit(std::string_view before, std::string_view after) {
  const size_t shorter = std::min(before.size(), after.size());
  size_t p = 0;
  while (p < shorter && before[p] == after[p]) p++;
  if (p == before.size() && p == after.size()) return;
  same_ = false;
  size_t s = 0;
  while (s < shorter - p && before[before.size() - 1 - s] == after[after.size() - 1 - s]) s++;
  const size_t oldEnd = before.size() - s;
  const size_t newEnd = after.size() - s;
  first_ = 1 + newlines(before.substr(0, p));
  firstAtStart_ = p == 0 || before[p - 1] == '\n';
  last_ = 1 + newlines(before.substr(0, oldEnd));
  lastAtStart_ = oldEnd == 0 || before[oldEnd - 1] == '\n';
  delta_ = newlines(after.substr(p, newEnd - p)) - newlines(before.substr(p, oldEnd - p));
}

std::optional<int> Edit::follow(int line) const {
  if (same_ || line < first_) return line;
  if (line > last_ || (line == last_ && lastAtStart_)) return line + delta_;
  if ((line > first_ || firstAtStart_) && line < last_) return std::nullopt;
  return first_;
}

}  // namespace sc8::project
