#include "core/machine.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>

namespace sc8 {

std::string_view crashKindName(CrashKind k) {
  switch (k) {
    case CrashKind::SignalConflict: return "signal-conflict";
    case CrashKind::NoMicrocode: return "no-microcode";
    case CrashKind::IllegalProgramAddress: return "illegal-program-address";
    case CrashKind::StackOverflow: return "stack-overflow";
    case CrashKind::StackUnderflow: return "stack-underflow";
    case CrashKind::BadTextCell: return "bad-text-cell";
  }
  return "?";
}

std::string_view statusName(Status s) {
  switch (s) {
    case Status::Running: return "running";
    case Status::Halted: return "halted";
    case Status::Crashed: return "crashed";
  }
  return "?";
}

namespace {

// Bus priority within a row: io > stack > ram > fetch.
int busRank(BusEvent::Kind k) {
  using enum BusEvent::Kind;
  switch (k) {
    case Fetch: return 0;
    case RamRead:
    case RamWrite: return 1;
    case StackRead:
    case StackWrite: return 2;
    case IoRead:
    case IoWrite: return 3;
  }
  return 0;
}

std::string hex(unsigned v) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "0x%x", v);
  return buf;
}

// A pending end-of-cycle write. The row collects them while every read
// still sees start-of-cycle values.
struct Write {
  enum class Target : uint8_t {
    PC, PCH, PCL, IR, ACC, A, B, D1, D1H, D1L, D2, D2H, D2L, D3, D3H, D3L, SP, FLAGS, RAM, STACK
  };
  Target target;
  uint16_t value;
  uint16_t addr;  // RAM or stack address, or the opcode for IR
  Flags flags;
};

// The most writes a row that passed the rules can make: rule 1 allows one
// writer per register atom, and there are fourteen atoms (a PC or D load
// counts once here but takes two of them), rules 2 and 4 allow one RAM
// and one stack access. A row of eleven signals that writes nine
// registers passed every rule and overflowed the eight this held before.
constexpr size_t REGISTER_ATOMS = 14;
constexpr size_t MAX_WRITES = REGISTER_ATOMS + 2;

struct WriteList {
  std::array<Write, MAX_WRITES> items{};
  size_t count = 0;
  void push(Write::Target t, uint16_t value, uint16_t addr = 0) {
    assert(count < items.size());
    items[count++] = Write{t, value, addr, {}};
  }
  void pushFlags(Flags f) {
    assert(count < items.size());
    items[count++] = Write{Write::Target::FLAGS, 0, 0, f};
  }
};

}  // namespace

Machine::Machine(std::vector<Instr> prog, Microcode mc, IoBus* io)
    : program(std::move(prog)),
      ramReadAt(RAM_SIZE),
      ramWriteAt(RAM_SIZE),
      ramReads(RAM_SIZE),
      ramWrites(RAM_SIZE) {
  if (io) io_ = io;
  io_->attachFaultSink(this);
  setMicrocode(std::move(mc));
}

Machine::~Machine() { io_->detachFaultSink(this); }

void Machine::setIo(IoBus* io) {
  io_->detachFaultSink(this);
  io_ = io ? io : &defaultIo_;
  io_->attachFaultSink(this);
}

void Machine::deviceFault(DeviceFault f) {
  if (!fault_) fault_ = std::move(f);
}

void Machine::setMicrocode(Microcode mc) {
  microcode_ = std::move(mc);
  rebuildDispatch();
  inFetch_ = true;
  section_ = -1;
  rowIndex_ = 0;
}

void Machine::rebuildDispatch() {
  dispatch_.fill(-1);
  fetchSection_ = -1;
  rowInfo_.clear();
  const auto& sections = microcode_.sections();
  rowInfo_.reserve(sections.size());
  for (size_t i = 0; i < sections.size(); i++) {
    const auto& s = sections[i];
    std::vector<RowInfo> infos;
    infos.reserve(s.rows.size());
    for (const Row& r : s.rows) infos.push_back(analyze(r));
    rowInfo_.push_back(std::move(infos));
    if (s.name == "fetch") {
      fetchSection_ = static_cast<int>(i);
      continue;
    }
    if (const OpDef* def = opByName(s.name)) dispatch_[def->op] = static_cast<int>(i);
  }
}

Machine::RowInfo Machine::analyze(const Row& row) {
  using enum MemAccess;
  RowInfo info;
  info.conflict = checkRow(row);
  if (info.conflict) return info;
  for (Signal s : row) {
    const SignalMeta& meta = signalMeta(s);
    if (meta.mem == RamRead || meta.mem == RamWrite) info.needsRam = true;
    // An address load uses the adder with no RAM access.
    if (s == Signal::D1_LOAD_EA || s == Signal::D2_LOAD_EA || s == Signal::D3_LOAD_EA || s == Signal::EA_FLAGS) {
      info.needsRam = true;
    }
    if (meta.mem == StackRead || meta.mem == StackWrite) info.needsStack = true;
    if (meta.aluSelect != AluOp::AluNone) info.aluOp = meta.aluSelect;
  }
  return info;
}

void Machine::fail(CrashKind kind, std::string message, const Row* row) {
  status = Status::Crashed;
  CrashInfo info{kind, std::move(message), pc, lastInstrPc, std::nullopt};
  if (row) info.row = *row;
  crash = std::move(info);
}

// Switching tracing off drops what was already recorded: a stale display is
// the one thing the machine must never show. reset() leaves this switch
// alone, because it describes the person watching, not the program.
void Machine::setTrace(bool on) {
  if (on == trace_) return;
  trace_ = on;
  if (on) return;
  lastBus.reset();
  lastMicro.reset();
  std::fill(ramReadAt.begin(), ramReadAt.end(), 0);
  std::fill(ramWriteAt.begin(), ramWriteAt.end(), 0);
}

std::optional<int> parseStackSize(std::string_view text) {
  std::string t(text);
  long scale = 1;
  if (!t.empty() && (t.back() == 'K' || t.back() == 'k')) {
    scale = 1024;
    t.pop_back();
  }
  if (t.empty()) return std::nullopt;
  char* end = nullptr;
  const long v = std::strtol(t.c_str(), &end, 0);
  if (end == t.c_str() || *end != '\0') return std::nullopt;
  const long bytes = v * scale;
  if (bytes < MIN_STACK_SIZE || bytes > MAX_STACK_SIZE) return std::nullopt;
  return static_cast<int>(bytes);
}

void Machine::setStackSize(int bytes) {
  stack.assign(static_cast<size_t>(std::clamp(bytes, MIN_STACK_SIZE, MAX_STACK_SIZE)), 0);
  sp = stackTop();
}

void Machine::reset() {
  fault_.reset();
  pc = 0;
  irOp = 0;
  irOperand = 0;
  acc = 0;
  aLatch = 0;
  bLatch = 0;
  d1 = 0;
  d2 = 0;
  d3 = 0;
  sp = stackTop();
  flags = {};
  status = Status::Running;
  crash.reset();
  cycles = 0;
  instructions = 0;
  lastInstrPc = 0;
  lastBus.reset();
  lastMicro.reset();
  inFetch_ = true;
  section_ = -1;
  rowIndex_ = 0;
  // Heat stamps are cycle numbers, not RAM content, so they reset with the
  // cycle counter.
  std::fill(ramReadAt.begin(), ramReadAt.end(), 0);
  std::fill(ramWriteAt.begin(), ramWriteAt.end(), 0);
  clearAccessCounts();
}

// Clear the access counts alone, so a program can be profiled over one
// phase of itself rather than over its startup.
void Machine::clearAccessCounts() {
  std::fill(ramReads.begin(), ramReads.end(), 0);
  std::fill(ramWrites.begin(), ramWrites.end(), 0);
}

Machine::AluOut Machine::alu(AluOp op) const {
  using enum AluOp;
  const unsigned a = aLatch;
  const unsigned b = bLatch;
  unsigned result = 0;
  Flags f = flags;
  bool arithmetic = false;
  switch (op) {
    case Add:
    case Adc: {
      const unsigned cin = (op == Adc && flags.c) ? 1 : 0;
      const unsigned wide = a + b + cin;
      result = wide & 0xff;
      f.c = wide > 0xff;
      f.v = ((a ^ result) & (b ^ result) & 0x80) != 0;
      arithmetic = true;
      break;
    }
    case Sub:
    case Sbc: {
      const unsigned bin = (op == Sbc && flags.c) ? 1 : 0;
      const int wide = static_cast<int>(a) - static_cast<int>(b) - static_cast<int>(bin);
      result = static_cast<unsigned>(wide) & 0xff;
      f.c = wide < 0;  // C means borrow occurred
      f.v = ((a ^ b) & (a ^ result) & 0x80) != 0;
      arithmetic = true;
      break;
    }
    case And: result = a & b; break;
    case Or: result = a | b; break;
    case Xor: result = a ^ b; break;
    case PassB: result = b; break;
    // The shifts read A alone. The bit that leaves goes to C, and the
    // rotates bring the old C in at the other end.
    case Shl:
    case Rol:
      result = ((a << 1) | (op == Rol && flags.c ? 1 : 0)) & 0xff;
      f.c = (a & 0x80) != 0;
      break;
    case Shr:
    case Ror:
    case Asr:
      result = (a >> 1) | (op == Ror && flags.c ? 0x80 : 0) | (op == Asr ? (a & 0x80) : 0);
      f.c = (a & 1) != 0;
      break;
    case AluNone: break;
  }
  f.z = result == 0;
  f.n = (result & 0x80) != 0;
  // Logic operations leave C and V as they were. Shifts set C and leave V.
  const bool shift = op == Shl || op == Shr || op == Rol || op == Ror || op == Asr;
  if (!arithmetic) {
    if (!shift) f.c = flags.c;
    f.v = flags.v;
  }
  return {static_cast<uint8_t>(result), f};
}

namespace {
bool has(const Row& row, Signal s) {
  for (Signal x : row) {
    if (x == s) return true;
  }
  return false;
}
}  // namespace

// Effective address for a data RAM access, from the row's address signals.
// The adder is stateless: this is the address it computed THIS cycle.
uint16_t Machine::effectiveAddress(const Row& row) {
  using S = Signal;
  unsigned base = 0;
  if (has(row, S::ADDR_OP8)) base = irOperand & 0xff;
  else if (has(row, S::ADDR_OP16)) base = irOperand;
  else if (has(row, S::ADDR_D1)) base = d1;
  else if (has(row, S::ADDR_D2)) base = d2;
  else if (has(row, S::ADDR_D3)) base = d3;
  else if (has(row, S::ADDR_A)) base = acc;
  // The adder is sixteen bits wide and every input is unsigned. D3-12 is
  // D3 plus $FFF4, which carries out of bit 15 unless D3 was below 12.
  unsigned ea = base;
  if (has(row, S::EA_OFF_OP8)) ea += irOperand & 0xff;
  if (has(row, S::EA_OFF_OP16)) ea += irOperand;
  if (has(row, S::EA_OFF_A)) ea += acc;
  if (has(row, S::EA_CIN)) ea += 1;
  eaCarry_ = ea > 0xffff;
  // No address is illegal. The adder masks to 16 bits and RAM is 64KB, so a
  // pointer walked off the end wraps to zero.
  ea &= 0xffff;
  if (lastMicro) lastMicro->ea = static_cast<uint16_t>(ea);
  return static_cast<uint16_t>(ea);
}

// The mask is the MEMORY's, not the register's. SP cannot leave the stack,
// because both bounds crash. The one address that can run past the top is
// STK_CIN's, read in the same row that then fails on SP_INC.
uint16_t Machine::stackAddress(const Row& row) const {
  return has(row, Signal::STK_CIN) ? static_cast<uint16_t>((sp + 1u) % stack.size()) : sp;
}

bool Machine::executeRow(const Row& row) {
  const RowInfo info = analyze(row);
  return executeRow(row, info);
}

bool Machine::executeRow(const Row& row, const RowInfo& info) {
  using S = Signal;
  using T = Write::Target;
  using K = BusEvent::Kind;
  if (status != Status::Running) return false;

  if (info.conflict) {
    fail(CrashKind::SignalConflict,
         "rule " + std::to_string(info.conflict->rule) + ": " + info.conflict->message, &row);
    return false;
  }

  WriteList writes;
  const bool tracing = trace_;
  const uint32_t stamp = static_cast<uint32_t>(cycles + 1);
  bool halt = false;

  uint16_t ea = 0;
  if (info.needsRam) ea = effectiveAddress(row);
  const uint16_t sa = info.needsStack ? stackAddress(row) : 0;
  const AluOut aluOut = info.aluOp != AluOp::AluNone ? alu(info.aluOp) : AluOut{};

  std::optional<BusEvent> bus;
  auto onBus = [&](K kind, uint32_t addr, uint8_t data) {
    if (!tracing) return;
    if (!bus || busRank(kind) >= busRank(bus->kind)) bus = BusEvent{kind, addr, data};
  };
  auto ramByte = [&]() -> uint8_t {
    if (countAccesses && ramReads[ea] < 0xffffffffu) ramReads[ea]++;
    if (tracing) ramReadAt[ea] = stamp;
    onBus(K::RamRead, ea, ram[ea]);
    return ram[ea];
  };
  auto stackByte = [&]() -> uint8_t {
    onBus(K::StackRead, sa, stack[sa]);
    return stack[sa];
  };
  auto port = [&]() -> uint8_t { return static_cast<uint8_t>(irOperand >> 8); };
  auto ioRead = [&](T target) {
    const uint8_t val = io_->read(port());
    onBus(K::IoRead, port(), val);
    writes.push(target, val);
  };

  for (Signal s : row) {
    switch (s) {
      case S::FETCH: {
        // The PC bounds check runs before fetch in the sequencer. A FETCH
        // placed elsewhere by user microcode reads a NOP past the end.
        const Instr instr = pc < program.size() ? program[pc] : Instr{};
        onBus(K::Fetch, pc, instr.op);
        writes.push(T::IR, instr.operand, instr.op);
        break;
      }
      case S::PC_INC: writes.push(T::PC, static_cast<uint16_t>(pc + 1)); break;
      case S::PC_LOAD: writes.push(T::PC, irOperand); break;
      // The register itself, not the byte it points at. Both are 16 bits.
      case S::PC_FROM_D1: writes.push(T::PC, d1); break;
      case S::PC_FROM_D2: writes.push(T::PC, d2); break;
      case S::PC_LOAD_Z: if (flags.z) writes.push(T::PC, irOperand); break;
      case S::PC_LOAD_C: if (flags.c) writes.push(T::PC, irOperand); break;
      case S::PC_LOAD_N: if (flags.n) writes.push(T::PC, irOperand); break;
      case S::PC_LOAD_V: if (flags.v) writes.push(T::PC, irOperand); break;
      case S::PC_LOAD_NZ: if (!flags.z) writes.push(T::PC, irOperand); break;
      case S::PC_LOAD_NC: if (!flags.c) writes.push(T::PC, irOperand); break;
      case S::PC_LOAD_NN: if (!flags.n) writes.push(T::PC, irOperand); break;
      case S::PC_LOAD_NV: if (!flags.v) writes.push(T::PC, irOperand); break;
      case S::HALT: halt = true; break;

      case S::ACC_TO_A: writes.push(T::A, acc); break;
      case S::IMM_TO_B: writes.push(T::B, irOperand & 0xff); break;
      case S::RAM_TO_B: writes.push(T::B, ramByte()); break;
      case S::STK_TO_B: writes.push(T::B, stackByte()); break;

      case S::ACC_LOAD_ALU: writes.push(T::ACC, aluOut.result); break;
      case S::FLAGS_LOAD: writes.pushFlags(aluOut.flags); break;

      case S::RAM_WRITE_ACC: writes.push(T::RAM, acc, ea); break;
      case S::RAM_WRITE_D1H: writes.push(T::RAM, d1 >> 8, ea); break;
      case S::RAM_WRITE_D1L: writes.push(T::RAM, d1 & 0xff, ea); break;
      case S::RAM_WRITE_D2H: writes.push(T::RAM, d2 >> 8, ea); break;
      case S::RAM_WRITE_D2L: writes.push(T::RAM, d2 & 0xff, ea); break;
      case S::RAM_TO_D1H: writes.push(T::D1H, ramByte()); break;
      case S::RAM_TO_D1L: writes.push(T::D1L, ramByte()); break;
      case S::RAM_TO_D2H: writes.push(T::D2H, ramByte()); break;
      case S::RAM_TO_D2L: writes.push(T::D2L, ramByte()); break;
      case S::RAM_TO_D3H: writes.push(T::D3H, ramByte()); break;
      case S::RAM_TO_D3L: writes.push(T::D3L, ramByte()); break;
      case S::RAM_WRITE_D3H: writes.push(T::RAM, d3 >> 8, ea); break;
      case S::RAM_WRITE_D3L: writes.push(T::RAM, d3 & 0xff, ea); break;

      case S::D1_LOAD_OP16: writes.push(T::D1, irOperand); break;
      case S::D2_LOAD_OP16: writes.push(T::D2, irOperand); break;
      // The adder's output itself, the address a RAM access would use.
      case S::D1_LOAD_EA: writes.push(T::D1, ea); break;
      case S::D2_LOAD_EA: writes.push(T::D2, ea); break;
      case S::D3_LOAD_EA: writes.push(T::D3, ea); break;
      // Z from the sum, C from the adder's carry out of bit 15. A negative
      // offset is a large one, so after LD D2 <- D3-FLOOR, C clear means D3
      // was below FLOOR: the carry is the inverse of a borrow.
      case S::EA_FLAGS: {
        Flags f = flags;
        f.z = ea == 0;
        f.c = eaCarry_;
        writes.pushFlags(f);
        break;
      }
      case S::D1_TSTZ: {
        Flags f = flags;
        f.z = d1 == 0;
        writes.pushFlags(f);
        break;
      }
      case S::D2_TSTZ: {
        Flags f = flags;
        f.z = d2 == 0;
        writes.pushFlags(f);
        break;
      }
      case S::D3_TSTZ: {
        Flags f = flags;
        f.z = d3 == 0;
        writes.pushFlags(f);
        break;
      }

      case S::D1_INC: writes.push(T::D1, static_cast<uint16_t>(d1 + 1)); break;
      case S::D2_INC: writes.push(T::D2, static_cast<uint16_t>(d2 + 1)); break;
      case S::ACC_INC: writes.push(T::ACC, (acc + 1) & 0xff); break;
      case S::SP_INC:
        if (sp == stackTop()) {
          fail(CrashKind::StackUnderflow, "SP_INC above the stack top", &row);
          return false;
        }
        writes.push(T::SP, static_cast<uint16_t>(sp + 1));
        break;
      case S::SP_DEC:
        if (sp == 0) {
          fail(CrashKind::StackOverflow, "SP_DEC below the stack bottom", &row);
          return false;
        }
        writes.push(T::SP, static_cast<uint16_t>(sp - 1));
        break;

      case S::STK_WRITE_ACC: writes.push(T::STACK, acc, sa); break;
      case S::STK_WRITE_PCH: writes.push(T::STACK, pc >> 8, sa); break;
      case S::STK_WRITE_PCL: writes.push(T::STACK, pc & 0xff, sa); break;
      case S::STK_WRITE_D1H: writes.push(T::STACK, d1 >> 8, sa); break;
      case S::STK_WRITE_D1L: writes.push(T::STACK, d1 & 0xff, sa); break;
      case S::STK_WRITE_D2H: writes.push(T::STACK, d2 >> 8, sa); break;
      case S::STK_WRITE_D2L: writes.push(T::STACK, d2 & 0xff, sa); break;
      case S::STK_TO_PCL: writes.push(T::PCL, stackByte()); break;
      case S::STK_TO_PCH: writes.push(T::PCH, stackByte()); break;
      case S::STK_TO_D1H: writes.push(T::D1H, stackByte()); break;
      case S::STK_TO_D1L: writes.push(T::D1L, stackByte()); break;
      case S::STK_TO_D2H: writes.push(T::D2H, stackByte()); break;
      case S::STK_TO_D2L: writes.push(T::D2L, stackByte()); break;

      case S::IO_WRITE_IMM: {
        const uint8_t val = static_cast<uint8_t>(irOperand & 0xff);
        onBus(K::IoWrite, port(), val);
        io_->write(port(), val);
        break;
      }
      case S::IO_WRITE_ACC:
        onBus(K::IoWrite, port(), acc);
        io_->write(port(), acc);
        break;
      case S::IO_READ: ioRead(T::ACC); break;
      case S::IO_READ_D1H: ioRead(T::D1H); break;
      case S::IO_READ_D1L: ioRead(T::D1L); break;
      case S::IO_READ_D2H: ioRead(T::D2H); break;
      case S::IO_READ_D2L: ioRead(T::D2L); break;

      // Pure address-stage and ALU-select signals carry no action of their own.
      case S::ADDR_OP8: case S::ADDR_OP16: case S::ADDR_D1: case S::ADDR_D2: case S::ADDR_D3:
      case S::ADDR_A: case S::EA_OFF_OP8: case S::EA_OFF_OP16: case S::EA_OFF_A: case S::EA_CIN:
      case S::STK_CIN:
      case S::ALU_ADD: case S::ALU_SUB: case S::ALU_ADC: case S::ALU_SBC:
      case S::ALU_AND: case S::ALU_OR: case S::ALU_XOR: case S::ALU_PASS_B:
      case S::ALU_SHL: case S::ALU_SHR: case S::ALU_ROL: case S::ALU_ROR: case S::ALU_ASR:
        break;
    }
  }

  for (size_t i = 0; i < writes.count; i++) {
    const Write& w = writes.items[i];
    const uint8_t byte = static_cast<uint8_t>(w.value);
    switch (w.target) {
      case T::IR:
        irOp = static_cast<uint8_t>(w.addr);
        irOperand = w.value;
        break;
      case T::PC: pc = w.value; break;
      case T::PCH: pc = static_cast<uint16_t>((byte << 8) | (pc & 0xff)); break;
      case T::PCL: pc = static_cast<uint16_t>((pc & 0xff00) | byte); break;
      case T::ACC: acc = byte; break;
      case T::A: aLatch = byte; break;
      case T::B: bLatch = byte; break;
      case T::D1: d1 = w.value; break;
      case T::D1H: d1 = static_cast<uint16_t>((byte << 8) | (d1 & 0xff)); break;
      case T::D1L: d1 = static_cast<uint16_t>((d1 & 0xff00) | byte); break;
      case T::D2: d2 = w.value; break;
      case T::D2H: d2 = static_cast<uint16_t>((byte << 8) | (d2 & 0xff)); break;
      case T::D2L: d2 = static_cast<uint16_t>((d2 & 0xff00) | byte); break;
      case T::D3: d3 = w.value; break;
      case T::D3H: d3 = static_cast<uint16_t>((byte << 8) | (d3 & 0xff)); break;
      case T::D3L: d3 = static_cast<uint16_t>((d3 & 0xff00) | byte); break;
      case T::SP: sp = w.value; break;
      case T::FLAGS: flags = w.flags; break;
      case T::RAM:
        ram[w.addr] = byte;
        if (countAccesses && ramWrites[w.addr] < 0xffffffffu) ramWrites[w.addr]++;
        if (tracing) ramWriteAt[w.addr] = stamp;
        onBus(K::RamWrite, w.addr, byte);
        break;
      case T::STACK:
        stack[w.addr] = byte;
        onBus(K::StackWrite, w.addr, byte);
        break;
    }
  }

  if (bus) lastBus = bus;
  cycles++;
  if (halt) status = Status::Halted;
  return status == Status::Running;
}

bool Machine::microStep() {
  if (status != Status::Running) return false;

  if (section_ < 0) {
    // About to start a fetch: the PC bounds check runs here.
    if (pc == program.size()) {
      status = Status::Halted;
      return false;
    }
    if (pc > program.size()) {
      fail(CrashKind::IllegalProgramAddress, "PC " + hex(pc) + " is outside the program");
      return false;
    }
    if (program[pc].op == UNLOADED_OP) {
      fail(CrashKind::IllegalProgramAddress, "PC " + hex(pc) + " is a slot nothing was loaded into");
      return false;
    }
    lastInstrPc = pc;
    if (fetchSection_ < 0) {
      fail(CrashKind::NoMicrocode, "no fetch microprogram");
      return false;
    }
    inFetch_ = true;
    section_ = fetchSection_;
    rowIndex_ = 0;
  }

  const Rows& rows = microcode_.sections()[static_cast<size_t>(section_)].rows;
  const auto& infos = rowInfo_[static_cast<size_t>(section_)];
  if (rowIndex_ < rows.size()) {
    const Row& row = rows[rowIndex_];
    if (trace_) {
      const OpDef* def = opByCode(irOp);
      lastMicro = MicroTrace{inFetch_ ? "fetch" : (def ? std::string(def->name) : "?"),
                             static_cast<int>(rowIndex_), row, std::nullopt};
    }
    const bool ok = executeRow(row, infos[rowIndex_]);
    rowIndex_++;
    if (!ok) return false;
  }

  if (rowIndex_ >= rows.size()) {
    if (inFetch_) {
      // Dispatch is hardwired and free.
      const int next = dispatch_[irOp];
      if (next < 0) {
        fail(CrashKind::NoMicrocode, "no microprogram for opcode " + hex(irOp));
        return false;
      }
      inFetch_ = false;
      section_ = next;
      rowIndex_ = 0;
      // An empty microprogram ends the instruction at once.
      if (microcode_.sections()[static_cast<size_t>(next)].rows.empty()) finishInstruction();
    } else {
      finishInstruction();
    }
  }
  return status == Status::Running;
}

void Machine::finishInstruction() {
  inFetch_ = true;
  section_ = -1;
  rowIndex_ = 0;
  instructions++;
  // PERF DECISION: a device reports a fault when it refuses a command, and
  // the machine looks at one flag here. Asking every device after every
  // port write cost 2.4 ns a write, 10% of a loop of nothing but writes.
  if (fault_) {
    DeviceFault f = std::move(*fault_);
    fault_.reset();
    fail(f.kind, std::move(f.message));
  }
}

bool Machine::instructionStep() {
  if (status != Status::Running) return false;
  const uint64_t start = instructions;
  while (status == Status::Running && instructions == start) {
    if (!microStep()) break;
  }
  return status == Status::Running;
}

void Machine::run(uint64_t maxInstructions) {
  // A machine copied or moved since it was built reports to its devices
  // here, once a call, rather than once an instruction.
  io_->attachFaultSink(this);
  for (uint64_t i = 0; i < maxInstructions && status == Status::Running; i++) {
    instructionStep();
  }
}

}  // namespace sc8
