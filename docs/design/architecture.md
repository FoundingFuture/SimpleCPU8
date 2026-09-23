# SimpleCPU-8 architecture

The machine as designed, from registers down to control signals. The project
notes in the repository root hold the working knowledge base. The design itself lives
here. docs/decisions.md records why each choice won.

## Contents

- [Model](#model)
- [Registers](#registers)
- [Memories](#memories)
- [Timing](#timing)
- [Datapath components](#datapath-components)
- [Control signals](#control-signals)
- [Conflict rules](#conflict-rules)
- [The sequencer](#the-sequencer)
- [GPU architecture](#gpu-architecture)
- [Simulation split](#simulation-split)

## Model

An 8 bit accumulator machine with two programmable levels. Assembly defines
what a program does. Microcode defines what each instruction does, one row of
control signals per cycle. The naive microcode set is open and defines
instruction semantics. The optimal set is sealed and exists to race against.

Two structural rules shape everything below. No duplicated intelligence: one
ALU, one step unit, one address adder. Registers remember, everything else
forgets: combinational units recompute from their inputs every cycle and
store nothing.

## Registers

| Register | Width | Role |
|---|---|---|
| ACC | 8 | The accumulator. The only architectural byte register. |
| PC | 16 | Program counter, in instructions, not bytes. |
| D1, D2 | 16 | Pointer registers. Big-endian halves, loadable separately. |
| SP | 16 | Stack pointer. Next free slot, starts at $0FFF, grows down. |
| IR | 8+16 | Opcode and operand of the current instruction. Set by FETCH only. |
| A, B | 8 | ALU input latches. Microcode-visible, not architectural. |
| FLAGS | 4 | N V Z C, displayed in that order. Persists across instructions. |

The A and B latches persist across rows. Load once, run more ALU rows.
FLAGS persisting is what chains ADD into ADC for wide arithmetic.

## Memories

| Memory | Size | Access |
|---|---|---|
| Program | 64K slots x 3 bytes | Read by FETCH only. Never readable as data. |
| Data RAM | 65536 bytes | Through the EA adder only. Zero page is the first 256. |
| Stack | 4096 bytes | Through SP only. Push, pop, JSR, RET. |
| Cartridge | up to 1MB | GPU and audio chip only. Built from the .data section. |

Words are big-endian everywhere: high byte at the lower address. No data
address is illegal. An effective address wraps at 16 bits, so a pointer
walked off the end wraps to zero. A jump past the instruction count still
crashes, and so do stack overflow and underflow. A crash freezes the machine
with the guilty instruction highlighted, for inspection.

## Timing

Cycles are the only clock. One microcode row runs per cycle. Reads see the
values from the start of the cycle. Writes commit at the end. One row can
therefore push the old PC and load the new one, which is how JSR works.
The GPU frame counter is the cycle counter shifted right 16, so one frame is
exactly 65536 cycles at every speed setting.

## Datapath components

Sixteen boxes in the schema, each documented in
packages/ui/src/componentdocs.ts with hover and click-through cards in the
app. The combinational ones:

- ALU: combines latches A and B under one selected operation. The result
  evaporates unless ACC_LOAD_ALU or FLAGS_LOAD captures it.
- EA adder: computes the effective address from a base, an optional offset,
  and an optional carry-in, fresh every cycle. See docs/lessons.md for the
  full walkthrough. There is no address register anywhere.
- STEP: the shared plus-or-minus-one unit. PC, SP, D1, D2, and ACC all use
  this one incrementer, one customer per cycle.
- IO: the port gateway. 256 ports, GPU on $00 to $1F.

## Control signals

Signal groups, defined in packages/ui/src/dpschema.ts with one-line meanings:

- Fetch and PC: FETCH, PC_INC, PC_LOAD and its four conditional forms,
  PC_FROM_D1 and PC_FROM_D2 for the indirect jump, HALT.
- ALU input latches: ACC_TO_A, IMM_TO_B, RAM_TO_B, STK_TO_B.
- ALU: eight operation selects plus ACC_LOAD_ALU and FLAGS_LOAD.
- Addressing: five base selects, two offsets, EA_CIN.
- Data RAM: RAM_WRITE_ACC plus D register half transfers in both directions.
- D registers and steps: loads, zero tests, and the five step signals.
- Stack: writes and reads of ACC, PC halves, D halves, plus STK_CIN.
- IO: IO_WRITE_IMM, IO_WRITE_ACC, IO_READ.

Signals may cooperate on one unit when no rule forbids it. The EA adder
composes base plus offset plus carry from separate signals in one row.

## Conflict rules

From packages/core/src/conflicts.ts. A row breaking any rule crashes the
machine on that row:

1. One writer per register.
2. One data RAM access.
3. One program memory access.
4. One stack RAM access.
5. One ALU operation select.
6. An ALU write needs exactly one operation select.
7. One step unit user per cycle.
8. One IO signal, one address base select, and a RAM access needs a base.

## The sequencer

Fetch is its own microcode entry, not row zero of an instruction. The machine
runs fetch rows first. It dispatches on the opcode for free, then runs the
instruction's rows. The naive fetch is one FETCH row. The optimal fetch adds
PC_INC in the same row. That saves a cycle on every instruction, because the
step unit is idle during fetch.

The UI presents fetch as an immutable preamble numbered 00 above every
instruction. That is display truth: the drawing simplifies, the simulation
does not. docs/decisions.md records the debate.

## GPU architecture

A second machine beside the CPU. The CPU never touches a pixel. It writes
bytes to ports, and the GPU watches the ports and paints. 256x256 pixels,
one byte each, through a 256 entry RGB palette, default 3-3-2.

Internals worth knowing:

- Latches: X, Y with sticky HI bytes for signed 16 bit coordinates, COLOR,
  ARG, palette index, cartridge address, sprite selector, scroll.
- Commands execute on a write to GPU_CMD. Drawing, palette rotation,
  backbuffer save and restore, sprites, STAMP, and transformed paths.
- A SIN ROM, Int8 with 256 steps per turn, drives path rotation with
  fixed point shifts. Paths are normalized bytes centered on 128.
  CMD_SET_SCALE maps the byte range to a pixel size. 3D paths project with
  the eye at z equal to -512.
- Reset leaves the GPU alone because the screen is not inside the CPU.
  Restart and program load power it on fresh.
- The frame counter derives from cycles. Fast-frame mode can force it
  forward. That mode is a debug switch on the person's side of the glass.

The full port and command tables live in docs/gpu-ports.md and in the app
manual's GPU reference section. Both derive from the constants in
packages/core/src/gpu.ts.

## Input

The controller is a second device on the bus, in packages/core/src/input.ts.
It claims two ports and forwards the rest, the same pattern as the GPU.

- IO_CONTROLLER at port $20 is one byte of seven game buttons. Up, down,
  left, right, fire, space, enter, one bit each. The read reports the level,
  so a held button stays set.
- IO_KEY at port $21 pops one key event from a device buffer. The low seven
  bits are the key code. The top bit is the release flag. A read of zero
  means the buffer is empty. Printable keys are uppercase ASCII, and the
  buffer holds a burst of fast typing until the program reads it.

Buttons and the key buffer are hardware state. They survive load and restart,
like the screen. The UI drives both from the keyboard. See docs/decisions.md
for why input is a bus device.

## Simulation split

The core simulates in a web worker. packages/ui/src/session.ts wraps the
machine and GPU headless, and tests drive it directly. The worker adds run
pacing, breakpoint stops, frame advance, and display frames that only ship
when the screen changed. The UI thread renders and never simulates.
