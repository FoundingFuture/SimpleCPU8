// The machine: registers, the four memories, the ALU, the effective address
// adder, and the hardwired sequencer that runs microcode rows.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/conflicts.h"
#include "core/isa.h"
#include "core/microcode.h"
#include "core/signals.h"

namespace sc8 {

constexpr int RAM_SIZE = 65536;
// DESIGN: the stack memory is 2048 bytes unless the host asks for more, up
// to 65536, which SP's 16 bits can reach. A return address costs two bytes,
// so the default nests about 1000 calls deep. The bound lives on the
// memory, beside RAM_SIZE, and simplecpu --stack-size sets it.
constexpr int STACK_SIZE = 2048;  // the default size
constexpr int STACK_TOP = STACK_SIZE - 1;
constexpr int MIN_STACK_SIZE = 256;
constexpr int MAX_STACK_SIZE = 65536;
constexpr int ZERO_PAGE = 256;

enum class CrashKind {
  SignalConflict,
  NoMicrocode,
  IllegalProgramAddress,
  StackOverflow,
  StackUnderflow,
  // A device refused a command. docs/design/font-design.md.
  BadTextCell,
};

std::string_view crashKindName(CrashKind k);

// A stack size as a command line writes it: 4096, 4K or 0x1000. Empty when
// it is not a number or falls outside MIN_STACK_SIZE to MAX_STACK_SIZE.
std::optional<int> parseStackSize(std::string_view text);

enum class Status { Running, Halted, Crashed };

std::string_view statusName(Status s);

// The most recent bus transaction, for pin-level visualization. A row with
// several accesses reports the most interesting one: io > stack > ram > fetch.
struct BusEvent {
  enum class Kind { Fetch, RamRead, RamWrite, StackRead, StackWrite, IoRead, IoWrite };
  Kind kind;
  uint32_t addr;
  uint8_t data;
};

struct CrashInfo {
  CrashKind kind;
  std::string message;
  uint16_t pc;
  uint16_t lastInstrPc;
  std::optional<Row> row;
};

struct Flags {
  bool z = false, n = false, c = false, v = false;
  bool operator==(const Flags&) const = default;
};

// A command a device refused, which crashes the machine the way a bad
// instruction does. A device has no other way to say no.
struct DeviceFault {
  CrashKind kind;
  std::string message;
};

// The port gateway. Devices implement it and chain to a fallback.
class IoBus {
 public:
  virtual ~IoBus() = default;
  virtual void write(uint8_t port, uint8_t value) = 0;
  virtual uint8_t read(uint8_t port) = 0;
  // The fault the last write raised, handed over once. The machine asks
  // after every port write. A device in a chain asks its fallback too.
  virtual std::optional<DeviceFault> takeFault() { return std::nullopt; }
};

// Deterministic default bus: writes are logged, reads return 0.
class LogIoBus : public IoBus {
 public:
  // A constructor rather than an aggregate: GCC 13 at -O3 misreads the
  // push_back of a two byte aggregate as a one byte write past the buffer.
  struct Entry {
    uint8_t port, value;
    Entry(uint8_t p, uint8_t v) : port(p), value(v) {}
    bool operator==(const Entry&) const = default;
  };
  std::vector<Entry> log;
  void write(uint8_t port, uint8_t value) override { log.emplace_back(port, value); }
  uint8_t read(uint8_t) override { return 0; }
};

// The last executed microcycle: which microprogram, which row, which
// signals. The datapath view lights up from this.
struct MicroTrace {
  std::string op;
  int row;
  Row signals;
  std::optional<uint16_t> ea;
};

class Machine {
 public:
  Machine(std::vector<Instr> program, Microcode microcode, IoBus* io = nullptr);

  // Architectural state. Public on purpose: tests and the IDE poke it.
  uint16_t pc = 0;
  uint8_t irOp = 0;
  uint16_t irOperand = 0;
  uint8_t acc = 0;
  uint8_t aLatch = 0;
  uint8_t bLatch = 0;
  uint16_t d1 = 0;
  uint16_t d2 = 0;
  // The data stack pointer by convention: the C runtime keeps its frames
  // at [D3+n]. The hardware gives it no meaning of its own.
  uint16_t d3 = 0;
  uint16_t sp = STACK_TOP;  // points at the next free cell, grows down
  Flags flags;
  std::array<uint8_t, RAM_SIZE> ram{};
  std::vector<uint8_t> stack = std::vector<uint8_t>(STACK_SIZE);

  // The stack memory's size, MIN_STACK_SIZE to MAX_STACK_SIZE. Setting it
  // clears the stack and puts SP at the new top.
  void setStackSize(int bytes);
  int stackSize() const { return static_cast<int>(stack.size()); }
  uint16_t stackTop() const { return static_cast<uint16_t>(stack.size() - 1); }

  std::vector<Instr> program;

  Status status = Status::Running;
  std::optional<CrashInfo> crash;
  uint64_t cycles = 0;
  uint64_t instructions = 0;
  uint16_t lastInstrPc = 0;

  // Access heat, in cycle stamps (cycle + 1, so 0 means never touched).
  std::vector<uint32_t> ramReadAt, ramWriteAt;
  // How OFTEN, beside the heat maps' how RECENTLY. They saturate rather than
  // wrap: an address that wrapped reads as a cold one, which is worse than a
  // stuck one. No program can observe them.
  std::vector<uint32_t> ramReads, ramWrites;
  // The counts cost 2 to 3 percent on a loop that touches RAM every
  // instruction. On by default so Measure works whenever it is pressed.
  bool countAccesses = true;

  std::optional<BusEvent> lastBus;
  std::optional<MicroTrace> lastMicro;

  // Tracing is the machine's record of what it did: lastBus, lastMicro and
  // the two heat maps. Off, a microcycle allocates no record.
  bool trace() const { return trace_; }
  void setTrace(bool on);

  const Microcode& microcode() const { return microcode_; }
  void setMicrocode(Microcode mc);
  void setIo(IoBus* io) { io_ = io ? io : &defaultIo_; }
  IoBus& io() { return *io_; }

  // Reset clears CPU state only. RAM and stack keep their content.
  void reset();
  void clearAccessCounts();

  // Execute one micro-instruction. All reads see start-of-cycle values.
  // All writes commit at the end. Returns false when the machine stopped.
  bool executeRow(const Row& row);

  // One microcycle of the hardwired sequencer.
  bool microStep();

  // Run one whole instruction: fetch, dispatch, execute.
  bool instructionStep();

  void run(uint64_t maxInstructions);

  // Where the sequencer stands: the row it runs next. Between two
  // instructions section is -1 and inFetch is true. Inside a fetch
  // rowIndex counts the fetch rows done. Inside an instruction it counts
  // the instruction's rows done, with the whole fetch behind it.
  struct SequencerPos {
    bool inFetch;
    int section;
    size_t rowIndex;
  };
  SequencerPos sequencer() const { return {inFetch_, section_, rowIndex_}; }

 private:
  struct RowInfo {
    std::optional<Conflict> conflict;
    bool needsRam = false;
    bool needsStack = false;
    AluOp aluOp = AluOp::AluNone;
  };
  struct AluOut {
    uint8_t result;
    Flags flags;
  };

  static RowInfo analyze(const Row& row);
  bool executeRow(const Row& row, const RowInfo& info);
  void fail(CrashKind kind, std::string message, const Row* row = nullptr);
  // After a port write: crash with the device's fault, when it raised one.
  bool deviceFailed(const Row& row);
  AluOut alu(AluOp op) const;
  uint16_t effectiveAddress(const Row& row);
  uint16_t stackAddress(const Row& row) const;
  bool eaCarry_ = false;  // the last sum carried out of bit 15, for EA_FLAGS
  void finishInstruction();
  void rebuildDispatch();

  Microcode microcode_;
  // Rows are static once microcode is loaded, so their legality and
  // resource profile are computed once. Indexed like the sections.
  std::vector<std::vector<RowInfo>> rowInfo_;
  // Opcode to section index, built on load. -1 for no microprogram.
  std::array<int, 256> dispatch_{};
  int fetchSection_ = -1;

  LogIoBus defaultIo_;
  IoBus* io_ = &defaultIo_;
  bool trace_ = true;

  // Sequencer position: the fetch program runs first, then the dispatched one.
  bool inFetch_ = true;
  int section_ = -1;
  size_t rowIndex_ = 0;
};

}  // namespace sc8
