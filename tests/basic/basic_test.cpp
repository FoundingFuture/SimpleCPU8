// BASIC, written in C, running on the CPU. The fourth layer.
//
// The screen is text mode mapped at $FAC0, so what the interpreter printed is
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
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "basic/basic_rom.h"
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

using namespace sc8;

namespace {

constexpr int SCREEN = 0xfac0;
constexpr int COLS = 42;
constexpr int ROWS = 32;

const Microcode& optimal() {
  static const Microcode mc = buildOptimal();
  return mc;
}

// The Session's machine and device chain, on the BASIC cartridge. The Gpu
// holds a closure over the machine's cycle counter, so a Session is built in
// place and never moved.
struct Session {
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  Cartridge cart;
  InputBus input;
  // The slots the storage device works on, standing in for the cartridge's
  // basic list, and how many times it said they changed.
  BasicSlots slots;
  int changes = 0;
  Storage storage{&input};
  Apu apu{&storage};
  Acp acp{&apu};
  Gpu gpu;
  std::unique_ptr<Machine> m;

  Session() : gpu([this] { return m ? m->cycles : 0; }, &acp) {
    const CartridgeResult r = decodeCartridge(std::vector<uint8_t>(basicRom().begin(), basicRom().end()));
    if (!r.cartridge) throw std::runtime_error("basic.rom does not decode: " + r.error);
    cart = *r.cartridge;
  }

  // On a cartridge of the caller's, one a project build made. Its BAS
  // slots are what the storage device serves, so AUTORUN runs at boot.
  explicit Session(Cartridge c) : cart(std::move(c)), slots(cart.basic), gpu([this] { return m ? m->cycles : 0; }, &acp) {}

  // Session.load: power the devices on, hand them the cartridge, build the
  // machine and apply the .ram image.
  void load() {
    gpu.powerOn();
    gpu.attachCart(cart.data);
    apu.powerOn();
    apu.attachCart(cart.data);
    m = std::make_unique<Machine>(cart.program, optimal(), &gpu);
    // Nothing here reads a recording, and a run of several million
    // instructions per test is what the budgets ask for.
    m->setTrace(false);
    m->countAccesses = false;
    gpu.attachRam(m->ram.data());
    acp.attachRam(m->ram.data());
    storage.powerOn();
    storage.attachRam(m->ram.data());
    storage.attach(&slots, [this] { changes++; });
    m->ram.fill(0);
    std::copy(cart.ram.begin(), cart.ram.end(), m->ram.begin());
  }

  void runBudget(uint64_t n) { m->run(n); }
  void pushKey(int code, bool release) { input.pushKey(static_cast<uint8_t>(code), release); }
};

std::unique_ptr<Session> boot() {
  auto s = std::make_unique<Session>();
  s->load();
  return s;
}

// Everything on screen, trailing blanks trimmed off each row.
std::vector<std::string> screen(const Session& s) {
  std::vector<std::string> out;
  for (int y = 0; y < ROWS; y++) {
    std::string line;
    for (int x = 0; x < COLS; x++) {
      line += static_cast<char>(s.m->ram[static_cast<size_t>(SCREEN + y * COLS + x)]);
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

// Type a line and press Enter, then let the interpreter act on it.
void type(Session& s, const std::string& line, uint64_t budget = 6000000) {
  for (char ch : line) {
    s.pushKey(std::toupper(static_cast<unsigned char>(ch)), false);
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
  for (int i = 0; i < ROWS * COLS; i++) {
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
}
