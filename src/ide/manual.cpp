#include "ide/manual.h"

#include <algorithm>
#include <map>
#include <string_view>

#include "core/isa.h"
#include "core/machine.h"
#include "devices/acp.h"
#include "devices/acp_ports.h"
#include "devices/apu_ports.h"
#include "devices/gpu.h"
#include "devices/gpu_ports.h"
#include "devices/input_ports.h"

namespace sc8::manual {

namespace {

const std::string UNAFFECTED = "not affected";
const FlagFacts NO_FLAGS{"none", UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED};

FlagFacts arithFlags(const std::string& c) {
  return {"N, V, Z, C", "set when the result is zero", "set to bit 7 of the result", c,
          "set on signed overflow: the result's sign contradicts the operands"};
}

const FlagFacts LOGIC_FLAGS{"N, Z", "set when the result is zero", "set to bit 7 of the result", UNAFFECTED,
                            UNAFFECTED};

FlagFacts readsFlag(char f) {
  FlagFacts out = NO_FLAGS;
  out.list = std::string("none (reads ") + f + ")";
  const std::string text = "read to decide the jump. never changed";
  switch (f) {
    case 'Z': out.z = text; break;
    case 'C': out.c = text; break;
    case 'N': out.n = text; break;
    default: out.v = text; break;
  }
  return out;
}

struct Meta {
  std::string title, usage, params;
  FlagFacts flags;
  std::string registers, description;
};

// The stack's size, written once by the machine. The text then follows
// the machine when the stack changes.
std::string stackBytes() { return std::to_string(STACK_SIZE) + " bytes"; }

// One entry per mnemonic, the uniform page fields of the browser's META.
const std::map<std::string, Meta>& meta() {
  static const std::map<std::string, Meta> M = {
      {"NOP", {"CONTROL", "NOP", "none", NO_FLAGS, "none",
               "Does nothing for one instruction. Every program slot holds three bytes, so NOP costs the same "
               "space as any instruction."}},
      {"HLT", {"CONTROL", "HLT", "none", NO_FLAGS, "none",
               "Stops the CPU. This is the graceful end of a program, and a program that has finished owes you "
               "one. A loop that waits for a frame it will do nothing with still runs the CPU flat out, and the "
               "picture it is holding up is one the GPU keeps composing on its own. So end on HLT: when the sum "
               "is printed, when the drawing is done, when the last life is gone. Reset or Power on brings the "
               "CPU back. Running past the last instruction halts the same way."}},
      {"JMP", {"BRANCH", "JMP target", "a code label, an instruction number, or D1 or D2", NO_FLAGS,
               "PC, D1, D2",
               "Loads the target into PC. The next fetch continues there. A jump to the instruction count is a "
               "graceful halt. A jump beyond it crashes with an illegal program address. JMP D1 and JMP D2 take "
               "the target from the register instead of the operand, in one cycle. That is the jump table: put "
               "slot numbers in RAM with dw &label, read one with LD D2 <- [D1+A], and JMP D2 goes there. Write "
               "the register bare. JMP [D1] is refused, because brackets are contents-of everywhere on this "
               "machine and [D1] is the single byte at that address, not a 16 bit target."}},
      {"JZ", {"BRANCH", "JZ target", "a code label, or an instruction number", readsFlag('Z'), "PC",
              "Jumps when the Z flag is set. Z is set by ALU results, byte loads into A, word loads into D1 or "
              "D2, and POPB or POPW."}},
      {"JC", {"BRANCH", "JC target", "a code label, or an instruction number", readsFlag('C'), "PC",
              "Jumps when the C flag is set. ADD sets C on overflow past 255. SUB sets C when a borrow occurred."}},
      {"JN", {"BRANCH", "JN target", "a code label, or an instruction number", readsFlag('N'), "PC",
              "Jumps when the N flag is set. N mirrors bit 7 of the last ALU or load result."}},
      {"JV", {"BRANCH", "JV target", "a code label, or an instruction number", readsFlag('V'), "PC",
              "Jumps when the V flag is set. V means signed overflow: the result's sign is wrong for two's "
              "complement math."}},
      {"JNZ", {"BRANCH", "JNZ target", "a code label, or an instruction number", readsFlag('Z'), "PC",
               "Jumps when the Z flag is clear, the inverse of JZ. A loop that counts down ends in SUB A <- 1 "
               "and JNZ loop. After a word load into D1 or D2, Z tells whether the word is zero, so LD D1 <- "
               "[addr16] and JNZ follow a pointer that is not NULL."}},
      {"JNC", {"BRANCH", "JNC target", "a code label, or an instruction number", readsFlag('C'), "PC",
               "Jumps when the C flag is clear, the inverse of JC. After SUB or a compare it jumps when no "
               "borrow occurred, so when the left side was at least the right side, unsigned."}},
      {"JP", {"BRANCH", "JP target", "a code label, or an instruction number", readsFlag('N'), "PC",
              "Jumps when the N flag is clear, the inverse of JN: jump if plus. Zero counts as plus, because "
              "bit 7 of zero is clear."}},
      {"JNV", {"BRANCH", "JNV target", "a code label, or an instruction number", readsFlag('V'), "PC",
               "Jumps when the V flag is clear, the inverse of JV. After a signed SUB, N alone is the sign of "
               "the difference when no overflow occurred."}},
      {"JSR", {"BRANCH", "JSR target", "a code label, an instruction number, or D1 or D2", NO_FLAGS,
               "PC, SP, stack, D1, D2",
               "Pushes the address of the next instruction on the stack, high byte first, then jumps. RET comes "
               "back to that address. The stack is its own " + stackBytes() +
                   ". SP points at the next free slot and grows down. JSR D1 and JSR D2 call the address in the "
                   "register, which is a C function pointer. They push exactly what JSR pushes and cost the same, "
                   "so RET comes back the same way. The register is written bare here too: JSR [D1] is refused."}},
      {"RET", {"BRANCH", "RET", "none", NO_FLAGS, "PC, SP, stack",
               "Pops two bytes off the stack into PC. Pair every RET with a JSR. Popping an empty stack crashes "
               "with a stack underflow."}},
      {"LD", {"LOAD & STORE", "LD src -> dst", "destination and source joined by an arrow, either direction",
              FlagFacts{"N, Z", "loads: set when the loaded value is zero. stores: " + UNAFFECTED,
                        "byte loads into A: set to bit 7. word loads and stores: " + UNAFFECTED, UNAFFECTED,
                        UNAFFECTED},
              "A, D1, D2, or RAM, depending on the shape",
              "The one data mover. LD A <- x and LD x -> A are the same instruction. Square brackets mean "
              "contents of. A trailing + steps the pointer by the transfer width after the access. The register "
              "outside the brackets decides the width: A moves one byte, D1 and D2 move two, big-endian. [A] "
              "addresses the zero page and never crashes, and [A]+ steps A after a word load. D registers reach "
              "all of data RAM. No data address is illegal. An effective address wraps at 16 bits, so a pointer "
              "walked off the end wraps to zero. Without brackets a sum is the address itself: LD D1 <- D1+8 "
              "adds 8 to D1, LD D1 <- D1-8 subtracts, LD D2 <- D1+A adds A, and LD D2 <- D1 copies. These run "
              "through the address adder, set Z like every load into a D register, and leave C alone."}},
      {"ADD", {"ARITHMETIC", "ADD A <- value", "A <- an 8 bit immediate, [zero page address], or [D1+n] or [D2+n]",
               arithFlags("set on carry out of bit 7"), "A",
               "Adds the operand to A. INC A is the same instruction as ADD A <- 1."}},
      {"SUB", {"ARITHMETIC", "SUB A <- value", "A <- an 8 bit immediate, [zero page address], or [D1+n] or [D2+n]",
               arithFlags("set when a borrow occurred"), "A",
               "Subtracts the operand from A. DEC A is the same instruction as SUB A <- 1."}},
      {"ADC", {"ARITHMETIC", "ADC A <- value", "A <- an 8 bit immediate, [zero page address], or [D1+n] or [D2+n]",
               [] {
                 FlagFacts f = arithFlags("read as carry in, then set to the carry out");
                 f.list = "N, V, Z, C (reads C)";
                 return f;
               }(),
               "A",
               "Adds the operand and the incoming C flag. This chains byte additions into wider ones: ADD the "
               "low bytes, then ADC each higher byte."}},
      {"SBC", {"ARITHMETIC", "SBC A <- value", "A <- an 8 bit immediate, [zero page address], or [D1+n] or [D2+n]",
               [] {
                 FlagFacts f = arithFlags("read as borrow in, then set when a borrow occurred");
                 f.list = "N, V, Z, C (reads C)";
                 return f;
               }(),
               "A", "Subtracts the operand and the incoming borrow. The mirror of ADC for multi byte subtraction."}},
      {"AND", {"LOGIC", "AND A <- value", "A <- an 8 bit immediate, [zero page address], or [D1+n] or [D2+n]", LOGIC_FLAGS,
               "A", "Bitwise AND into A. Masks bits: AND A <- $0F keeps the low nibble."}},
      {"OR", {"LOGIC", "OR A <- value", "A <- an 8 bit immediate, [zero page address], or [D1+n] or [D2+n]", LOGIC_FLAGS,
              "A", "Bitwise OR into A. Sets bits."}},
      {"XOR", {"LOGIC", "XOR A <- value", "A <- an 8 bit immediate, [zero page address], or [D1+n] or [D2+n]", LOGIC_FLAGS,
               "A", "Bitwise XOR into A. Flips bits. XOR A <- $FF inverts A. XOR with itself clears A."}},
      {"CMP", {"ARITHMETIC", "CMP A, value", "A, then an 8 bit immediate, [zero page address], or [D1+n] or [D2+n]",
               arithFlags("set when A is below the value, unsigned"), "none",
               "Compares A with the operand: the flags of SUB, and A keeps its value. So one byte can be tested "
               "against several values in a row. JZ jumps when they are equal, JC when A is lower and JNC when "
               "it is the same or higher, unsigned. Signed order is N xor V, as after SUB."}},
      {"TST", {"LOGIC", "TST A, value", "A, then an 8 bit immediate, [zero page address], or [D1+n] or [D2+n]",
               LOGIC_FLAGS, "none",
               "Tests bits: the flags of AND, and A keeps its value. TST A, $80 sets Z when bit 7 is clear. TST "
               "A, $FF tests A itself, which a port read needs, since IN sets no flags. TST D1 and TST D2 are "
               "LD D1 <- D1+0 and LD D2 <- D2+0: Z set when the whole pointer is zero, the NULL test."}},
      {"SHL", {"SHIFT", "SHL A", "A", FlagFacts{"N, Z, C", "set when the result is zero", "set to bit 7 of the result", "set to the bit shifted out of bit 7", UNAFFECTED}, "A",
               "Shifts A one bit left, a 0 into bit 0. A doubles. For a 16 bit value, SHL the low byte and ROL "
               "the high byte."}},
      {"SHR", {"SHIFT", "SHR A", "A", FlagFacts{"N, Z, C", "set when the result is zero", "set to bit 7 of the result", "set to the bit shifted out of bit 0", UNAFFECTED}, "A",
               "Shifts A one bit right, a 0 into bit 7. A halves, unsigned. For a 16 bit value, SHR the high "
               "byte and ROR the low byte."}},
      {"ROL", {"SHIFT", "ROL A", "A", FlagFacts{"N, Z, C", "set when the result is zero", "set to bit 7 of the result", "read into bit 0, then set to the old bit 7", UNAFFECTED}, "A",
               "Rotates A one bit left through C: C goes into bit 0 and bit 7 goes into C. It carries a shift "
               "from one byte into the next."}},
      {"ROR", {"SHIFT", "ROR A", "A", FlagFacts{"N, Z, C", "set when the result is zero", "set to bit 7 of the result", "read into bit 7, then set to the old bit 0", UNAFFECTED}, "A",
               "Rotates A one bit right through C: C goes into bit 7 and bit 0 goes into C."}},
      {"ASR", {"SHIFT", "ASR A", "A", FlagFacts{"N, Z, C", "set when the result is zero", "set to bit 7 of the result", "set to the bit shifted out of bit 0", UNAFFECTED}, "A",
               "Shifts A one bit right and keeps bit 7, so a negative byte stays negative. A halves, signed, "
               "rounding down. For a 16 bit value, ASR the high byte and ROR the low byte."}},
      {"INC", {"ARITHMETIC", "INC A | INC D1 | INC D2", "A, D1, or D2",
               FlagFacts{"N, V, Z, C (INC A only)", "INC A: set when A wraps to zero. INC D1, INC D2: " + UNAFFECTED,
                         "INC A only: set to bit 7 of the result", "INC A only: set on carry out",
                         "INC A only: set on signed overflow"},
               "the named register",
               "INC A assembles as ADD A <- 1, with full flags. INC D1 and INC D2 step a pointer through the "
               "shared step unit and touch no flags. Stepping past $FFFF wraps to zero. No data address is "
               "illegal, so the dereference that follows is legal too."}},
      {"DEC", {"ARITHMETIC", "DEC A", "A", arithFlags("set when a borrow occurred"), "A",
               "DEC A assembles as SUB A <- 1. A D register steps back with LD D1 <- D1-1, through the "
               "address adder."}},
      {"PUSHB", {"STACK", "PUSHB A", "A", NO_FLAGS, "A, SP, stack",
                 "Writes A to the stack and steps SP down. Pushing with SP at 0 crashes with a stack overflow."}},
      {"POPB", {"STACK", "POPB A", "A",
                FlagFacts{"N, Z", "set when the popped byte is zero", "set to bit 7 of the popped byte",
                          UNAFFECTED, UNAFFECTED},
                "A, SP, stack", "Steps SP up and reads the byte into A. The value passes the ALU, so Z and N are set."}},
      {"PUSHW", {"STACK", "PUSHW D1 | PUSHW D2", "D1 or D2", NO_FLAGS, "D1 or D2, SP, stack",
                 "Pushes a 16 bit register, high byte first. Two stack bytes, matching POPW."}},
      {"POPW", {"STACK", "POPW D1 | POPW D2", "D1 or D2",
                FlagFacts{"Z", "set when the popped word is zero", UNAFFECTED, UNAFFECTED, UNAFFECTED},
                "D1 or D2, SP, stack",
                "Pops two bytes into a D register, low byte first. Z reports whether the word is zero. This is "
                "how addresses come back off the stack."}},
      {"PUSH", {"STACK", "PUSH A | PUSH D1 | PUSH D2", "A, D1, or D2", NO_FLAGS, "the named register, SP, stack",
                "One push mnemonic for any register. PUSH A assembles to PUSHB, PUSH D1 and PUSH D2 to PUSHW. "
                "The store spelling LD [SP]- <- A says the same push, since [SP]- writes to the stack and steps "
                "SP down. Each spelling is one opcode."}},
      {"POP", {"STACK", "POP A | POP D1 | POP D2", "A, D1, or D2",
               FlagFacts{"Z, N", "set when the popped value is zero", "POP A only: bit 7 of the byte", UNAFFECTED,
                         UNAFFECTED},
               "the named register, SP, stack",
               "One pop mnemonic for any register. POP A assembles to POPB, POP D1 and POP D2 to POPW. The load "
               "spelling LD A <- [SP]+ says the same pop, since [SP]+ steps SP up and reads. Each spelling is "
               "one opcode."}},
      {"OUT", {"IO", "OUT port, value", "port and value bytes, names allowed", NO_FLAGS, "none",
               "Writes an immediate byte to an IO port. Port and value both accept built-in names: OUT GPU_CMD, "
               "CMD_CLEAR. The GPU listens on ports $00 to $1F. With an arrow, OUT A -> port sends the "
               "accumulator instead: that spelling assembles to OUTA."}},
      {"OUTA", {"IO", "OUTA port", "a port (byte or name)", NO_FLAGS, "A",
                "Writes A to an IO port. The computed twin of OUT. The arrow spellings OUT A -> port and OUT "
                "port <- A assemble to this same instruction."}},
      {"IN", {"IO", "IN port -> A | D1 | D2", "a port, and A, D1, or D2",
              FlagFacts{"Z", "word reads only: set when the 16 bit value is zero", UNAFFECTED, UNAFFECTED,
                        UNAFFECTED},
              "A, or D1 or D2",
              "One mnemonic reads a port. The target picks the width, the way PUSH picks PUSHB or PUSHW. IN "
              "port -> A reads one byte (INB). IN port -> D1 or D2 reads two bytes, high byte first, into the "
              "register (INW). A word read is two port reads, so a random port gives a 16 bit random. IN "
              "GPU_FRAME -> A is the device clock. IN GPU_RAND -> D2 is a random word."}},
      {"INB", {"IO", "INB port -> A", "a port (byte or name)", NO_FLAGS, "A",
               "Reads one byte from a port into A. The explicit byte form of IN. IN port and IN port -> A "
               "assemble to this. No flags change, so test the value afterwards."}},
      {"INW", {"IO", "INW port -> D1 | D2", "a port, and D1 or D2",
               FlagFacts{"Z", "set when the 16 bit value read is zero", UNAFFECTED, UNAFFECTED, UNAFFECTED},
               "D1 or D2",
               "Reads two bytes from a port into a D register, high byte first. The explicit word form of IN. "
               "Two reads of one port, so a stream port like GPU_RAND yields a 16 bit value. Store it with LD "
               "[D1] <- D2."}},
  };
  return M;
}

std::string mnemonicOf(std::string_view name) { return std::string(name.substr(0, name.find(' '))); }

InstrPage pageFrom(const std::string& mnem, const Meta& m) {
  return {mnem, m.title, m.usage, m.params, m.flags, m.registers, m.description, {}};
}

const InstrPage* find(const std::vector<InstrPage>& pages, const std::string& mnem) {
  for (const InstrPage& p : pages) {
    if (p.mnemonic == mnem) return &p;
  }
  return nullptr;
}

std::vector<InstrPage> buildPages() {
  const Microcode naive = buildNaive();
  const Microcode optimal = buildOptimal();
  std::vector<InstrPage> pages;
  for (const OpDef& def : ops()) {
    const std::string mnem = mnemonicOf(def.name);
    auto it = meta().find(mnem);
    if (it == meta().end()) continue;
    InstrPage* page = nullptr;
    for (InstrPage& p : pages) {
      if (p.mnemonic == mnem) page = &p;
    }
    if (!page) {
      pages.push_back(pageFrom(mnem, it->second));
      page = &pages.back();
    }
    const std::string name(def.name);
    page->shapes.push_back({name, def.op, cyclesUnder(naive, name), cyclesUnder(optimal, name)});
  }
  // The inverted jumps have opcodes of their own at $79 to $7C, but they
  // read best beside the jumps they invert.
  const auto afterJv = std::find_if(pages.begin(), pages.end(), [](const InstrPage& p) { return p.mnemonic == "JV"; });
  const auto firstInverted =
      std::find_if(pages.begin(), pages.end(), [](const InstrPage& p) { return p.mnemonic == "JNZ"; });
  if (afterJv != pages.end() && pages.end() - firstInverted >= 4 && firstInverted > afterJv) {
    std::rotate(afterJv + 1, firstInverted, firstInverted + 4);
  }
  // The sugar pages own no opcode rows. They borrow the shapes of the
  // opcodes they assemble to.
  auto sugar = [&](const std::string& mnem, std::initializer_list<std::string> from,
                   std::initializer_list<std::string> only = {}) {
    InstrPage page = pageFrom(mnem, meta().at(mnem));
    for (const std::string& src : from) {
      if (const InstrPage* p = find(pages, src)) {
        for (const InstrShape& s : p->shapes) {
          if (only.size() == 0 || std::find(only.begin(), only.end(), s.name) != only.end()) page.shapes.push_back(s);
        }
      }
    }
    pages.push_back(std::move(page));
  };
  sugar("DEC", {"SUB"}, {"SUB A <- imm8"});
  sugar("PUSH", {"PUSHB", "PUSHW"});
  sugar("POP", {"POPB", "POPW"});
  sugar("IN", {"INB", "INW"});
  return pages;
}

std::vector<RefRow> refRows(std::span<const gpu::NamedValue> entries, const std::map<std::string, std::string>& docs) {
  std::vector<RefRow> out;
  for (const gpu::NamedValue& e : entries) {
    auto it = docs.find(std::string(e.name));
    out.push_back({std::string(e.name), e.value, it == docs.end() ? "" : it->second});
  }
  return out;
}

const std::map<std::string, std::string> PORT_DOCS = {
    {"GPU_CMD", "write a command byte to run it"},
    {"GPU_CMD_MOD", "modifier bits for the next command"},
    {"GPU_DATA0", "arguments in, results out. What it means is the command's own"},
    {"GPU_RAND", "read: a random byte. write: reseed"},
    {"GPU_FRAME", "read: the frame counter"},
    {"GPU_DATA1", "see the running command"},
    {"GPU_DATA2", "see the running command"},
    {"GPU_DATA3", "see the running command"},
    {"GPU_DATA4", "see the running command"},
    {"GPU_DATA5", "see the running command"},
    {"GPU_DATA6", "see the running command"},
};

const std::map<std::string, std::string> CMD_DOCS = {
    {"CMD_CLEAR", "0: colour. Its own, so clearing leaves the pen alone"},
    {"CMD_SET_COLOR", "0: colour. The pen's colour"},
    {"CMD_MOVE_TO", "0-3: x, y. Moves the pen"},
    {"CMD_LINE_TO", "0-3: x, y. Draws from the pen, then moves it"},
    {"CMD_RECT", "0-3: x, y. The other corner is the pen"},
    {"CMD_CIRCLE", "0: radius, centred on the pen"},
    {"CMD_RING", "0: radius, centred on the pen"},
    {"CMD_PLOT", "0-3: x, y. 4: colour"},
    {"CMD_READ_PIXEL", "0-3: x, y. Reads back on DATA0"},
    {"CMD_RESTORE_SCREEN", "0-3: x, y. A shift applied on restore"},
    {"CMD_RESET_PALETTE", "back to 3-3-2"},
    {"CMD_LOAD_PALETTE", "0-2: cart address. The blob leads with its count"},
    {"CMD_STORE_PALETTE", "to the MAP_PALETTE_OUT address"},
    {"CMD_FETCH_PALETTE", "from the MAP_PALETTE_IN address"},
    {"CMD_ROTATE_SPEED", "0: frames per step, 0 turns it off"},
    {"CMD_ROTATE_DIR", "0: direction"},
    {"CMD_ROTATE_RANGE", "0: first. 1: last"},
    {"CMD_COPY", "0-2: cart address. 3-4: RAM address. 5-6: length"},
    {"CMD_RAM_MOVE", "1-2: from. 3-4: to. 5-6: length. Data RAM to data RAM, and the two blocks may overlap"},
    {"CMD_MEMMAP", "0: what. 1-2: RAM address"},
    {"CMD_SPRITE_DEF", "0: index. 1-3: cart address"},
    {"CMD_SPRITE_MOVE", "0: index. 1-4: x, y"},
    {"CMD_SPRITE_SHOW", "0: index"},
    {"CMD_SPRITE_HIDE", "0: index"},
    {"CMD_SPRITE_FRAME", "0: index. 1: frame"},
    {"CMD_SPRITE_FLIP", "0: index. 1: bit 0 horizontal, bit 1 vertical"},
    {"CMD_STAMP", "0: index. 1-4: x, y. Bakes the sprite into the screen"},
    {"CMD_BLIT", "0-2: cart address. Draws an image at the pen"},
    {"CMD_HIT_TEST", "0: index. 1: index. Reads back on DATA0"},
    {"CMD_HIT_SCAN", "0: index. Reads back on DATA0"},
    {"CMD_SPRITE_HITS", "which groups are touching this sprite, as a mask. Every one of them, not the first"},
    {"CMD_GROUP_HITS",
     "which groups are touching this group, as a mask. A group reports itself when two of its own overlap"},
    {"CMD_HIT_IN_GROUP",
     "the first sprite in a group that is touching this sprite, searching from GPU_HIT_FROM. 0 when there is none"},
    {"CMD_COLLIDE_GROUP_ALL",
     "every sprite's group mask at once, to the MAP_GROUPS address. The bulk form of CMD_SPRITE_HITS"},
    {"CMD_COLLIDE_ALL", "fills the MAP_COLLIDE address"},
    {"CMD_ROT_X", "0: angle"},
    {"CMD_SET_SCALE", "0: scale"},
    {"CMD_DRAW_PATH", "0-2: cart address. The blob leads with its count"},
    {"CMD_DRAW_PATH3D", "0-2: cart address. Same"},
    {"CMD_SET_TEXTMODE", "0-1: RAM address"},
    {"CMD_TEXT_STYLE", "0: foreground. 1: background. 2: flags"},
    {"CMD_TEXT_AT", "0: column. 1: row"},
    {"CMD_TEXT_CHAR", "0: character, drawn at the cursor"},
    {"CMD_PRINTF", "0-2: template in cart. 3-4: arguments in RAM"},
    {"CMD_LOAD_FONT", "0-2: cart address"},
    {"CMD_SET_WORLDMODE", "0-1: scene address. 2-3: object count"},
    {"CMD_MESH_LOAD", "0: mesh id. 1-3: cart address"},
    {"CMD_MESH_WRITE", "0: byte"},
    {"CMD_WORLD_RAMP", "0: ramp. 1-3: red, green, blue"},
    {"CMD_MATRIX_MAP", "0-1: base address. 2-3: count"},
    {"CMD_SAVE_SCREEN", "see the command that uses it"},
    {"CMD_ROTATE_LEFT", "see the command that uses it"},
    {"CMD_ROTATE_RIGHT", "see the command that uses it"},
    {"CMD_ROT_Y", "see the command that uses it"},
    {"CMD_ROT_Z", "see the command that uses it"},
    {"CMD_SET_GRAPHICSMODE", "see the command that uses it"},
    {"CMD_TEXT_CLEAR", "see the command that uses it"},
};

const std::map<std::string, std::string> GPU_ALIAS_DOCS = {
    {"GPU_X_HI", "x, high byte. Coordinates are signed 16 bit, high byte first"},
    {"GPU_X", "x, low byte"},
    {"GPU_Y_HI", "y, high byte"},
    {"GPU_Y", "y, low byte"},
    {"GPU_PIXEL", "the colour CMD_PLOT draws with"},
    {"GPU_COLOR", "the pen's colour, and the colour CMD_CLEAR fills with"},
    {"GPU_RADIUS", "radius for CMD_CIRCLE and CMD_RING, centred on the pen"},
    {"GPU_CART_BANK", "cartridge address bank, on a command that takes no index"},
    {"GPU_CART_HI", "cartridge address high byte"},
    {"GPU_CART_LO", "cartridge address low byte"},
    {"GPU_SRC_BANK", "cartridge address bank, on a command that takes an index first"},
    {"GPU_SRC_HI", "cartridge address high byte, after an index"},
    {"GPU_SRC_LO", "cartridge address low byte, after an index"},
    {"GPU_ADDR_HI", "a data RAM address, high byte"},
    {"GPU_ADDR_LO", "a data RAM address, low byte"},
    {"GPU_COUNT_HI", "how many records are there, high byte"},
    {"GPU_COUNT_LO", "how many records, low byte"},
    {"GPU_FROM_HI", "CMD_RAM_MOVE's source in data RAM, high byte"},
    {"GPU_FROM_LO", "CMD_RAM_MOVE's source, low byte"},
    {"GPU_DEST_HI", "the destination in data RAM, high byte"},
    {"GPU_DEST_LO", "the destination, low byte"},
    {"GPU_LEN_HI", "the byte count, high byte"},
    {"GPU_LEN_LO", "the byte count, low byte"},
    {"GPU_MAP", "which table CMD_MEMMAP is pointing at: one of the MAP_ names"},
    {"GPU_MAP_HI", "where that table lives in data RAM, high byte"},
    {"GPU_MAP_LO", "where that table lives, low byte"},
    {"GPU_SPRITE", "which sprite. The index is the first argument on every command that takes one"},
    {"GPU_SPRITE_B", "the second sprite CMD_HIT_TEST compares against"},
    {"GPU_SPRITE_FRAME", "which frame of the sprite's animation to show"},
    {"GPU_SPRITE_FLIP", "flip bits: 1 horizontal, 2 vertical"},
    {"GPU_SPRITE_X_HI", "the sprite's x, high byte"},
    {"GPU_SPRITE_X", "the sprite's x, low byte"},
    {"GPU_SPRITE_Y_HI", "the sprite's y, high byte"},
    {"GPU_SPRITE_Y", "the sprite's y, low byte"},
    {"GPU_HIT", "read back: what a collision command found. A result, so it survives the clear"},
    {"GPU_GROUP", "which groups, as a mask of eight bits, when the group is the first argument"},
    {"GPU_GROUP_B", "which groups, as a mask, on a command that took a sprite first"},
    {"GPU_HIT_FROM",
     "the sprite id CMD_HIT_IN_GROUP starts searching from. Feed back the last answer plus one to walk them all"},
    {"GPU_SPRITE_GROUP",
     "the groups this sprite belongs to, a mask of eight bits. 0 is no group, and a redefine is the only way to "
     "change it"},
    {"GPU_ANGLE", "rotation for CMD_ROT_X, CMD_ROT_Y and CMD_ROT_Z, a whole turn per 256"},
    {"GPU_SCALE", "scale for CMD_SET_SCALE, where 64 is life size"},
    {"GPU_SPEED", "how many frames a palette rotation waits between steps"},
    {"GPU_DIR", "which way a palette rotation runs"},
    {"GPU_FIRST", "the first palette entry a rotation touches"},
    {"GPU_LAST", "the last palette entry a rotation touches"},
    {"GPU_TEXT_COLOR", "the palette entry the character plane draws in"},
    {"GPU_TEXT_BG", "the character plane's background entry"},
    {"GPU_TEXT_FLAGS", "style bits for the character plane"},
    {"GPU_TEXT_COL", "the column CMD_TEXT_AT moves the text cursor to"},
    {"GPU_TEXT_ROW", "the row CMD_TEXT_AT moves the text cursor to"},
    {"GPU_TEXT_CHAR", "the character CMD_TEXT_CHAR writes at the cursor"},
    {"GPU_TEXT_ARG_HI", "where CMD_PRINTF finds its arguments in data RAM, high byte"},
    {"GPU_TEXT_ARG_LO", "where CMD_PRINTF finds its arguments, low byte"},
    {"GPU_MESH", "which mesh, for CMD_MESH_LOAD and CMD_MESH_WRITE"},
    {"GPU_MESH_BYTE", "one byte of geometry, streamed by CMD_MESH_WRITE"},
    {"GPU_RAMP", "which of the sixteen colour ramps CMD_WORLD_RAMP fills"},
    {"GPU_RED", "the ramp's shade 0, red"},
    {"GPU_GREEN", "the ramp's shade 0, green"},
    {"GPU_BLUE", "the ramp's shade 0, blue"},
};

const std::map<std::string, std::string> GPU_MOD_DOCS = {
    {"MOD_STICKY", "the data ports keep their values after a command, instead of clearing"},
    {"MOD_INC0", "the first data port steps by one after each command"},
};

// What a command leaves behind to be read, and on which port. A result
// survives the clear that wipes the arguments.
const std::map<std::string, std::string> GPU_CMD_RESULT = {
    {"CMD_READ_PIXEL", "GPU_DATA0: the palette index at (x, y), or 0 off screen"},
    {"CMD_HIT_TEST", "GPU_HIT: nonzero when the two sprites overlap"},
    {"CMD_HIT_SCAN", "GPU_HIT: the id of the first sprite hit, or 0 for none"},
    {"CMD_SPRITE_HITS", "GPU_HIT: a mask of every group touching that sprite"},
    {"CMD_GROUP_HITS", "GPU_HIT: a mask of every group touching that group"},
    {"CMD_HIT_IN_GROUP", "GPU_HIT: the sprite found, or 0 when the walk is over"},
};

const std::map<std::string, std::string> APU_PORT_DOCS = {
    {"APU_CART_LO", "cartridge address low byte, for the next define or load"},
    {"APU_CART_HI", "cartridge address high byte"},
    {"APU_CART_BANK", "cartridge bank (0..15): flat 1MB addressing"},
    {"APU_TRACK", "select the track (0..7) for the next instrument or note command"},
    {"APU_NOTE", "note number (0..127), and the root note for a define"},
    {"APU_ARG", "scalar argument: velocity, length high byte, octave, mode"},
    {"APU_ARG2", "scalar argument: length low byte. A 16 bit value goes high byte first"},
    {"APU_SLOT", "select the sample slot (0..63)"},
    {"APU_CMD", "command port: writing a command byte executes it"},
    {"APU_STATUS", "read: bit 0 set while a tune plays"},
    {"APU_VOICES", "read: the count of sounding voices"},
};

const std::map<std::string, std::string> APU_CMD_DOCS = {
    {"CMD_DEF_SAMPLE",
     "define APU_SLOT from the cartridge latches: length in the ARG pair, root in APU_NOTE. Clears the slot's "
     "loop flag"},
    {"CMD_SET_INSTRUMENT", "bind the track to APU_SLOT. APU_ARG 0 melodic, 1 keymap by octave"},
    {"CMD_KEYMAP_ENTRY", "set octave APU_ARG of the track's keymap to APU_SLOT, for a drum kit"},
    {"CMD_NOTE_ON", "start note APU_NOTE on the track at velocity APU_ARG"},
    {"CMD_NOTE_OFF", "stop note APU_NOTE on the track. APU_ARG 0 stops every note on it"},
    {"CMD_TRIGGER", "play APU_SLOT at APU_NOTE in one command, the sound-effect path"},
    {"CMD_LOAD_MIDI", "parse the MIDI file at the cartridge latches"},
    {"CMD_PLAY", "start the loaded tune from the beginning"},
    {"CMD_STOP", "stop the tune and silence its voices"},
    {"CMD_STOP_ALL", "silence every voice at once, the tune left alone"},
    {"CMD_LOOP_SLOT", "APU_ARG 1 makes APU_SLOT repeat, 0 plays it once. A looping voice ends only on a stop"},
};

const std::map<std::string, std::string> ACP_PORT_DOCS = {
    {"ACP_ADDR_HI", "block address high byte: where A, B and the result live in data RAM"},
    {"ACP_ADDR_LO", "block address low byte"},
    {"ACP_FMT", "operand element type. Writing it sets the result type to match"},
    {"ACP_RFMT", "result element type, when it should differ from the operands'"},
    {"ACP_CMD", "write a command byte to run it. One cycle, however big the shape"},
    {"ACP_FLAGS", "read: how the last command went. Every command rewrites every bit"},
    {"ACP_ROWS", "rows in A. Powers on at 1"},
    {"ACP_COLS", "columns in A. Powers on at 1, so the default shape is a scalar"},
    {"ACP_COLS_B", "columns in B. Only ACP_MATMUL reads it"},
    {"ACP_ARG", "how many entries ACP_GEN_TABLE writes, high byte"},
    {"ACP_ARG2", "the same count, low byte"},
    {"ACP_FUNC", "which function ACP_GEN_TABLE samples: a transcendental command's own value"},
};

const std::map<std::string, std::string> ACP_FMT_DOCS = {
    {"ACP_I64", "signed 64 bit integer, 8 bytes, big-endian"},
    {"ACP_U64", "unsigned 64 bit integer, 8 bytes"},
    {"ACP_F64", "IEEE 754 double, 8 bytes. The host's own arithmetic, exactly"},
    {"ACP_C64", "complex: two doubles, real part first, 16 bytes"},
};

const std::map<std::string, std::string> ACP_FLAG_DOCS = {
    {"ACP_ZERO", "every element of the result was zero"},
    {"ACP_NEGATIVE", "the result was negative. A 1 by 1 result only, because an aggregate has no single sign"},
    {"ACP_DIVZERO", "a divide or a remainder by zero"},
    {"ACP_OVERFLOW",
     "an integer result did not fit. On a shift or a rotate it means the bit that fell out instead, whichever "
     "end it left by"},
    {"ACP_NAN", "a float result was NaN or infinite"},
    {"ACP_BADFMT", "the element type is not one this command takes"},
    {"ACP_SINGULAR", "a matrix has no inverse"},
    {"ACP_BADDIM", "a dimension was zero, or a shape this command cannot use"},
};

// Each command's operand shapes, with r for ACP_ROWS, c for ACP_COLS and n
// for ACP_COLS_B. A dash means the operand takes no room. The flags listed
// are what a command raises beyond the ones every command sets. ACP_BADFMT
// and ACP_BADDIM are derived from acpKindsFor and acpShapesFor at build.
struct AcpDoc {
  std::string a, b, r;
  std::vector<std::string> raises;
  std::string what;
};

const std::map<std::string, AcpDoc> ACP_CMD_DOCS = {
    {"ACP_ADD", {"r*c", "r*c", "r*c", {}, "A plus B, element by element"}},
    {"ACP_SUB", {"r*c", "r*c", "r*c", {}, "A minus B"}},
    {"ACP_MUL", {"r*c", "r*c", "r*c", {},
                 "element by element, never the matrix product. A 1 by 1 integer multiply writes 16 bytes, the "
                 "one widening case"}},
    {"ACP_DIV", {"r*c", "r*c", "r*c", {"ACP_DIVZERO"}, "A divided by B"}},
    {"ACP_REM", {"r*c", "r*c", "r*c", {"ACP_DIVZERO"}, "remainder"}},
    {"ACP_NEG", {"r*c", "-", "r*c", {}, "minus A"}},
    {"ACP_ABS", {"r*c", "-", "r*c", {}, "magnitude. A complex gives a real"}},
    {"ACP_CMP", {"r*c", "r*c", "r*c", {}, "minus one, zero or one"}},
    {"ACP_CVT", {"r*c", "-", "r*c", {}, "A in the result type. Set ACP_RFMT first"}},
    {"ACP_SCALE", {"r*c", "1*1", "r*c", {}, "every element of A times one number"}},
    {"ACP_SQRT", {"r*c", "-", "r*c", {}, "square root"}},
    {"ACP_SIN", {"r*c", "-", "r*c", {}, "sine, in radians"}},
    {"ACP_COS", {"r*c", "-", "r*c", {}, "cosine, in radians"}},
    {"ACP_TAN", {"r*c", "-", "r*c", {}, "tangent, in radians"}},
    {"ACP_ASIN", {"r*c", "-", "r*c", {}, "arc sine"}},
    {"ACP_ACOS", {"r*c", "-", "r*c", {}, "arc cosine"}},
    {"ACP_ATAN", {"r*c", "-", "r*c", {}, "arc tangent"}},
    {"ACP_LOG", {"r*c", "-", "r*c", {}, "natural logarithm"}},
    {"ACP_LOG10", {"r*c", "-", "r*c", {}, "base ten logarithm"}},
    {"ACP_EXP", {"r*c", "-", "r*c", {}, "e to the power A"}},
    {"ACP_ATAN2", {"r*c", "r*c", "r*c", {}, "the angle to the point A, B"}},
    {"ACP_POW", {"r*c", "r*c", "r*c", {}, "A to the power B"}},
    {"ACP_HYPOT", {"r*c", "r*c", "r*c", {}, "the hypotenuse of the leg pair"}},
    {"ACP_ARG_OF", {"r*c", "-", "r*c", {}, "the angle, a real, in radians. ACP_ABS gives the magnitude"}},
    {"ACP_CONJ", {"r*c", "-", "r*c", {}, "the conjugate"}},
    {"ACP_DOT", {"r*1", "r*1", "1*1", {}, "dot product. A column vector, so ACP_COLS must be 1"}},
    {"ACP_CROSS", {"3*1", "3*1", "3*1", {}, "cross product. Three rows exactly"}},
    {"ACP_NORM", {"r*1", "-", "1*1", {}, "length of the vector"}},
    {"ACP_NORMALIZE", {"r*1", "-", "r*1", {"ACP_DIVZERO"}, "the vector scaled to length one"}},
    {"ACP_DIST", {"r*1", "r*1", "1*1", {}, "distance between two points"}},
    {"ACP_MATMUL", {"r*c", "c*n", "r*n", {}, "matrix product. Set ACP_COLS_B to n"}},
    {"ACP_MATVEC", {"r*c", "c*1", "r*1", {}, "matrix times a column vector"}},
    {"ACP_TRANSPOSE", {"r*c", "-", "c*r", {}, "A with rows and columns swapped"}},
    {"ACP_IDENTITY", {"r*c", "-", "r*r", {}, "the r by r identity. A is not read"}},
    {"ACP_DET", {"r*r", "-", "1*1", {}, "determinant. Square only"}},
    {"ACP_INVERSE", {"r*r", "-", "r*r", {"ACP_SINGULAR"}, "inverse. Square only, and ACP_SINGULAR when there is none"}},
    {"ACP_AND", {"r*c", "r*c", "r*c", {}, "bitwise and"}},
    {"ACP_OR", {"r*c", "r*c", "r*c", {}, "bitwise or"}},
    {"ACP_XOR", {"r*c", "r*c", "r*c", {}, "bitwise exclusive or"}},
    {"ACP_NOT", {"r*c", "-", "r*c", {}, "every bit of A flipped"}},
    {"ACP_SHL", {"r*c", "r*c", "r*c", {"ACP_OVERFLOW"},
                 "shift left by B places. B is a count per element, so a vector can shift each of its own"}},
    {"ACP_SHR", {"r*c", "r*c", "r*c", {"ACP_OVERFLOW"}, "shift right by B, filling with zero"}},
    {"ACP_ASR", {"r*c", "r*c", "r*c", {"ACP_OVERFLOW"},
                 "shift right by B, filling with the sign. Not the same as ACP_DIV by a power of two, which "
                 "truncates toward zero"}},
    {"ACP_ROL", {"r*c", "r*c", "r*c", {"ACP_OVERFLOW"}, "rotate left by B. Nothing is lost"}},
    {"ACP_ROR", {"r*c", "r*c", "r*c", {"ACP_OVERFLOW"}, "rotate right by B"}},
    {"ACP_GEN_TABLE", {"1*1", "1*1", "1*1", {},
                       "sample ACP_FUNC from A to B, in as many steps as ACP_ARG and ACP_ARG2 ask for. The result "
                       "runs for that many entries"}},
};

// Whether a command refuses any real shape, asked of the machine: a null
// answer from acpShapesFor is what ACP_BADDIM reports. A zero dimension
// makes every command answer null, so the probes are all non-zero.
bool acpCanBadDim(uint8_t cmd) {
  const int probes[][3] = {{5, 4, 3}, {5, 1, 3}, {5, 5, 3}, {3, 1, 3}, {1, 1, 3}, {4, 3, 2}};
  for (const auto& p : probes) {
    if (!acpShapesFor(cmd, p[0], p[1], p[2])) return true;
  }
  return false;
}

std::string kindsText(const std::vector<acp::Kind>& kinds) {
  if (kinds.size() == 3) return "any";
  std::string out;
  for (acp::Kind k : kinds) {
    if (!out.empty()) out += ", ";
    out += k == acp::Kind::Int ? "int" : k == acp::Kind::Float ? "float" : "complex";
  }
  return out;
}

const std::map<std::string, std::string> INPUT_PORT_DOCS = {
    {"IO_CONTROLLER", "read: the button byte, a level, so a held button stays set"},
    {"IO_KEY", "read: pop one key event. Low seven bits the key code, the top bit the release flag. Zero means "
               "the queue is empty"},
    {"IO_MODS", "read: the modifier keys held right now. A level, so reading it costs no key event"},
};

// Which keys drive each button, from the table in keys.cpp.
const std::map<std::string, std::string> BTN_KEYS = {
    {"BTN_UP", "Up arrow, W"},       {"BTN_DOWN", "Down arrow, S"}, {"BTN_LEFT", "Left arrow, A"},
    {"BTN_RIGHT", "Right arrow, D"}, {"BTN_FIRE", "Z, Ctrl"},       {"BTN_SPACE", "Space"},
    {"BTN_ENTER", "Enter"},
};

const std::map<std::string, std::string> MOD_DOCS = {
    {"MOD_SHIFT", "either shift key is down"},
    {"MOD_CTRL", "either control key is down"},
    {"MOD_ALT", "either alt key is down"},
    {"MOD_META", "a meta key is down"},
};

}  // namespace

int cyclesUnder(const Microcode& mc, const std::string& shape) {
  const Rows* fetch = mc.get("fetch");
  const Rows* rows = mc.get(shape);
  if (!fetch || !rows) return -1;
  return static_cast<int>(fetch->size() + rows->size());
}

const std::vector<InstrPage>& instructionPages() {
  static const std::vector<InstrPage> PAGES = buildPages();
  return PAGES;
}

const std::vector<RefRow>& gpuPorts() {
  static const std::vector<RefRow> R = refRows(gpu::PORTS, PORT_DOCS);
  return R;
}

// Ordered by the port each name stands for. The names that share a port
// then sit together, and the sharing is visible rather than stated.
const std::vector<RefRow>& gpuAliases() {
  static const std::vector<RefRow> R = [] {
    std::vector<RefRow> rows = refRows(gpu::ALIASES, GPU_ALIAS_DOCS);
    std::stable_sort(rows.begin(), rows.end(), [](const RefRow& a, const RefRow& b) {
      return a.value != b.value ? a.value < b.value : a.name < b.name;
    });
    return rows;
  }();
  return R;
}

const std::vector<RefRow>& gpuMods() {
  static const std::vector<RefRow> R = refRows(gpu::MODS, GPU_MOD_DOCS);
  return R;
}

const std::vector<GpuCmdRow>& gpuCommands() {
  static const std::vector<GpuCmdRow> R = [] {
    std::vector<GpuCmdRow> rows;
    for (const gpu::NamedValue& c : gpu::CMDS) {
      GpuCmdRow row{std::string(c.name), c.value, "", "", ""};
      for (const gpu::CmdArgs& a : gpu::CMD_ARGS) {
        if (a.cmd != c.name) continue;
        if (a.count == 0) row.args = "none";
        for (std::string_view n : a.args()) {
          if (!row.args.empty()) row.args += ", ";
          row.args += n;
        }
      }
      if (auto it = GPU_CMD_RESULT.find(row.name); it != GPU_CMD_RESULT.end()) row.leaves = it->second;
      if (auto it = CMD_DOCS.find(row.name); it != CMD_DOCS.end()) row.meaning = it->second;
      rows.push_back(std::move(row));
    }
    return rows;
  }();
  return R;
}

const std::vector<RefRow>& apuPorts() {
  static const std::vector<RefRow> R = refRows(apu::PORTS, APU_PORT_DOCS);
  return R;
}

const std::vector<RefRow>& apuCommands() {
  static const std::vector<RefRow> R = refRows(apu::CMDS, APU_CMD_DOCS);
  return R;
}

const std::vector<RefRow>& acpPorts() {
  static const std::vector<RefRow> R = refRows(acp::PORTS, ACP_PORT_DOCS);
  return R;
}

const std::vector<RefRow>& acpFormats() {
  static const std::vector<RefRow> R = refRows(acp::FMTS, ACP_FMT_DOCS);
  return R;
}

const std::vector<AcpCmdRow>& acpCommands() {
  static const std::vector<AcpCmdRow> R = [] {
    std::vector<AcpCmdRow> rows;
    for (const gpu::NamedValue& c : acp::CMDS) {
      auto it = ACP_CMD_DOCS.find(std::string(c.name));
      if (it == ACP_CMD_DOCS.end()) continue;
      const AcpDoc& d = it->second;
      const auto cmd = static_cast<uint8_t>(c.value);
      std::vector<std::string> raised = d.raises;
      if (acpKindsFor(cmd).size() < 3) raised.push_back("ACP_BADFMT");
      if (acpCanBadDim(cmd)) raised.push_back("ACP_BADDIM");
      std::string flags;
      for (const std::string& f : raised) flags += (flags.empty() ? "" : ", ") + f;
      if (flags.empty()) flags = "the usual";
      rows.push_back({std::string(c.name), c.value, d.a, d.b, d.r, kindsText(acpKindsFor(cmd)), flags, d.what});
    }
    return rows;
  }();
  return R;
}

const std::vector<RefRow>& acpFlags() {
  static const std::vector<RefRow> R = refRows(acp::FLAG_BITS, ACP_FLAG_DOCS);
  return R;
}

const std::vector<RefRow>& inputPorts() {
  static const std::vector<RefRow> R = refRows(input::PORTS, INPUT_PORT_DOCS);
  return R;
}

const std::vector<RefRow>& inputButtons() {
  static const std::vector<RefRow> R = refRows(input::BUTTONS, BTN_KEYS);
  return R;
}

const std::vector<RefRow>& inputMods() {
  static const std::vector<RefRow> R = refRows(input::MODS, MOD_DOCS);
  return R;
}

}  // namespace sc8::manual
