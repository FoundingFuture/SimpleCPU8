# SimpleCPU core specification v2

Supersedes v1 (docs/cpu-core-spec.md). This revision adds the pointer registers and
the stack. It also adds the shared step unit, the effective-address adder, the V flag,
and the unified LD syntax. Items marked "assumed" use the recommended default and await Eddie's veto.
Milestone 1 scope: CPU, microcode engine, assembler. Headless TypeScript core.

## Contents

- [Machine overview](#machine-overview)
- [Shared units](#shared-units)
- [Memories](#memories)
- [Instruction format](#instruction-format)
- [Assembly syntax](#assembly-syntax)
- [Addressing modes](#addressing-modes)
- [Instruction set](#instruction-set)
- [Flags](#flags)
- [Control signals](#control-signals)
- [Row semantics](#row-semantics)
- [Conflict rules](#conflict-rules)
- [Sequencer](#sequencer)
- [Halt and crash rules](#halt-and-crash-rules)
- [Microcode file format](#microcode-file-format)
- [Shipped microcode](#shipped-microcode)
- [Naive microcode by family](#naive-microcode-by-family)
- [Optimal microcode by family](#optimal-microcode-by-family)
- [Cycle counts](#cycle-counts)
- [Golf scoreboard](#golf-scoreboard)
- [Assembly file format](#assembly-file-format)
- [Calling conventions](#calling-conventions)
- [Breakpoints](#breakpoints)
- [Project structure](#project-structure)
- [Assumed defaults](#assumed-defaults)

## Machine overview

Accumulator machine, 8-bit data path, 16-bit addresses, big-endian.

| Register | Width | Description |
|---|---|---|
| PC | 16 bit | Instruction counter. Counts 3-byte slots. |
| IR | 24 bit | Fetched instruction: opcode byte plus 16-bit operand. |
| ACC | 8 bit | Accumulator. The only architectural byte register. |
| A, B | 8 bit | ALU input latches. Internal, not part of the ISA. |
| D1, D2 | 16 bit | Pointer registers. Address all of data RAM. |
| SP | 16 bit | Stack pointer into the separate stack RAM. Points at the next free cell. Widened from 8 bit after v2. |
| FLAGS | 4 bit | Z, N, C, V. |

Big-endian everywhere: a 16-bit value 0x1234 stores 0x12 at the lower address and 0x34
at the next one.

## Shared units

The machine holds no duplicated intelligence. Three computational units exist.

| Unit | Function | Serves |
|---|---|---|
| ALU | 8-bit ADD, SUB, ADC, SBC, AND, OR, XOR, PASS_B. Produces flags. | ACC, FLAGS |
| Step unit | 16-bit +1 or -1, one register per cycle through a mux. | PC, SP, D1, D2, ACC (low lane, wraps mod 256) |
| EA adder | Effective address = base + offset + carry-in. Combinational, address stage. | Data RAM addressing, stack +1 tap |

The step unit serves PC too. There is no private PC incrementer. At most one step
operation fires per row. This scarcity is a deliberate part of the microcode game.
The Intel 8080 shared one increment unit between PC and SP the same way.

The EA adder takes a base, an offset, and a carry-in bit. Bases: D1, D2, A
zero-extended, or the operand. Offsets: zero, the operand low byte, or A. Displacement
and indexed addressing use it. Word transfers use the carry-in to reach address plus
one without stepping any register.

## Memories

Four memories, each with one owner and one access discipline.

| Memory | Size | Access |
|---|---|---|
| Program | 64K slots of 3 bytes | Fetch only. Holds exactly the N assembled instructions. |
| Data RAM | 65536 bytes | LD instructions. Low 256 bytes form the zero page. |
| Stack RAM | 4096 bytes | Through SP only. Push, pop, JSR, RET. Grown from 256 after v2. |
| Cartridge (.data) | Up to 1MB | GPU only. Not reachable from the CPU. Milestone 3. |

The zero page (0x00 to 0xFF) is directly addressable with an 8-bit operand and
indirectly through `[A]`. Addresses 0x100 and up are reachable only through D1
and D2. No data address is illegal. The effective address wraps at 16 bits, so
a pointer walked off the end wraps to zero. Stepping a register never crashes.

## Instruction format

One slot per instruction: opcode byte plus 16-bit operand. Three bytes, fixed, even
for NOP. Fetch reads a whole slot in one cycle. Operand use per instruction is listed
in the instruction set table. Unused operand fields must be zero in the encoding. The
assembler owns all encoding decisions. Surface syntax never exposes them.

## Assembly syntax

The syntax is a readability layer. It does not mirror the encoding.

- Arrow notation, both directions: `LD A <- [D1]+` and `LD [D1]+ -> A` are the same
  instruction. The formatter can normalize direction on request.
- Square brackets mean contents-of. Bare means the value itself. `LD D1 <- node` loads
  the address of `node`. `LD A <- [node]` loads the byte stored there.
- Round brackets never dereference. They hold a call's argument, as in `sin(t)` and
  `get_lowbyte(x)`. They also group arithmetic, which expr.ts evaluates at assembly
  time. A group holding an addressing shape is the old spelling and is refused, with
  the square spelling to use.
- Constant expressions work anywhere a number works, with C's operators and C's
  precedence. Everything folds before a byte is emitted. The CPU still has no
  multiply, no divide and no shift.
- One mnemonic, LD, covers every load and store. Assumed.
- `+` after a bracketed register selects the post-increment opcode. Plain and
  post-increment forms are distinct opcodes. The pointer steps by the transfer width.
- Width law: the transfer width equals the width of the register outside the
  brackets. Byte for A, word for D1 and D2. Auto-increment steps by that width.
- Sugar rule: aliases that map one-to-one onto an instruction are fine. `INC A` is
  `ADD A <- 1`. Anything that expands to more than one instruction is a macro and must
  look like one. Cycle counts stay honest.
- Comments start with `;`. Numbers: decimal, `0x` hex, `0b` binary.

## Addressing modes

| Mode | Example | Meaning |
|---|---|---|
| Immediate | `LD A <- 5`, `LD D1 <- node` | Value from the operand field. |
| Zero page direct | `LD A <- [counter]` | Byte at an 8-bit address. |
| Word direct | `LD D1 <- [ptrvar]` | 16-bit value at a 16-bit address. |
| Register indirect | `LD A <- [D1]` | Byte at the address in D1. |
| Post-increment | `LD A <- [D1]+` | Same, then D1 steps by the width. |
| Accumulator indirect | `LD A <- [A]` | Byte at zero-page address in A. Cannot crash. |
| Displacement | `LD D2 <- [D1+3]` | Address is D1 plus a constant from the operand. |
| Indexed | `LD A <- [D1+A]` | Address is D1 plus the runtime value of A. |

Restrictions, all enforced by the assembler:

- `LD [A]+ -> A` is an error. The load overwrites the register being stepped.
- Word loads into the register that provides the address are errors:
  `LD D1 <- [D1]` and every displacement or indexed form of it. The first written half
  would corrupt the address for the second read. Use the other D register.
- Displacement and indexed forms take no `+`.
- Indexed stores are errors. A is the index and also the only byte source.

## Instruction set

74 opcodes, 23 surface mnemonics. Flags column lists what the instruction writes.

Control, branches, calls:

| Opcode | Syntax | Effect | Flags |
|---|---|---|---|
| 0x00 | NOP | Nothing. | |
| 0x01 | HLT | Stop the clock. | |
| 0x04 | JMP D1 | `PC <- D1` | |
| 0x05 | JMP D2 | `PC <- D2` | |
| 0x06 | JSR D1 | Push PC+1 (2 bytes), `PC <- D1` | |
| 0x07 | JSR D2 | Push PC+1 (2 bytes), `PC <- D2` | |
| 0x08 | JMP t | `PC <- t` | |
| 0x09 | JZ t | `PC <- t` when Z | |
| 0x0A | JC t | `PC <- t` when C | |
| 0x0B | JN t | `PC <- t` when N | |
| 0x0C | JV t | `PC <- t` when V | |
| 0x79 | JNZ t | `PC <- t` when not Z. Added after v2 | |
| 0x7A | JNC t | `PC <- t` when not C. Added after v2 | |
| 0x7B | JP t | `PC <- t` when not N, jump if plus. Added after v2 | |
| 0x7C | JNV t | `PC <- t` when not V. Added after v2 | |
| 0x0E | JSR t | Push PC+1 (2 bytes), `PC <- t` | |
| 0x0F | RET | Pop 2 bytes into PC | |

Byte loads and stores (all byte loads land in ACC and set Z, N):

| Opcode | Syntax | Flags |
|---|---|---|
| 0x10 | LD A <- imm8 | Z N |
| 0x11 | LD A <- [addr8] | Z N |
| 0x12 | LD [addr8] <- A | |
| 0x13 | LD A <- [A] | Z N |
| 0x14 | LD A <- [D1] | Z N |
| 0x15 | LD A <- [D2] | Z N |
| 0x16 | LD A <- [D1]+ | Z N |
| 0x17 | LD A <- [D2]+ | Z N |
| 0x18 | LD [D1] <- A | |
| 0x19 | LD [D2] <- A | |
| 0x1A | LD [D1]+ <- A | |
| 0x1B | LD [D2]+ <- A | |
| 0x1C | LD A <- [D1+n] | Z N |
| 0x1D | LD A <- [D2+n] | Z N |
| 0x1E | LD A <- [D1+A] | Z N |
| 0x1F | LD A <- [D2+A] | Z N |
| 0x20 | LD [D1+n] <- A | |
| 0x21 | LD [D2+n] <- A | |

Word loads and stores (all word loads into D set Z from the loaded value):

| Opcode | Syntax | Flags |
|---|---|---|
| 0x22 | LD D1 <- imm16 | Z |
| 0x23 | LD D2 <- imm16 | Z |
| 0x24 | LD D1 <- [addr16] | Z |
| 0x25 | LD D2 <- [addr16] | Z |
| 0x26 | LD [addr16] <- D1 | |
| 0x27 | LD [addr16] <- D2 | |
| 0x28 | LD D2 <- [D1] | Z |
| 0x29 | LD D1 <- [D2] | Z |
| 0x2A | LD D2 <- [D1]+ | Z |
| 0x2B | LD D1 <- [D2]+ | Z |
| 0x2C | LD D1 <- [A] | Z |
| 0x2D | LD D2 <- [A] | Z |
| 0x2E | LD D1 <- [A]+ | Z |
| 0x2F | LD D2 <- [A]+ | Z |
| 0x30 | LD D2 <- [D1+n] | Z |
| 0x31 | LD D1 <- [D2+n] | Z |
| 0x32 | LD D2 <- [D1+A] | Z |
| 0x33 | LD D1 <- [D2+A] | Z |
| 0x34 | LD [D1] <- D2 | |
| 0x35 | LD [D2] <- D1 | |
| 0x36 | LD [D1+n] <- D2 | |
| 0x37 | LD [D2+n] <- D1 | |

ALU (direct operand from the zero page, or immediate):

| Opcode | Syntax | Flags |
|---|---|---|
| 0x40 | ADD A <- [addr8] | Z N C V |
| 0x41 | SUB A <- [addr8] | Z N C V |
| 0x42 | AND A <- [addr8] | Z N |
| 0x43 | OR A <- [addr8] | Z N |
| 0x44 | XOR A <- [addr8] | Z N |
| 0x45 | ADC A <- [addr8] | Z N C V |
| 0x46 | SBC A <- [addr8] | Z N C V |
| 0x48 | ADD A <- imm8 | Z N C V |
| 0x49 | SUB A <- imm8 | Z N C V |
| 0x4A | AND A <- imm8 | Z N |
| 0x4B | OR A <- imm8 | Z N |
| 0x4C | XOR A <- imm8 | Z N |
| 0x4D | ADC A <- imm8 | Z N C V |
| 0x4E | SBC A <- imm8 | Z N C V |

Stack, step, IO:

| Opcode | Syntax | Effect | Flags |
|---|---|---|---|
| 0x50 | PUSHB A | Stack byte from ACC | |
| 0x51 | POPB A | ACC from stack | Z N |
| 0x52 | PUSHW D1 | Stack word, big-endian | |
| 0x53 | PUSHW D2 | | |
| 0x54 | POPW D1 | D1 from stack | Z |
| 0x55 | POPW D2 | D2 from stack | Z |
| 0x58 | INC D1 | `D1 <- D1 + 1` | |
| 0x59 | INC D2 | `D2 <- D2 + 1` | |
| 0x60 | OUT port, imm8 | Port from operand data byte | |
| 0x61 | OUTA port | Port from ACC | |
| 0x62 | IN port | ACC from port | |

Aliases (one-to-one, no hidden cost): `INC A` = `ADD A <- 1`, `DEC A` = `SUB A <- 1`.
INC D1 and INC D2 set no flags. The step unit produces none.

Deferred by the beggars rule: DEC D1, DEC D2, CMP, and indirect ALU operands. Also
deferred: a second byte register, D-to-ACC transfers, LDS stack peeks.

## Flags

| Flag | Meaning |
|---|---|
| Z | Result is zero. For word loads: the 16-bit value is zero (null pointer test). |
| N | Bit 7 of the byte result is set. |
| C | ADD, ADC: carry out of bit 7. SUB, SBC: borrow occurred. Assumed. |
| V | ADD, SUB, ADC, SBC: signed overflow of the byte result. |

Flag masks per ALU operation: ADD, SUB, ADC, and SBC write Z, N, C, V. AND, OR, XOR,
and PASS_B write Z and N. IN writes nothing. Assumed. `FLAGS_LOAD` applies the mask
of the selected operation. ADC adds C into the sum. SBC subtracts C as a borrow. Both
read the flag as it stood at the start of the row.

ADC and SBC entered the ISA on benchmark evidence: multi-byte arithmetic without them
costs a branch cascade per byte. Byte loads write only Z and N, so the carry survives
the load-store traffic between chained ADCs.

## Control signals

A micro-instruction is a set of signals asserted for one cycle. `OPLO`, `OPHI`, `OP16`
name the operand fields.

Sequencing and PC:

| Signal | Transfer |
|---|---|
| FETCH | `IR <- PROG[PC]` |
| PC_INC | `PC <- PC + 1` (step unit) |
| PC_LOAD | `PC <- OP16` |
| PC_LOAD_Z / _C / _N / _V | `PC <- OP16` when the flag is set |
| PC_LOAD_NZ / _NC / _NN / _NV | `PC <- OP16` when the flag is clear. Added after v2 |
| PC_FROM_D1 / PC_FROM_D2 | `PC <- D1` or `PC <- D2`. Added after v2 |
| STK_TO_PCL / STK_TO_PCH | PC half from stack RAM |
| HALT | Stop the clock at end of cycle |
| WAIT_FRAME | Reserved. Stalls until the next frame boundary (GPU milestone). |

ALU path:

| Signal | Transfer |
|---|---|
| ACC_TO_A | `A <- ACC` |
| IMM_TO_B | `B <- OPLO` |
| RAM_TO_B | `B <- RAM[ea]` |
| STK_TO_B | `B <- STACK[sa]` |
| ALU_ADD / SUB / ADC / SBC / AND / OR / XOR / PASS_B | Select the ALU operation |
| ACC_LOAD_ALU | `ACC <- ALU result` |
| FLAGS_LOAD | `FLAGS <- ALU flags`, masked per operation |

Data RAM (each needs exactly one address base select):

| Signal | Transfer |
|---|---|
| RAM_WRITE_ACC | `RAM[ea] <- ACC` |
| RAM_TO_D1H / D1L / D2H / D2L | D half from `RAM[ea]` |
| RAM_WRITE_D1H / D1L / D2H / D2L | `RAM[ea] <-` D half |

Address stage (data RAM):

| Signal | Meaning |
|---|---|
| ADDR_OP8 | Base = operand low byte (zero page) |
| ADDR_OP16 | Base = full operand |
| ADDR_D1 / ADDR_D2 | Base = pointer register |
| ADDR_A | Base = ACC, zero-extended |
| EA_OFF_OP8 | Add operand low byte to the base |
| EA_OFF_A | Add ACC to the base |
| EA_CIN | Add one (second byte of a word transfer) |

D registers and step unit:

| Signal | Transfer |
|---|---|
| D1_LOAD_OP16 / D2_LOAD_OP16 | `Dx <- OP16` |
| D1_TSTZ / D2_TSTZ | `Z <- (Dx == 0)`, tests the start-of-cycle value |
| D1_INC / D2_INC / ACC_INC / SP_INC / SP_DEC | One step through the shared unit |

Stack RAM. The address is SP. STK_CIN addresses SP plus one:

| Signal | Transfer |
|---|---|
| STK_WRITE_ACC / PCH / PCL / D1H / D1L / D2H / D2L | `STACK[sa] <-` source |
| STK_TO_D1H / D1L / D2H / D2L | D half from `STACK[sa]` |
| STK_CIN | Stack address = SP + 1 for this row |

IO:

| Signal | Transfer |
|---|---|
| IO_WRITE_IMM | `port[OPHI] <- OPLO` |
| IO_WRITE_ACC | `port[OPHI] <- ACC` |
| IO_READ | `ACC <- port[OPHI]` |

## Row semantics

- Reads see start-of-cycle register values. Writes commit at end of cycle.
- The ALU and the EA adder are combinational inside the row.
- A post-increment read fits one row: the memory access uses the old address, the step
  commits afterward.
- A word transfer takes two rows. The second row reaches the next address with EA_CIN
  or with the step committed by the first row.
- TSTZ tests the value a register held at the start of the row. Test after load means
  a separate row.
- A flag-gated PC load whose condition is false does nothing.

## Conflict rules

An illegal row is marked by the validation panel and crashes the CPU when executed.

1. One writer per register per row. PC halves, D halves, ACC, A, B, FLAGS, SP each
   count. Flag-gated PC loads count as writers.
2. One data RAM access per row.
3. One program memory access per row.
4. One stack RAM access per row.
5. At most one ALU operation select per row.
6. ACC_LOAD_ALU and FLAGS_LOAD require exactly one ALU operation select in the row.
7. One step operation per row, PC_INC included. The step unit is shared.
8. One IO signal per row. A data RAM access needs exactly one address base select.

## Sequencer

Hardwired cycle: run the `fetch` microprogram, then dispatch on the opcode in IR.
Dispatch costs zero cycles. Run that opcode's rows top to bottom, return to fetch. Microprograms are
straight lines: no branches, no loops, guaranteed termination. End of the row list
ends the instruction. An empty microprogram is legal. Cap: 16 rows per microprogram.
Assumed.

## Halt and crash rules

N is the number of assembled instructions. The PC check runs before each fetch.

| Condition | Result |
|---|---|
| PC = N | Halt, end of program. Covers fall-through and `JMP N`. Assumed. |
| PC > N | Crash: illegal program address. The jump that did it is highlighted. |
| HALT signal | Halt. |
| Illegal row executed | Crash: signal conflict. Frozen mid-cycle, conflicting signals lit. |
| Opcode without microprogram | Crash: no microcode. |
| Push below the stack bottom | Crash: stack overflow. |
| Pop above the stack top | Crash: stack underflow. |

No data address is illegal. An effective address wraps at 16 bits instead of
crashing. Halted: inspect freely, restart to run again. Crashed: inspect
freely, reset required. Reset clears registers, flags, latches, and the crash
state. RAM and stack keep their content. Restart resets, then loads the
`.ram` image as a visible load phase.

## Microcode file format

Plain text. One section per microprogram, signals comma separated, `#` comments.
Unknown signals, duplicate sections, and rows over the cap are parse errors. A missing
opcode section parses and crashes on execute. The `fetch` section is required.
Sections are named by syntax shape, mirroring the instruction table, for example
`LD A <- [D1]+`. The shipped files are generated from templates because D1 and D2
sections mirror each other.

## Shipped microcode

Two sets ship. `naive` is fully visible and editable. It is the reference. The
equivalence checker defines every other microcode as correct exactly when its net
architectural effect matches naive's.

`optimal` is sealed. It can be selected for running programs. Its per-opcode cycle
counts are public as the golf par. Its rows are not viewable. While it is selected,
microcycle stepping and the datapath animation are disabled, because watching signals
fire would leak the implementation. Improve on naive yourself. No
peeking.

Uploaded microcode is always fully visible, stepping included, even when it matches
the sealed optimal cycle for cycle. Writing it yourself is the unlock. The equivalence
proof naive = optimal for all 74 opcodes is a permanent test in the suite.

## Naive microcode by family

One architectural step per row. Rows that write from the ALU carry the operation
select, because rule 6 requires it. D2 variants mirror D1. Address selects are implied
by the section's addressing mode and omitted here for readability. The real files
spell them out.

```text
fetch:
  FETCH

NOP:
  PC_INC

LD A <- imm8:
  IMM_TO_B
  ALU_PASS_B, ACC_LOAD_ALU
  ALU_PASS_B, FLAGS_LOAD
  PC_INC

LD A <- [D1]+:
  RAM_TO_B
  D1_INC
  ALU_PASS_B, ACC_LOAD_ALU
  ALU_PASS_B, FLAGS_LOAD
  PC_INC

LD [D1]+ <- A:
  RAM_WRITE_ACC
  D1_INC
  PC_INC

LD D2 <- [D1]+:
  RAM_TO_D2H
  D1_INC
  RAM_TO_D2L, EA_CIN     # D1 already stepped once, CIN unused here
  D1_INC
  D2_TSTZ
  PC_INC

LD D1 <- imm16:
  D1_LOAD_OP16
  D1_TSTZ
  PC_INC

LD [D1] <- D2:
  RAM_WRITE_D2H
  RAM_WRITE_D2L, EA_CIN
  PC_INC

ADD A <- [addr8]:
  RAM_TO_B
  ACC_TO_A
  ALU_ADD, ACC_LOAD_ALU
  ALU_ADD, FLAGS_LOAD
  PC_INC

JMP t:
  PC_LOAD

JZ t:
  PC_INC
  PC_LOAD_Z

JNZ t:
  PC_INC
  PC_LOAD_NZ

JSR t:
  STK_WRITE_PCH
  SP_DEC
  STK_WRITE_PCL
  SP_DEC
  PC_LOAD

RET:
  SP_INC
  STK_TO_PCL
  SP_INC
  STK_TO_PCH

PUSHB A:
  STK_WRITE_ACC
  SP_DEC
  PC_INC

POPB A:
  SP_INC
  STK_TO_B
  ALU_PASS_B, ACC_LOAD_ALU
  ALU_PASS_B, FLAGS_LOAD
  PC_INC

PUSHW D1:
  STK_WRITE_D1H
  SP_DEC
  STK_WRITE_D1L
  SP_DEC
  PC_INC

POPW D1:
  SP_INC
  STK_TO_D1L
  SP_INC
  STK_TO_D1H
  D1_TSTZ
  PC_INC

INC D1:
  D1_INC
  PC_INC

OUT port, imm8:
  IO_WRITE_IMM
  PC_INC

HLT:
  HALT
```

The naive fetch is bare FETCH, so JSR's pushes would read the unstepped PC. The naive
JSR therefore places PC_INC before its pushes, so the pushed value equals PC+1:

```text
JSR t:
  PC_INC
  STK_WRITE_PCH
  SP_DEC
  STK_WRITE_PCL
  SP_DEC
  PC_LOAD
```

## Optimal microcode by family

Sealed in the product. Reproduced here as the developer reference and the golden test
source. `FETCH, PC_INC` is legal: the fetch reads at the old PC, the step commits at
end of cycle. One access, one step, one writer each.

```text
fetch:
  FETCH, PC_INC

NOP:

LD A <- imm8:
  IMM_TO_B
  ALU_PASS_B, ACC_LOAD_ALU, FLAGS_LOAD

LD A <- [D1]+:
  RAM_TO_B, D1_INC
  ALU_PASS_B, ACC_LOAD_ALU, FLAGS_LOAD

LD [D1]+ <- A:
  RAM_WRITE_ACC, D1_INC

LD D2 <- [D1]+:
  RAM_TO_D2H, D1_INC
  RAM_TO_D2L, D1_INC
  D2_TSTZ

LD D1 <- imm16:
  D1_LOAD_OP16
  D1_TSTZ

LD [D1] <- D2:
  RAM_WRITE_D2H
  RAM_WRITE_D2L, EA_CIN

ADD A <- [addr8]:
  RAM_TO_B, ACC_TO_A
  ALU_ADD, ACC_LOAD_ALU, FLAGS_LOAD

JMP t:
  PC_LOAD

JZ t:
  PC_LOAD_Z

JNZ t:
  PC_LOAD_NZ

JSR t:
  STK_WRITE_PCH, SP_DEC
  STK_WRITE_PCL, SP_DEC, PC_LOAD

RET:
  STK_TO_PCL, STK_CIN, SP_INC
  STK_TO_PCH, STK_CIN, SP_INC

PUSHB A:
  STK_WRITE_ACC, SP_DEC

POPB A:
  STK_TO_B, STK_CIN, SP_INC
  ALU_PASS_B, ACC_LOAD_ALU, FLAGS_LOAD

PUSHW D1:
  STK_WRITE_D1H, SP_DEC
  STK_WRITE_D1L, SP_DEC

POPW D1:
  STK_TO_D1L, STK_CIN, SP_INC
  STK_TO_D1H, STK_CIN, SP_INC
  D1_TSTZ

INC D1:
  D1_INC

OUT port, imm8:
  IO_WRITE_IMM

HLT:
  HALT
```

The JSR merge is the flagship trick. The second push reads the old PC when the cycle
starts. PC_LOAD commits the target when it ends. Legal under row semantics, and the
reason JSR reaches three cycles total.

## Cycle counts

Golden test values. Totals include fetch (1 cycle in both sets: naive bare FETCH,
optimal FETCH with PC_INC). Floor = the resource lower bound, the maximum count of any
single resource the instruction must use, plus forced dependency rows.

| Family | Naive | Optimal | Floor |
|---|---|---|---|
| NOP | 2 | 1 | 1 |
| LD A <- imm8 | 5 | 3 | 3 |
| LD A <- [addr8], [D1], [A], displacement, indexed | 5 | 3 | 3 |
| LD A <- [D1]+ and other post-inc byte loads | 6 | 3 | 3 |
| LD [addr8] <- A and plain byte stores | 3 | 2 | 2 |
| LD [D1]+ <- A post-inc byte stores | 4 | 2 | 2 |
| LD D1 <- imm16 | 4 | 3 | 3 |
| LD D1 <- [addr16] and word loads via pointers | 7 | 4 | 4 |
| LD [D1] <- D2 and word stores | 4 | 3 | 3 |
| LD [addr16] <- D1 | 4 | 3 | 3 |
| ADD, SUB, ADC, SBC, AND, OR, XOR (direct and immediate) | 6 | 3 | 3 |
| JMP | 2 | 2 | 2 |
| JZ, JC, JN, JV, JNZ, JNC, JP, JNV | 3 | 2 | 2 |
| JSR | 7 | 3 | 3 |
| RET | 5 | 3 | 3 |
| PUSHB A | 4 | 2 | 2 |
| POPB A | 6 | 3 | 3 |
| PUSHW D1, PUSHW D2 | 6 | 3 | 3 |
| POPW D1, POPW D2 | 7 | 4 | 4 |
| INC D1, INC D2 | 3 | 2 | 2 |
| OUT, OUTA, IN | 3 | 2 | 2 |
| HLT | 2 | 2 | 2 |

A change to either shipped microcode must update this table and the tests together.

## Golf scoreboard

The validation panel shows three numbers per opcode. Yours: your cycle count. Par:
the sealed optimal's count. Floor: the lower bound. The floor comes from resource
counting. It is labeled with >= because dependency chains can force the true optimum
above it. The shipped optimal meets the floor everywhere in the table above.

## Assembly file format

```asm
; sum a null-terminated list
        LD D1 <- head
loop:   LD A <- [D1]+       ; value byte
        ADD A <- [sum]
        LD [sum] <- A
        LD D1 <- [D1]       ; is an error: same-register word load
        LD D2 <- [D1]       ; next pointer, Z set when it is NULL
        JZ done
        ; continue from D2 next iteration
done:   HLT

.ram
sum:    db 0
head:   dw &node1
node1:  db 5, dw &node2
node2:  db 3, dw 0
```

The erroneous line is shown struck through here for teaching. The assembler rejects it.

- One instruction per line. Labels end with `:`. Code labels are instruction
  addresses. RAM labels are RAM addresses.
- `.ram` starts the RAM image. `db` emits bytes, `dw` emits big-endian words. `&label`
  is the address of a label. Bare labels in code operands mean the same. Allocation is
  sequential from 0x000. Over 1024 bytes is an error.
- The assembler emits a source map, both directions, driving breakpoints and the run
  highlight.

## Calling conventions

Two, taught in order.

Zero page convention: parameters in ACC and fixed zero-page cells, return value in
ACC. The stack carries return addresses and saved registers only.

Argblock convention: the caller pushes a pointer to an argument block, then JSR. The
callee juggles: `POPW D1` takes the return address as raw bits, `POPW D2` takes the
argument pointer, `PUSHW D1` restores the return address, then `LD A <- [D2]+` walks
the arguments. D1 briefly holds a program address numerically. Nothing dereferences
it. This is legal and deterministic.

## Breakpoints

Dev mode feature, not an opcode. Breakpoints are source lines mapped to instruction
addresses through the source map. The runtime checks PC against the set before each
fetch. A hit pauses with full state intact. Resume continues exactly. An empty set
costs nothing. Pausing never alters the trajectory, so determinism and termination
certificates are untouched. Planned later: microcode row breakpoints, RAM watchpoints.

## Project structure

- Toolchain: Vite, Vitest, TypeScript strict.
- Layout: `packages/core` holds the machine, microcode engine, assembler, equivalence
  checker, and access-heat arrays. Pure TypeScript, no DOM. `packages/ui` arrives with
  milestone 2 and talks to the core through a Web Worker.
- Pipeline: `npm test` must pass before `npm run build` produces a distribution.
- Development is test driven. Each component starts from its test file.

Test layers: row executor semantics, then the conflict detector, then ALU and flags.
Then the step unit sharing, the EA adder, and the microcode parser. Then the
assembler with its errors and source map. Then the equivalence checker, property
based against random microprograms. Then the crash taxonomy including stack faults.
Last: golden program runs with exact cycle counts, determinism replays, and the
naive = optimal proof for all 74 opcodes.

## Changes since v2 was written

The machine moved on. These lines are corrections, not new proposals.

- SP is 16 bit and the stack is 4096 bytes. A return address costs two of
  them, so nesting stops near 2048 rather than 128.
- Four opcodes at 0x04 to 0x07 jump and call through a D register, on the two
  signals PC_FROM_D1 and PC_FROM_D2. It is `JMP D1`, not `JMP [D1]`.
- Four opcodes at 0x79 to 0x7C jump when a flag is clear. They are JNZ, JNC,
  JP and JNV. Their signals are PC_LOAD_NZ, PC_LOAD_NC, PC_LOAD_NN and PC_LOAD_NV. The
  low nibble is the opcode of the jump each inverts. They cost what the set
  jumps cost. Without them a test that went the other way was a jump over a
  jump. The machine now has 84 opcodes and 30 mnemonics.
- Data RAM is 65536 bytes, not the 2048 this document assumed.
- The machine keeps four arrays over data RAM that no program can reach.
  `ramReadAt` and `ramWriteAt` hold cycle stamps, `ramReads` and `ramWrites`
  hold counts.

## Assumed defaults

Each of these is in effect unless vetoed.

1. Unified LD with the bracket rule.
2. SP points at the next free cell.
3. PC = N halts, including `JMP N`. PC > N crashes.
4. C on SUB means borrow occurred.
5. Byte loads set Z and N. Word loads into D set Z. IN sets nothing.
6. AND, OR, XOR leave C and V unchanged.
7. 16-row cap per microprogram.
8. FETCH is legal outside the fetch section. Deterministic, documented as a curiosity.
9. POPW sets Z from the popped value.
10. INC D1 and INC D2 set no flags.
11. Word loads into the address-providing register are assembler errors.
12. The golf scoreboard shows count, optimal par, and floor.
