#include "ide/datapath.h"

#include <string>

#include "core/machine.h"

namespace sc8::dp {

namespace {

// The RAM box reads its size from the machine, never from a typed label.
// The browser's box read "RAM 2K" for as long as the machine had 64K.
const std::string RAM_LABEL = "RAM " + std::to_string(RAM_SIZE / 1024) + "K";
// The stack card reads its size and SP its start the same way. The
// browser's cards still said 256 bytes and $FF after the stack grew.
const std::string STACK_TITLE = "STACK · stack memory, " + std::to_string(STACK_SIZE) + " bytes";
char hex4(int v, int shift) { return "0123456789ABCDEF"[(v >> shift) & 15]; }
const std::string SP_WHAT = std::string("A 16 bit register that addresses the stack memory and nothing else. It starts at $") +
                            hex4(STACK_TOP, 12) + hex4(STACK_TOP, 8) + hex4(STACK_TOP, 4) + hex4(STACK_TOP, 0) +
                            " and points at the next free slot, growing downward.";

const Box BOXES[] = {
    {"PROG", 8, 8, 64, 34, "PROG", false},
    {"IR", 104, 8, 96, 34, "IR", true},
    {"PC", 240, 8, 64, 34, "PC", true},
    {"STEP", 336, 8, 66, 34, "STEP +-1", false},
    {"RAM", 8, 70, 64, 34, RAM_LABEL, false},
    {"EA", 104, 70, 64, 30, "EA", true},
    {"D1", 240, 70, 64, 30, "D1", true},
    {"D2", 336, 70, 66, 30, "D2", true},
    // D3 sits past D2, and its three wires run under the D row.
    {"D3", 424, 70, 64, 30, "D3", true},
    {"STACK", 8, 142, 64, 34, "STACK", false},
    {"SP", 104, 144, 48, 30, "SP", true},
    {"A", 184, 144, 48, 30, "A", true},
    {"B", 256, 144, 48, 30, "B", true},
    {"IO", 336, 144, 66, 30, "IO", false},
    {"ACC", 8, 208, 64, 34, "ACC", true},
    {"ALU", 160, 202, 124, 44, "ALU", false},
    {"FLAGS", 336, 208, 66, 34, "FLAGS", true},
};

// Routed the way a real schematic would: a wire only touches a box it
// connects to. Crossings are fine.
const Wire WIRES[] = {
    {"pc-prog", {{240, 18}, {72, 18}}},
    {"prog-ir", {{72, 32}, {104, 32}}},
    {"ir-pc", {{200, 25}, {240, 25}}},
    {"step-pc", {{336, 25}, {304, 25}}},
    {"d1-pc", {{250, 70}, {250, 42}}},
    {"d2-pc", {{350, 70}, {350, 50}, {290, 50}, {290, 42}}},
    {"ir-ea", {{136, 42}, {136, 70}}},
    {"ea-ram", {{104, 86}, {72, 86}}},
    {"d1-ea", {{240, 84}, {168, 84}}},
    {"d2-ea", {{336, 88}, {168, 88}}},
    {"ea-d1", {{168, 96}, {240, 96}}},
    {"ea-d2", {{160, 100}, {160, 104}, {320, 104}, {320, 96}, {336, 96}}},
    {"acc-ea", {{44, 208}, {44, 196}, {88, 196}, {88, 120}, {132, 120}, {132, 100}}},
    {"ram-d1", {{72, 76}, {240, 76}}},
    {"ram-d2", {{72, 72}, {336, 72}}},
    {"ir-d", {{176, 42}, {176, 56}, {260, 56}, {260, 70}}},
    {"ir-b", {{160, 42}, {160, 50}, {220, 50}, {220, 115}, {276, 115}, {276, 144}}},
    {"ram-b", {{40, 104}, {40, 112}, {244, 112}, {244, 158}, {256, 158}}},
    {"acc-ram", {{30, 208}, {30, 104}}},
    {"sp-stack", {{104, 158}, {72, 158}}},
    {"stack-b", {{40, 176}, {40, 184}, {264, 184}, {264, 174}}},
    {"stack-pc", {{52, 142}, {52, 115}, {88, 115}, {88, 48}, {268, 48}, {268, 42}}},
    {"stack-d", {{60, 142}, {60, 108}, {252, 108}, {252, 100}}},
    {"acc-stack", {{20, 208}, {20, 176}}},
    {"acc-a", {{72, 218}, {96, 218}, {96, 188}, {196, 188}, {196, 174}}},
    {"a-alu", {{208, 174}, {208, 202}}},
    {"b-alu", {{272, 174}, {272, 202}}},
    {"alu-acc", {{160, 224}, {72, 224}}},
    {"alu-flags", {{284, 224}, {336, 224}}},
    {"io-acc", {{350, 174}, {350, 192}, {90, 192}, {90, 212}, {72, 212}}},
    {"ir-io", {{192, 42}, {192, 62}, {320, 62}, {320, 122}, {352, 122}, {352, 144}}},
    {"step-d1", {{356, 42}, {356, 56}, {284, 56}, {284, 70}}},
    {"step-d2", {{372, 42}, {372, 70}}},
    {"step-sp", {{344, 42}, {344, 66}, {226, 66}, {226, 118}, {148, 118}, {148, 144}}},
    {"d3-ea", {{432, 100}, {432, 126}, {146, 126}, {146, 100}}},
    {"ea-d3", {{154, 100}, {154, 131}, {448, 131}, {448, 100}}},
    {"ram-d3", {{66, 104}, {66, 136}, {472, 136}, {472, 100}}},
};

constexpr std::string_view COMPONENTS[] = {
    "PROG", "IR", "PC", "STEP", "RAM", "EA", "D1", "D2", "D3", "STACK", "SP", "A", "B", "IO", "ALU", "FLAGS", "ACC",
};

bool starts(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

// The regular families the browser matched with regular expressions.
// RAM_TO_D1H, RAM_WRITE_D2L and the like.
bool ramD(std::string_view n, char which) {
  return (starts(n, "RAM_TO_D") || starts(n, "RAM_WRITE_D")) && n[n.size() - 2] == which;
}
bool stkD(std::string_view n, char which) {
  return (starts(n, "STK_TO_D") || starts(n, "STK_WRITE_D")) && n[n.size() - 2] == which;
}
bool stkPc(std::string_view n) { return starts(n, "STK_TO_PC") || starts(n, "STK_WRITE_PC"); }

const Card CARDS[] = {
    {"PROG", "PROG · program memory", "memory",
     "Holds the assembled program: 64K slots of exactly 3 bytes each, an opcode plus a 16 bit operand. It is "
     "written once, when a program loads, and read-only ever after."},
    {"IR", "IR · instruction register", "register · remembers across cycles",
     "Holds the current instruction: an 8 bit opcode and a 16 bit operand. FETCH loads it, and from then on it "
     "is immutable for the rest of the instruction. Every microcode row can rely on the operand staying put."},
    {"PC", "PC · program counter", "register · remembers across cycles",
     "A 16 bit register that counts instructions, not bytes. It always names the NEXT slot to fetch."},
    {"STEP", "STEP · the shared +-1 unit", "combinational · forgets every cycle",
     "One adder that adds or subtracts exactly one. PC, SP, D1, D2, and ACC all share it: there is only one, "
     "so only one of them can step per cycle. That is conflict rule 7."},
    {"RAM", "RAM · data memory, 64K", "memory",
     "65536 bytes of data RAM. The first 256 bytes are the zero page, reachable with a one byte address. The D "
     "registers reach all of it. Text mode can map a 1344-byte character screen here. $FAC0, the top of RAM, "
     "is the convention."},
    {"EA", "EA · effective address adder", "combinational · forgets every cycle",
     "EA stands for effective address: the address that actually takes effect on RAM this cycle, after all "
     "the addressing math is done. The EA adder computes it fresh, every cycle, from three ingredients: a "
     "base, an optional offset, and an optional carry-in. D1_LOAD_EA, D2_LOAD_EA and D3_LOAD_EA keep the sum "
     "in a D register instead, which is how LD D1 <- D1+8 steps a pointer by any amount. EA_FLAGS sets Z from "
     "the sum and C from the adder's carry out of bit 15."},
    {"D1", "D1 · pointer register", "register · remembers across cycles",
     "A 16 bit pointer register. It can load whole from the operand (D1_LOAD_OP16), or one byte half at a "
     "time from RAM or the stack, high half then low half, matching the machine's big-endian words."},
    {"D2", "D2 · pointer register", "register · remembers across cycles",
     "The twin of D1: a second 16 bit pointer with exactly the same connections, so two pointers can walk "
     "two structures at once, the linked list demo's whole trick."},
    {"D3", "D3 · data stack pointer", "register · remembers across cycles",
     "A third 16 bit pointer, for a stack in RAM. It reaches memory only as [D3+n] and moves only through the "
     "EA adder, LD D3 <- D3-12. The C runtime keeps its frames there. The hardware gives it no other meaning."},
    {"STACK", STACK_TITLE, "memory",
     "A separate memory that only SP can address. Programs reach it exclusively through push, pop, JSR, and "
     "RET; no pointer arithmetic, no EA adder, no way in from data RAM."},
    {"SP", "SP · stack pointer", "register · remembers across cycles", SP_WHAT},
    {"A", "A · ALU input latch", "register · remembers across cycles",
     "The ALU's left-hand input latch. ACC_TO_A copies the accumulator into it, and from then on it holds "
     "that value until something loads it again, across as many rows as the instruction needs."},
    {"B", "B · ALU input latch", "register · remembers across cycles",
     "The ALU's right-hand input latch, with three loaders: an immediate from the operand low byte "
     "(IMM_TO_B), a RAM byte at the effective address (RAM_TO_B), or a stack byte (STK_TO_B)."},
    {"IO", "IO · port gateway", "combinational · forgets every cycle",
     "The gateway to peripherals: 256 ports, addressed by a byte, one byte at a time. The GPU takes $00 to "
     "$1F, the controller $20 to $22, the audio chip $30 to $3F and the coprocessor $40 to $4F. A port no "
     "device claims reads as zero."},
    {"ALU", "ALU · arithmetic logic unit", "combinational · forgets every cycle",
     "The one place arithmetic happens. It combines the A and B latches under one selected operation: add, "
     "subtract, with or without carry, and, or, xor, or pass B through untouched (the load path). The five "
     "shifts read A alone and move it one bit, with the bit that leaves going to C."},
    {"FLAGS", "FLAGS · condition codes", "register · remembers across cycles",
     "Four bits that remember what the last captured result looked like: Z (zero), N (bit 7), C (carry or "
     "borrow), V (signed overflow)."},
    {"ACC", "ACC · the accumulator", "register · remembers across cycles",
     "The one architectural byte register, the A that assembly names. Every ALU result that matters lands "
     "here, and almost every byte that leaves the CPU departs from here."},
};

}  // namespace

std::span<const Box> boxes() { return BOXES; }
std::span<const Wire> wires() { return WIRES; }
std::span<const std::string_view> components() { return COMPONENTS; }

// DIFFERENCE from datapath.ts. The map is a switch over the enum rather
// than a record keyed by name. A signal the switch forgot then falls to
// the family rules below instead of lighting nothing.
std::vector<std::string_view> componentsFor(Signal s) {
  const std::string_view n = signalName(s);
  switch (s) {
    case Signal::FETCH: return {"PROG", "IR", "PC"};
    case Signal::PC_INC: return {"PC", "STEP"};
    case Signal::PC_LOAD: return {"PC", "IR"};
    case Signal::PC_LOAD_Z:
    case Signal::PC_LOAD_C:
    case Signal::PC_LOAD_N:
    case Signal::PC_LOAD_V:
    case Signal::PC_LOAD_NZ:
    case Signal::PC_LOAD_NC:
    case Signal::PC_LOAD_NN:
    case Signal::PC_LOAD_NV: return {"PC", "IR", "FLAGS"};
    // No IR: the target is in the register, not in the operand.
    case Signal::PC_FROM_D1: return {"PC", "D1"};
    case Signal::PC_FROM_D2: return {"PC", "D2"};
    case Signal::HALT: return {};
    case Signal::ACC_TO_A: return {"ACC", "A"};
    case Signal::IMM_TO_B: return {"IR", "B"};
    case Signal::RAM_TO_B: return {"RAM", "B"};
    case Signal::STK_TO_B: return {"STACK", "SP", "B"};
    case Signal::ACC_LOAD_ALU: return {"ALU", "ACC"};
    case Signal::FLAGS_LOAD: return {"ALU", "FLAGS"};
    case Signal::RAM_WRITE_ACC: return {"RAM", "ACC"};
    case Signal::ADDR_OP8:
    case Signal::ADDR_OP16: return {"EA", "IR"};
    case Signal::ADDR_D1: return {"EA", "D1"};
    case Signal::ADDR_D2: return {"EA", "D2"};
    case Signal::ADDR_D3: return {"EA", "D3"};
    case Signal::ADDR_A: return {"EA", "ACC"};
    case Signal::EA_OFF_OP8:
    case Signal::EA_OFF_OP16: return {"EA", "IR"};
    case Signal::D1_LOAD_EA: return {"EA", "D1"};
    case Signal::D2_LOAD_EA: return {"EA", "D2"};
    case Signal::D3_LOAD_EA: return {"EA", "D3"};
    case Signal::EA_FLAGS: return {"EA", "FLAGS"};
    // A shift reads the A latch alone.
    case Signal::ALU_SHL:
    case Signal::ALU_SHR:
    case Signal::ALU_ROL:
    case Signal::ALU_ROR:
    case Signal::ALU_ASR: return {"ALU", "A"};
    case Signal::EA_OFF_A: return {"EA", "ACC"};
    case Signal::EA_CIN: return {"EA"};
    case Signal::D1_LOAD_OP16: return {"D1", "IR"};
    case Signal::D2_LOAD_OP16: return {"D2", "IR"};
    case Signal::D1_TSTZ: return {"D1", "FLAGS"};
    case Signal::D2_TSTZ: return {"D2", "FLAGS"};
    case Signal::D3_TSTZ: return {"D3", "FLAGS"};
    case Signal::D1_INC: return {"D1", "STEP"};
    case Signal::D2_INC: return {"D2", "STEP"};
    case Signal::ACC_INC: return {"ACC", "STEP"};
    case Signal::SP_INC:
    case Signal::SP_DEC: return {"SP", "STEP"};
    case Signal::STK_CIN: return {"STACK", "SP"};
    case Signal::IO_WRITE_IMM: return {"IO", "IR"};
    case Signal::IO_WRITE_ACC:
    case Signal::IO_READ: return {"IO", "ACC"};
    case Signal::IO_READ_D1H:
    case Signal::IO_READ_D1L: return {"IO", "D1"};
    case Signal::IO_READ_D2H:
    case Signal::IO_READ_D2L: return {"IO", "D2"};
    default: break;
  }
  // The regular families. RAM and stack transfers to and from the D
  // registers, ALU selects, stack writes of PC and ACC.
  if (ramD(n, '1')) return {"RAM", "D1"};
  if (ramD(n, '2')) return {"RAM", "D2"};
  if (ramD(n, '3')) return {"RAM", "D3"};
  if (stkD(n, '1')) return {"STACK", "SP", "D1"};
  if (stkD(n, '2')) return {"STACK", "SP", "D2"};
  if (stkPc(n)) return {"STACK", "SP", "PC"};
  if (n == "STK_WRITE_ACC") return {"STACK", "SP", "ACC"};
  if (starts(n, "ALU_")) return {"ALU", "A", "B"};
  return {};
}

std::vector<std::string_view> wiresFor(Signal s) {
  const std::string_view n = signalName(s);
  std::vector<std::string_view> out;
  switch (s) {
    case Signal::FETCH: out = {"pc-prog", "prog-ir"}; break;
    case Signal::PC_INC: out = {"step-pc"}; break;
    case Signal::PC_LOAD:
    case Signal::PC_LOAD_Z:
    case Signal::PC_LOAD_C:
    case Signal::PC_LOAD_N:
    case Signal::PC_LOAD_V:
    case Signal::PC_LOAD_NZ:
    case Signal::PC_LOAD_NC:
    case Signal::PC_LOAD_NN:
    case Signal::PC_LOAD_NV: out = {"ir-pc"}; break;
    case Signal::PC_FROM_D1: out = {"d1-pc"}; break;
    case Signal::PC_FROM_D2: out = {"d2-pc"}; break;
    case Signal::ACC_TO_A: out = {"acc-a"}; break;
    case Signal::IMM_TO_B: out = {"ir-b"}; break;
    case Signal::RAM_TO_B: out = {"ram-b", "ea-ram"}; break;
    case Signal::STK_TO_B: out = {"stack-b", "sp-stack"}; break;
    case Signal::ACC_LOAD_ALU: out = {"alu-acc"}; break;
    case Signal::FLAGS_LOAD: out = {"alu-flags"}; break;
    // The address the adder computes goes to RAM when a RAM signal uses
    // it, and to a D register when an address load does. The address
    // signals light only the adder's inputs.
    case Signal::RAM_WRITE_ACC: out = {"acc-ram", "ea-ram"}; break;
    case Signal::ADDR_OP8:
    case Signal::ADDR_OP16:
    case Signal::EA_OFF_OP8:
    case Signal::EA_OFF_OP16: out = {"ir-ea"}; break;
    case Signal::ADDR_D1: out = {"d1-ea"}; break;
    case Signal::ADDR_D2: out = {"d2-ea"}; break;
    case Signal::ADDR_D3: out = {"d3-ea"}; break;
    case Signal::ADDR_A:
    case Signal::EA_OFF_A: out = {"acc-ea"}; break;
    case Signal::EA_CIN: break;
    case Signal::D1_LOAD_EA: out = {"ea-d1"}; break;
    case Signal::D2_LOAD_EA: out = {"ea-d2"}; break;
    case Signal::D3_LOAD_EA: out = {"ea-d3"}; break;
    case Signal::D1_LOAD_OP16:
    case Signal::D2_LOAD_OP16: out = {"ir-d"}; break;
    case Signal::D1_INC: out = {"step-d1"}; break;
    case Signal::D2_INC: out = {"step-d2"}; break;
    case Signal::SP_INC:
    case Signal::SP_DEC: out = {"step-sp"}; break;
    case Signal::STK_CIN: out = {"sp-stack"}; break;
    case Signal::STK_WRITE_ACC: out = {"acc-stack", "sp-stack"}; break;
    case Signal::IO_WRITE_IMM: out = {"ir-io"}; break;
    case Signal::IO_WRITE_ACC:
    case Signal::IO_READ: out = {"io-acc"}; break;
    default: break;
  }
  const bool shift = s == Signal::ALU_SHL || s == Signal::ALU_SHR || s == Signal::ALU_ROL ||
                     s == Signal::ALU_ROR || s == Signal::ALU_ASR;
  if (starts(n, "ALU_")) {
    out.push_back("a-alu");
    if (!shift) out.push_back("b-alu");
  }
  if (ramD(n, '1')) out.push_back("ram-d1");
  if (ramD(n, '2')) out.push_back("ram-d2");
  if (ramD(n, '3')) out.push_back("ram-d3");
  if (ramD(n, '1') || ramD(n, '2') || ramD(n, '3')) out.push_back("ea-ram");
  if (stkPc(n)) {
    out.push_back("stack-pc");
    out.push_back("sp-stack");
  }
  if (stkD(n, '1') || stkD(n, '2')) {
    out.push_back("stack-d");
    out.push_back("sp-stack");
  }
  return out;
}

const Card* cardFor(std::string_view id) {
  for (const Card& c : CARDS) {
    if (c.id == id) return &c;
  }
  return nullptr;
}

}  // namespace sc8::dp
