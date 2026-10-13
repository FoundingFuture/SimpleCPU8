// Breakpoints on source lines: the address a file's line stops at, and
// where a breakpoint's line goes when the text around it is edited.

#include <doctest.h>

#include <optional>
#include <string>
#include <vector>

#include "core/machine.h"
#include "core/mcparse.h"
#include "core/microcode.h"
#include "project/breakpoints.h"
#include "project/project.h"

#if SC8_HAVE_BASIC
#include "basic/program.h"
#include "basic/session.h"
#endif

using namespace sc8;
using namespace sc8::project;

namespace {

Built buildOf(const std::vector<Source>& sources) {
  Built b = buildSources(sources, Assets{}, Options{}, "test", "");
  REQUIRE_MESSAGE(b.cartridge, (b.errors.empty() ? std::string() : b.errors[0]));
  return b;
}

std::optional<Stop> stopOf(const Built& b, const std::string& file, int line) {
  return stopAt(b.lines, b.assembled, file, line);
}

const char* C_SRC = R"(unsigned char a;
int main(void)
{
    a = 1;

    a = 2;
    a = 3;
    return 0;
}
)";

}  // namespace

TEST_SUITE("a breakpoint's address") {
  TEST_CASE("an instruction line stops at its instruction") {
    const Built b = buildOf({{"main.asm", "start:  LD A <- 1\n        INC A\n        HLT\n"}});
    const auto s = stopOf(b, "main.asm", 2);
    REQUIRE(s);
    CHECK(s->line == 2);
    CHECK(s->pc == 1);
  }

  TEST_CASE("a line without code stops at the next line that has some") {
    const Built b = buildOf({{"main.asm", "        LD A <- 1\n; a comment\n\n        HLT\n"}});
    const auto s = stopOf(b, "main.asm", 2);
    REQUIRE(s);
    CHECK(s->line == 4);
    CHECK(s->pc == 1);
  }

  TEST_CASE("nothing after the file's last instruction stops anywhere") {
    const Built b = buildOf({{"a.asm", "        HLT\n.data\nx: db 1\n"}, {"b.asm", "        HLT\n"}});
    CHECK_FALSE(stopOf(b, "a.asm", 2));
    CHECK_FALSE(stopOf(b, "a.asm", 40));
  }

  TEST_CASE("each file counts its own lines, in the order the build joins them") {
    const Built b = buildOf({{"b.asm", "bee:    INC A\n        RET\n"}, {"a.asm", "        JSR bee\n        HLT\n"}});
    const auto a1 = stopOf(b, "a.asm", 1);
    const auto b1 = stopOf(b, "b.asm", 1);
    REQUIRE(a1);
    REQUIRE(b1);
    CHECK(a1->pc == 0);
    CHECK(b1->pc == 2);
  }

  TEST_CASE("a file the build never saw stops nowhere") {
    const Built b = buildOf({{"main.asm", "        HLT\n"}});
    CHECK_FALSE(stopOf(b, "other.asm", 1));
  }

  TEST_CASE("a C statement stops at its first instruction, later lines later") {
    const Built b = buildOf({{"main.c", C_SRC}});
    const auto s4 = stopOf(b, "main.c", 4);
    const auto s6 = stopOf(b, "main.c", 6);
    const auto s7 = stopOf(b, "main.c", 7);
    REQUIRE(s4);
    REQUIRE(s6);
    REQUIRE(s7);
    CHECK(s4->line == 4);
    CHECK(s6->pc > s4->pc);
    CHECK(s7->pc > s6->pc);
    // The instruction before the stop belongs to an earlier line, so the
    // machine stops before the statement runs.
    const auto pc = static_cast<size_t>(s6->pc);
    CHECK(b.assembled.instrToLine[pc - 1] < b.assembled.instrToLine[pc]);
  }

  TEST_CASE("a C line without a statement stops at the next statement") {
    const Built b = buildOf({{"main.c", C_SRC}});
    const auto blank = stopOf(b, "main.c", 5);
    const auto six = stopOf(b, "main.c", 6);
    REQUIRE(blank);
    REQUIRE(six);
    CHECK(blank->line == 6);
    CHECK(blank->pc == six->pc);
  }

  TEST_CASE("a declaration that makes no code stops at the statement after it") {
    const Built b = buildOf({{"main.c", "unsigned char a;\nint main(void)\n{\n    int i;\n    a = 1;\n    return 0;\n}\n"}});
    const auto decl = stopOf(b, "main.c", 4);
    const auto five = stopOf(b, "main.c", 5);
    REQUIRE(decl);
    REQUIRE(five);
    CHECK(decl->line == 5);
    CHECK(decl->pc == five->pc);
  }

  TEST_CASE("an assembly file beside C counts from its own first line") {
    const Built b = buildOf({{"main.c", "void tick(void);\nint main(void)\n{\n    tick();\n    return 0;\n}\n"},
                             {"tick.asm", "tick:   INC A\n        RET\n"}});
    const auto s = stopOf(b, "tick.asm", 1);
    REQUIRE(s);
    CHECK(s->line == 1);
    CHECK(b.assembled.labels.at("tick").value == s->pc);
  }
}

TEST_SUITE("the source line of an address") {
  TEST_CASE("every instruction of a C statement belongs to the statement's line") {
    const Built b = buildOf({{"main.c", C_SRC}});
    const PcLines lines(b.lines, b.assembled);
    const auto s4 = stopOf(b, "main.c", 4);
    const auto s6 = stopOf(b, "main.c", 6);
    REQUIRE(s4);
    REQUIRE(s6);
    for (int pc = s4->pc; pc < s6->pc; pc++) {
      const SourceLine* at = lines.at(pc);
      REQUIRE(at);
      CHECK(at->file == "main.c");
      CHECK(at->line == 4);
    }
    const SourceLine* six = lines.at(s6->pc);
    REQUIRE(six);
    CHECK(six->line == 6);
  }

  TEST_CASE("an assembly file's instruction belongs to its own line") {
    const Built b = buildOf({{"main.asm", "start:  LD A <- 1\n; a comment\n        INC A\n        HLT\n"}});
    const PcLines lines(b.lines, b.assembled);
    const SourceLine* at = lines.at(1);
    REQUIRE(at);
    CHECK(at->file == "main.asm");
    CHECK(at->line == 3);
  }

  TEST_CASE("an address past the program belongs to no line") {
    const Built b = buildOf({{"main.asm", "        HLT\n"}});
    const PcLines lines(b.lines, b.assembled);
    CHECK(lines.at(1) == nullptr);
    CHECK(lines.at(-1) == nullptr);
    CHECK(PcLines().at(0) == nullptr);
  }
}

#if SC8_HAVE_BASIC

namespace {

// The numbers of the lines that start while the machine runs RUN. A
// watch looks at the RAM after every instruction, as the IDE's does.
std::vector<int> linesStarted(const std::string& program) {
  basic::Session s;
  s.load();
  s.runBudget(3000000);
  const std::vector<uint8_t> bytes = basic::encodeProgram(program);
  basic::storeProgram(s.m->ram, bytes);
  for (char c : std::string("RUN\r")) s.pushKey(c == '\r' ? 13 : c, false);
  BasicLines watch;
  std::vector<int> out;
  for (int i = 0; i < 3000000 && out.size() < 50; i++) {
    if (!s.m->instructionStep()) break;
    if (const int n = watch.started(s.m->ram)) out.push_back(n);
    if (!out.empty() && basic::atPrompt(s.m->ram)) break;
  }
  return out;
}

}  // namespace

// DESIGN: the host watches the system page. Nothing in the interpreter
// or the program changes for a step or a breakpoint.
TEST_SUITE("a BASIC line starting, seen in the system page") {
  TEST_CASE("each line that runs starts once, in the order it runs") {
    CHECK(linesStarted("10 A = 1\n20 A = 2\n30 A = 3\n") == std::vector<int>{10, 20, 30});
  }

  TEST_CASE("a line a GOTO jumps over never starts") {
    CHECK(linesStarted("10 A = 1\n20 GOTO 40\n30 A = 2\n40 A = 3\n") == std::vector<int>{10, 20, 40});
  }

  // SYS_PC holds line 20's offset while line 10 runs. The jump moves it
  // to line 30's, the place line 20 would have moved it to by starting.
  TEST_CASE("a jump to the line after the next does not start the line between") {
    CHECK(linesStarted("10 GOTO 30\n20 A = 2\n30 A = 3\n") == std::vector<int>{10, 30});
  }

  TEST_CASE("a line that jumps to itself starts every time round") {
    const std::vector<int> loop = linesStarted("10 A = A + 1 : IF A < 3 THEN 10\n20 END\n");
    CHECK(loop == std::vector<int>{10, 10, 10, 20});
  }

  TEST_CASE("a GOSUB starts its routine's lines and comes back to the line after") {
    CHECK(linesStarted("10 GOSUB 100\n20 END\n100 A = 1\n110 RETURN\n") == std::vector<int>{10, 100, 110, 20});
  }

  // A line carries its file's index, so a breakpoint in a library is
  // told from one on the same number in the main file.
  TEST_CASE("a line that starts in an included file says which file it is in") {
    basic::Session s;
    s.slots.emplace_back("LIB", "20 A = 2\n30 RETURN\n");
    s.load();
    s.runBudget(3000000);
    basic::FileTable files{{"MAIN"}};
    basic::Store store;
    store.files.push_back({"MAIN", true, basic::encodeLines("10 INCLUDE LIB\n20 GOSUB LIB:20\n30 A = 3\n", files, 0)});
    store.adopt(files);
    basic::storeProgram(s.m->ram, basic::writeStore(store));
    for (char c : std::string("RUN\r")) s.pushKey(c == '\r' ? 13 : c, false);
    BasicLines watch;
    std::vector<std::pair<int, int>> started;
    for (int i = 0; i < 6000000 && started.size() < 20; i++) {
      if (!s.m->instructionStep()) break;
      if (const int n = watch.started(s.m->ram)) started.emplace_back(watch.file(), n);
      if (!started.empty() && basic::atPrompt(s.m->ram)) break;
    }
    CHECK(started == std::vector<std::pair<int, int>>{{0, 10}, {0, 20}, {1, 20}, {1, 30}, {0, 30}});
  }

  TEST_CASE("a line typed at the prompt starts no program line") {
    basic::Session s;
    s.load();
    s.runBudget(3000000);
    basic::storeProgram(s.m->ram, basic::encodeProgram("10 A = 1\n"));
    BasicLines watch;
    int started = 0;
    for (char c : std::string("PRINT 7\rCHECK\r")) {
      s.pushKey(c == '\r' ? 13 : c, false);
      for (int i = 0; i < 40000; i++) {
        s.m->instructionStep();
        if (watch.started(s.m->ram)) started++;
      }
    }
    CHECK(started == 0);
  }
}

#endif

namespace {

const char* SET_TEXT = "fetch:\n  FETCH\n# a comment\nHLT:\n  HALT\n\nJZ:\n  PC_INC\n  PC_LOAD_Z\n";

// A machine on the naive set with a program, for row breakpoints.
Machine machineOf(const std::string& source) {
  const Assembled a = assemble(source, nullptr);
  REQUIRE(a.errors.empty());
  return Machine(a.cartridge().program, buildNaive());
}

}  // namespace

TEST_SUITE("a row of a microcode set's text") {
  TEST_CASE("a row's line names its section and its place in it") {
    const auto row = rowAt(SET_TEXT, 9);
    REQUIRE(row);
    CHECK(row->section == "JZ");
    CHECK(row->row == 1);
    CHECK(row->line == 9);
  }

  TEST_CASE("a line without a row gives the next row in the text") {
    const auto comment = rowAt(SET_TEXT, 3);
    REQUIRE(comment);
    CHECK(comment->section == "HLT");
    CHECK(comment->row == 0);
    CHECK(comment->line == 5);
    const auto header = rowAt(SET_TEXT, 7);
    REQUIRE(header);
    CHECK(header->section == "JZ");
    CHECK(header->row == 0);
    CHECK(header->line == 8);
  }

  TEST_CASE("nothing after the last row, and no line before the first") {
    CHECK_FALSE(rowAt(SET_TEXT, 10));
    CHECK_FALSE(rowAt(SET_TEXT, 0));
  }

  TEST_CASE("a row's section and place give its line back") {
    const std::vector<RowStop> rows = rowLines(SET_TEXT);
    CHECK(lineOfRow(rows, "fetch", 0) == 2);
    CHECK(lineOfRow(rows, "HLT", 0) == 5);
    CHECK(lineOfRow(rows, "JZ", 1) == 9);
  }

  TEST_CASE("a row the text does not have has no line") {
    const std::vector<RowStop> rows = rowLines(SET_TEXT);
    CHECK_FALSE(lineOfRow(rows, "JZ", 2));
    CHECK_FALSE(lineOfRow(rows, "LD", 0));
    CHECK_FALSE(lineOfRow(rows, "jz", 0));
  }

  // The IDE lists the set the machine runs by writing it out, and lights
  // the row the machine names by its section and its place.
  TEST_CASE("every row of a set written out as text is found on its own line") {
    const Microcode set = buildNaive();
    const std::string text = serializeMicrocode(set);
    const std::vector<RowStop> rows = rowLines(text);
    std::vector<std::string> lines;
    for (size_t at = 0; at <= text.size();) {
      size_t nl = text.find('\n', at);
      if (nl == std::string::npos) nl = text.size();
      lines.push_back(text.substr(at, nl - at));
      at = nl + 1;
    }
    size_t count = 0;
    for (const Microcode::Section& section : set.sections()) {
      for (size_t r = 0; r < section.rows.size(); r++) {
        const std::optional<int> line = lineOfRow(rows, section.name, static_cast<int>(r));
        REQUIRE_MESSAGE(line, section.name);
        CHECK(lines[static_cast<size_t>(*line - 1)] == "  " + rowText(section.rows[r]));
        count++;
      }
    }
    CHECK(rows.size() == count);
  }
}

// The naive set runs one FETCH row, then JZ's two rows: PC_INC and
// PC_LOAD_Z.
TEST_SUITE("a row breakpoint against the machine") {
  TEST_CASE("the machine stops before the row, inside the instruction") {
    Machine m = machineOf("        LD A <- 1\n        JZ skip\nskip:   HLT\n");
    RowBreaks breaks;
    breaks.set({{"JZ", 1, 9}});
    CHECK_FALSE(breaks.onNext(m));
    CHECK_FALSE(breaks.advance(m));
    CHECK(m.instructions == 0);
    m.instructionStep();
    REQUIRE(breaks.onNext(m));
    REQUIRE(breaks.advance(m));
    const Machine::SequencerPos at = m.sequencer();
    CHECK_FALSE(at.inFetch);
    CHECK(at.rowIndex == 1);
    CHECK(m.instructions == 1);
    CHECK(breaks.stopped().section == "JZ");
    CHECK(breaks.stopped().line == 9);
  }

  TEST_CASE("the row a stopped machine stands before is named by its section and place") {
    Machine m = machineOf("        JZ skip\nskip:   HLT\n");
    RowBreaks breaks;
    breaks.set({{"JZ", 1, 9}});
    REQUIRE(breaks.advance(m));
    const std::optional<RowStop> before = rowBefore(m);
    REQUIRE(before);
    CHECK(before->section == "JZ");
    CHECK(before->row == 1);
  }

  TEST_CASE("with the fetch behind it the machine stands before the instruction's first row") {
    Machine m = machineOf("        JZ skip\nskip:   HLT\n");
    RowBreaks breaks;
    breaks.set({{"JZ", 0, 8}});
    REQUIRE(breaks.advance(m));
    const std::optional<RowStop> own = rowBefore(m);
    REQUIRE(own);
    CHECK(own->section == "JZ");
    CHECK(own->row == 0);
  }

  // A fetch of two rows: one that moves nothing, then the naive FETCH.
  TEST_CASE("inside the fetch the machine stands before a row of fetch") {
    std::string text = serializeMicrocode(buildNaive());
    const std::string fetch = "fetch:\n";
    REQUIRE(text.compare(0, fetch.size(), fetch) == 0);
    text.insert(fetch.size(), "  ALU_PASS_B\n");
    McParsed set = parseMicrocode(text);
    REQUIRE(set.errors.empty());
    const Assembled a = assemble("        JZ skip\nskip:   HLT\n", nullptr);
    REQUIRE(a.errors.empty());
    Machine m(a.cartridge().program, std::move(set.microcode));
    REQUIRE(m.microStep());
    const std::optional<RowStop> before = rowBefore(m);
    REQUIRE(before);
    CHECK(before->section == "fetch");
    CHECK(before->row == 1);
  }

  TEST_CASE("between two instructions the machine stands before no row") {
    Machine m = machineOf("        JZ skip\nskip:   HLT\n");
    CHECK_FALSE(rowBefore(m));
    m.instructionStep();
    CHECK_FALSE(rowBefore(m));
  }

  TEST_CASE("a row it already stopped at does not stop it twice") {
    Machine m = machineOf("        JZ skip\nskip:   HLT\n");
    RowBreaks breaks;
    breaks.set({{"JZ", 1, 9}});
    REQUIRE(breaks.advance(m));
    const uint64_t cycles = m.cycles;
    CHECK_FALSE(breaks.advance(m));
    CHECK(m.cycles == cycles);
  }

  TEST_CASE("two rows of one instruction stop it twice, in order") {
    Machine m = machineOf("        JZ skip\nskip:   HLT\n");
    RowBreaks breaks;
    breaks.set({{"JZ", 0, 8}, {"JZ", 1, 9}});
    REQUIRE(breaks.advance(m));
    CHECK(m.sequencer().rowIndex == 0);
    CHECK_FALSE(m.sequencer().inFetch);
    REQUIRE(breaks.advance(m));
    CHECK(m.sequencer().rowIndex == 1);
    CHECK_FALSE(breaks.advance(m));
  }

  TEST_CASE("a fetch row stops every instruction") {
    Machine m = machineOf("        LD A <- 1\n        LD A <- 2\n        HLT\n");
    RowBreaks breaks;
    breaks.set({{"fetch", 0, 2}});
    REQUIRE(breaks.onNext(m));
    REQUIRE(breaks.advance(m));
    CHECK(m.instructions == 0);
    CHECK_FALSE(breaks.advance(m));
    m.instructionStep();
    REQUIRE(breaks.advance(m));
    CHECK(m.instructions == 1);
    CHECK(m.acc == 1);
  }

  TEST_CASE("a row the machine's set does not have never stops it") {
    Machine m = machineOf("        JZ skip\nskip:   HLT\n");
    RowBreaks breaks;
    breaks.set({{"JZ", 7, 20}, {"NO SUCH INSTRUCTION", 0, 30}});
    CHECK_FALSE(breaks.advance(m));
    CHECK(m.cycles == 0);
  }

  TEST_CASE("no breakpoint, no stop") {
    Machine m = machineOf("        HLT\n");
    RowBreaks breaks;
    CHECK(breaks.empty());
    CHECK_FALSE(breaks.onNext(m));
    CHECK_FALSE(breaks.advance(m));
  }
}

TEST_SUITE("a breakpoint follows an edit") {
  TEST_CASE("an unchanged text moves nothing") {
    const Edit e("a\nb\nc\n", "a\nb\nc\n");
    CHECK(e.follow(1) == 1);
    CHECK(e.follow(3) == 3);
  }

  TEST_CASE("typing inside a line moves nothing") {
    const Edit e("a\nb\nc\n", "a\nbxy\nc\n");
    CHECK(e.follow(1) == 1);
    CHECK(e.follow(2) == 2);
    CHECK(e.follow(3) == 3);
  }

  TEST_CASE("Enter at the end of a line moves the lines below it") {
    const Edit e("a\nb\nc\n", "a\n\nb\nc\n");
    CHECK(e.follow(1) == 1);
    CHECK(e.follow(2) == 3);
    CHECK(e.follow(3) == 4);
  }

  TEST_CASE("Enter at the start of a line moves that line down") {
    const Edit e("a\nb\nc\n", "a\nx\nb\nc\n");
    CHECK(e.follow(1) == 1);
    CHECK(e.follow(2) == 3);
  }

  TEST_CASE("a pasted block moves everything below it by its lines") {
    const Edit e("a\nb\n", "p\nq\nr\na\nb\n");
    CHECK(e.follow(1) == 4);
    CHECK(e.follow(2) == 5);
  }

  TEST_CASE("a deleted line loses its breakpoint and the lines below move up") {
    const Edit e("a\nb\nc\nd\n", "a\nc\nd\n");
    CHECK(e.follow(1) == 1);
    CHECK_FALSE(e.follow(2));
    CHECK(e.follow(3) == 2);
    CHECK(e.follow(4) == 3);
  }

  TEST_CASE("a line joined onto the one above keeps its breakpoint there") {
    const Edit e("a\nb\nc\n", "ab\nc\n");
    CHECK(e.follow(1) == 1);
    CHECK(e.follow(2) == 1);
    CHECK(e.follow(3) == 2);
  }

  TEST_CASE("a selection deleted across lines leaves the lines it cut into on one line") {
    const Edit e("one\ntwo\nthree\nfour\n", "onree\nfour\n");
    CHECK(e.follow(1) == 1);
    CHECK_FALSE(e.follow(2));
    CHECK(e.follow(3) == 1);
    CHECK(e.follow(4) == 2);
  }

  TEST_CASE("the last line without a newline moves too") {
    const Edit e("a\nb", "x\na\nb");
    CHECK(e.follow(2) == 3);
  }
}
