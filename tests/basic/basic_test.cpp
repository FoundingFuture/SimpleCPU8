// BASIC, written in C, running on the CPU. The fourth layer.
//
// The screen is text mode mapped at $F000, so what the interpreter printed is
// readable straight out of RAM. That is also how a person sees it.
//
// Ported from the browser project's basic.test.ts. That file drove a
// Session, which compiled the seven C files on every boot. Here the ROM is
// compiled once at build time and comes from sc8::basicRom(). The Session
// below wires the device chain the browser's did: Gpu, then the Acp, then
// the Apu, then the InputBus, with the Storage device between the Apu and
// the InputBus, which the browser never had.

#include <doctest.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/font.h"
#include "basic/basic_rom.h"
#include "basic/keywords.h"
#include "basic/program.h"
#include "core/cartridge.h"
#include "core/machine.h"
#include "core/microcode.h"
#include "devices/acp.h"
#include "devices/apu.h"
#include "devices/gpu.h"
#include "devices/input.h"
#include "devices/storage.h"
#include "project/project.h"
#include "support/basic_session.h"

using namespace sc8;
using sc8::testing::Session;

namespace {

// The screen is 4 KB at basic::SCREEN, and the grid is whatever the system
// page says: SYS_COLS and SYS_ROWS, which the interpreter reads from the GPU.
constexpr int SCREEN = basic::SCREEN;

std::unique_ptr<Session> boot() {
  auto s = std::make_unique<Session>();
  s->load();
  return s;
}

// Everything on screen, trailing blanks trimmed off each row.
std::vector<std::string> screen(const Session& s) {
  std::vector<std::string> out;
  const int cols = s.m->ram[basic::SYS_COLS];
  const int rows = s.m->ram[basic::SYS_ROWS];
  for (int y = 0; y < rows; y++) {
    std::string line;
    for (int x = 0; x < cols; x++) {
      line += static_cast<char>(s.m->ram[static_cast<size_t>(SCREEN + y * cols + x)]);
    }
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
    out.push_back(line);
  }
  return out;
}

std::string text(const Session& s) {
  std::string t;
  for (const std::string& row : screen(s)) t += row + "\n";
  while (!t.empty() && t.back() == '\n') t.pop_back();
  return t;
}

// Run until the interpreter is waiting for a key. That is where it spends
// every idle moment: term_readline polls IO_KEY and does nothing else.
void settle(Session& s, uint64_t budget = 4000000) { s.runBudget(budget); }

// Type a line and press Enter, then let the interpreter act on it. The
// characters go in as written, small letters included, as the keyboard
// sends them.
void type(Session& s, const std::string& line, uint64_t budget = 6000000) {
  for (char ch : line) {
    s.pushKey(static_cast<unsigned char>(ch), false);
    s.runBudget(120000);
  }
  s.pushKey(13, false);
  s.runBudget(budget);
}

bool has(const std::string& t, const std::string& needle) { return t.find(needle) != std::string::npos; }

// The screen as one line, rows run together and spaces collapsed, so a
// message the terminal wrapped at the edge still reads whole.
std::string flat(const Session& s) {
  std::string t;
  for (int i = 0; i < s.m->ram[basic::SYS_ROWS] * s.m->ram[basic::SYS_COLS]; i++) {
    const char c = static_cast<char>(s.m->ram[static_cast<size_t>(SCREEN + i)]);
    const char ch = std::isprint(static_cast<unsigned char>(c)) ? c : ' ';
    if (ch == ' ' && !t.empty() && t.back() == ' ') continue;
    t += ch;
  }
  return t;
}

// The text after the first occurrence of a marker, or empty. The tests
// split on the echoed command to look at what a RUN printed.
std::string after(const std::string& t, const std::string& marker) {
  const size_t at = t.find(marker);
  return at == std::string::npos ? "" : t.substr(at + marker.size());
}

// JavaScript's indexOf and lastIndexOf: -1 when the needle is missing.
// An ordering check then fails rather than passing on a missing word.
long indexOf(const std::string& t, const std::string& needle) {
  const size_t at = t.find(needle);
  return at == std::string::npos ? -1 : static_cast<long>(at);
}

long lastIndexOf(const std::string& t, const std::string& needle) {
  const size_t at = t.rfind(needle);
  return at == std::string::npos ? -1 : static_cast<long>(at);
}

// The needles in order, each found after the previous one, like the
// reference's /a[\s\S]*b[\s\S]*c/.
bool inOrder(const std::string& t, const std::vector<std::string>& needles) {
  size_t from = 0;
  for (const std::string& n : needles) {
    const size_t at = t.find(n, from);
    if (at == std::string::npos) return false;
    from = at + n.size();
  }
  return true;
}

int countOf(const std::string& t, const std::string& needle) {
  int n = 0;
  size_t at = 0;
  while ((at = t.find(needle, at)) != std::string::npos) {
    n++;
    at += needle.size();
  }
  return n;
}

// The program, written into memory by the function the IDE's writeProgram
// calls. It is faster than typing a long program and it is the IDE's path.
void setProgram(Session& s, const std::string& program) {
  basic::storeProgram(s.m->ram, basic::encodeProgram(program));
}

// Type a command and return the screen row right under its echo, which is
// what it printed on its first line.
std::string printed(Session& s, const std::string& command) {
  type(s, command);
  const auto scr = screen(s);
  for (size_t row = 0; row + 1 < scr.size(); row++) {
    if (scr[row] == ">" + command) return scr[row + 1];
  }
  return "(no such row)";
}

// The word at an address of the system page, high byte first.
int sysWord(const Session& s, uint16_t at) {
  return (s.m->ram[at] << 8) | s.m->ram[at + 1];
}

// The offset in the program of line n's record, or -1.
int lineOffset(const Session& s, int n) {
  const auto& ram = s.m->ram;
  const size_t prog = static_cast<size_t>(sysWord(s, basic::SYS_PROG));
  size_t p = 0;
  while (true) {
    const int line = (ram[prog + p] << 8) | ram[prog + p + 1];
    if (line == 0) return -1;
    if (line == n) return static_cast<int>(p);
    p += ram[prog + p + 2];
  }
}

// Where the program ends in memory, which is where its data area starts.
int programEnd(const Session& s) {
  const auto& ram = s.m->ram;
  return ((ram[basic::SYS_PROG] << 8) | ram[basic::SYS_PROG + 1]) +
         ((ram[basic::SYS_PROG_LEN] << 8) | ram[basic::SYS_PROG_LEN + 1]);
}

// An integer variable, A to Z, read out of memory. A letter owns eleven
// word slots, high byte first.
int16_t intVar(const Session& s, char letter) {
  const auto& ram = s.m->ram;
  const size_t vars = static_cast<size_t>((ram[12] << 8) | ram[13]);
  const size_t at = vars + static_cast<size_t>(letter - 'A') * 22;
  return static_cast<int16_t>((ram[at] << 8) | ram[at + 1]);
}

}  // namespace

TEST_SUITE("it boots") {
  TEST_CASE("says who it is and prompts") {
    auto s = boot();
    settle(*s);
    const auto scr = screen(*s);
    CHECK(scr[0] == "SimpleCPU-8 BASIC");
    CHECK(scr[1] == "READY");
    CHECK(scr[2][0] == '>');
  }

  // The key buffer is hardware and survives a load, so anything typed at the
  // program before is still queued. BASIC throws it away rather than reading
  // it as its first command.
  TEST_CASE("ignores keys typed at whatever ran before it") {
    auto s = std::make_unique<Session>();
    s->load();
    for (char c : std::string("JUNK")) s->pushKey(c, false);
    s->pushKey(13, false);
    settle(*s);
    const auto scr = screen(*s);
    CHECK_FALSE(has(text(*s), "JUNK"));
    CHECK(scr[2][0] == '>');
  }

  // The cursor is a solid block both fonts carry. It used to be code 219,
  // which neither has, so nothing was drawn. This test passed anyway by
  // reading the RAM byte. It checks the PIXELS now.
  TEST_CASE("draws the cursor where typing will go") {
    auto s = boot();
    settle(*s);
    CHECK(static_cast<unsigned char>(screen(*s)[2][1]) == 0x7f);
  }

  TEST_CASE("really puts the cursor on the screen, not just in memory") {
    auto s = boot();
    settle(*s);
    const Gpu::Frame px = s->gpu.composeFrame();
    // The second cell of the prompt row, filled solid by the block.
    int lit = 0;
    for (int y = 2 * 8; y < 3 * 8; y++) {
      for (int x = 1 * 6; x < 2 * 6; x++) {
        if (px[static_cast<size_t>(y * 256 + x)] != 0) lit++;
      }
    }
    INFO("the cursor is invisible");
    CHECK(lit == 6 * 8);
  }
}

TEST_SUITE("immediate mode") {
  TEST_CASE("prints a number") {
    auto s = boot();
    settle(*s);
    type(*s, "PRINT 6*7");
    CHECK(has(text(*s), "42"));
  }

  TEST_CASE("prints a string") {
    auto s = boot();
    settle(*s);
    type(*s, "PRINT \"HI\"");
    CHECK(has(text(*s), "HI"));
  }

  TEST_CASE("works out precedence the way C does") {
    auto s = boot();
    settle(*s);
    type(*s, "PRINT 2+3*4");
    CHECK(has(text(*s), "14"));
  }

  TEST_CASE("divides and takes a remainder") {
    auto s = boot();
    settle(*s);
    type(*s, "PRINT 17/5, 17 MOD 5");
    CHECK(has(text(*s), "3 2"));
  }

  TEST_CASE("holds a variable between lines") {
    auto s = boot();
    settle(*s);
    type(*s, "A=12");
    type(*s, "PRINT A*2");
    CHECK(has(text(*s), "24"));
  }

  TEST_CASE("says READY when it is done") {
    auto s = boot();
    settle(*s);
    type(*s, "A=1");
    CHECK(has(text(*s), "READY"));
  }
}

TEST_SUITE("stored programs") {
  TEST_CASE("stores lines and runs them in order") {
    auto s = boot();
    settle(*s);
    type(*s, "10 PRINT \"ONE\"");
    type(*s, "20 PRINT \"TWO\"");
    type(*s, "RUN");
    const std::string t = text(*s);
    CHECK(indexOf(t, "ONE") > 0);
    CHECK(indexOf(t, "TWO") > indexOf(t, "ONE"));
  }

  TEST_CASE("keeps them sorted however they were typed") {
    auto s = boot();
    settle(*s);
    type(*s, "20 PRINT \"B\"");
    type(*s, "10 PRINT \"A\"");
    type(*s, "RUN");
    const std::string t = text(*s);
    CHECK(indexOf(t, "A") < lastIndexOf(t, "B"));
  }

  TEST_CASE("lists what it holds") {
    auto s = boot();
    settle(*s);
    type(*s, "10 PRINT \"X\"");
    type(*s, "LIST");
    CHECK(has(text(*s), "10 PRINT \"X\""));
  }

  TEST_CASE("lists one line or a range") {
    auto s = boot();
    settle(*s);
    type(*s, "10 REM A1");
    type(*s, "20 REM B2");
    type(*s, "30 REM C3");
    type(*s, "40 REM D4");
    auto shown = [&](const std::string& command) {
      type(*s, command);
      const std::string t = after(text(*s), command);
      std::string marks;
      for (const char* m : {"A1", "B2", "C3", "D4"}) {
        if (has(t, m)) marks += m;
      }
      type(*s, "CLS");
      return marks;
    };
    CHECK_EQ(shown("LIST 20"), "B2");
    CHECK_EQ(shown("LIST 20-30"), "B2C3");
    CHECK_EQ(shown("LIST -20"), "A1B2");
    CHECK_EQ(shown("LIST 30-"), "C3D4");
    CHECK_EQ(shown("LIST 15-25"), "B2");
    CHECK_EQ(shown("LIST"), "A1B2C3D4");
    type(*s, "LIST 10 20");
    CHECK(has(after(text(*s), "LIST 10 20"), "SYNTAX"));
  }

  TEST_CASE("replaces a line with the same number") {
    auto s = boot();
    settle(*s);
    type(*s, "10 PRINT \"OLD\"");
    type(*s, "10 PRINT \"NEW\"");
    type(*s, "RUN");
    const std::string t = text(*s);
    CHECK(has(t, "NEW"));
    CHECK_FALSE(has(after(t, "RUN"), "OLD"));
  }

  TEST_CASE("deletes a line when only its number is typed") {
    auto s = boot();
    settle(*s);
    type(*s, "10 PRINT \"GONE\"");
    type(*s, "10");
    type(*s, "LIST");
    CHECK_FALSE(has(after(text(*s), "LIST"), "GONE"));
  }

  TEST_CASE("RENUM numbers the lines again, and GOTO, GOSUB and THEN follow") {
    auto s = boot();
    settle(*s);
    type(*s, "5 PRINT \"GOTO 5\"");
    type(*s, "7 IF A THEN 20");
    type(*s, "20 GOSUB 5: GOTO 7");
    type(*s, "30 REM GOTO 5");
    type(*s, "31 GOTO 99");
    type(*s, "RENUM 100,50");
    const auto& ram = s->m->ram;
    const size_t at = static_cast<size_t>((ram[basic::SYS_PROG] << 8) | ram[basic::SYS_PROG + 1]);
    const size_t len = static_cast<size_t>((ram[basic::SYS_PROG_LEN] << 8) | ram[basic::SYS_PROG_LEN + 1]);
    CHECK_EQ(basic::decodeProgram(std::span<const uint8_t>(ram.data() + at, len)),
             "100 PRINT \"GOTO 5\"\n150 IF A THEN 200\n200 GOSUB 100: GOTO 150\n250 REM GOTO 5\n300 GOTO 99\n");
  }

  TEST_CASE("RENUM on its own counts in tens, and a program still runs after it") {
    auto s = boot();
    settle(*s);
    type(*s, "1 A=0");
    type(*s, "2 A=A+1");
    type(*s, "3 IF A<3 THEN 2");
    type(*s, "4 PRINT A*111");
    type(*s, "RENUM");
    type(*s, "LIST");
    const std::string t = after(text(*s), "LIST");
    CHECK(has(t, "10 A=0"));
    CHECK(has(t, "30 IF A<3 THEN 20"));
    CHECK(has(t, "40 PRINT A*111"));
    type(*s, "RUN");
    CHECK(has(after(text(*s), "RUN"), "333"));
  }

  TEST_CASE("RENUM that would pass 65535 says so and changes nothing") {
    auto s = boot();
    settle(*s);
    type(*s, "10 GOTO 20");
    type(*s, "20 END");
    type(*s, "RENUM 65000,1000");
    CHECK(has(flat(*s), "RENUM WOULD NUMBER A LINE PAST 65535"));
    type(*s, "CLS");
    type(*s, "LIST");
    CHECK(has(text(*s), "10 GOTO 20"));
    type(*s, "RENUM 10,0");
    CHECK(has(flat(*s), "NUMBER OUT OF RANGE"));
  }

  TEST_CASE("empties itself on NEW") {
    auto s = boot();
    settle(*s);
    type(*s, "10 PRINT \"X\"");
    type(*s, "NEW");
    type(*s, "LIST");
    CHECK(countOf(text(*s), "10 PRINT") == 1);  // only the echo
  }
}

TEST_SUITE("control flow") {
  TEST_CASE("loops with FOR and NEXT") {
    auto s = boot();
    settle(*s);
    type(*s, "10 FOR I=1 TO 3");
    type(*s, "20 PRINT I");
    type(*s, "30 NEXT I");
    type(*s, "RUN", 20000000);
    CHECK(inOrder(text(*s), {"1", "2", "3"}));
  }

  TEST_CASE("takes a STEP") {
    auto s = boot();
    settle(*s);
    type(*s, "10 FOR I=0 TO 10 STEP 5");
    type(*s, "20 PRINT I;");
    type(*s, "30 NEXT");
    type(*s, "RUN", 20000000);
    CHECK(has(text(*s), "0510"));
  }

  // The FOR stack kept only the line to go back to, and that was the line
  // AFTER the FOR. A body on the FOR's own line ran once, and a FOR at the
  // start of the next line was skipped as if it were the outer one.
  TEST_CASE("runs a body that shares the FOR's line") {
    auto s = boot();
    settle(*s);
    type(*s, "10 FOR I=1 TO 3: PRINT I;: NEXT I");
    type(*s, "20 PRINT \"END\"");
    type(*s, "RUN", 20000000);
    CHECK(has(after(text(*s), "RUN"), "123END"));
  }

  TEST_CASE("runs a loop typed at the prompt") {
    auto s = boot();
    settle(*s);
    type(*s, "FOR I=1 TO 3: PRINT I;: NEXT I", 20000000);
    CHECK(has(after(text(*s), "NEXT I"), "123"));
  }

  TEST_CASE("nests a FOR on the line right after a FOR") {
    auto s = boot();
    settle(*s);
    type(*s, "10 FOR I=1 TO 2");
    type(*s, "20 FOR J=1 TO 3");
    type(*s, "30 PRINT I*10+J;");
    type(*s, "40 NEXT J");
    type(*s, "50 NEXT I");
    type(*s, "RUN", 40000000);
    const std::string t = after(text(*s), "RUN");
    CHECK(has(t, "111213212223"));
    CHECK_FALSE(has(t, "TOO DEEP"));
  }

  TEST_CASE("nests two loops on one line") {
    auto s = boot();
    settle(*s);
    type(*s, "10 FOR I=1 TO 2: FOR J=1 TO 2: PRINT I;J;\" \";: NEXT J: NEXT I");
    type(*s, "RUN", 40000000);
    CHECK(has(after(text(*s), "RUN"), "11 12 21 22"));
  }

  TEST_CASE("breaks out of a loop that lives on one line") {
    auto s = boot();
    settle(*s);
    type(*s, "10 FOR I=1 TO 2 STEP 0: NEXT I");
    for (char ch : std::string("RUN")) {
      s->pushKey(ch, false);
      s->runBudget(120000);
    }
    s->pushKey(13, false);
    s->runBudget(2000000);
    s->pushKey(27, false);
    s->runBudget(3000000);
    CHECK(has(text(*s), "BREAK IN LINE 10"));
  }

  TEST_CASE("branches with IF THEN") {
    auto s = boot();
    settle(*s);
    type(*s, "10 IF 1 THEN PRINT \"YES\"");
    type(*s, "20 IF 0 THEN PRINT \"NO\"");
    type(*s, "RUN", 8000000);
    const std::string t = after(text(*s), "RUN");
    CHECK(has(t, "YES"));
    CHECK_FALSE(has(t, "NO"));
  }

  TEST_CASE("goes to a line") {
    auto s = boot();
    settle(*s);
    type(*s, "10 GOTO 30");
    type(*s, "20 PRINT \"SKIPPED\"");
    type(*s, "30 PRINT \"HERE\"");
    type(*s, "RUN", 8000000);
    const std::string t = after(text(*s), "RUN");
    CHECK(has(t, "HERE"));
    CHECK_FALSE(has(t, "SKIPPED"));
  }

  TEST_CASE("calls and returns with GOSUB") {
    auto s = boot();
    settle(*s);
    type(*s, "10 GOSUB 100");
    type(*s, "20 PRINT \"BACK\"");
    type(*s, "30 END");
    type(*s, "100 PRINT \"SUB\"");
    type(*s, "110 RETURN");
    type(*s, "RUN", 10000000);
    const std::string t = after(text(*s), "RUN");
    CHECK(indexOf(t, "SUB") >= 0);
    CHECK(indexOf(t, "BACK") > indexOf(t, "SUB"));
  }

  TEST_CASE("runs several statements on one line") {
    auto s = boot();
    settle(*s);
    type(*s, "PRINT \"A\";:PRINT \"B\"");
    CHECK(has(text(*s), "AB"));
  }
}

TEST_SUITE("strings") {
  TEST_CASE("holds one in a variable") {
    auto s = boot();
    settle(*s);
    type(*s, "A$=\"HELLO\"");
    type(*s, "PRINT A$");
    CHECK(has(text(*s), "HELLO"));
  }

  TEST_CASE("joins two") {
    auto s = boot();
    settle(*s);
    type(*s, "A$=\"AB\"");
    type(*s, "PRINT A$+\"CD\"");
    CHECK(has(text(*s), "ABCD"));
  }

  TEST_CASE("measures one") {
    auto s = boot();
    settle(*s);
    type(*s, "PRINT LEN(\"ABCDE\")");
    CHECK(has(text(*s), "5"));
  }

  TEST_CASE("takes a piece out of one") {
    auto s = boot();
    settle(*s);
    type(*s, "PRINT MID$(\"ABCDEF\",2,3)");
    CHECK(has(text(*s), "BCD"));
  }

  TEST_CASE("turns a number into text and back") {
    auto s = boot();
    settle(*s);
    type(*s, "PRINT STR$(123)+\"!\"");
    CHECK(has(text(*s), "123!"));
    type(*s, "PRINT VAL(\"45\")+1");
    CHECK(has(text(*s), "46"));
  }

  TEST_CASE("HEX$ writes a number as unsigned hex in capitals, in the fewest digits") {
    auto s = boot();
    settle(*s);
    CHECK_EQ(printed(*s, "PRINT HEX$(255)"), "FF");
    CHECK_EQ(printed(*s, "PRINT HEX$(-8192)"), "E000");
    CHECK_EQ(printed(*s, "PRINT HEX$(40000)"), "9C40");
    CHECK_EQ(printed(*s, "PRINT HEX$(0)"), "0");
    CHECK_EQ(printed(*s, "PRINT HEX$(65535)"), "FFFF");
  }

  TEST_CASE("HEX$ with a width pads with zeros and never cuts") {
    auto s = boot();
    settle(*s);
    CHECK_EQ(printed(*s, "PRINT HEX$(10, 4)"), "000A");
    CHECK_EQ(printed(*s, "PRINT HEX$(0, 4)"), "0000");
    CHECK_EQ(printed(*s, "PRINT HEX$(256, 2)"), "100");
  }

  TEST_CASE("HEX$ with a width outside 1 to 4 says so") {
    auto s = boot();
    settle(*s);
    type(*s, "PRINT HEX$(1, 0)");
    CHECK(has(flat(*s), "HEX WIDTH IS OUT OF RANGE [1,4]"));
    CHECK_EQ(s->m->ram[0x12], 31);
    type(*s, "CLS");
    type(*s, "PRINT HEX$(1, 5)");
    CHECK(has(flat(*s), "HEX WIDTH IS OUT OF RANGE [1,4]"));
    CHECK_EQ(s->m->ram[0x12], 31);
  }

  TEST_CASE("HEX$ joins a string expression") {
    auto s = boot();
    settle(*s);
    char want[8];
    std::snprintf(want, sizeof want, "AT %04X", static_cast<unsigned>(sysWord(*s, basic::SYS_PROG)));
    CHECK_EQ(printed(*s, "PRINT \"AT \" + HEX$(DEEK(8), 4)"), want);
  }

  TEST_CASE("HEX$ is a reserved word, stored in capitals") {
    CHECK(basic::basicKeywords().count("HEX$") == 1);
    CHECK_EQ(basic::canonicalLine("print hex$(255)"), "PRINT HEX$(255)");
  }

  TEST_CASE("compares two") {
    auto s = boot();
    settle(*s);
    type(*s, "IF \"A\"<\"B\" THEN PRINT \"LT\"");
    CHECK(has(text(*s), "LT"));
  }
}

namespace {

// A program that loops forever has to be stoppable. The only other way out
// is Restart, which throws the typed program away with it.
void runaway(Session& s) {
  type(s, "10 PRINT 1;");
  type(s, "20 GOTO 10");
  // RUN never returns, so this one gets a budget rather than a wait.
  for (char ch : std::string("RUN")) {
    s.pushKey(ch, false);
    s.runBudget(120000);
  }
  s.pushKey(13, false);
  s.runBudget(3000000);
}

}  // namespace

TEST_SUITE("breaking out of a running program") {
  TEST_CASE("stops on Escape, and says which line") {
    auto s = boot();
    settle(*s);
    runaway(*s);
    CHECK_FALSE(has(text(*s), "BREAK"));
    s->pushKey(27, false);
    s->runBudget(3000000);
    // Rows run together: the message can wrap at the screen's edge.
    const std::string t = flat(*s);
    CHECK(has(t, "BREAK"));
    CHECK((has(t, "BREAK IN LINE 10") || has(t, "BREAK IN LINE 20")));
  }

  TEST_CASE("stops on Ctrl and C, which arrives as control code 3") {
    auto s = boot();
    settle(*s);
    runaway(*s);
    s->pushKey(3, false);
    s->runBudget(3000000);
    CHECK(has(flat(*s), "BREAK"));
  }

  // The jam this had: the check pushed a key back. On the next poll it
  // found its OWN pushback, for ever. The release event left over from
  // the Enter that started the program blocked every later key. Escape sat
  // unread behind it.
  TEST_CASE("is not jammed by the key release that started it") {
    auto s = boot();
    settle(*s);
    type(*s, "10 GOTO 10");
    for (char ch : std::string("RUN")) {
      s->pushKey(ch, false);
      s->runBudget(200000);
    }
    s->pushKey(13, false);
    s->pushKey(13, true);  // the release a real keyboard sends
    s->runBudget(3000000);
    s->pushKey(27, false);
    s->runBudget(3000000);
    CHECK(has(text(*s), "BREAK"));
  }

  // A bare loop rather than a printing one: this is about the key path. A
  // program that scrolls the screen every pass spends its budget on that.
  TEST_CASE("is not jammed by a key nothing ever reads") {
    auto s = boot();
    settle(*s);
    type(*s, "10 GOTO 10");
    for (char ch : std::string("RUN")) {
      s->pushKey(ch, false);
      s->runBudget(200000);
    }
    s->pushKey(13, false);
    s->runBudget(3000000);
    s->pushKey(90, false);  // a Z the program does not want
    s->runBudget(3000000);
    s->pushKey(27, false);
    s->runBudget(4000000);
    CHECK(has(text(*s), "BREAK"));
  }

  // The key it did not want is not lost either: it is waiting at the prompt.
  TEST_CASE("keeps the key it pushed back, for whoever reads next") {
    auto s = boot();
    settle(*s);
    type(*s, "10 GOTO 10");
    for (char ch : std::string("RUN")) {
      s->pushKey(ch, false);
      s->runBudget(200000);
    }
    s->pushKey(13, false);
    s->runBudget(3000000);
    s->pushKey(90, false);
    s->runBudget(3000000);
    s->pushKey(27, false);
    s->runBudget(4000000);
    CHECK(inOrder(text(*s), {"BREAK IN LINE 10", "Z"}));
  }

  TEST_CASE("leaves a plain C alone, so typing does not stop a program") {
    auto s = boot();
    settle(*s);
    runaway(*s);
    s->pushKey(67, false);
    s->runBudget(2000000);
    CHECK_FALSE(has(text(*s), "BREAK"));
  }

  TEST_CASE("comes back to the prompt with the program still there") {
    auto s = boot();
    settle(*s);
    runaway(*s);
    s->pushKey(27, false);
    s->runBudget(3000000);
    type(*s, "LIST");
    CHECK(has(text(*s), "20 GOTO 10"));
  }

  // The break check reads a key to look at it. Anything that is not a
  // break has to go back, or INKEY$ loses every second keystroke.
  TEST_CASE("does not eat the keys a program is reading") {
    auto s = boot();
    settle(*s);
    type(*s, "10 A$=INKEY$");
    type(*s, "20 IF A$<>\"\" THEN PRINT A$");
    type(*s, "30 GOTO 10");
    for (char ch : std::string("RUN")) {
      s->pushKey(ch, false);
      s->runBudget(120000);
    }
    s->pushKey(13, false);
    s->runBudget(2000000);
    s->pushKey(90, false);  // Z
    s->runBudget(3000000);
    s->pushKey(27, false);
    s->runBudget(2000000);
    CHECK(has(after(text(*s), "RUN"), "Z"));
  }
}

TEST_SUITE("errors are reported rather than swallowed") {
  TEST_CASE("names a syntax error") {
    auto s = boot();
    settle(*s);
    type(*s, "PRINT )");
    CHECK(has(text(*s), "SYNTAX ERROR"));
  }

  TEST_CASE("names a divide by zero") {
    auto s = boot();
    settle(*s);
    type(*s, "PRINT 1/0");
    CHECK(has(text(*s), "DIVISION BY ZERO"));
  }

  // PRINT evaluated into the printer, and a failed expression comes back
  // as 0. So the message came after a stray 0 on the line above it.
  TEST_CASE("prints nothing before a divide by zero") {
    auto s = boot();
    settle(*s);
    type(*s, "PRINT 10/0");
    const auto scr = screen(*s);
    size_t row = 0;
    while (row < scr.size() && scr[row] != ">PRINT 10/0") row++;
    REQUIRE_LT(row + 1, scr.size());
    CHECK_EQ(scr[row + 1], "? DIVISION BY ZERO");
  }

  TEST_CASE("names a missing line, and which line asked") {
    auto s = boot();
    settle(*s);
    type(*s, "10 GOTO 999");
    type(*s, "RUN", 8000000);
    const std::string t = text(*s);
    CHECK(has(flat(*s), "THERE IS NO LINE 999 IN LINE 10"));
  }
}

TEST_SUITE("it can draw, which is why the GPU is on the bus") {
  TEST_CASE("plots a pixel, and the screen shows it under the text") {
    auto s = boot();
    settle(*s);
    type(*s, "PAPER 0");
    type(*s, "INK 255");
    type(*s, "PLOT 10,10");
    CHECK(s->m->status == Status::Running);
    // Text mode shows the VRAM under the characters, so the dot is on the
    // composed frame rather than only in the device.
    const auto out = s->gpu.composeFrame();
    CHECK_EQ(out[10 * 256 + 10], 255);
  }

  TEST_CASE("PIXEL reads back what INK drew, and COLOR and POINT are gone") {
    auto s = boot();
    settle(*s);
    type(*s, "PAPER 0");
    type(*s, "INK 224");
    type(*s, "MOVE 10,10");
    type(*s, "DRAW 20,10");
    type(*s, "PRINT PIXEL(15,10)");
    const auto lines = screen(*s);
    // A number prints after a space, where its sign would go.
    const bool read = std::any_of(lines.begin(), lines.end(), [](const std::string& l) {
      return l.find_first_not_of(' ') != std::string::npos && l.substr(l.find_first_not_of(' ')) == "224";
    });
    CHECK(read);
    type(*s, "CLS");
    type(*s, "COLOR 255");
    type(*s, "PRINT POINT(15,10)");
    CHECK(has(flat(*s), "UNKNOWN WORD COLOR"));
    CHECK(has(flat(*s), "POINT IS NOT A VARIABLE"));
  }

  TEST_CASE("every drawing word draws in INK, which starts white") {
    auto s = boot();
    settle(*s);
    type(*s, "PAPER 0");
    type(*s, "PLOT 5,5");
    CHECK_EQ(s->gpu.vram[5 * 256 + 5], 255);
    type(*s, "INK 28");
    type(*s, "PLOT 10,10");
    CHECK_EQ(s->gpu.vram[10 * 256 + 10], 28);
    type(*s, "MOVE 20,20");
    type(*s, "DRAW 40,20");
    CHECK_EQ(s->gpu.vram[20 * 256 + 30], 28);
  }

  TEST_CASE("CIRCLE outlines in INK, takes a second radius, and fills when given a colour") {
    auto s = boot();
    settle(*s);
    type(*s, "PAPER 0");
    type(*s, "INK 224");
    type(*s, "MOVE 100,100");
    type(*s, "CIRCLE 20");
    auto at = [&](int x, int y) { return s->gpu.vram[static_cast<size_t>(y * 256 + x)]; };
    CHECK_EQ(at(120, 100), 224);
    CHECK_EQ(at(100, 120), 224);
    CHECK_EQ(at(100, 100), 0);  // an outline, so the middle stays paper

    type(*s, "MOVE 60,180");
    type(*s, "CIRCLE 30,10,3");
    CHECK_EQ(at(90, 180), 224);   // the outline, 30 across
    CHECK_EQ(at(60, 190), 224);   // and 10 down
    CHECK_EQ(at(60, 180), 3);     // the fill
    CHECK_EQ(at(60, 195), 0);     // below the ellipse, paper

    type(*s, "MOVE 200,60");
    type(*s, "CIRCLE(15,15,28)");
    CHECK_EQ(at(215, 60), 224);
    CHECK_EQ(at(200, 60), 28);
    type(*s, "PLOT 1,1");
    CHECK_EQ(at(1, 1), 224);  // the fill colour did not become the ink
  }

  TEST_CASE("CLS clears the picture to the PAPER colour as well as the text") {
    auto s = boot();
    settle(*s);
    type(*s, "PAPER 3");
    type(*s, "INK 255");
    type(*s, "PLOT 10,10");
    CHECK_EQ(s->gpu.vram[10 * 256 + 10], 255);
    type(*s, "CLS");
    CHECK_EQ(s->gpu.vram[10 * 256 + 10], 3);
    CHECK_EQ(s->gpu.vram[200 * 256 + 200], 3);
    CHECK_EQ(screen(*s)[0], "READY");  // the text is gone too, only the prompt after CLS
  }
}

TEST_SUITE("memory is reachable a byte or a word at a time") {
  TEST_CASE("POKE and PEEK a byte") {
    auto s = boot();
    settle(*s);
    type(*s, "POKE 40000,77");
    type(*s, "PRINT PEEK(40000)");
    CHECK(has(after(text(*s), "PEEK(40000)"), "77"));
    CHECK(s->m->ram[40000] == 77);
  }

  // A word is big-endian, the way the machine stores every word and the
  // way an assembly driver reads the vector.
  TEST_CASE("DOKE and DEEK a word, high byte first") {
    auto s = boot();
    settle(*s);
    type(*s, "DOKE 40000,4660");
    type(*s, "PRINT DEEK(40000)");
    CHECK(s->m->ram[40000] == 0x12);
    CHECK(s->m->ram[40001] == 0x34);
    CHECK(has(after(text(*s), "DEEK(40000)"), "4660"));
  }
}

TEST_SUITE("error messages say what went wrong") {
  TEST_CASE("a syntax error names what it expected and what it found") {
    auto s = boot();
    settle(*s);
    type(*s, "PRINT )");
    CHECK(has(flat(*s), "? SYNTAX ERROR: EXPECTED A NUMBER, A VARIABLE OR ( BUT FOUND )"));
    type(*s, "FOR I 1 TO 5");
    CHECK(has(flat(*s), "EXPECTED = BUT FOUND 1"));
    type(*s, "PRINT (1 + 2");
    CHECK(has(flat(*s), "EXPECTED ) BUT FOUND THE END OF THE LINE"));
    type(*s, "A = 1 B = 2");
    CHECK(has(flat(*s), "EXPECTED : OR THE END OF THE LINE BUT FOUND B"));
  }

  TEST_CASE("a long name is not a variable, and an unknown word says so") {
    auto s = boot();
    settle(*s);
    type(*s, "SCORE = 5");
    CHECK(has(flat(*s), "SCORE IS NOT A VARIABLE: A VARIABLE IS ONE LETTER, OR A LETTER AND A DIGIT"));
    type(*s, "PRINT SCORE");
    CHECK(has(after(flat(*s), "PRINT SCORE"), "SCORE IS NOT A VARIABLE"));
    type(*s, "PRNT 5");
    CHECK(has(flat(*s), "UNKNOWN WORD PRNT"));
  }

  TEST_CASE("the loop and subroutine errors are told apart") {
    auto s = boot();
    settle(*s);
    type(*s, "RETURN");
    CHECK(has(flat(*s), "RETURN WITHOUT A GOSUB"));
    type(*s, "NEXT");
    CHECK(has(flat(*s), "NEXT WITHOUT A FOR"));
    type(*s, "10 GOSUB 10");
    type(*s, "RUN", 8000000);
    CHECK(has(flat(*s), "TOO MANY GOSUBS INSIDE EACH OTHER: 16 AT MOST IN LINE 10"));
  }

  TEST_CASE("strings and numbers are told apart") {
    auto s = boot();
    settle(*s);
    type(*s, "PRINT 1 + A$");
    CHECK(has(flat(*s), "A STRING CANNOT BE USED AS A NUMBER"));
    // A$ + starts a string, so the 1 after it is the one out of place.
    type(*s, "PRINT A$ + 1");
    CHECK(has(after(flat(*s), "A$ + 1"), "A NUMBER CANNOT BE USED AS A STRING"));
  }

  TEST_CASE("the code stays readable in SYS_ERR") {
    auto s = boot();
    settle(*s);
    type(*s, "NEXT");
    CHECK_EQ(s->m->ram[0x12], 18);
  }
}

TEST_SUITE("the bang statement") {
  TEST_CASE("says when nobody knows the command") {
    auto s = boot();
    settle(*s);
    type(*s, "!FROB");
    CHECK(has(flat(*s), "NO DRIVER KNOWS THE COMMAND !FROB"));
  }

  TEST_CASE("reports the unknown command from a program, with its line") {
    auto s = boot();
    settle(*s);
    type(*s, "10 !FROB");
    type(*s, "RUN", 8000000);
    const std::string t = text(*s);
    CHECK(has(flat(*s), "NO DRIVER KNOWS THE COMMAND !FROB IN LINE 10"));
  }

  // The vector holds the default routine's slot after boot, so a driver has
  // a next to keep. Zero would mean the chain ends in a jump to slot 0.
  TEST_CASE("powers on with a routine in the vector") {
    auto s = boot();
    settle(*s);
    const int vec = (s->m->ram[0] << 8) | s->m->ram[1];
    CHECK(vec != 0);
    type(*s, "PRINT DEEK(0)");
    CHECK(has(after(text(*s), "DEEK(0)"), std::to_string(vec > 32767 ? vec - 65536 : vec)));
  }
}

// CALL n calls the routine at instruction slot n and parks the A it came
// back with at $04, and JSR is the same word. JMP n goes there for good. A name in place of the
// number is the project build's to resolve, so a project is built here, in
// a temporary folder, and its cartridge booted.
namespace {

struct TempProject {
  std::filesystem::path path;
  TempProject() {
    path = std::filesystem::temp_directory_path() / std::filesystem::path("sc8-basic-" + std::to_string(std::rand()));
    std::filesystem::remove_all(path);
  }
  ~TempProject() { std::filesystem::remove_all(path); }
  void write(const std::string& name, const std::string& text) {
    std::filesystem::create_directories(path / "src");
    std::ofstream(path / "src" / name) << text;
  }
  Cartridge build() {
    project::Built b = project::build(project::layoutOf(path), {});
    if (!b.cartridge) throw std::runtime_error(b.errors.empty() ? "no cartridge" : b.errors[0]);
    return *b.cartridge;
  }
};

}  // namespace

TEST_SUITE("CALL and JMP") {
  TEST_CASE("HEX$ of the A a routine left at 4") {
    TempProject p;
    p.write("fifteen.asm", ".org $F000\nFIFTEEN: LD A <- 15\n        RET\n");
    p.write("demo.bas", "10 CALL FIFTEEN\n");
    auto s = std::make_unique<Session>(p.build());
    s->load();
    settle(*s);
    type(*s, "CALL $F000");
    CHECK_EQ(printed(*s, "PRINT HEX$(PEEK(4), 2)"), "0F");
  }

  TEST_CASE("a name at the prompt is a syntax error, because only a build knows labels") {
    auto s = boot();
    settle(*s);
    type(*s, "CALL DOUBLE");
    CHECK(has(flat(*s), "DOUBLE IS A ROUTINE NAME, WHICH ONLY A BUILT PROJECT KNOWS"));
    CHECK(s->m->status == Status::Running);
  }

  TEST_CASE("calls an assembly routine by slot and parks its A at 4, in decimal or hex, as CALL or JSR") {
    TempProject p;
    p.write("answer.asm", ".org $F000\nANSWER: LD A <- 42\n        RET\n");
    p.write("demo.bas", "10 CALL ANSWER\n20 PRINT PEEK(4)\n");
    Cartridge c = p.build();
    // The build put the slot in. Type the same number at the prompt.
    const std::string line = c.basic[0].second.substr(0, c.basic[0].second.find('\n'));
    REQUIRE(line == "10 CALL 61440");
    auto s = std::make_unique<Session>(std::move(c));
    s->load();
    settle(*s);
    type(*s, line.substr(3));
    type(*s, "PRINT PEEK(4)");
    CHECK(has(after(text(*s), "PEEK(4)"), "42"));
    CHECK_EQ(s->m->ram[4], 42);
    // The same slot as a hex number, and under the machine's own spelling.
    type(*s, "POKE 4, 0");
    type(*s, "CALL $F000");
    CHECK_EQ(s->m->ram[4], 42);
    type(*s, "POKE 4, 0");
    type(*s, "JSR $f000");
    CHECK_EQ(s->m->ram[4], 42);
    type(*s, "PRINT $FF, $10 + 1");
    CHECK(has(after(text(*s), "$10 + 1"), "255 17"));
  }

  TEST_CASE("calls a C function that keeps a global, calls another and reads the variables") {
    TempProject p;
    p.write("double.c",
            "#include <basicvars.h>\n"
            "int calls;\n"
            "static int twice(int v) { return v + v; }\n"
            "void DOUBLE(void) { calls = calls + 1; basic_set('a', twice(basic_get('A'))); basic_set('N', calls); }\n");
    p.write("autorun.bas", "10 A = 21\n20 CALL DOUBLE\n30 CALL DOUBLE\n40 PRINT A, N\n50 END\n");
    Cartridge c = p.build();
    const std::string& bas = c.basic[0].second;
    const size_t at = bas.find("20 CALL ") + 8;
    const std::string slot = bas.substr(at, bas.find('\n', at) - at);
    auto s = std::make_unique<Session>(std::move(c));
    s->load();
    settle(*s, 12000000);
    CHECK_MESSAGE(has(text(*s), "84 2"), text(*s));
    // And again from the prompt, by number: the C is still there, with
    // its global.
    type(*s, "A = 5");
    type(*s, "CALL " + slot);
    type(*s, "PRINT A, N");
    CHECK(has(after(text(*s), "PRINT A, N"), "10 3"));
  }

  TEST_CASE("JMP never comes back") {
    TempProject p;
    p.write("spin.asm", "SPIN:   LD A <- 99\n        LD [$0004] <- A\nHERE:   JMP HERE\n");
    p.write("demo.bas", "10 JMP SPIN\n20 PRINT \"NOT REACHED\"\n");
    Cartridge c = p.build();
    const std::string line = c.basic[0].second.substr(0, c.basic[0].second.find('\n'));
    auto s = std::make_unique<Session>(std::move(c));
    s->load();
    settle(*s);
    type(*s, line.substr(3));
    type(*s, "PRINT 1");
    // Nobody reads the keys any more, so the second line is never echoed.
    CHECK_EQ(s->m->ram[4], 99);
    CHECK_FALSE(has(text(*s), "PRINT"));
  }
}

// USR(width, target, p1, p2, p3) calls a routine the way compiled C calls
// a function: the parameters as words on the software stack, the answer
// in the return cells. So a C function needs no glue, and an assembly
// routine follows the same two rules.
TEST_SUITE("USR") {
  TEST_CASE("calls C functions with parameters and returns a byte or a word") {
    TempProject p;
    p.write("funcs.c",
            "int TWICE(int x) { return x + x; }\n"
            "int ADD3(int a, int b, int c) { return a + b + c; }\n"
            "int SEVEN(void) { return 7; }\n"
            "unsigned char buf[4];\n"
            "unsigned char *FILL(int n) { buf[0] = n; buf[1] = n + n; return buf; }\n");
    p.write("autorun.bas",
            "10 PRINT USR(2, TWICE, 21); \" \"; USR(1, ADD3, 1, 2, 3); \" \"; USR(1, SEVEN)\n"
            "20 P = USR(2, FILL, 7)\n"
            "30 PRINT PEEK(P); \" \"; PEEK(P + 1)\n"
            "40 PRINT USR(1, TWICE, 200); \" \"; USR(2, TWICE, 200)\n"
            "50 END\n");
    Cartridge c = p.build();
    auto s = std::make_unique<Session>(std::move(c));
    s->load();
    settle(*s, 12000000);
    const std::string t = text(*s);
    CHECK_MESSAGE(has(t, "42 6 7"), t);
    CHECK_MESSAGE(has(t, "7 14"), t);
    // 400 is $0190: width 1 keeps the low byte.
    CHECK_MESSAGE(has(t, "144 400"), t);
  }

  TEST_CASE("an assembly routine reads its parameter and answers in the return cells") {
    TempProject p;
    p.write("plus.asm",
            ".org $F000\n"
            "PLUS1:  LD A <- [D3+1]\n"
            "        INC A\n"
            "        LD [__ret+1] <- A\n"
            "        LD A <- 0\n"
            "        LD [__ret] <- A\n"
            "        RET\n");
    p.write("demo.bas", "10 PRINT 1\n");
    Cartridge c = p.build();
    auto s = std::make_unique<Session>(std::move(c));
    s->load();
    settle(*s);
    type(*s, "PRINT USR(1, $F000, 41)");
    CHECK(has(after(text(*s), "41)"), "42"));
    // Width 0 calls for the effect and gives 0.
    type(*s, "PRINT USR(0, $F000, 41)");
    const std::string zero = after(text(*s), "USR(0, $F000, 41)");
    const bool gaveZero = has(zero, "\n 0") || has(zero, "\n0");
    CHECK_MESSAGE(gaveZero, zero);
    // Three hundred calls that push six bytes each leave the stack level,
    // or BASIC would run out of it long before the end.
    type(*s, "FOR I = 1 TO 300 : X = USR(1, 61440, I) : NEXT I : PRINT X");
    settle(*s, 12000000);
    CHECK_MESSAGE(has(after(text(*s), "PRINT X"), "45"), text(*s));
  }

  TEST_CASE("a width other than 0, 1 or 2 is out of range, a missing target a syntax error") {
    auto s = boot();
    settle(*s);
    type(*s, "PRINT USR(3, 0)");
    CHECK(has(flat(*s), "RETURN VALUE WIDTH IS OUT OF RANGE [0,1,2]"));
    type(*s, "PRINT USR(1)");
    CHECK(has(flat(*s), "EXPECTED , AND A ROUTINE AFTER THE WIDTH BUT FOUND )"));
    CHECK(s->m->status == Status::Running);
  }
}

TEST_SUITE("the storage driver") {
  TEST_CASE("saves, and the slot holds the listing") {
    auto s = boot();
    settle(*s);
    type(*s, "10 PRINT \"HI\"");
    type(*s, "20 END");
    type(*s, "!SAVE \"A\"");
    REQUIRE(s->slots.size() == 1);
    CHECK(s->slots[0].first == "A");
    CHECK(s->slots[0].second == "10 PRINT \"HI\"\n20 END\n");
    CHECK(s->changes == 1);
    CHECK_FALSE(has(text(*s), "ERROR"));
  }

  TEST_CASE("loads what it saved after NEW") {
    auto s = boot();
    settle(*s);
    type(*s, "10 PRINT \"HI\"");
    type(*s, "!SAVE \"A\"");
    type(*s, "NEW");
    type(*s, "!LOAD \"A\"");
    type(*s, "LIST");
    const std::string t = text(*s);
    CHECK(has(after(t, "!LOAD"), "10 PRINT \"HI\""));
    type(*s, "RUN");
    CHECK(has(after(text(*s), "RUN"), "HI"));
  }

  TEST_CASE("loads a slot the host wrote, as the IDE's editor would") {
    auto s = boot();
    s->slots.emplace_back("DEMO", "10 FOR I=1 TO 2\r\n20 PRINT I*7\r\n30 NEXT I\r\n");
    settle(*s);
    type(*s, "!LOAD \"DEMO\"");
    type(*s, "RUN", 8000000);
    CHECK(inOrder(after(text(*s), "RUN"), {"7", "14"}));
  }

  TEST_CASE("replaces the program it had") {
    auto s = boot();
    s->slots.emplace_back("B", "10 PRINT \"NEW\"\n");
    settle(*s);
    type(*s, "10 PRINT \"OLD\"");
    type(*s, "20 PRINT \"KEEP\"");
    type(*s, "!LOAD \"B\"");
    type(*s, "LIST");
    const std::string t = after(text(*s), "!LOAD");
    CHECK(has(t, "10 PRINT \"NEW\""));
    CHECK_FALSE(has(t, "KEEP"));
  }

  TEST_CASE("lists the names in the catalog") {
    auto s = boot();
    s->slots.emplace_back("ONE", "10 END\n");
    s->slots.emplace_back("TWO", "10 END\n");
    settle(*s);
    type(*s, "!CATALOG");
    const auto scr = screen(*s);
    // The two names on their own rows, right after the echoed command.
    size_t row = 0;
    while (row < scr.size() && scr[row] != ">!CATALOG") row++;
    REQUIRE(row + 2 < scr.size());
    CHECK(scr[row + 1] == "ONE");
    CHECK(scr[row + 2] == "TWO");
  }

  TEST_CASE("deletes one and leaves the other") {
    auto s = boot();
    s->slots.emplace_back("ONE", "10 END\n");
    s->slots.emplace_back("TWO", "10 END\n");
    settle(*s);
    type(*s, "!DELETE \"ONE\"");
    REQUIRE(s->slots.size() == 1);
    CHECK(s->slots[0].first == "TWO");
    CHECK(s->changes == 1);
    type(*s, "!CATALOG");
    const std::string t = after(text(*s), "!CATALOG");
    CHECK(has(t, "TWO"));
    CHECK_FALSE(has(t, "ONE"));
  }

  TEST_CASE("says NOT FOUND for a slot the cartridge lacks") {
    auto s = boot();
    settle(*s);
    type(*s, "!LOAD \"NOPE\"");
    CHECK(has(flat(*s), "NO PROGRAM CALLED NOPE ON THE CARTRIDGE"));
    CHECK(s->changes == 0);
  }

  TEST_CASE("wants a quoted name") {
    auto s = boot();
    settle(*s);
    type(*s, "!SAVE A");
    CHECK(has(flat(*s), "SYNTAX ERROR: EXPECTED A PROGRAM NAME IN QUOTES BUT FOUND A"));
    CHECK(s->slots.empty());
  }

  TEST_CASE("refuses a name the device refuses") {
    auto s = boot();
    settle(*s);
    type(*s, "!SAVE \"\"");
    CHECK(has(flat(*s), "A PROGRAM NAME IS 1 TO 16 CHARACTERS"));
    CHECK(s->slots.empty());
  }
}

// The system page: BASIC's own state at fixed zero page addresses, so a
// program reads and writes it with the peek family and a driver in assembly
// finds it. docs/basic-system-page.md is the table these pin.
TEST_SUITE("numbers") {
  // A number is 16 bits, so a decimal written past 32767 wraps the way a
  // sum does: 40000 is 40000 - 65536. PEEK and POKE take the wrapped value
  // back to the same box.
  TEST_CASE("a decimal number past 32767 wraps, and still names the same box") {
    auto s = boot();
    settle(*s);
    CHECK_EQ(printed(*s, "PRINT 40000"), "-25536");
    CHECK_EQ(printed(*s, "PRINT 65536"), "0");
    CHECK_EQ(printed(*s, "PRINT 70000"), "4464");
    type(*s, "POKE 40000,7");
    CHECK_EQ(s->m->ram[0x9c40], 7);
    type(*s, "DOKE 40000,61440: PRINT DEEK($9C40)=$F000");
    CHECK_EQ(s->m->ram[0x9c40], 0xf0);
    CHECK_EQ(s->m->ram[0x9c41], 0x00);
  }
}

TEST_SUITE("the system page") {
  TEST_CASE("the cursor position lives at 5 and 6") {
    auto s = boot();
    settle(*s);
    type(*s, "PRINT PEEK(5), PEEK(6)");
    // The column is 0 after the newline PRINT ends with. The row is the
    // line after the one the command was typed on.
    const std::string t = after(text(*s), "PEEK(6)");
    CHECK(has(t, "0"));
    // The prompt is back on screen by now, so the column is 1, past the
    // greater-than sign.
    CHECK_EQ(s->m->ram[5], 1);
    CHECK(s->m->ram[6] > 2);
  }

  TEST_CASE("the last key pressed lives at 7") {
    auto s = boot();
    settle(*s);
    type(*s, "PRINT PEEK(7)");
    // Enter, code 13, was the last key of the line just typed.
    CHECK(has(after(text(*s), "PEEK(7)"), "13"));
  }

  // The break check reads a key and pushes it back for INKEY$. The note
  // at 7 was made only on a read from the device, so a key that came by
  // the pushback left PEEK(7) at the Enter that started the program.
  TEST_CASE("box 7 follows the keys a program reads with INKEY$") {
    auto s = boot();
    settle(*s);
    type(*s, "10 A$=INKEY$");
    type(*s, "20 IF A$<>\"\" THEN PRINT PEEK(7)");
    type(*s, "30 GOTO 10");
    for (char ch : std::string("RUN")) {
      s->pushKey(ch, false);
      s->runBudget(120000);
    }
    s->pushKey(13, false);
    s->runBudget(2000000);
    s->pushKey(90, false);  // Z
    s->runBudget(3000000);
    s->pushKey(27, false);
    s->runBudget(2000000);
    CHECK(has(after(text(*s), "RUN"), "90"));
  }

  TEST_CASE("the program and its length are at 8 and 10") {
    auto s = boot();
    settle(*s);
    type(*s, "10 PRINT 1");
    const int prog = (s->m->ram[8] << 8) | s->m->ram[9];
    const int len = (s->m->ram[10] << 8) | s->m->ram[11];
    CHECK(prog > 0x20);
    CHECK(len > 0);
    type(*s, "NEW");
    // An empty program is its three byte end marker.
    CHECK_EQ((s->m->ram[10] << 8) | s->m->ram[11], 3);
  }

  TEST_CASE("the variables are reachable through the pointer at 12") {
    auto s = boot();
    settle(*s);
    type(*s, "A=1234");
    type(*s, "PRINT DEEK(DEEK(12))");
    CHECK(has(after(text(*s), "DEEK(12))"), "1234"));
    const int vars = (s->m->ram[12] << 8) | s->m->ram[13];
    CHECK_EQ((s->m->ram[static_cast<size_t>(vars)] << 8) | s->m->ram[static_cast<size_t>(vars + 1)], 1234);
    // A letter owns eleven slots: the bare name, then A0 to A9. So B is
    // slot 11, at 22 bytes in.
    type(*s, "DOKE DEEK(12)+22, 77");
    CHECK_EQ(s->m->ram[static_cast<size_t>(vars + 23)], 77);
    type(*s, "PRINT B");
    CHECK(has(after(text(*s), "PRINT B"), "77"));
  }

  TEST_CASE("the error code and line are at 18 and 19") {
    auto s = boot();
    settle(*s);
    type(*s, "10 GOTO 999");
    type(*s, "RUN");
    CHECK_EQ(s->m->ram[0x12], 2);  // E_NOLINE
    CHECK_EQ((s->m->ram[0x13] << 8) | s->m->ram[0x14], 10);
    // The next command clears it.
    type(*s, "PRINT 1");
    CHECK_EQ(s->m->ram[0x12], 0);
  }
}

TEST_SUITE("small letters") {
  TEST_CASE("a string keeps its small letters") {
    auto s = boot();
    settle(*s);
    type(*s, "print \"Hallo, World!\"");
    CHECK(has(text(*s), "Hallo, World!"));
  }
  TEST_CASE("a line typed in small letters is stored as the IDE stores it") {
    auto s = boot();
    settle(*s);
    const std::vector<std::string> lines = {
        "10 for i = 1 to 3 : print \"Hallo, World!\" ; i : next i",
        "20 a$ = chr$(65) + mid$(\"xyz\", 2) : rem print this",
        "30 ! print this",
        "40 print \"a\" ; forx ; \"unterminated",
    };
    std::string program;
    for (const std::string& line : lines) {
      type(*s, line);
      program += line + "\n";
    }
    const auto& ram = s->m->ram;
    const size_t at = static_cast<size_t>((ram[basic::SYS_PROG] << 8) | ram[basic::SYS_PROG + 1]);
    const size_t len = static_cast<size_t>((ram[basic::SYS_PROG_LEN] << 8) | ram[basic::SYS_PROG_LEN + 1]);
    const std::vector<uint8_t> stored(ram.begin() + static_cast<std::ptrdiff_t>(at),
                                      ram.begin() + static_cast<std::ptrdiff_t>(at + len));
    CHECK_EQ(basic::decodeProgram(stored), basic::decodeProgram(basic::encodeProgram(program)));
    CHECK(stored == basic::encodeProgram(program));
  }
  TEST_CASE("a program typed in small letters runs") {
    auto s = boot();
    settle(*s);
    type(*s, "10 for i = 1 to 2 : print \"hi\" ; i : next i");
    type(*s, "run", 20000000);
    CHECK(has(text(*s), "hi1"));
    CHECK(has(text(*s), "hi2"));
  }
}

TEST_SUITE("basic program codec") {
  TEST_CASE("encode sorts, replaces, deletes and skips unnumbered lines") {
    const std::vector<uint8_t> p = basic::encodeProgram("  20 B\r\nnot a line\n10 A\n20 C\n30 D\n30\n");
    CHECK(basic::decodeProgram(p) == "10 A\n20 C\n");
    CHECK(p == std::vector<uint8_t>{0, 10, 5, 'A', 0, 0, 20, 5, 'C', 0, 0, 0, 3});
  }
  TEST_CASE("a program that does not fit is cut at a whole line") {
    std::string text;
    for (int i = 1; i <= 100; i++) text += std::to_string(i) + " " + std::string(100, 'X') + "\n";
    const std::vector<uint8_t> p = basic::encodeProgram(text);
    CHECK(p.size() < basic::PROGRAM_MAX);
    const std::string back = basic::decodeProgram(p);
    CHECK(back.find("1 XXX") == 0);
    CHECK(back.back() == '\n');
  }
  TEST_CASE("a merge keeps what each side changed") {
    const auto base = basic::encodeProgram("10 A\n20 B\n30 C\n");
    const auto doc = basic::encodeProgram("10 A\n20 EDITED\n30 C\n");
    const auto machine = basic::encodeProgram("10 A\n20 B\n25 TYPED\n");
    CHECK_EQ(basic::decodeProgram(basic::mergePrograms(base, doc, machine)), "10 A\n20 EDITED\n25 TYPED\n");
    // Both changed one line: the document's version stands.
    const auto both = basic::encodeProgram("10 A\n20 OTHER\n30 C\n");
    CHECK_EQ(basic::decodeProgram(basic::mergePrograms(base, doc, both)), "10 A\n20 EDITED\n30 C\n");
  }
  TEST_CASE("patching the text changes only what the program changed") {
    const std::string text = "10 print \"hi\"\n\n20   goto 10\n40 END\n";
    const auto program = basic::encodeProgram("10 print \"hi\"\n20 goto 10\n30 REM NEW\n");
    CHECK_EQ(basic::patchText(text, program), "10 print \"hi\"\n\n20   goto 10\n30 REM NEW\n");
    // A changed line takes the listing's spelling; a repeated number
    // keeps only the row encodeProgram reads.
    const auto changed = basic::encodeProgram("10 PRINT 1\n");
    CHECK_EQ(basic::patchText("10 A\n10 B\n", changed), "10 PRINT 1\n");
    CHECK_EQ(basic::encodeProgram(basic::patchText(text, program)), program);
  }
  TEST_CASE("encode capitalizes the BASIC words and nothing else") {
    auto encoded = [](const std::string& line) { return basic::decodeProgram(basic::encodeProgram(line + "\n")); };
    CHECK_EQ(encoded("10 for i = 1 to 10 : print \"Hallo, World!\" ; i : next i"),
             "10 FOR i = 1 TO 10 : PRINT \"Hallo, World!\" ; i : NEXT i\n");
    CHECK_EQ(encoded("20 a$ = chr$(65) + mid$(b$, 2)"), "20 a$ = CHR$(65) + MID$(b$, 2)\n");
    CHECK_EQ(encoded("30 rem print this"), "30 REM print this\n");
    CHECK_EQ(encoded("40 ! print this"), "40 ! print this\n");
    CHECK_EQ(encoded("50 print \"a\" ; forx ; \"b\""), "50 PRINT \"a\" ; forx ; \"b\"\n");
    CHECK_EQ(encoded("60 print \"unterminated to end"), "60 PRINT \"unterminated to end\n");
    CHECK_EQ(encoded("70 poke $f000, peek($F001)"), "70 POKE $f000, PEEK($F001)\n");
  }
  TEST_CASE("the editor's text takes the stored spelling of every numbered row") {
    const std::string text = "10 print i\r\n  20   goto 10\nnot a line: print\n\n30 rem for\n";
    const std::string canonical = basic::canonicalText(text);
    CHECK_EQ(canonical, "10 PRINT i\r\n  20   GOTO 10\nnot a line: print\n\n30 REM for\n");
    CHECK_EQ(canonical.size(), text.size());
    CHECK_EQ(basic::encodeProgram(canonical), basic::encodeProgram(text));
    CHECK_EQ(basic::patchText(canonical, basic::encodeProgram(text)), canonical);
    // The row being typed in is left alone.
    CHECK_EQ(basic::canonicalText("10 print\n20 prin\n30 goto 10\n", 1), "10 PRINT\n20 prin\n30 GOTO 10\n");
  }
  TEST_CASE("a line already in capitals encodes to the same bytes") {
    const std::vector<uint8_t> p = basic::encodeProgram("10 PRINT \"x\"\n");
    CHECK(p == std::vector<uint8_t>{0, 10, 9, KW_PRINT, ' ', '"', 'x', '"', 0, 0, 0, 3});
    CHECK(basic::encodeProgram("10 print \"x\"\n") == p);
  }
  TEST_CASE("a lowercase document keeps its spelling where the machine did not change it") {
    const std::string text = "10 for i = 1 to 3\n20   print i\n30 next i\n";
    const auto stored = basic::encodeProgram(text);
    CHECK_EQ(basic::decodeProgram(stored), "10 FOR i = 1 TO 3\n20 PRINT i\n30 NEXT i\n");
    CHECK_EQ(basic::patchText(text, stored), text);
    // The machine changes line 20 and adds 40. Only those rows are rewritten.
    const auto machine = basic::encodeProgram(basic::decodeProgram(stored) + "20 PRINT i * 2\n40 END\n");
    CHECK_EQ(basic::patchText(text, machine), "10 for i = 1 to 3\n20 PRINT i * 2\n30 next i\n40 END\n");
  }
}

TEST_SUITE("DATA, READ and RESTORE") {
  TEST_CASE("RUN places a line's bytes at its address, and the next line carries on") {
    auto s = boot();
    settle(*s);
    type(*s, "10 END");
    type(*s, "100 DATA $9C40,10,20,30");
    type(*s, "110 DATA 40,50,60");
    type(*s, "RUN");
    for (int i = 0; i < 6; i++) CHECK_EQ(s->m->ram[static_cast<size_t>(0x9c40 + i)], 10 * (i + 1));
  }

  TEST_CASE("a second address moves the cursor again") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA $9C40,1,2");
    type(*s, "110 DATA $9D00,3");
    type(*s, "120 DATA 4");
    type(*s, "RUN");
    CHECK_EQ(s->m->ram[0x9c40], 1);
    CHECK_EQ(s->m->ram[0x9c41], 2);
    CHECK_EQ(s->m->ram[0x9d00], 3);
    CHECK_EQ(s->m->ram[0x9d01], 4);
  }

  TEST_CASE("a negative value and a string place as bytes") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA $9C40,-1,-128,\"A,B\",\"\",$0FF,0255");
    type(*s, "RUN");
    CHECK_EQ(s->m->ram[0x9c40], 255);
    CHECK_EQ(s->m->ram[0x9c41], 128);
    CHECK_EQ(s->m->ram[0x9c42], 'A');
    CHECK_EQ(s->m->ram[0x9c43], ',');
    CHECK_EQ(s->m->ram[0x9c44], 'B');
    CHECK_EQ(s->m->ram[0x9c45], 255);
    CHECK_EQ(s->m->ram[0x9c46], 255);
    CHECK_EQ(s->m->ram[0x9c47], 0);
  }

  TEST_CASE("lines before any address go to the free memory after the program") {
    auto s = boot();
    settle(*s);
    type(*s, "10 END");
    type(*s, "100 DATA 7,8");
    type(*s, "RUN");
    const int end = programEnd(*s);
    CHECK_EQ(s->m->ram[static_cast<size_t>(end)], 7);
    CHECK_EQ(s->m->ram[static_cast<size_t>(end + 1)], 8);
  }

  TEST_CASE("a DATA line typed in small letters places its bytes") {
    auto s = boot();
    settle(*s);
    type(*s, "100 data $9c40,1");
    type(*s, "RUN");
    CHECK_EQ(s->m->ram[0x9c40], 1);
  }

  TEST_CASE("a value that is not a byte stops RUN at its line") {
    auto s = boot();
    settle(*s);
    type(*s, "10 PRINT \"RAN\"");
    type(*s, "100 DATA 1,256");
    type(*s, "RUN");
    CHECK(has(flat(*s), "A DATA VALUE IS ONE BYTE: -128 TO 255 IN LINE 100"));
    CHECK_FALSE(has(after(text(*s), "RUN"), "RAN"));
    CHECK_EQ(s->m->ram[0x12], 30);
    type(*s, "100 DATA -129");
    type(*s, "RUN");
    CHECK_EQ(s->m->ram[0x12], 30);
    type(*s, "100 DATA 1,$100");
    type(*s, "RUN");
    CHECK_EQ(s->m->ram[0x12], 30);
    type(*s, "100 DATA 65546");
    type(*s, "RUN");
    CHECK_EQ(s->m->ram[0x12], 30);
  }

  TEST_CASE("an expression or a name in DATA is a syntax error at its line") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA 1+2");
    type(*s, "RUN");
    CHECK(has(flat(*s), "SYNTAX ERROR IN LINE 100: EXPECTED , OR THE END OF THE LINE BUT FOUND +"));
    type(*s, "CLS");
    type(*s, "100 DATA X");
    type(*s, "RUN");
    CHECK(has(flat(*s), "SYNTAX ERROR IN LINE 100: EXPECTED A NUMBER OR A STRING BUT FOUND X"));
  }

  TEST_CASE("DATA after a colon is a syntax error") {
    auto s = boot();
    settle(*s);
    type(*s, "10 PRINT 1: DATA 5");
    type(*s, "RUN");
    CHECK(has(flat(*s), "EXPECTED A STATEMENT BUT FOUND DATA"));
  }

  TEST_CASE("DATA reached by a program, or typed at the prompt, does nothing") {
    auto s = boot();
    settle(*s);
    type(*s, "10 DATA 1,2");
    type(*s, "20 PRINT 7");
    type(*s, "RUN");
    CHECK(has(after(text(*s), "RUN"), "7"));
    CHECK_FALSE(has(text(*s), "?"));
    type(*s, "DATA 1,2");
    CHECK_FALSE(has(text(*s), "?"));
  }

  TEST_CASE("DATA that does not fit after the program stops RUN") {
    auto s = boot();
    settle(*s);
    // 23 remarks of 248 bytes, one of 145 and a DATA line of 248 leave 44
    // bytes free, and the line holds 120 values.
    std::string program;
    for (int n = 1; n <= 23; n++) program += std::to_string(n) + " REM " + std::string(240, 'X') + "\n";
    program += "24 REM " + std::string(137, 'X') + "\n";
    program += "1000 DATA 1";
    for (int i = 1; i < 120; i++) program += ",1";
    program += "\n";
    setProgram(*s, program);
    type(*s, "RUN");
    CHECK(has(flat(*s), "THE PROGRAM MEMORY IS FULL: 6144 BYTES AT MOST IN LINE 1000"));
  }

  TEST_CASE("DATA(n) gives the address, after an address too, and writes nothing") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA $9C40,10,20,30");
    type(*s, "110 DATA 40");
    type(*s, "120 DATA $9D00");
    type(*s, "130 DATA 5");
    type(*s, "A=DATA(100)");
    type(*s, "B=DATA(110)");
    type(*s, "C=DATA(120)");
    type(*s, "D=DATA(130)");
    CHECK_EQ(intVar(*s, 'A'), static_cast<int16_t>(0x9c40));
    CHECK_EQ(intVar(*s, 'B'), static_cast<int16_t>(0x9c43));
    CHECK_EQ(intVar(*s, 'C'), static_cast<int16_t>(0x9d00));
    CHECK_EQ(intVar(*s, 'D'), static_cast<int16_t>(0x9d00));
    CHECK_EQ(s->m->ram[0x9c40], 0);
  }

  TEST_CASE("DATA(n) of a line in the data area matches where RUN puts it") {
    auto s = boot();
    settle(*s);
    type(*s, "10 A=DATA(110)");
    type(*s, "100 DATA 1,2");
    type(*s, "110 DATA 3");
    type(*s, "RUN");
    CHECK_EQ(intVar(*s, 'A'), static_cast<int16_t>(programEnd(*s) + 2));
    CHECK_EQ(s->m->ram[static_cast<size_t>(programEnd(*s) + 2)], 3);
  }

  TEST_CASE("DATA(n) inside a longer expression leaves the rest of the line") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA $9C40,1");
    type(*s, "A=DATA(100)+1: B=7");
    CHECK_EQ(intVar(*s, 'A'), static_cast<int16_t>(0x9c41));
    CHECK_EQ(intVar(*s, 'B'), 7);
  }

  TEST_CASE("DATA(n) of a line without DATA, or of no line, says so") {
    auto s = boot();
    settle(*s);
    type(*s, "20 END");
    type(*s, "PRINT DATA(20)");
    CHECK(has(flat(*s), "LINE 20 HOLDS NO DATA"));
    CHECK_EQ(s->m->ram[0x12], 29);
    type(*s, "PRINT DATA(99)");
    CHECK(has(flat(*s), "THERE IS NO LINE 99"));
    CHECK_EQ(s->m->ram[0x12], 2);
  }

  TEST_CASE("READ runs across lines and never returns the address") {
    auto s = boot();
    settle(*s);
    type(*s, "10 FOR I=1 TO 6: READ A: PRINT A;: NEXT I");
    type(*s, "100 DATA $9C40,10,20,30");
    type(*s, "110 DATA 40,50,60");
    type(*s, "RUN");
    CHECK(has(after(text(*s), "RUN"), "102030405060"));
  }

  TEST_CASE("READ takes strings, and a value as written") {
    auto s = boot();
    settle(*s);
    type(*s, "10 READ A$,B,C$,D$: PRINT A$;B;C$;D$;\"!\"");
    type(*s, "100 DATA \"HI\",-1,\"A,B\",\"\"");
    type(*s, "RUN");
    CHECK(has(after(text(*s), "RUN"), "HI-1A,B!"));
  }

  TEST_CASE("RESTORE starts again, and RESTORE n starts at line n") {
    auto s = boot();
    settle(*s);
    type(*s, "10 READ A,B: RESTORE: READ C: RESTORE 110: READ D: PRINT A;B;C;D");
    type(*s, "100 DATA 1,2");
    type(*s, "110 DATA $9C40,3");
    type(*s, "RUN");
    CHECK(has(after(text(*s), "RUN"), "1213"));
  }

  TEST_CASE("each RUN reads from the first value again") {
    auto s = boot();
    settle(*s);
    type(*s, "10 READ A: PRINT A*11");
    type(*s, "100 DATA 5,6");
    type(*s, "RUN");
    type(*s, "RUN");
    CHECK_EQ(countOf(text(*s), "55"), 2);
    CHECK_FALSE(has(text(*s), "66"));
  }

  TEST_CASE("READ past the last value says so") {
    auto s = boot();
    settle(*s);
    type(*s, "10 READ A,B");
    type(*s, "100 DATA 1");
    type(*s, "RUN");
    CHECK(has(flat(*s), "READ FOUND NO MORE DATA IN LINE 10"));
    CHECK_EQ(s->m->ram[0x12], 28);
  }

  TEST_CASE("RESTORE n of a line without DATA, or of no line, says so") {
    auto s = boot();
    settle(*s);
    type(*s, "10 RESTORE 20");
    type(*s, "20 END");
    type(*s, "RUN");
    CHECK(has(flat(*s), "LINE 20 HOLDS NO DATA IN LINE 10"));
    CHECK_EQ(s->m->ram[0x12], 29);
    type(*s, "10 RESTORE 99");
    type(*s, "RUN");
    CHECK(has(flat(*s), "THERE IS NO LINE 99 IN LINE 10"));
  }

  TEST_CASE("line 0 names no line, even after NEW") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA 1,2");
    type(*s, "NEW");
    type(*s, "RESTORE 0");
    CHECK(has(flat(*s), "THERE IS NO LINE 0"));
    CHECK_EQ(s->m->ram[0x12], 2);
    type(*s, "PRINT DATA(0)");
    CHECK(has(flat(*s), "THERE IS NO LINE 0"));
    CHECK_EQ(s->m->ram[0x12], 2);
  }

  TEST_CASE("READ of the wrong kind of value names the READ line") {
    auto s = boot();
    settle(*s);
    type(*s, "10 READ A");
    type(*s, "100 DATA \"X\"");
    type(*s, "RUN");
    CHECK(has(flat(*s), "A STRING CANNOT BE USED AS A NUMBER IN LINE 10"));
    type(*s, "10 READ A$");
    type(*s, "100 DATA 5");
    type(*s, "RUN");
    CHECK(has(flat(*s), "A NUMBER CANNOT BE USED AS A STRING IN LINE 10"));
  }

  TEST_CASE("a program changed at the prompt reads from the first value again") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA 1,2");
    type(*s, "READ A");
    type(*s, "READ B");
    CHECK_EQ(intVar(*s, 'A'), 1);
    CHECK_EQ(intVar(*s, 'B'), 2);
    type(*s, "110 DATA 3");
    type(*s, "READ C");
    CHECK_EQ(intVar(*s, 'C'), 1);
  }

  TEST_CASE("a program the IDE rewrote reads from the first value again") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA 1,2");
    type(*s, "READ A");
    // Same length, so only the bytes tell the programs apart.
    setProgram(*s, "100 DATA 7,2\n");
    type(*s, "READ B");
    CHECK_EQ(intVar(*s, 'B'), 7);
  }

  TEST_CASE("an IDE swap of two values the old program hash missed reads from the first value again") {
    auto s = boot();
    settle(*s);
    // 0 and 8 swapped 256 bytes apart gave the old hash the same value.
    const std::string filler = "200 REM " + std::string(238, 'X') + "\n";
    setProgram(*s, "100 DATA 0\n" + filler + "300 DATA 8\n");
    type(*s, "READ A");
    CHECK_EQ(intVar(*s, 'A'), 0);
    setProgram(*s, "100 DATA 8\n" + filler + "300 DATA 0\n");
    type(*s, "READ B");
    CHECK_EQ(intVar(*s, 'B'), 8);
  }

  TEST_CASE("READ keeps its place in the word at 32") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA 1,2");
    CHECK_EQ(sysWord(*s, basic::SYS_READ), 0);
    type(*s, "READ A");
    // Past "DATA 1," in line 100's text, which starts 3 bytes into its record.
    CHECK_EQ(sysWord(*s, basic::SYS_READ), lineOffset(*s, 100) + 3 + 7);
    type(*s, "RESTORE");
    CHECK_EQ(sysWord(*s, basic::SYS_READ), 0);
    type(*s, "READ A");
    type(*s, "RUN");
    CHECK_EQ(sysWord(*s, basic::SYS_READ), 0);
    type(*s, "READ A");
    type(*s, "110 DATA 3");
    CHECK_EQ(sysWord(*s, basic::SYS_READ), 0);
    type(*s, "READ A");
    type(*s, "RENUM");
    CHECK_EQ(sysWord(*s, basic::SYS_READ), 0);
    type(*s, "READ A");
    type(*s, "NEW");
    CHECK_EQ(sysWord(*s, basic::SYS_READ), 0);
  }

  TEST_CASE("RESTORE n puts line n's offset in the word at 32, and READ starts there") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA 1,2");
    type(*s, "110 DATA $9C40,3,4");
    type(*s, "READ A");
    type(*s, "RESTORE 110");
    CHECK_EQ(sysWord(*s, basic::SYS_READ), lineOffset(*s, 110));
    type(*s, "READ B");
    CHECK_EQ(intVar(*s, 'B'), 3);
  }

  TEST_CASE("LOAD makes READ start again, at the loaded program's first value") {
    auto s = boot();
    s->slots.emplace_back("TABLE", "100 DATA 7,8\n");
    settle(*s);
    type(*s, "100 DATA 1,2");
    type(*s, "READ A");
    CHECK_EQ(intVar(*s, 'A'), 1);
    type(*s, "!LOAD \"TABLE\"");
    CHECK_EQ(sysWord(*s, basic::SYS_READ), 0);
    type(*s, "READ B");
    CHECK_EQ(intVar(*s, 'B'), 7);
  }

  TEST_CASE("DOKE 32,0 is RESTORE") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA 1,2");
    type(*s, "READ A,B");
    type(*s, "DOKE 32,0: READ C");
    CHECK_EQ(intVar(*s, 'C'), 1);
  }

  TEST_CASE("the IDE's write puts READ back at the first value") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA 1,2");
    type(*s, "READ A");
    CHECK(sysWord(*s, basic::SYS_READ) != 0);
    setProgram(*s, "100 DATA 1,2\n");
    CHECK_EQ(sysWord(*s, basic::SYS_READ), 0);
  }

  TEST_CASE("a first value with one, two or five hex digits is a value, not an address") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA $10,1");
    type(*s, "RUN");
    const int end = programEnd(*s);
    CHECK_EQ(s->m->ram[static_cast<size_t>(end)], 16);
    CHECK_EQ(s->m->ram[static_cast<size_t>(end + 1)], 1);
    type(*s, "100 DATA $12345");
    type(*s, "RUN");
    CHECK(has(flat(*s), "A DATA VALUE IS ONE BYTE: -128 TO 255 IN LINE 100"));
  }

  TEST_CASE("NEW makes READ start again") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA 1,2");
    type(*s, "READ A");
    CHECK_EQ(intVar(*s, 'A'), 1);
    type(*s, "NEW");
    type(*s, "100 DATA 7");
    type(*s, "READ B");
    CHECK_EQ(intVar(*s, 'B'), 7);
  }

  TEST_CASE("DATA after THEN is a syntax error") {
    auto s = boot();
    settle(*s);
    type(*s, "10 IF 1 THEN DATA 5");
    type(*s, "RUN");
    CHECK(has(flat(*s), "EXPECTED A STATEMENT BUT FOUND DATA"));
  }

  TEST_CASE("READ inside a subroutine carries on across calls") {
    auto s = boot();
    settle(*s);
    type(*s, "10 GOSUB 100");
    type(*s, "20 GOSUB 100");
    type(*s, "30 GOSUB 100");
    type(*s, "40 END");
    type(*s, "100 READ A: PRINT A;: RETURN");
    type(*s, "200 DATA 4,5,6");
    type(*s, "RUN");
    CHECK(has(after(text(*s), "RUN"), "456"));
  }

  TEST_CASE("POKE with a list writes each value to the next box") {
    auto s = boot();
    settle(*s);
    type(*s, "POKE $9C40,1,2,3");
    CHECK_EQ(s->m->ram[0x9c40], 1);
    CHECK_EQ(s->m->ram[0x9c41], 2);
    CHECK_EQ(s->m->ram[0x9c42], 3);
    type(*s, "POKE $9C50,9: PRINT PEEK($9C50)");
    CHECK(has(after(text(*s), "PEEK($9C50)"), "9"));
  }

  TEST_CASE("RENUM changes RESTORE n and DATA(n) and leaves a DATA line's values") {
    auto s = boot();
    settle(*s);
    type(*s, "5 RESTORE 7: A=DATA (7)");
    type(*s, "7 DATA 5, 7");
    type(*s, "RENUM");
    const auto& ram = s->m->ram;
    const size_t at = static_cast<size_t>((ram[basic::SYS_PROG] << 8) | ram[basic::SYS_PROG + 1]);
    const size_t len = static_cast<size_t>((ram[basic::SYS_PROG_LEN] << 8) | ram[basic::SYS_PROG_LEN + 1]);
    CHECK_EQ(basic::decodeProgram(std::span<const uint8_t>(ram.data() + at, len)),
             "10 RESTORE 20: A=DATA (20)\n20 DATA 5, 7\n");
  }
}

TEST_SUITE("the system page") {
  // The page size is written three times: basic.h's SYS_END, the
  // -zp-reserve the interpreter is compiled with, and program.h for the
  // IDE. These hold the three to one value.
  TEST_CASE("basic.h, the compiled interpreter and program.h agree on its size") {
    std::string header;
    for (const auto& [name, text] : basicSources()) {
      if (name == "basic.h") header = text;
    }
    REQUIRE_FALSE(header.empty());
    const size_t end = header.find("#define SYS_END");
    REQUIRE(end != std::string::npos);
    CHECK_EQ(std::stoi(header.substr(header.find("0x", end)), nullptr, 16), basic::SYSTEM_PAGE_SIZE);
    const auto defined = [&](const char* name) {
      const size_t at = header.find(std::string("#define ") + name + " ");
      REQUIRE_MESSAGE(at != std::string::npos, name);
      return std::stoi(header.substr(header.find("0x", at)), nullptr, 16);
    };
    CHECK_EQ(defined("SYS_READ"), basic::SYS_READ);
    CHECK_EQ(defined("SYS_COLS"), basic::SYS_COLS);
    CHECK_EQ(defined("SYS_ROWS"), basic::SYS_ROWS);
    CHECK_EQ(defined("SYS_COL"), basic::SYS_COL);
    CHECK_EQ(defined("SYS_ROW"), basic::SYS_ROW);
    CHECK_EQ(defined("SCREEN"), basic::SCREEN);
    const std::string asmText(basicAsm());
    const size_t sys = asmText.find("__sys:");
    REQUIRE(sys != std::string::npos);
    const std::string line = asmText.substr(sys, asmText.find('\n', sys) - sys);
    CHECK(has(line, "ds " + std::to_string(basic::SYSTEM_PAGE_SIZE)));
    // The interpreter's heap stack starts at its screen, which the build
    // passes as --heap-stack-top.
    CHECK(has(asmText, "LD D1 <- " + std::to_string(basic::SCREEN) + "\n        LD D3 <- D1"));
  }
}

// Fonts and the text grid: LOADFONT and SETTEXT. The screen is 4 KB at
// $F000 and the grid lives on the system page. docs/design/font-design.md.
namespace {

// Pixels at `fg` in the w by h cell at col, row of a composed frame.
int litInCell(const Gpu::Frame& f, int col, int row, int w, int h, int fg) {
  int n = 0;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      if (f[static_cast<size_t>((row * h + y) * gpu::SCREEN_W + col * w + x)] == fg) n++;
    }
  }
  return n;
}

// A BASIC project with an 8 by 8 font whose A is a solid block, and a
// picture beside it, booted.
std::unique_ptr<Session> bootWithFont() {
  TempProject p;
  p.write("demo.bas", "10 PRINT 1\n");
  font::Font f;
  f.width = 8;
  f.height = 8;
  for (size_t r = 0; r < 8; r++) f.glyphs['A' * 8 + r] = 0xff;
  std::filesystem::create_directories(p.path / "assets");
  std::ofstream(p.path / "assets" / "small.font") << font::write(f);
  std::ofstream(p.path / "assets" / "ship.png", std::ios::binary) << "not a picture, a file";
  auto s = std::make_unique<Session>(p.build());
  s->load();
  settle(*s);
  return s;
}

}  // namespace

TEST_SUITE("fonts and the text grid") {
  TEST_CASE("BASIC boots at 42 by 32 with its screen at $F000") {
    auto s = boot();
    settle(*s);
    CHECK_EQ(s->m->ram[basic::SYS_COLS], 42);
    CHECK_EQ(s->m->ram[basic::SYS_ROWS], 32);
    CHECK_EQ(s->gpu.textBase, 0xF000);
    const std::string top(reinterpret_cast<const char*>(&s->m->ram[0xF000]), 17);
    CHECK_EQ(top, "SimpleCPU-8 BASIC");
  }

  TEST_CASE("SETTEXT 8, 8 gives 32 by 32, and the cursor wraps at column 32") {
    auto s = boot();
    settle(*s);
    type(*s, "SETTEXT 8, 8");
    CHECK_EQ(s->m->ram[basic::SYS_COLS], 32);
    CHECK_EQ(s->m->ram[basic::SYS_ROWS], 32);
    CHECK_EQ(s->gpu.read(gpu::GPU_TEXT_COLS), 32);
    // The guide's example: boxes 34 and 35 are the grid.
    CHECK_EQ(printed(*s, "PRINT PEEK(34),PEEK(35)"), "32 32");
    type(*s, "PRINT \"" + std::string(33, 'X') + "\"");
    const auto scr = screen(*s);
    bool wrapped = false;
    for (size_t row = 0; row + 1 < scr.size(); row++) {
      if (scr[row] == std::string(32, 'X') && scr[row + 1] == "X") wrapped = true;
    }
    CHECK(wrapped);
  }

  TEST_CASE("SETTEXT 4, 4 gives 64 by 64 and scrolls at row 64") {
    auto s = boot();
    settle(*s);
    type(*s, "SETTEXT 4, 4");
    CHECK_EQ(s->m->ram[basic::SYS_COLS], 64);
    CHECK_EQ(s->m->ram[basic::SYS_ROWS], 64);
    type(*s, "FOR I=1 TO 70: PRINT I: NEXT I", 40000000);
    const auto scr = screen(*s);
    REQUIRE_EQ(scr.size(), 64u);
    CHECK(std::find(scr.begin(), scr.end(), "70") != scr.end());
    CHECK(std::find(scr.begin(), scr.end(), "5") == scr.end());
    CHECK_LT(s->m->ram[basic::SYS_ROW], 64);
  }

  TEST_CASE("SETTEXT outside 4 to 8 is error 32 and leaves the grid as it was") {
    auto s = boot();
    settle(*s);
    type(*s, "SETTEXT 8, 8");
    type(*s, "SETTEXT 3, 8");
    CHECK(has(flat(*s), "TEXT SIZE IS OUT OF RANGE [4,8]"));
    CHECK_EQ(s->m->ram[0x12], 32);
    CHECK_EQ(s->m->ram[basic::SYS_COLS], 32);
    CHECK_EQ(s->gpu.read(gpu::GPU_TEXT_COLS), 32);
    type(*s, "SETTEXT 8, 9");
    CHECK_EQ(s->m->ram[0x12], 32);
    CHECK_EQ(s->m->ram[basic::SYS_ROWS], 32);
    CHECK_EQ(s->m->status, Status::Running);
  }

  TEST_CASE("SYS_COL and SYS_ROW stay inside the grid after a scroll") {
    auto s = boot();
    settle(*s);
    type(*s, "SETTEXT 8, 8");
    type(*s, "FOR I=1 TO 40: PRINT \"" + std::string(31, 'Y') + "\": NEXT I", 40000000);
    CHECK_LT(s->m->ram[basic::SYS_COL], 32);
    CHECK_LT(s->m->ram[basic::SYS_ROW], 32);
  }

  TEST_CASE("LOADFONT SMALL draws with the font's glyphs and cell, and LIST keeps the name") {
    auto s = bootWithFont();
    type(*s, "10 loadfont small");
    // Row 3, column 4 of a 32 column grid, clear of the READY that RUN's
    // end prints at the top.
    type(*s, "20 POKE 61440+100,65");
    type(*s, "LIST");
    CHECK(has(text(*s), "10 LOADFONT small"));
    type(*s, "RUN");
    CHECK_EQ(s->m->ram[basic::SYS_COLS], 32);
    CHECK_EQ(s->m->ram[basic::SYS_ROWS], 32);
    CHECK_EQ(s->gpu.glyphRowOf('A', 0), 0xff);
    CHECK_EQ(litInCell(s->gpu.composeFrame(), 4, 3, 8, 8, s->gpu.textColor), 64);
  }

  TEST_CASE("LOADFONT alone brings back 6 by 8, 42 by 32 and the built-in glyphs") {
    auto s = bootWithFont();
    type(*s, "LOADFONT SMALL");
    CHECK_EQ(s->m->ram[basic::SYS_COLS], 32);
    type(*s, "LOADFONT");
    CHECK_EQ(s->m->ram[basic::SYS_COLS], 42);
    CHECK_EQ(s->m->ram[basic::SYS_ROWS], 32);
    CHECK_EQ(s->gpu.glyphRowOf('A', 0), glyphRow('A', 0));
  }

  TEST_CASE("LOADFONT of a name that is no font on the cartridge is error 33") {
    auto s = bootWithFont();
    type(*s, "LOADFONT NOPE");
    CHECK(has(flat(*s), "NO FONT CALLED NOPE ON THE CARTRIDGE"));
    CHECK_EQ(s->m->ram[0x12], 33);
    type(*s, "CLS");
    type(*s, "LOADFONT ship");
    CHECK(has(flat(*s), "NO FONT CALLED SHIP ON THE CARTRIDGE"));
    CHECK_EQ(s->m->ram[basic::SYS_COLS], 42);
    // BASIC's own ROM carries no ASET at all.
    auto plain = boot();
    settle(*plain);
    type(*plain, "LOADFONT SMALL");
    CHECK(has(flat(*plain), "NO FONT CALLED SMALL ON THE CARTRIDGE"));
  }

  TEST_CASE("LOADFONT and SETTEXT are stored in capitals, the name as typed") {
    CHECK_EQ(basic::canonicalLine("loadfont small"), "LOADFONT small");
    CHECK_EQ(basic::canonicalLine("settext 8, 8"), "SETTEXT 8, 8");
    CHECK(basic::basicKeywords().count("LOADFONT") == 1);
    CHECK(basic::basicKeywords().count("SETTEXT") == 1);
  }
}


// docs/design/basic-speed.md: the jump cache, keywords stored as tokens,
// literals stored with their value, the operand fast path and the lexer's
// byte wide position. Every test here passes on the interpreter before
// those changes too, unless it says otherwise.
TEST_SUITE("basic speed") {
  namespace {

  // The binary operators, loosest first.
  const std::vector<std::string> OPERATORS = {"OR", "AND", "=", "<>", "<", ">", "<=", ">=", "+", "-", "*", "/", "MOD"};

  // What RUN printed, from the row under its echo.
  std::string runOutput(const std::string& program) {
    auto s = boot();
    settle(*s);
    setProgram(*s, program);
    type(*s, "RUN", 200000000);
    return after(text(*s), ">RUN\n");
  }

  // One line per operator. `left` is the bare operand in front of it and
  // `wrap` puts the expression in its place in a statement. Each operator
  // stands alone, then before a * and then before a +, so a fast path that
  // took the wrong precedence would print a different number.
  std::string operatorLines(int first, const std::string& setup, const std::string& left, const std::string& right,
                            const std::function<std::string(const std::string&)>& wrap) {
    std::string p = std::to_string(first) + " " + setup + "\n";
    int n = first + 1;
    for (const std::string& op : OPERATORS) {
      for (const std::string& tail : {std::string(), std::string(" * 2"), std::string(" + 2")}) {
        p += std::to_string(n++) + " " + wrap(left + " " + op + " " + right + tail) + "\n";
      }
    }
    return p;
  }

  // The addresses are around $6000, above the interpreter's globals and
  // below its stack, where nothing else lives. A comparison's 0 or 1 as an
  // address reaches the bang vector, the word at 0. So the vector holds a
  // known word while the program runs, and gets its own back at the end.
  const std::string SAVE_VEC = "S=DEEK(0):DOKE 0,4660";
  const std::string RESTORE_VEC = "DOKE 0,S";

  // The cycles from the Enter after RUN until the program stops.
  uint64_t runCycles(const std::string& program) {
    auto s = boot();
    settle(*s);
    setProgram(*s, program);
    for (char ch : std::string("RUN")) {
      s->pushKey(ch, false);
      s->runBudget(120000);
    }
    s->pushKey(13, false);
    const uint64_t from = s->m->cycles;
    bool started = false;
    for (int i = 0; i < 100000; i++) {
      s->runBudget(2000);
      const bool running = s->m->ram[basic::SYS_RUNNING] != 0;
      if (running) started = true;
      if (started && !running) break;
    }
    return s->m->cycles - from;
  }

  // Lines numbered from 1, padded with REM lines to n.
  std::string numberedTo(std::vector<std::string> bodies, size_t n) {
    while (bodies.size() < n) bodies.push_back("REM FILLER");
    std::string t;
    for (size_t i = 0; i < bodies.size(); i++) t += std::to_string(i + 1) + " " + bodies[i] + "\n";
    return t;
  }

  }  // namespace

  TEST_CASE("an operand without an operator: every operator reads as it did before the fast path") {
    // POKE: the operator in the value, then in the address. Each address
    // line writes its own number. The bytes it could reach are shown at the
    // end, so an address worked out wrong shows on the screen.
    auto poke = [](const std::string& left, const std::string& addr) {
      return operatorLines(10, SAVE_VEC + ":V=7:M=24576", left, "3", [&](const std::string& e) {
               return "POKE " + addr + ", " + e + ":PRINT PEEK(" + addr + ");\" \";";
             }) +
             operatorLines(100, "M=24576:I=1", addr == "M" ? "M" : "24576", "1",
                           [](const std::string& e) { return "POKE " + e + ", I:I=I+1"; }) +
             "190 FOR J=24570 TO 24585:PRINT PEEK(J);\" \";:NEXT:PRINT PEEK(0);PEEK(1);PEEK(49152)\n" + "200 " +
             RESTORE_VEC + "\n";
    };
    // PEEK: addresses around 24576 hold known bytes, and 0 and 1 the vector.
    auto peek = [](const std::string& left) {
      return "5 " + SAVE_VEC + "\n6 FOR I=24560 TO 24600:POKE I,I-24500:NEXT\n" +
             operatorLines(10, "V=24576", left, "1", [](const std::string& e) { return "PRINT PEEK(" + e + ");\" \";"; }) +
             "200 " + RESTORE_VEC + "\n";
    };
    // A FOR limit: how many passes the loop makes.
    auto forLimit = [](const std::string& left) {
      return operatorLines(10, "V=7", left, "3", [](const std::string& e) {
        return "N=0:FOR I=1 TO " + e + ":N=N+1:NEXT:PRINT N;\" \";";
      });
    };
    auto ifThen = [](const std::string& left) {
      return operatorLines(10, "V=7", left, "3", [](const std::string& e) {
        return "PRINT \"|\";:IF " + e + " THEN PRINT 1;";
      });
    };
    const std::vector<std::pair<std::string, std::string>> programs = {
        {"POKE after a variable", poke("V", "M")},
        {"POKE after a literal", poke("7", "24576")},
        {"PEEK after a variable", peek("V")},
        {"PEEK after a literal", peek("24576")},
        {"FOR limit after a variable", forLimit("V")},
        {"FOR limit after a literal", forLimit("7")},
        {"IF after a variable", ifThen("V")},
        {"IF after a literal", ifThen("7")},
    };
    // The screens the interpreter printed before the fast path, recorded
    // from the ROM at commit f185ac8.
    const std::map<std::string, std::string> before = {
        {"POKE after a variable",
             "1 1 1 1 1 1 0 0 0 1 1 1 0 0 0 1 1 1 0 0 0\n"
             "1 1 1 10 13 12 4 1 6 21 42 23 2 4 4 1 2 3\n"
             "0 0 0 0 29 28 34 30 36 27 0 0 0 0 0 0 3824\n"
             "35\n"
             "READY\n"
             ">\x7f"},
        {"POKE after a literal",
             "1 1 1 1 1 1 0 0 0 1 1 1 0 0 0 1 1 1 0 0 0\n"
             "1 1 1 10 13 12 4 1 6 21 42 23 2 4 4 1 2 3\n"
             "0 0 0 0 29 28 34 30 36 27 0 0 0 0 0 0 3824\n"
             "35\n"
             "READY\n"
             ">\x7f"},
        {"PEEK after a variable",
             "52 52 52 52 52 52 18 18 18 52 52 52 18 18\n"
             "18 52 52 52 18 18 18 52 52 52 77 78 79 75\n"
             "74 77 76 0 78 76 0 78 18 18 0 READY\n"
             ">\x7f"},
        {"PEEK after a literal",
             "52 52 52 52 52 52 18 18 18 52 52 52 18 18\n"
             "18 52 52 52 18 18 18 52 52 52 77 78 79 75\n"
             "74 77 76 0 78 76 0 78 18 18 0 READY\n"
             ">\x7f"},
        {"FOR limit after a variable",
             "1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1\n"
             "1 1 1 10 13 12 4 1 6 21 42 23 2 4 4 1 2 3\n"
             "READY\n"
             ">\x7f"},
        {"FOR limit after a literal",
             "1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1\n"
             "1 1 1 10 13 12 4 1 6 21 42 23 2 4 4 1 2 3\n"
             "READY\n"
             ">\x7f"},
        {"IF after a variable",
             "|1|1|1|1|1|1||||1|1|1||||1|1|1||||1|1|1|1|\n"
             "1|1|1|1|1|1|1|1|1|1|1|1|1|1READY\n"
             ">\x7f"},
        {"IF after a literal",
             "|1|1|1|1|1|1||||1|1|1||||1|1|1||||1|1|1|1|\n"
             "1|1|1|1|1|1|1|1|1|1|1|1|1|1READY\n"
             ">\x7f"},
    };
    for (const auto& [name, program] : programs) {
      CAPTURE(name);
      CHECK_EQ(runOutput(program), before.at(name));
    }
  }

  // Proposal 1: the jump cache.

  TEST_CASE("a jump 300 lines down finds its line, and costs what a jump to the next line costs") {
    // 300 lines. The FOR is on line 2, its NEXT on line 299, and line 3
    // jumps there over 295 lines of REM.
    auto far = [](int passes) {
      std::vector<std::string> b = {"C=0", "FOR I=1 TO " + std::to_string(passes), "GOTO 299"};
      while (b.size() < 298) b.push_back("REM FILLER");
      b.push_back("C=C+1:NEXT I");
      b.push_back("PRINT \"DONE\";C");
      return numberedTo(b, 300);
    };
    auto near = [](int passes) {
      return numberedTo({"C=0", "FOR I=1 TO " + std::to_string(passes), "GOTO 4", "C=C+1:NEXT I", "PRINT C"}, 5);
    };
    CHECK(has(runOutput(far(100)), "DONE100"));
    // A pass's cost is the difference between 200 passes and 100, so RUN's
    // own walk over the program drops out.
    const uint64_t farPass = (runCycles(far(200)) - runCycles(far(100))) / 100;
    const uint64_t nearPass = (runCycles(near(200)) - runCycles(near(100))) / 100;
    CAPTURE(farPass);
    CAPTURE(nearPass);
    // A walk of 295 lines costs 83,000 cycles a pass. The first pass
    // walks, and every later one reads the cache.
    CHECK(farPass < nearPass + nearPass / 5);
  }

  TEST_CASE("two jump sites on one line keep their own targets") {
    // A false IF skips the rest of its line and a jump leaves it, so one
    // pass takes one site. X picks which: 1 takes THEN 60, 2 takes the
    // GOTO inside the second IF's THEN, 3 takes neither.
    const std::string program =
        "10 FOR X=1 TO 3\n"
        "20 IF X=1 THEN 60:REM\n"
        "30 IF X>1 THEN IF X=2 THEN GOTO 70\n"
        "40 PRINT \"N\";X;\n"
        "50 GOTO 80\n"
        "60 PRINT \"A\";X;:GOTO 80\n"
        "70 PRINT \"B\";X;\n"
        "80 NEXT X\n"
        "90 FOR X=1 TO 3:IF X=2 THEN 110\n"
        "100 PRINT \"C\";X;:NEXT X:GOTO 120\n"
        "110 PRINT \"D\";X;:NEXT X\n"
        "120 PRINT\n";
    // Twice, so the second pass of each line reads what the first kept.
    const std::string twice = "5 FOR K=1 TO 2\n" + program + "130 NEXT K\n";
    const std::string out = runOutput(twice);
    CHECK_EQ(countOf(out, "A1B2N3C1D2C3"), 2);
  }

  TEST_CASE("a computed GOTO is worked out on every pass") {
    const std::string program =
        "10 FOR I=1 TO 3\n"
        "20 GOTO 100+I*10\n"
        "110 PRINT \"X\";:GOTO 200\n"
        "120 PRINT \"Y\";:GOTO 200\n"
        "130 PRINT \"Z\";\n"
        "200 NEXT I:GOSUB 290+I\n"
        "210 END\n"
        "293 PRINT \"!\":RETURN\n"
        "294 PRINT \"?\":RETURN\n";
    CHECK(has(runOutput(program), "XYZ?"));
  }

  TEST_CASE("THERE IS NO LINE is raised when the jump runs, not before") {
    auto s = boot();
    settle(*s);
    setProgram(*s, "10 FOR I=1 TO 3\n20 PRINT \"P\";I;\n30 IF I=3 THEN 999\n40 GOTO 50\n50 NEXT I\n");
    type(*s, "RUN", 20000000);
    CHECK(has(flat(*s), "P1P2P3? THERE IS NO LINE 999 IN LINE 30"));
    type(*s, "CLS");
    setProgram(*s, "10 FOR I=1 TO 3\n20 PRINT \"Q\";I;\n30 IF I=3 THEN GOSUB 998\n40 GOTO 50\n50 NEXT I\n");
    type(*s, "RUN", 20000000);
    CHECK(has(flat(*s), "Q1Q2Q3? THERE IS NO LINE 998 IN LINE 30"));
  }

  TEST_CASE("RUN after an edit finds the lines where they moved to") {
    auto s = boot();
    settle(*s);
    setProgram(*s, "10 FOR I=1 TO 2\n15 GOSUB 100\n18 NEXT I\n20 END\n100 PRINT \"R\";I;:RETURN\n");
    type(*s, "RUN", 20000000);
    // Lines typed in above the routine move it and its offset.
    type(*s, "50 REM A LINE THAT MOVES LINE 100 FURTHER DOWN THE PROGRAM");
    type(*s, "60 REM AND ANOTHER");
    type(*s, "100 PRINT \"S\";I;:RETURN");
    type(*s, "RUN", 20000000);
    type(*s, "RENUM 100, 5");
    type(*s, "RUN", 20000000);
    type(*s, "NEW");
    type(*s, "10 GOTO 20");
    type(*s, "20 PRINT \"T\"");
    type(*s, "RUN", 20000000);
    const std::string t = flat(*s);
    CHECK(inOrder(t, {"R1R2", "S1S2", "S1S2", "T"}));
    CHECK_FALSE(has(t, "?"));
  }

  TEST_CASE("more jump sites than the cache has slots all keep their targets") {
    // 280 lines of one GOTO each, visited in a scrambled order, twice. A
    // site that read another site's target would skip lines or loop.
    const int n = 280;
    std::string program = "1 FOR P=1 TO 2:GOTO 100\n";
    for (int i = 0; i < n; i++) {
      const int next = i + 1 < n ? 100 + ((i + 1) * 97) % n : 900;
      program += std::to_string(100 + (i * 97) % n) + " C=C+1:GOTO " + std::to_string(next) + "\n";
    }
    program += "900 NEXT P:PRINT \"VISITS\";C\n";
    REQUIRE(basic::encodeProgram(program).size() < basic::PROGRAM_MAX - 3);
    CHECK(has(runOutput(program), "VISITS560"));
  }

  TEST_CASE("a jump to a line written in hex keeps its own target") {
    // GOSUB 100 notes its site, then THEN $32 and GOTO $3C jump. Each must
    // keep its target at its own site, never at the one noted before.
    const std::string program =
        "10 FOR I=1 TO 3\n"
        "20 GOSUB 100\n"
        "30 IF I>0 THEN $32\n"
        "40 PRINT \"BAD\";\n"
        "50 GOSUB 100:REM\n"
        "55 GOTO $3C\n"
        "58 PRINT \"BAD\";\n"
        "60 NEXT I\n"
        "70 END\n"
        "100 PRINT I;:RETURN\n";
    const std::string out = runOutput(program);
    CHECK(has(out, "112233"));
    CHECK_FALSE(has(out, "BAD"));
  }

  // Proposal 2: keywords stored as their byte.

  TEST_CASE("each KW_ constant is 128 plus its word's place in the keyword list") {
    const std::vector<std::pair<int, std::string>> named = {
        {KW_ABS, "ABS"},
        {KW_AND, "AND"},
        {KW_ASC, "ASC"},
        {KW_CALL, "CALL"},
        {KW_CATALOG, "CATALOG"},
        {KW_CHRS, "CHR$"},
        {KW_CIRCLE, "CIRCLE"},
        {KW_CLS, "CLS"},
        {KW_DATA, "DATA"},
        {KW_DEEK, "DEEK"},
        {KW_DELETE, "DELETE"},
        {KW_DOKE, "DOKE"},
        {KW_DRAW, "DRAW"},
        {KW_END, "END"},
        {KW_FOR, "FOR"},
        {KW_GOSUB, "GOSUB"},
        {KW_GOTO, "GOTO"},
        {KW_HEXS, "HEX$"},
        {KW_IF, "IF"},
        {KW_INK, "INK"},
        {KW_INKEYS, "INKEY$"},
        {KW_INPUT, "INPUT"},
        {KW_JMP, "JMP"},
        {KW_JSR, "JSR"},
        {KW_KEY, "KEY"},
        {KW_LEN, "LEN"},
        {KW_LET, "LET"},
        {KW_LIST, "LIST"},
        {KW_LOAD, "LOAD"},
        {KW_LOADFONT, "LOADFONT"},
        {KW_MIDS, "MID$"},
        {KW_MOD, "MOD"},
        {KW_MOVE, "MOVE"},
        {KW_NEW, "NEW"},
        {KW_NEXT, "NEXT"},
        {KW_NOT, "NOT"},
        {KW_OR, "OR"},
        {KW_PAD, "PAD"},
        {KW_PAPER, "PAPER"},
        {KW_PEEK, "PEEK"},
        {KW_PIXEL, "PIXEL"},
        {KW_PLOT, "PLOT"},
        {KW_POKE, "POKE"},
        {KW_PRINT, "PRINT"},
        {KW_READ, "READ"},
        {KW_REM, "REM"},
        {KW_RENUM, "RENUM"},
        {KW_RESTORE, "RESTORE"},
        {KW_RETURN, "RETURN"},
        {KW_RND, "RND"},
        {KW_RUN, "RUN"},
        {KW_SAVE, "SAVE"},
        {KW_SETTEXT, "SETTEXT"},
        {KW_STEP, "STEP"},
        {KW_STOP, "STOP"},
        {KW_STRS, "STR$"},
        {KW_THEN, "THEN"},
        {KW_TO, "TO"},
        {KW_USR, "USR"},
        {KW_VAL, "VAL"},
        {KW_WAIT, "WAIT"}};
    std::vector<std::string> order;
    const std::string all = BASIC_KEYWORDS;
    for (size_t at = all.find_first_not_of(' '); at != std::string::npos; at = all.find_first_not_of(' ', at)) {
      const size_t end = all.find(' ', at);
      order.push_back(all.substr(at, end - at));
      at = end;
    }
    CHECK_EQ(order.size(), static_cast<size_t>(KW_COUNT));
    CHECK_EQ(named.size(), order.size());
    for (const auto& [byte, word] : named) {
      CAPTURE(word);
      REQUIRE(byte >= KW_FIRST);
      REQUIRE(static_cast<size_t>(byte - KW_FIRST) < order.size());
      CHECK_EQ(order[static_cast<size_t>(byte - KW_FIRST)], word);
    }
  }

  namespace {

  // Every keyword, four to a line in code position. Then each place a
  // keyword stays text: a string, the rest of a REM line, a DATA line and
  // a bang's text. Last, words run into a number or a name. Each line fits
  // one screen row, so LIST shows it on a row of its own.
  std::vector<std::string> everyKeywordLines() {
    std::vector<std::string> words;
    const std::string all = BASIC_KEYWORDS;
    for (size_t at = all.find_first_not_of(' '); at != std::string::npos; at = all.find_first_not_of(' ', at)) {
      const size_t end = all.find(' ', at);
      const std::string w = all.substr(at, end - at);
      if (w != "REM") words.push_back(w);
      at = end;
    }
    std::vector<std::string> lines;
    int n = 10;
    for (size_t i = 0; i < words.size(); i += 4) {
      // DATA first would make a DATA line, so every line opens with A=.
      std::string line = std::to_string(n) + " A=";
      for (size_t k = i; k < i + 4 && k < words.size(); k++) line += " " + words[k];
      lines.push_back(line);
      n += 10;
    }
    for (const std::string& l : {std::string("PRINT \"GOTO PRINT\";A$:REM GOTO print"), std::string("DATA 1,\"PRINT\",$FF,print"),
                                 std::string("! SAVE print"), std::string("FOR I=1TO 3:forx=PRINT_:NEXT"),
                                 std::string("A=CHR$(1)+STR$(2)+X$:GOTO10"), std::string("REM")}) {
      lines.push_back(std::to_string(n) + " " + l);
      n += 10;
    }
    return lines;
  }

  // The program's bytes in the interpreter's memory.
  std::vector<uint8_t> storedProgram(const Session& s) {
    const auto& ram = s.m->ram;
    const size_t at = static_cast<size_t>(sysWord(s, basic::SYS_PROG));
    const size_t len = static_cast<size_t>(sysWord(s, basic::SYS_PROG_LEN));
    return std::vector<uint8_t>(ram.begin() + static_cast<std::ptrdiff_t>(at),
                                ram.begin() + static_cast<std::ptrdiff_t>(at + len));
  }

  // The rows LIST printed, from the row under its echo to READY.
  std::vector<std::string> listed(Session& s) {
    type(s, "CLS");
    type(s, "LIST", 20000000);
    std::vector<std::string> rows;
    bool on = false;
    for (const std::string& row : screen(s)) {
      if (on && row == "READY") break;
      if (on) rows.push_back(row);
      if (row == ">LIST") on = true;
    }
    return rows;
  }

  }  // namespace

  TEST_CASE("LIST, SAVE and LOAD give back every keyword as it was typed") {
    const std::vector<std::string> lines = everyKeywordLines();
    std::string program;
    for (const std::string& l : lines) program += l + "\n";
    auto s = boot();
    settle(*s);
    for (const std::string& l : lines) type(*s, l);
    // Stored as bytes: the program is shorter than its text.
    CHECK(storedProgram(*s).size() < program.size());
    // The listing shows a keyword typed in small letters in capitals.
    std::vector<std::string> want = lines;
    for (std::string& l : want) {
      const size_t sp = l.find(' ');
      l = l.substr(0, sp + 1) + basic::canonicalLine(l.substr(sp + 1));
    }
    CHECK(listed(*s) == want);
    type(*s, "!SAVE \"KW\"");
    REQUIRE(s->slots.size() == 1);
    std::string saved;
    for (const std::string& l : want) saved += l + "\n";
    CHECK_EQ(s->slots[0].second, saved);
    type(*s, "NEW");
    type(*s, "!LOAD \"KW\"", 40000000);
    CHECK(listed(*s) == want);
    CHECK(storedProgram(*s) == basic::encodeProgram(program));
  }

  TEST_CASE("the machine and the IDE store a line to the same bytes") {
    // edit.c's crunch and program.cpp's crunchLine, over every keyword in
    // capitals and in small letters.
    std::string program;
    auto s = boot();
    settle(*s);
    for (const std::string& l : everyKeywordLines()) {
      std::string lower = l;
      for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      // The small letter copy goes 5 past each line's own number.
      const size_t sp = lower.find(' ');
      lower = std::to_string(std::stoi(lower.substr(0, sp)) + 5) + lower.substr(sp);
      for (const std::string& line : {l, lower}) {
        type(*s, line);
        program += line + "\n";
      }
    }
    CHECK(storedProgram(*s) == basic::encodeProgram(program));
    CHECK_EQ(basic::decodeProgram(storedProgram(*s)), basic::decodeProgram(basic::encodeProgram(program)));
    for (const std::string& l : everyKeywordLines()) {
      const std::string body = l.substr(l.find(' ') + 1);
      CAPTURE(body);
      CHECK_EQ(basic::expandLine(basic::crunchLine(body)), basic::canonicalLine(body));
    }
  }

  TEST_CASE("RENUM over stored keywords changes the targets and nothing else") {
    auto s = boot();
    settle(*s);
    setProgram(*s,
               "5 REM GOTO 10 STAYS\n"
               "10 FOR I=1 TO 2:GOSUB 40\n"
               "20 NEXT I:IF I>2 THEN 30\n"
               "30 RESTORE 50:A=DATA(50):GOTO 60\n"
               "40 PRINT \"GOTO 10\";I;:RETURN\n"
               "50 DATA 1,2\n"
               "60 READ B:PRINT B\n");
    type(*s, "RENUM 100, 5");
    CHECK(listed(*s) == std::vector<std::string>{"100 REM GOTO 10 STAYS", "105 FOR I=1 TO 2:GOSUB 120",
                                                 "110 NEXT I:IF I>2 THEN 115",
                                                 "115 RESTORE 125:A=DATA(125):GOTO 130",
                                                 "120 PRINT \"GOTO 10\";I;:RETURN", "125 DATA 1,2", "130 READ B:PRINT B"});
    type(*s, "RUN", 40000000);
    CHECK(has(text(*s), ">RUN\nGOTO 101GOTO 1021\nREADY"));
  }

  TEST_CASE("a keyword written out still runs as the keyword") {
    // 1TO is one word to the store, and 1 then TO to the lexer. So TO
    // stays text in the line, and the lexer looks it up.
    CHECK(has(runOutput("10 FOR I=1TO 3:PRINT I;:NEXT\n"), "123"));
    auto s = boot();
    settle(*s);
    type(*s, "for i=1 to 2:print i*3;:next");
    CHECK(has(text(*s), "36"));
  }

  TEST_CASE("a keyword where a name stands gets the message it got as a name") {
    // The screens after RUN, recorded from the interpreter before keywords
    // were stored as bytes, with each line typed in.
    const std::vector<std::pair<std::string, std::string>> before = {
        {"10 TO = 5",
         "? TO IS NOT A VARIABLE: A VARIABLE IS ONE LETTER, OR A LETTER AND A DIGIT IN LINE 10 READY >"},
        {"10 LOAD \"X\"",
         "? UNKNOWN WORD LOAD IN LINE 10 READY >"},
        {"10 PRINT 1 THEN",
         "1? THEN IS NOT A VARIABLE: A VARIABLE IS ONE LETTER, OR A LETTER AND A DIGIT IN LINE 10 READY >"},
        {"10 INPUT PRINT",
         "?"},
        {"10 CALL PEEK(5)",
         "? PEEK IS A ROUTINE NAME, WHICH ONLY A BUILT PROJECT KNOWS: USE ITS NUMBER HERE IN LINE 10 READY >"},
        {"10 LET PRINT 5",
         "? UNKNOWN WORD PRINT IN LINE 10 READY >"},
        {"10 FOR TO=1 TO 5",
         "? TO IS NOT A VARIABLE: A VARIABLE IS ONE LETTER, OR A LETTER AND A DIGIT IN LINE 10 READY >"},
        {"10 IF 1 THEN THEN",
         "? UNKNOWN WORD THEN IN LINE 10 READY >"},
        {"10 A = 5 MOD",
         "? SYNTAX ERROR IN LINE 10: EXPECTED A NUMBER, A VARIABLE OR ( BUT FOUND THE END OF THE LINE READY >"},
        {"10 A = CHR$(5)",
         "? STRINGS ARE COMPARED WITH = <> < > <= OR >= IN LINE 10 READY >"},
        {"10 A$ = 5 + ABS",
         "? A NUMBER CANNOT BE USED AS A STRING IN LINE 10 READY >"},
        {"10 NEXT PRINT",
         "? NEXT WITHOUT A FOR IN LINE 10 READY >"},
        {"10 READ STEP",
         "? STEP IS NOT A VARIABLE: A VARIABLE IS ONE LETTER, OR A LETTER AND A DIGIT IN LINE 10 READY >"},
        {"10 LOADFONT PRINT",
         "? NO FONT CALLED PRINT ON THE CARTRIDGE IN LINE 10 READY >"},
        {"10 USR(1, RUN)",
         "? UNKNOWN WORD USR IN LINE 10 READY >"},
        {"10 A = STEP",
         "? STEP IS NOT A VARIABLE: A VARIABLE IS ONE LETTER, OR A LETTER AND A DIGIT IN LINE 10 READY >"},
        {"10 POKE 1 STEP 2",
         "? STEP IS NOT A VARIABLE: A VARIABLE IS ONE LETTER, OR A LETTER AND A DIGIT IN LINE 10 READY >"},
        {"10 A = 1: DATA 5",
         "? SYNTAX ERROR IN LINE 10: EXPECTED A STATEMENT BUT FOUND DATA READY >"},
        {"10 PRINT CHR$",
         "? SYNTAX ERROR IN LINE 10: EXPECTED A NUMBER, A VARIABLE OR ( BUT FOUND THE END OF THE LINE READY >"},
        {"10 X = NOT NOT",
         "? SYNTAX ERROR IN LINE 10: EXPECTED A NUMBER, A VARIABLE OR ( BUT FOUND THE END OF THE LINE READY >"},
    };
    for (const auto& [program, screenAfter] : before) {
      CAPTURE(program);
      auto s = boot();
      settle(*s);
      type(*s, program);
      type(*s, "RUN", 20000000);
      std::string got = after(flat(*s), ">RUN ");
      while (!got.empty() && got.back() == ' ') got.pop_back();
      CHECK_EQ(got, screenAfter);
    }
  }


  // Proposal 4: numbers stored with their value.

  namespace {

  // Numbers in every written form. Then values whose bytes would fool a
  // reader that looked at them. Those are 0, a colon, a quote, a bang, a
  // keyword's byte and the number marker itself.
  std::vector<std::string> everyLiteralLines() {
    return {
        "10 A=61440+$F000+0b1010+0B11+$ff+007",
        "20 PRINT 65535;65536;$FFFFF",
        "25 PRINT 0b11111111111111111",
        "30 A=0:B=$0:C=0b0:D=00",
        "40 A=58:B=$3A3A:C=34:D=$2222:E=$2200",
        "50 A=$2121:B=33:C=$8000:D=$BDBD:E=$BD",
        "60 A=128:B=189:C=$FF00:REM 10 $F 0b1",
        "70 POKE $F000,0b01000001:PRINT \"42 $2A\"",
        "80 FOR I=1TO 3:A=1ABC:B=0B2:C=0bx:NEXT",
        "90 DATA 7,$2A,0b101,\"9\"",
        "100 ! 12 $34 0b1",
    };
  }

  }  // namespace

  TEST_CASE("LIST, SAVE and LOAD give back every number as it was typed") {
    const std::vector<std::string> lines = everyLiteralLines();
    std::string program;
    for (const std::string& l : lines) program += l + "\n";
    auto s = boot();
    settle(*s);
    for (const std::string& l : lines) type(*s, l);
    CHECK(listed(*s) == lines);
    type(*s, "!SAVE \"NUM\"");
    REQUIRE(s->slots.size() == 1);
    CHECK_EQ(s->slots[0].second, program);
    type(*s, "NEW");
    type(*s, "!LOAD \"NUM\"", 40000000);
    CHECK(listed(*s) == lines);
    CHECK(storedProgram(*s) == basic::encodeProgram(program));
    CHECK_EQ(basic::decodeProgram(storedProgram(*s)), program);
    // The IDE's reading of the machine's memory, line by line.
    const auto back = basic::programLines(storedProgram(*s));
    CHECK_EQ(back.size(), lines.size());
    for (const std::string& l : lines) {
      const size_t sp = l.find(' ');
      CHECK_EQ(back.at(std::stoi(l.substr(0, sp))), l.substr(sp + 1));
    }
  }

  TEST_CASE("the machine and the IDE store a number to the same bytes") {
    std::string program;
    auto s = boot();
    settle(*s);
    for (const std::string& l : everyLiteralLines()) {
      type(*s, l);
      program += l + "\n";
    }
    CHECK(storedProgram(*s) == basic::encodeProgram(program));
    // A number keeps its value behind the marker: 61440 is $F000.
    const auto p = basic::encodeProgram("10 A=61440\n");
    CHECK(p == std::vector<uint8_t>{0, 10, 14, 'A', '=', KW_LITERAL, 0xF0, 0x00, '6', '1', '4', '4', '0', 0, 0, 0, 3});
  }

  TEST_CASE("a number runs as its value in every written form") {
    CHECK(has(runOutput("10 PRINT 61440;$F000;0b1010;0B11;$ff;007;0;$0;0b0\n"), "-4096-40961032557000"));
    CHECK(has(runOutput("10 PRINT 65535;65536;$FFFFF;0b11111111111111111\n"), "-10-1-1"));
    CHECK(has(runOutput("10 A=$2222:B=$3A3A:C=$BDBD:D=$8000:PRINT A;B;C;D\n"), "873814906-16963-32768"));
    CHECK(has(runOutput("10 PRINT 2+0b1*3:IF 0b1 THEN PRINT \"Y\"\n"), "5\nY"));
    // Typed at the prompt, where a line is not stored.
    auto s = boot();
    settle(*s);
    CHECK_EQ(printed(*s, "PRINT 0b101;$1F;0B1+1"), "5312");
  }

  TEST_CASE("DATA reads 0b numbers, a byte each") {
    CHECK(has(runOutput("10 READ A,B,C:PRINT A;B;C\n20 DATA 0b1010,0B11111111,0b00000000001\n"), "102551"));
    auto s = boot();
    settle(*s);
    setProgram(*s, "10 DATA 0b111111111\n");
    type(*s, "RUN", 20000000);
    CHECK(has(flat(*s), "A DATA VALUE IS ONE BYTE: -128 TO 255 IN LINE 10"));
  }

  TEST_CASE("a line too long for every number's value keeps the rest as digits") {
    // 83 numbers in 190 characters. Their values would take 249 bytes
    // more, so only the first ones get one. The line lists and runs as
    // typed all the same.
    std::string body = "POKE 24576";
    for (int i = 0; i < 81; i++) body += ",7";
    body += ":PRINT PEEK(24656)";
    REQUIRE(body.size() <= 250);
    const std::string line = "10 " + body;
    // The prompt takes 79 characters, so the line comes in by LOAD, which
    // stores it the way a typed line is stored.
    auto s = boot();
    settle(*s);
    s->slots.emplace_back("LONG", line + "\n");
    type(*s, "!LOAD \"LONG\"", 40000000);
    type(*s, "!SAVE \"BACK\"");
    REQUIRE(s->slots.size() == 2);
    CHECK_EQ(s->slots[1].second, line + "\n");
    const std::vector<uint8_t> stored = storedProgram(*s);
    CHECK(stored.size() <= 250 + 4 + 3);
    CHECK(stored == basic::encodeProgram(line + "\n"));
    type(*s, "RUN", 40000000);
    CHECK(has(text(*s), ">RUN\n7\nREADY"));
  }
}
