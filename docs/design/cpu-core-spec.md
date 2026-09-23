# SimpleCPU core specification

Milestone 1 scope: the CPU, the microcode engine, and the assembler. Headless TypeScript
core, no DOM. This document is the review copy of the signal set and the file formats.
Items marked "review" need a decision from Eddie before implementation.

## Contents

- [Machine overview](#machine-overview)
- [Instruction format](#instruction-format)
- [Instruction set](#instruction-set)
- [Flags](#flags)
- [Control signals](#control-signals)
- [Row semantics](#row-semantics)
- [Conflict rules](#conflict-rules)
- [Sequencer](#sequencer)
- [Halt and crash rules](#halt-and-crash-rules)
- [Microcode file format](#microcode-file-format)
- [Naive microcode](#naive-microcode)
- [Optimal microcode](#optimal-microcode)
- [Cycle counts](#cycle-counts)
- [Assembly format](#assembly-format)
- [Breakpoints](#breakpoints)
- [Project structure](#project-structure)
- [Review points](#review-points)

## Machine overview

Accumulator machine with an 8-bit data path and a 16-bit program counter.

| Component | Width | Description |
|---|---|---|
| PC | 16 bit | Instruction counter. Counts instructions, not bytes. Has a private incrementer. |
| IR | 24 bit | Holds the fetched instruction: opcode byte plus 16-bit operand. |
| ACC | 8 bit | Accumulator. |
| A | 8 bit | ALU input latch, left operand. |
| B | 8 bit | ALU input latch, right operand. |
| FLAGS | 3 bit | Z, N, C. |
| Data RAM | 128 bytes | Addresses 0x00 to 0x7F. Address 0x80 and up crashes. |
| Program memory | 64K slots | One instruction per slot. Holds exactly the N assembled instructions. |

Program memory and data RAM are separate address spaces. The CPU cannot read or write
program memory. Programs cannot modify themselves.

## Instruction format

One program slot holds one instruction: an opcode byte and a 16-bit operand.
A fetch reads one whole slot in one cycle.

Operand layout by group:

| Group | Operand high byte | Operand low byte |
|---|---|---|
| Memory ops (LDA, STA, ADD, SUB, AND, OR, XOR) | 0 | RAM address, 0x00 to 0x7F |
| LDI | 0 | Immediate value |
| Jumps (JMP, JZ, JC) | Target, high | Target, low |
| OUT | Port number | Immediate data |
| OUTA, IN | Port number | 0 |
| NOP, HLT | 0 | 0 |

The assembler rejects a memory op whose address exceeds 0x7F.

## Instruction set

| Opcode | Mnemonic | Effect |
|---|---|---|
| 0x00 | NOP | Nothing. |
| 0x01 | LDA addr | `ACC <- RAM[addr]`. Sets Z, N. |
| 0x02 | LDI imm | `ACC <- imm`. Sets Z, N. |
| 0x03 | STA addr | `RAM[addr] <- ACC`. |
| 0x04 | ADD addr | `ACC <- ACC + RAM[addr]`. Sets Z, N, C. |
| 0x05 | SUB addr | `ACC <- ACC - RAM[addr]`. Sets Z, N, C. |
| 0x06 | AND addr | `ACC <- ACC & RAM[addr]`. Sets Z, N. |
| 0x07 | OR addr | `ACC <- ACC \| RAM[addr]`. Sets Z, N. |
| 0x08 | XOR addr | `ACC <- ACC ^ RAM[addr]`. Sets Z, N. |
| 0x09 | JMP target | `PC <- target`. |
| 0x0A | JZ target | `PC <- target` when Z is set. |
| 0x0B | JC target | `PC <- target` when C is set. |
| 0x0C | OUT port, imm | Write imm to the port. |
| 0x0D | OUTA port | Write ACC to the port. |
| 0x0E | IN port | `ACC <- port`. Flags unchanged. |
| 0x0F | HLT | Stop the clock. Graceful halt. |

The naive microcode defines the ISA semantics. The equivalence checker compares every
other microcode against it.

## Flags

| Flag | Meaning |
|---|---|
| Z | Result is zero. |
| N | Bit 7 of the result is set. |
| C | ADD: carry out of bit 7. SUB: borrow occurred. Review. |

Each ALU operation produces a flag mask. `FLAGS_LOAD` writes only the masked flags.

| ALU op | Writes |
|---|---|
| ADD, SUB | Z, N, C |
| AND, OR, XOR, PASS_B | Z, N. C is unchanged. |

## Control signals

A micro-instruction is a set of signals asserted in one cycle. `OPLO` is the operand low
byte, `OPHI` the high byte, `OP16` the full operand.

| Signal | Transfer | Resource |
|---|---|---|
| FETCH | `IR <- PROG[PC]` | Program memory |
| PC_INC | `PC <- PC + 1` | PC (write) |
| PC_LOAD | `PC <- OP16` | PC (write) |
| PC_LOAD_Z | `PC <- OP16` when Z is set | PC (write) |
| PC_LOAD_C | `PC <- OP16` when C is set | PC (write) |
| ACC_TO_A | `A <- ACC` | A (write) |
| RAM_TO_B | `B <- RAM[OPLO]` | Data RAM, B (write) |
| IMM_TO_B | `B <- OPLO` | B (write) |
| ACC_TO_RAM | `RAM[OPLO] <- ACC` | Data RAM |
| ALU_ADD | Select ALU operation `A + B` | ALU |
| ALU_SUB | Select ALU operation `A - B` | ALU |
| ALU_AND | Select ALU operation `A & B` | ALU |
| ALU_OR | Select ALU operation `A \| B` | ALU |
| ALU_XOR | Select ALU operation `A ^ B` | ALU |
| ALU_PASS_B | Select ALU operation `B` | ALU |
| ACC_LOAD_ALU | `ACC <- ALU result` | ACC (write) |
| FLAGS_LOAD | `FLAGS <- ALU flags`, masked per op | FLAGS (write) |
| IO_WRITE_IMM | `port[OPHI] <- OPLO` | IO |
| IO_WRITE_ACC | `port[OPHI] <- ACC` | IO |
| IO_READ | `ACC <- port[OPHI]` | IO, ACC (write) |
| HALT | Stop the clock at end of cycle | Sequencer |

`WAIT_FRAME` is reserved for the GPU milestone. It stalls the sequencer until the next
frame boundary.

## Row semantics

- Every read in a row sees the register values from the start of the cycle.
- Every write commits at the end of the cycle.
- The ALU is combinational inside the row. The selected op, applied to the start-of-cycle
  A and B, produces the result and flags within the same cycle.
- A flag-gated signal whose condition is false does nothing that cycle.

Consequence: `RAM_TO_B` and `ACC_TO_A` merge into one row. `ACC_LOAD_ALU` cannot merge
with the row that loads its inputs, because the ALU would see the old latches.

## Conflict rules

A row violating any rule is an illegal micro-instruction. The validation panel marks it.
Executing it crashes the CPU.

1. One writer per register per row. PC, ACC, A, B, and FLAGS each accept one writing
   signal. Flag-gated PC loads count as writers.
2. One data RAM access per row. `RAM_TO_B` and `ACC_TO_RAM` cannot share a row.
3. One program memory access per row.
4. At most one ALU op select per row.
5. `ACC_LOAD_ALU` and `FLAGS_LOAD` require exactly one ALU op select in the same row.
6. One IO signal per row.

## Sequencer

Hardwired cycle: run the `fetch` microprogram, dispatch on the opcode in IR, run that
opcode's microprogram top to bottom, return to `fetch`.

- Microprograms are straight lines. No branches, no loops, guaranteed termination.
- End of the row list ends the instruction. There is no END signal.
- An empty microprogram is legal. The instruction then costs only the fetch.
- Cap: 16 rows per microprogram. Review.

## Halt and crash rules

Let N be the number of assembled instructions. The check runs before each fetch.

| Condition | Result |
|---|---|
| PC = N | Graceful halt, "end of program". Applies to fall-through and to `JMP N`. Review. |
| PC > N | Crash: illegal program address. The instruction that set PC is highlighted. |
| HALT signal | Graceful halt. |
| Illegal row executed | Crash: signal conflict. Frozen mid-cycle, conflicting signals lit. |
| Opcode without microprogram | Crash: no microcode. |
| Data address above 0x7F | Crash: illegal data address. |

Halted state: inspect freely, restart to run again. Crashed state: inspect freely, reset
required before the machine runs again.

Reset clears registers, flags, latches, and the crash state. RAM keeps its content.
Restart performs a reset, then loads the `.ram` image as a visible load phase.

## Microcode file format

Plain text. One section per microprogram. Signals in a row are comma separated.
`#` starts a comment. Signal names are case insensitive, uppercase by convention.

```text
# fetch runs before every instruction
fetch:
  FETCH

ADD:
  RAM_TO_B
  ACC_TO_A
  ALU_ADD, ACC_LOAD_ALU
  ALU_ADD, FLAGS_LOAD
  PC_INC
```

Rules:

- Unknown signal names and duplicate sections are parse errors at upload.
- A missing opcode section parses fine. Executing that opcode crashes.
- Row count above the cap is a parse error.
- The `fetch` section is required.

## Naive microcode

One architectural step per row. Rows that write from the ALU carry the op select too,
because rule 5 requires it.

```text
fetch:
  FETCH

NOP:
  PC_INC

LDA:
  RAM_TO_B
  ALU_PASS_B, ACC_LOAD_ALU
  ALU_PASS_B, FLAGS_LOAD
  PC_INC

LDI:
  IMM_TO_B
  ALU_PASS_B, ACC_LOAD_ALU
  ALU_PASS_B, FLAGS_LOAD
  PC_INC

STA:
  ACC_TO_RAM
  PC_INC

ADD:
  RAM_TO_B
  ACC_TO_A
  ALU_ADD, ACC_LOAD_ALU
  ALU_ADD, FLAGS_LOAD
  PC_INC

SUB:
  RAM_TO_B
  ACC_TO_A
  ALU_SUB, ACC_LOAD_ALU
  ALU_SUB, FLAGS_LOAD
  PC_INC

AND:
  RAM_TO_B
  ACC_TO_A
  ALU_AND, ACC_LOAD_ALU
  ALU_AND, FLAGS_LOAD
  PC_INC

OR:
  RAM_TO_B
  ACC_TO_A
  ALU_OR, ACC_LOAD_ALU
  ALU_OR, FLAGS_LOAD
  PC_INC

XOR:
  RAM_TO_B
  ACC_TO_A
  ALU_XOR, ACC_LOAD_ALU
  ALU_XOR, FLAGS_LOAD
  PC_INC

JMP:
  PC_LOAD

JZ:
  PC_INC
  PC_LOAD_Z

JC:
  PC_INC
  PC_LOAD_C

OUT:
  IO_WRITE_IMM
  PC_INC

OUTA:
  IO_WRITE_ACC
  PC_INC

IN:
  IO_READ
  PC_INC

HLT:
  HALT
```

## Optimal microcode

`PC_INC` moves into fetch. The operand is safe in IR, and jumps overwrite PC afterward.
Independent transfers merge. ALU write and flag write merge with the op select.

```text
fetch:
  FETCH, PC_INC

NOP:

LDA:
  RAM_TO_B
  ALU_PASS_B, ACC_LOAD_ALU, FLAGS_LOAD

LDI:
  IMM_TO_B
  ALU_PASS_B, ACC_LOAD_ALU, FLAGS_LOAD

STA:
  ACC_TO_RAM

ADD:
  RAM_TO_B, ACC_TO_A
  ALU_ADD, ACC_LOAD_ALU, FLAGS_LOAD

SUB:
  RAM_TO_B, ACC_TO_A
  ALU_SUB, ACC_LOAD_ALU, FLAGS_LOAD

AND:
  RAM_TO_B, ACC_TO_A
  ALU_AND, ACC_LOAD_ALU, FLAGS_LOAD

OR:
  RAM_TO_B, ACC_TO_A
  ALU_OR, ACC_LOAD_ALU, FLAGS_LOAD

XOR:
  RAM_TO_B, ACC_TO_A
  ALU_XOR, ACC_LOAD_ALU, FLAGS_LOAD

JMP:
  PC_LOAD

JZ:
  PC_LOAD_Z

JC:
  PC_LOAD_C

OUT:
  IO_WRITE_IMM

OUTA:
  IO_WRITE_ACC

IN:
  IO_READ

HLT:
  HALT
```

`FETCH, PC_INC` is legal: FETCH reads program memory at the start-of-cycle PC, and
PC_INC commits at end of cycle. One memory access, one PC writer.

The equivalence checker must prove naive and optimal equivalent for all 16 opcodes.
That proof is a permanent test in the suite.

## Cycle counts

Totals include the fetch cycles.

| Instruction | Naive | Optimal |
|---|---|---|
| NOP | 2 | 1 |
| LDA, LDI | 5 | 3 |
| STA | 3 | 2 |
| ADD, SUB, AND, OR, XOR | 6 | 3 |
| JMP | 2 | 2 |
| JZ, JC | 3 | 2 |
| OUT, OUTA, IN | 3 | 2 |
| HLT | 2 | 2 |

These numbers are golden test values. A change to either shipped microcode must update
the table and the tests together.

## Assembly format

Milestone 1 syntax. The `.data` cartridge directives arrive in milestone 3.

```asm
; count down from 5, result lands in RAM
        LDI 5
loop:   SUB one
        JZ done
        JMP loop
done:   STA result
        HLT

.ram
one:     1
result:  0
```

- One instruction per line. `;` starts a comment.
- Labels end with `:`. Code labels resolve to instruction addresses. RAM labels resolve
  to RAM addresses.
- Numbers: decimal, `0x` hex, `0b` binary.
- `.ram` starts the RAM image. Bytes are allocated sequentially from 0x00. A label names
  the address of the byte that follows it. More than 128 bytes is an assembly error.
- The assembler emits a source map: source line to instruction address, both directions.
  The source map drives breakpoints and the run highlight.

## Breakpoints

A dev mode feature, not an opcode. The user sets breakpoints on source lines. The
runtime keeps a set of instruction addresses via the source map. Before each fetch, a PC
hit pauses the machine with full state intact. Resume continues exactly where it
stopped. Pausing never changes the trajectory. Determinism and certificates are
untouched. An empty breakpoint set costs nothing per cycle.

Planned later: microcode row breakpoints and watchpoints on RAM writes.

## Project structure

Presentation is separated from the core. The core is a pure TypeScript package with no
DOM access, so every component is unit testable. The UI package arrives with
milestone 2 and talks to the core through a Web Worker.

- Toolchain: Vite, Vitest, TypeScript strict mode.
- Layout: `packages/core` (machine, microcode engine, assembler, checkers),
  `packages/ui` (later). Tests live next to the code they test.
- Pipeline: `npm test` must pass before `npm run build` produces a distribution.
- Development is test driven: each component starts from its test file.

Test layers for milestone 1: row executor semantics, conflict detector, ALU and flags.
Then the assembler with its errors and source map, and the microcode parser. Then the
equivalence checker, property based against random microprograms. Finally the crash
taxonomy, golden program runs with exact cycle counts, and determinism replays.

## Review points

1. `PC = N` halts like HLT does, including an explicit `JMP N`. Confirm.
2. C flag on SUB means borrow occurred. The 6502 uses the opposite convention. Confirm.
3. Loads (LDA, LDI) set Z and N. Confirm.
4. AND, OR, XOR leave C unchanged. Confirm.
5. Row cap of 16 per microprogram. Confirm.
6. FETCH is legal outside the fetch section. It reloads IR mid-instruction, which is
   strange but deterministic. Forbid or allow?
7. IN leaves flags unchanged. A Z update here would help polling loops later. Confirm.
8. `.ram` allocates sequentially with no address directive. Enough for milestone 1?
