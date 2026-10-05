#include "basic/check.h"

#include <memory>
#include <string>

#include "basic/program.h"
#include "basic/session.h"

namespace sc8::basic {

namespace {

// The slot the program goes into. LOAD takes it from there by the path
// AUTORUN takes, so a line BASIC refuses to store is refused here too.
constexpr const char* SLOT = "BUILDCHECK";

// PERF DECISION: a check that has not finished after this many cycles
// fails the build with a message. CHECK reads each line once, so only a
// broken interpreter comes near it: a 16 KB program checks in a few
// million cycles.
constexpr uint64_t CYCLE_LIMIT = 2000000000;

// The slice the machine runs between looks at the screen. The IDE's key
// pump keeps the queue at 32 events or fewer, and so does this one.
constexpr uint64_t SLICE = 20000;
constexpr size_t QUEUE_ROOM = 32;

class Runner {
 public:
  explicit Runner(Session& s) : s_(s) {}

  bool failed() const { return !failure.empty(); }
  std::string failure;

  // True when BASIC waits at its prompt with every key typed read.
  bool atPrompt() const { return s_.input.queued() == 0 && basic::atPrompt(s_.m->ram); }

  void untilPrompt() {
    while (!failed() && !atPrompt()) run();
  }

  // Clear the screen and put the cursor home, type the command and wait
  // for the prompt. What the command printed then starts on the second
  // row, under its echo.
  void command(const std::string& text) {
    if (failed()) return;
    auto& ram = s_.m->ram;
    const int cells = ram[SYS_COLS] * ram[SYS_ROWS];
    for (int i = 0; i < cells; i++) ram[static_cast<size_t>(SCREEN + i)] = ' ';
    ram[SYS_COL] = 0;
    ram[SYS_ROW] = 0;
    const std::string keys = text + "\r";
    for (size_t at = 0; at < keys.size() && !failed();) {
      if (s_.input.queued() > QUEUE_ROOM) {
        run();
        continue;
      }
      const char c = keys[at++];
      s_.pushKey(c == '\r' ? 13 : static_cast<unsigned char>(c), false);
    }
    // BASIC clears the flag once it has the line, a few instructions after
    // the queue empties. Cleared here, it reads 1 again only at the prompt
    // after the command.
    ram[SYS_PROMPT] = 0;
    untilPrompt();
  }

  // The rows under the echo, run together up to the READY row. A message
  // the terminal wrapped at the edge reads whole that way.
  std::string printed() const {
    const auto& ram = s_.m->ram;
    const int cols = ram[SYS_COLS];
    const int rows = ram[SYS_ROWS];
    std::string out;
    for (int row = 1; row < rows; row++) {
      std::string line;
      for (int col = 0; col < cols; col++) line += static_cast<char>(ram[static_cast<size_t>(SCREEN + row * cols + col)]);
      if (line.rfind("READY", 0) == 0) break;
      out += line;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (out.rfind("? ", 0) == 0) out.erase(0, 2);
    return out;
  }

  int error() const { return s_.m->ram[SYS_ERR]; }
  int errorLine() const { return (s_.m->ram[SYS_ERR_LINE] << 8) | s_.m->ram[SYS_ERR_LINE + 1]; }

 private:
  void run() {
    s_.runBudget(SLICE);
    if (s_.m->cycles > CYCLE_LIMIT) failure = "the BASIC check did not finish";
  }

  Session& s_;
};

}  // namespace

Checked checkProgram(const Cartridge& interpreter, const std::string& text) {
  Cartridge cart = interpreter;
  cart.basic.clear();
  cart.basic.emplace_back(SLOT, text);
  // A Session holds a closure over itself, so it lives where it is built.
  auto s = std::make_unique<Session>(cart);
  s->load();

  Checked out;
  Runner r(*s);
  r.untilPrompt();
  r.command(std::string("!LOAD \"") + SLOT + "\"");
  if (!r.failed() && r.error() != 0) {
    // LOAD stops at the first line it cannot store, and the rest of the
    // program never reaches memory to be checked.
    out.issues.push_back({r.errorLine(), r.printed()});
    return out;
  }
  int from = 0;
  while (!r.failed()) {
    r.command(from == 0 ? "CHECK" : "CHECK " + std::to_string(from));
    if (r.failed() || r.error() == 0) break;
    const int line = r.errorLine();
    out.issues.push_back({line, r.printed()});
    // CHECK n reports nothing above n, so a line at or before the last
    // one would come round again.
    if (line < from || line == 0 || line >= 65535) break;
    from = line + 1;
  }
  out.failure = r.failure;
  return out;
}

}  // namespace sc8::basic
