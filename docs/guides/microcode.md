# Microcode on SimpleCPU-8

A guide to the inside of the processor. It follows the BASIC guide at
docs/guides/basic.md, the C guide at docs/guides/c.md and the assembly
guide at docs/guides/assembly.md. Read the assembly guide first. It ends
with a cycle count it does not explain, and this guide starts there.
Every row printed here was taken from the shipped set or run on the
machine before it went in. The reference behind the guide is
docs/design/cpu-core-spec-v2.md, and the code is src/core.

## Contents

- [From instructions to cycles](#from-instructions-to-cycles)
- [The datapath](#the-datapath)
- [Row semantics](#row-semantics)
- [The control signals](#the-control-signals)
- [The eight conflict rules](#the-eight-conflict-rules)
- [Microprograms and the fetch](#microprograms-and-the-fetch)
- [The two shipped sets](#the-two-shipped-sets)
- [The seal](#the-seal)
- [The text format](#the-text-format)
- [A project with its own set](#a-project-with-its-own-set)
- [The Microcode level](#the-microcode-level)
- [A shorter add](#a-shorter-add)
- [The trap](#the-trap)
- [How a set is tested](#how-a-set-is-tested)
- [The sequencer's crashes](#the-sequencers-crashes)
- [Where to go next](#where-to-go-next)

## From instructions to cycles

The assembly guide traced a four line program and read 16 on the cycle
counter. The load cost 5, the add 6, the store 3 and the halt 2. Under
the optimal set the same program costs 10. Nothing about the program
changed. What changed is the list of steps the processor takes for each
instruction. That list is the microcode, and it is the subject here.

The processor has one clock. On every tick it does one thing, and that
thing is a row. A row is a set of control signals that fire together.
It is the smallest unit of work the machine has. So a tick of the
clock is called a microcycle. The cycle counter in the Registers pane counts
them. There is no other clock and no hidden time. An instruction costs
exactly as many cycles as it has rows, plus the rows of the fetch.

So an instruction is a short program of its own, written in signals
rather than in mnemonics. `ADD A <- 7` is five rows under the naive
set. The rows latch the constant, then latch the accumulator. They add
and capture the sum, add again and capture the flags, then step the
program counter. Each row is one cycle. A different set of rows that
ends in the same state is a faster `ADD`. A set of rows that ends
elsewhere is a broken one. Both are yours to write.

## The datapath

The datapath is the sixteen boxes the IDE draws on its Microcode level,
with the wires between them. src/ide/datapath.cpp holds the drawing and
the card behind each box. The boxes come in two kinds. A register
remembers its value across cycles. A combinational unit recomputes its
output from its inputs every cycle and stores nothing. The design rule
in docs/design/architecture.md is that nothing is duplicated: one ALU,
one step unit, one address adder.

The registers:

| Box | Bits | Holds |
|---|---|---|
| `PC` | 16 | the slot number of the next instruction to fetch |
| `IR` | 8 + 16 | the opcode and operand of the current instruction. `FETCH` writes it, nothing else does |
| `ACC` | 8 | the accumulator. This is the register assembly calls `A` |
| `A` | 8 | the ALU's left input latch, loaded from `ACC` |
| `B` | 8 | the ALU's right input latch, loaded from the operand, RAM or the stack |
| `D1`, `D2` | 16 | the pointer registers, in two halves of 8 bits each |
| `SP` | 16 | the stack pointer. Starts at `$0FFF` and names the next free cell |
| `FLAGS` | 4 | `N`, `V`, `Z` and `C` |

Two names collide here, and the collision matters for the whole guide.
In assembly, `A` is the accumulator. In microcode, `ACC` is the
accumulator and `A` is a latch in front of the ALU. The Registers pane
shows both: `A 05` is the accumulator and `latches A 00 B 05` are the
two latches. The assembly guide promised to explain those two. They are
where the ALU's inputs sit, and every arithmetic row reads them.

The combinational units:

| Box | Does |
|---|---|
| `ALU` | combines `A` and `B` under one selected operation. The result vanishes unless a signal captures it |
| `STEP` | adds or subtracts exactly one. `PC`, `SP`, `D1`, `D2` and `ACC` all share it, so one of them steps per cycle |
| `EA` | the effective address adder. Computes a base plus an optional offset plus an optional carry, fresh every cycle |
| `IO` | the port gateway. 256 ports, one byte at a time |

The memories:

| Box | Holds |
|---|---|
| `PROG` | 64K slots of three bytes. `FETCH` reads it, nothing writes it |
| `RAM` | 65536 bytes of data. Reached only through `EA` |
| `STACK` | 4096 bytes. Reached only through `SP` |

The wires are drawn as a schematic would draw them. A wire touches only
the boxes it joins. `PC` feeds `PROG`, and `PROG` feeds `IR`. `IR`
feeds `PC`, `EA`, `B`, `D1`, `D2` and `IO`, because the operand can be
a target, an address, a constant, a word or a port. `RAM` feeds `B`,
`D1` and `D2`, and `ACC`, `D1` and `D2` feed `RAM`. `STACK` feeds `B`,
`PC`, `D1` and `D2`. `ACC` feeds `A`, and `A` and `B` feed the `ALU`,
which feeds `ACC` and `FLAGS`. `STEP` feeds `PC`, `SP`, `D1` and `D2`. The drawing has no wire from
`STEP` to `ACC`, though `ACC_INC` lights both boxes. There is no wire from `D1` to `D2`, none from `RAM` to `ACC` and none
from `PROG` to anywhere but `IR`. Those absences are the instruction
set. A register to register move does not exist because no wire
carries it.

## Row semantics

Two rules from src/core/machine.cpp govern every row. Reads see the
values registers held at the start of the cycle. Writes commit at the
end of the cycle. The code collects the row's writes in a list while it
runs the signals, then applies the list. So a row that reads `PC` and
writes `PC` sees the old value and stores the new one. The order of
the signals in the row does not matter.

This is what makes a post-increment read fit one row. `RAM_TO_B,
ADDR_D1, D1_INC` reads RAM at the old `D1` and steps `D1` afterwards.
It is also what makes the flagship trick of the optimal set legal:
`STK_WRITE_PCL, SP_DEC, PC_LOAD` pushes the old `PC` and loads the new
one in the same cycle. The tests in tests/core/machine_test.cpp pin
both of these under the name `row semantics`.

The two combinational units work inside the row. The ALU computes from
the latches as they stood at the start of the cycle. If a row loads a
latch and reads the ALU together, the ALU sees the old latch. The
effective address is computed once per row, from the address signals in
that row. Every RAM signal in the row uses it.

## The control signals

src/core/signals.h lists every signal in one table. The table gives each
one the register atoms it writes and the memory it touches. It also
gives its use of the step unit, its ALU role, its IO role and its
address role. The conflict rules and the executor both read that table.
A new signal cannot exist in one and not the other. There are 71 signals. They are
grouped here by the unit they drive. `OP16` is the whole operand,
`OPLO` its low byte and `OPHI` its high byte.

Sequencing and the program counter:

| Signal | Does |
|---|---|
| `FETCH` | `IR <- PROG[PC]`. One program memory read |
| `PC_INC` | `PC <- PC + 1`, through the step unit |
| `PC_LOAD` | `PC <- OP16` |
| `PC_LOAD_Z`, `PC_LOAD_C`, `PC_LOAD_N`, `PC_LOAD_V` | `PC <- OP16` when that flag is set. When it is clear the row does nothing to `PC` |
| `PC_FROM_D1`, `PC_FROM_D2` | `PC <- D1` or `D2`, the register itself, not the byte it points at |
| `HALT` | stops the clock at the end of the cycle. The row's other writes still commit |

The ALU's input latches:

| Signal | Does |
|---|---|
| `ACC_TO_A` | `A <- ACC` |
| `IMM_TO_B` | `B <- OPLO` |
| `RAM_TO_B` | `B <- RAM[ea]`. One data RAM read |
| `STK_TO_B` | `B <- STACK[sa]`. One stack read |

The ALU:

| Signal | Does |
|---|---|
| `ALU_ADD`, `ALU_SUB`, `ALU_ADC`, `ALU_SBC` | select an arithmetic operation. All four produce `N`, `V`, `Z` and `C` |
| `ALU_AND`, `ALU_OR`, `ALU_XOR` | select a logic operation. These produce `N` and `Z` and leave `C` and `V` alone |
| `ALU_PASS_B` | select pass through: the result is `B`. Produces `N` and `Z`. This is the load path |
| `ACC_LOAD_ALU` | `ACC <- ALU result` |
| `FLAGS_LOAD` | `FLAGS <- ALU flags` |

A select alone does nothing. The result exists inside the row and
vanishes unless `ACC_LOAD_ALU` or `FLAGS_LOAD` captures it. `ALU_ADC`
adds `C` as it stood at the start of the row, and `ALU_SBC` subtracts it
as a borrow.

Data RAM:

| Signal | Does |
|---|---|
| `RAM_WRITE_ACC` | `RAM[ea] <- ACC` |
| `RAM_TO_D1H`, `RAM_TO_D1L`, `RAM_TO_D2H`, `RAM_TO_D2L` | one half of a pointer from `RAM[ea]` |
| `RAM_WRITE_D1H`, `RAM_WRITE_D1L`, `RAM_WRITE_D2H`, `RAM_WRITE_D2L` | `RAM[ea] <-` one half of a pointer |

Every one of these is one data RAM access, and every one needs the
address stage to say where. Words are two bytes, high byte first, so a
word transfer is two of these rows.

The address stage:

| Signal | Does |
|---|---|
| `ADDR_OP8` | base is `OPLO`, a zero page address |
| `ADDR_OP16` | base is `OP16` |
| `ADDR_D1`, `ADDR_D2` | base is the pointer register |
| `ADDR_A` | base is `ACC`, zero extended |
| `EA_OFF_OP8` | adds `OPLO` to the base |
| `EA_OFF_A` | adds `ACC` to the base |
| `EA_CIN` | adds one to the sum |

The effective address is the base plus the offsets, masked to 16 bits.
`[D1+n]` is `ADDR_D1, EA_OFF_OP8`. `[D1+A]` is `ADDR_D1, EA_OFF_A`.
`EA_CIN` reaches the second byte of a word without stepping any
register. These signals carry no action of their own. They shape the
address the RAM signals in the same row use.

The pointer registers and the step unit:

| Signal | Does |
|---|---|
| `D1_LOAD_OP16`, `D2_LOAD_OP16` | `D1 <- OP16` or `D2 <- OP16`, both halves at once |
| `D1_TSTZ`, `D2_TSTZ` | `Z <- (D1 == 0)` or `(D2 == 0)`, from the start of cycle value. The other flags keep their values |
| `D1_INC`, `D2_INC` | step a pointer by one |
| `ACC_INC` | step the accumulator by one, wrapping at 256 |
| `SP_INC`, `SP_DEC` | step the stack pointer up or down |

All five step signals go through the one `STEP` unit. So does
`PC_INC`. A test of a pointer after its load takes a separate row. The
test reads the start of cycle value.

The stack:

| Signal | Does |
|---|---|
| `STK_WRITE_ACC` | `STACK[sa] <- ACC` |
| `STK_WRITE_PCH`, `STK_WRITE_PCL` | `STACK[sa] <-` one half of `PC` |
| `STK_WRITE_D1H`, `STK_WRITE_D1L`, `STK_WRITE_D2H`, `STK_WRITE_D2L` | `STACK[sa] <-` one half of a pointer |
| `STK_TO_PCL`, `STK_TO_PCH` | one half of `PC` from `STACK[sa]` |
| `STK_TO_D1H`, `STK_TO_D1L`, `STK_TO_D2H`, `STK_TO_D2L` | one half of a pointer from `STACK[sa]` |
| `STK_CIN` | the stack address is `SP + 1` for this row |

The stack address `sa` is `SP`, or `SP + 1` when `STK_CIN` is in the
row. `SP` names the next free cell, so a push writes at `SP` and steps
down. The top byte of the stack is at `SP + 1`. `STK_CIN` is what lets
a pop read that byte and step `SP` up in one row.

The port gateway:

| Signal | Does |
|---|---|
| `IO_WRITE_IMM` | `port[OPHI] <- OPLO` |
| `IO_WRITE_ACC` | `port[OPHI] <- ACC` |
| `IO_READ` | `ACC <- port[OPHI]`. Sets no flags |
| `IO_READ_D1H`, `IO_READ_D1L`, `IO_READ_D2H`, `IO_READ_D2L` | one half of a pointer from `port[OPHI]` |

The port number is the high byte of the operand and the value is the
low byte. That is why `OUT GPU_CMD, CMD_CLEAR` fits one slot. A device
runs its command inside the same cycle as the write.

## The eight conflict rules

A row is a set of signals, and not every set is a row the machine can
run. src/core/conflicts.cpp holds `checkRow`, which tests a row against
eight rules and returns the number and message of the first rule
broken. The machine checks every row of a set once, when the set loads.
Running a row that breaks a rule crashes the machine with
`signal-conflict` and the message. The rules are checked in order, so a
row that breaks two reports the lower number.

Rule 1. One writer per register atom. The message names the pair:
`PC_INC and PC_LOAD both write PCL`. The atoms are `PCH`, `PCL`, `IR`,
`ACC`, `A`, `B`, `D1H`, `D1L`, `D2H`, `D2L`, `SP` and `FLAGS`. A
register is a set of bits, and two signals writing it in one cycle
would leave it holding neither value. Half writes exist, so the halves
of `PC`, `D1` and `D2` count apart. A flag gated load counts as a
writer even when its flag is clear, because the rule is about wiring,
not about this cycle's values. `D1_TSTZ` and `FLAGS_LOAD` collide,
because both write `FLAGS`.

Rule 2. One data RAM access per row: `more than one data RAM access`.
The RAM has one port. Two reads or a read and a write in one cycle
would need two.

Rule 3. One program memory access per row: `more than one program
memory access`. Only `FETCH` reads it, and two `FETCH` signals already
fall to rule 1 for writing `IR` twice.

Rule 4. One stack RAM access per row: `more than one stack RAM access`.
The stack, too, has one port.

Rule 5. At most one ALU operation select per row: `more than one ALU
operation select`. The ALU computes one function of its two inputs.
`ALU_ADD, ALU_SUB` asks for two.

Rule 6. An ALU write needs exactly one select: `ACC_LOAD_ALU needs
exactly one ALU operation select`. Without a select there is no result
to capture. The naive set carries the select on both capture rows for
this reason.

Rule 7. One step through the shared unit, `PC_INC` included: `D1_INC and
D2_INC both need the step unit`. There is one adder that adds or
subtracts one, and five registers queue for it. This is the rule that
shapes the whole optimization game. `PC_INC, SP_DEC` is refused, so a
push cannot step the program counter in its own row.

Rule 8. One IO signal, and a data RAM access needs exactly one address
base select. Three messages: `more than one IO signal`, `more than one
address base select` and `a data RAM access needs exactly one address
base select`. The port gateway moves one byte a cycle. The address
adder takes one base, and a RAM access without a base has no address.
`RAM_TO_B` alone is refused. `RAM_TO_B, ADDR_D1, ADDR_D2` is refused.

tests/core/conflicts_test.cpp has one case per rule. It also has three
rows that must pass: the optimal fetch, a post-increment read and the
`JSR` merge. That file is the shortest complete statement of what a legal
row is.

## Microprograms and the fetch

A microprogram is the rows of one instruction, run top to bottom. There
is no branch and no loop inside one, so every microprogram ends. A set
holds one microprogram per opcode, named by the instruction's shape,
plus one more named `fetch`. The sequencer in src/core/machine.cpp
runs like this. Run the `fetch` rows. Look at the opcode now in `IR`
and jump to its microprogram, which costs nothing. Run its rows. Count
one instruction and go back to `fetch`.

The cycle accounting follows at once. The cost of an instruction is the
number of rows in `fetch` plus the number of rows in its own section.
Both shipped sets have a one row fetch. So under the naive set `LD A
<- [addr8]` is 1 + 4 = 5, `ADD A <- [addr8]` is 1 + 5 = 6, `LD [addr8]
<- A` is 1 + 2 = 3 and `HLT` is 1 + 1 = 2. That is the 16 the assembly
guide read. Under the optimal set the same four are 3, 3, 2 and 2, and
that is the 10.

An empty microprogram is legal and ends the instruction at once. The
optimal `NOP` has no rows, so `NOP` costs the fetch alone, one cycle.
The cap is 16 rows per microprogram, `ROW_CAP` in src/core/mcparse.h.

The fetch is shared by every instruction, so a row saved there is saved
eighty times. The naive fetch is `FETCH` alone. Every naive instruction
then ends with its own `PC_INC` row. The optimal fetch is `FETCH,
PC_INC`. That row is legal: `FETCH` reads program memory at the old
`PC` and writes `IR`, while `PC_INC` writes `PC`. One program access,
one step, one writer per atom. With `PC` already stepped, no optimal
instruction needs a `PC_INC` row of its own.

The change has a price elsewhere. `JSR` pushes the return address, and
the return address is `PC + 1`. Under the naive fetch `PC` is still the
slot of the `JSR` itself, so the naive `JSR` steps it first. Under the
optimal fetch it already holds the right value. A fetch that steps `PC`
therefore needs a different `JSR`, and a different `JZ` with it.
Microcode is one set, not eighty independent programs.

## The two shipped sets

src/core/microcode.cpp builds both sets from family templates, because
every `D1` section has a `D2` twin. `buildNaive` is the reference set.
One architectural step per row, nothing shared, nothing merged. It is
the definition of what every instruction means. `buildOptimal` does the
same work in fewer rows. The design's rule in
docs/design/cpu-core-spec-v2.md is that any set is correct exactly when
its net effect matches naive's, instruction by instruction.

Five instructions, side by side. The naive rows are quoted from the
file `simplecpu-make` writes, and the optimal rows from the spec.

`LD A <- imm8`, 5 cycles against 3:

```text
LD A <- imm8:
  IMM_TO_B
  ALU_PASS_B, ACC_LOAD_ALU
  ALU_PASS_B, FLAGS_LOAD
  PC_INC
```

```text
LD A <- imm8:
  IMM_TO_B
  ALU_PASS_B, ACC_LOAD_ALU, FLAGS_LOAD
```

The two captures share a row. They write `ACC` and `FLAGS`, two
different atoms, from one select. The `PC_INC` row is gone because the
fetch did it.

`ADD A <- [addr8]`, 6 cycles against 3:

```text
ADD A <- [addr8]:
  RAM_TO_B, ADDR_OP8
  ACC_TO_A
  ALU_ADD, ACC_LOAD_ALU
  ALU_ADD, FLAGS_LOAD
  PC_INC
```

```text
ADD A <- [addr8]:
  RAM_TO_B, ADDR_OP8, ACC_TO_A
  ALU_ADD, ACC_LOAD_ALU, FLAGS_LOAD
```

The two latch loads share a row. `RAM_TO_B` writes `B` and `ACC_TO_A`
writes `A`. The add must wait for the next row, because the ALU reads
the latches as they stood when the row began.

`LD A <- [D1]+`, 6 cycles against 3:

```text
LD A <- [D1]+:
  RAM_TO_B, ADDR_D1
  D1_INC
  ALU_PASS_B, ACC_LOAD_ALU
  ALU_PASS_B, FLAGS_LOAD
  PC_INC
```

```text
LD A <- [D1]+:
  RAM_TO_B, ADDR_D1, D1_INC
  ALU_PASS_B, ACC_LOAD_ALU, FLAGS_LOAD
```

The read and the step share a row. The read uses the old `D1`, the
step commits at the end. This is row semantics doing the work.

`JSR`, 7 cycles against 3:

```text
JSR:
  PC_INC
  STK_WRITE_PCH
  SP_DEC
  STK_WRITE_PCL
  SP_DEC
  PC_LOAD
```

```text
JSR:
  STK_WRITE_PCH, SP_DEC
  STK_WRITE_PCL, SP_DEC, PC_LOAD
```

Each push writes at `SP` and steps `SP` down in the same row. The last
row also loads the target. The push reads the old `PC`, `PC_LOAD`
writes the new one, and rule 1 is satisfied because `STK_WRITE_PCL`
writes the stack, not `PC`. The spec calls this the flagship trick.

`RET`, 5 cycles against 3:

```text
RET:
  SP_INC
  STK_TO_PCL
  SP_INC
  STK_TO_PCH
```

```text
RET:
  STK_TO_PCL, STK_CIN, SP_INC
  STK_TO_PCH, STK_CIN, SP_INC
```

The top byte is at `SP + 1`. `STK_CIN` addresses it while `SP_INC`
steps the pointer, so the read and the step share a row twice.

The full table of costs is in the spec under `Cycle counts`. The IDE's
manual pane shows both counts beside every instruction. The spec
also gives a floor for each family, the count of the busiest resource
the instruction must use. The optimal set meets the floor everywhere.

## The seal

The optimal set is sealed. It can be selected from the Run menu and
raced against, and its cycle counts are public. Its rows are not shown.
While it is selected the IDE replaces the Flow pane and the row list
with one message. It disables the Microstep button and the trace speed.
The datapath lights from bus events alone. Its message reads: `The optimal set is
sealed. It runs and can be raced, never read. Select @naive or your own
set to trace, step and edit rows.`

docs/design/decisions.md gives the reason. The optimization game is the
point, and seeing the answer ends it. A project file carries the naive
set or your own, never the optimal one. A set you write is always fully
visible, even when it matches the sealed one cycle for cycle. Writing
it is the unlock.

## The text format

A set is a text file. src/core/mcparse.cpp reads it and
`serializeMicrocode` writes it. The rules:

- A section starts with its name and a colon on a line of its own. The
  name is `fetch` or an instruction shape as the spec spells it, such as
  `LD A <- [D1]+`.
- Each following line is one row: signal names separated by commas.
  Names are case insensitive. `fetch, pc_inc` is the optimal fetch.
- `#` starts a comment that runs to the end of the line.
- Blank lines are ignored.
- A section holds at most 16 rows.

The parser reports every error with its line number. The messages:

| Message | Cause |
|---|---|
| `unknown signal: PC_INK` | a name not in src/core/signals.h |
| `unknown instruction section: BOGUS OP` | a section name that is no instruction shape |
| `duplicate section: fetch` | a name used twice |
| `row outside a section` | a row before the first section line |
| `section exceeds the 16 row cap` | a seventeenth row |
| `the fetch section is required` | no `fetch` section |

A missing instruction section is not a parse error. The set loads, and
the machine crashes with `no-microcode` when that opcode is fetched. A
row that breaks a conflict rule is not a parse error either. The rules
are checked on load and the crash comes when the row runs. The parser
knows names, not wiring.

The shipped naive set in this format is 469 lines. Its sections come in
the order `buildNaive` sets them. Control and jumps come first, then
the byte loads, the `D1` block and the `D2` block. The arithmetic
families, the stack and the ports follow. The IDE's Microcode pane lists them in the same
order.

## A project with its own set

```bash
build/release/src/tools/simplecpu-make new mine --microcode
build/release/src/tools/simplecpu-make mine
build/release/src/vm/simplecpu --rom mine/build/mine.rom
```

The folder has `src/main.asm` and `src/microcode.txt`. The microcode
file starts as the naive set, written by `serializeMicrocode` with two
comment lines on top. The build reads it, checks it and writes it into
the ROM. A ROM carries its set, so `simplecpu` and `simplecpu-run` both
run the program under the rows you wrote. `--microcode naive` or
`--microcode optimal` on either command overrides the ROM's set.

The program in `main.asm` prints a table. It lists every instruction
with two costs. `MY` is the cost under the ROM's set and `OP` the cost
under the optimal set. The build counts the rows of each section plus the
rows of `fetch`, and writes the lines into a generated file,
`cycles.asm`, which it appends to the program. Eighty instructions fill
two pages of forty. A key press turns the page, and so do four seconds.
A fresh project shows `ADD A <- i8` at `6 3` on page two. Change a
row, build, run, and the `MY` number moves. The table is the truth
about the set the ROM carries, because the same parser counted it.

A parse error stops the build and names the line:

```text
mine/src/microcode.txt:345: unknown signal: PC_INK
```

examples/cycles is the same project, kept in the repository, so
`simplecpu-make examples/cycles` builds it without a `new`.

## The Microcode level

`./r --ide` opens the IDE, and F3 switches to the Microcode level. The
Listing and the Registers panes are the ones from the Run level. The
Registers pane has the buttons Run, Step, Microstep, Frame and Power on,
then the speed box. The first speed is `trace microcode`, three rows a
second, so every row is drawn. F10 runs one instruction. F11 runs one
microcycle. F5 runs and pauses. Shift+F5 powers on. The Registers pane
shows `latches A 00 B 05`, the `IR` contents, and a `bus` line naming
what the last memory access did.

The Datapath pane draws the sixteen boxes with their live values. The
`EA` box shows `----` until a row computes an address, then that
address. Hover a box and its card appears, with what it is and whether
it remembers or forgets. After a microcycle, the signals of the row
light the boxes and wires they touch. Each signal in the row has a
colour. The first signal touching a box colours it, and every later one
adds a dot on its top edge. Under the picture the row's signals are
listed in the same colours. A grey light marks the last bus event under
whatever the signals say. src/ide/datapath.cpp maps every signal to its
boxes and wires: `FETCH` lights `PROG`, `IR` and `PC`, `ACC_TO_A` lights
`ACC` and `A`, and an `ALU_` select lights `ALU`, `A` and `B`.

The Flow pane shows the instruction in progress as numbered rows. Line
`00` is the fetch, marked done with a `v` and naming the instruction it
fetched. Lines `01` to `N` are the instruction's rows. The row that ran
last is green with a `>`, the rows before it are dimmed. Step with F11
and the mark walks down.

The Microcode pane has three radio buttons: `@naive`, `@optimal` and
`my own`. Below them is the list of sections, with a `<` on the one
running, and the rows of the selected section with the live row in
green. `Edit rows...` opens the whole set as text in this pane. `Apply`
parses it and either lists the errors in red or loads the set and
restarts the machine. `Reset to naive` puts the naive text back.
Choosing `my own` for the first time starts from a copy of the naive
set. Swapping a set always restarts the machine, because a machine is
built from its set.

Breakpoints work here as on the Run level. Click the margin left of a
line in the Listing and run. The machine stops when `PC` reaches that
slot, before its fetch, and the Messages pane says `stopped at
breakpoint, PC 0005`. The Flow pane still shows the rows of the
instruction before, all marked done. The next F11 runs the fetch.

One thing to know about projects. The IDE's Project tab lists the
`.c`, `.h`, `.asm` and `.bas` files of a folder and its Build button
assembles them. Build keeps whichever set the IDE has selected. It does
not read `src/microcode.txt`. To step through the set a project
carries, open the built ROM: `simplecpu-ide mine/build/mine.rom`. The
listing header then reads `microcode: your own set` and the `my own`
radio is on. The Messages pane prints the whole set on load. The other
route is to paste the file's text into `Edit rows...` and apply it.

## A shorter add

The instruction is `ADD A <- imm8`, and the goal is the optimal count
of 3 without the optimal fetch. Lay out a project and open
`src/microcode.txt`. The section reads:

```text
ADD A <- imm8:
  IMM_TO_B
  ACC_TO_A
  ALU_ADD, ACC_LOAD_ALU
  ALU_ADD, FLAGS_LOAD
  PC_INC
```

Five rows plus one fetch row, and the table shows 6.

Step one. `IMM_TO_B` writes `B` and `ACC_TO_A` writes `A`. Neither
touches a memory, the ALU, the step unit or a port. Rule 1 sees two
different atoms. No other rule applies. Merge them:

```text
ADD A <- imm8:
  IMM_TO_B, ACC_TO_A
  ALU_ADD, ACC_LOAD_ALU
  ALU_ADD, FLAGS_LOAD
  PC_INC
```

Step two. The two capture rows both select `ALU_ADD`. One select in a
row satisfies rule 5, and both writes then satisfy rule 6. `ACC` and
`FLAGS` are different atoms. Merge them:

```text
ADD A <- imm8:
  IMM_TO_B, ACC_TO_A
  ALU_ADD, ACC_LOAD_ALU, FLAGS_LOAD
```

The `PC_INC` row is still needed under the naive fetch, and step two
dropped it. Put it back and ask where it fits. `PC_INC` writes `PC`
and uses the step unit. The second row writes `ACC` and `FLAGS` and
steps nothing. Rules 1 and 7 both pass:

```text
ADD A <- imm8:
  IMM_TO_B, ACC_TO_A
  ALU_ADD, ACC_LOAD_ALU, FLAGS_LOAD, PC_INC
```

Two rows. Build and run:

```bash
build/release/src/tools/simplecpu-make mine
build/release/src/vm/simplecpu --rom mine/build/mine.rom
```

Press a key for page two. `ADD A <- i8` now reads `3 3`. The other six
arithmetic families still read `6 3`, because each is its own section.
The same two edits apply to all fourteen arithmetic sections, and to
every byte load once `ACC_TO_A` is left out.

Check the result before trusting the count. Put a second project beside
the first, with the edited set copied in and a program that halts:

```asm
; Adds two constants under the project's own set.
        LD A <- 5
        ADD A <- 7
        LD [sum] <- A
        ADD A <- 1
        LD [sum+1] <- A
        HLT
.ram
sum:    db 0, 0
```

```bash
build/release/src/tools/simplecpu-make test
build/release/src/tools/simplecpu-run test/build/test.rom --ram 0 2
```

```text
status: halted
instructions: 5  cycles: 19
A=0d D1=0000 D2=0000 SP=0fff PC=0005 flags=----
0000: 0c 0d
```

12 and 13, the right answers, in 19 cycles. The same program under the
untouched naive set takes 25. Add `--microcode naive` to see it.

## The trap

Why stop at two rows? Put everything in one:

```text
ADD A <- imm8:
  IMM_TO_B, ACC_TO_A, ALU_ADD, ACC_LOAD_ALU, FLAGS_LOAD, PC_INC
```

Run it through the rules. Five writers on five different atoms. No
memory. One select, so rules 5 and 6 pass. One step. No port. The row
is legal, and the machine loads it without a word. Build and run the
test program:

```text
status: halted
instructions: 5  cycles: 17
A=0c D1=0000 D2=0000 SP=0fff PC=0005 flags=----
0000: 05 0c
```

The status is `halted`. The cycle count dropped to 17. The answers are
5 and 12, and both are wrong. `5 + 7` came out as 5 because the ALU read
the latches as they stood when the row began. `A` held 0 and `B` held
the 5 that the previous load had left there. The second add then
computed `5 + 7`, the operands of the instruction before it. Each `ADD`
adds what the last instruction latched.

This is the trap, and the conflict rules do not guard it. The rules say
which signals can share the wiring. They say nothing about whether the
values are the ones you meant. A set that breaks a rule crashes, and a
crash is honest. A set that keeps the rules and ends in the wrong state
runs to `halted`. It prints a wrong number with a straight face. Every
program on that machine lies, and nothing in the machine can tell. The
working notes have a rule that the machine never lies. A wrong set is
the one way to break it.

The same goes for a set that is right about `ADD` and wrong about a
flag. A byte load that forgets `FLAGS_LOAD` runs every program that
never tests `Z` after a load, and breaks every `JZ` that does. The
error shows up far from its cause, in a loop that never ends or a
branch that never falls through.

## How a set is tested

The naive set defines what each instruction means, so the test of any
other set is agreement with naive. tests/core holds the suite, and
`build/release/tests/core/sc8_core_tests` runs it. The core tests pass
56 cases and 8110 assertions in under a second. `-ltc` lists the case
names.

Three layers apply to a set. tests/core/conflicts_test.cpp checks the
rules, one case per rule. tests/core/microcode_test.cpp checks that both
shipped sets cover `fetch` and every opcode. It checks that every row
is legal and every section is under the cap. It also checks that the
naive set survives a round trip through the text format. tests/core/golden_test.cpp pins the
cycle count of every instruction under both sets, from the spec's
table. A change to either set that moves a number fails the build until
the table moves with it.

The layer that catches the trap is tests/core/differential_test.cpp. It
generates 250 random programs of 25 instructions from a list of thirty
shapes, with random operands kept inside RAM. It runs each program on a
machine with the naive set and on a machine with the optimal set, and
compares the two at the end. The comparison covers the status, the
crash kind, `ACC`, `D1`, `D2`, `SP`, the flags, every byte of RAM,
every byte of the stack and the log of every port write. A crash is
compared by kind and instruction count only, because a crash freezes
mid instruction, where the two sets differ by design. The generator is
seeded, so the 250 programs are the same on every run. The golden
programs in tests/core/golden_test.cpp add six hand written ones, a
countdown, a list sum, a table lookup, an argument block call, a tree
walk and an indirect call, each run under both sets.

A differential run of the one row `ADD` against naive fails on the
first program that adds. The differential idea is the one to keep. A set is right
when no program can tell it from naive. To apply it to your own set
without writing C++, run the same ROM twice:

```bash
build/release/src/tools/simplecpu-run test/build/test.rom --ram 0 16
build/release/src/tools/simplecpu-run test/build/test.rom --ram 0 16 --microcode naive
```

The first run uses the set in the ROM. The second overrides it with
naive. Everything after the `cycles:` figure must match: the
registers, the flags and every byte of RAM you dump. Write the program
so that it exercises the instruction you changed. Use values that
matter, such as a carry out and a zero result. The 25 example programs
in examples/ are larger tests. A game that plays the same under both
sets is strong evidence. A game that drifts is a set with a bug.

## The sequencer's crashes

The machine stops in one of two ways. `halted` is the program's own
choice, through `HLT` or by running off the end. `crashed` is the
machine refusing to go on, with a kind and a message. The IDE freezes
the state for inspection and Shift+F5 powers on again. `simplecpu-run`
prints the kind, the message and the slot of the instruction that did
it.

| Kind | Raised when | Message |
|---|---|---|
| `illegal-program-address` | a jump lands past the last slot | `PC 0x1234 is outside the program` |
| `illegal-program-address` | a jump lands in a gap left by `.org` | `PC 0x1234 is a slot nothing was loaded into` |
| `no-microcode` | the set has no `fetch` section | `no fetch microprogram` |
| `no-microcode` | the fetched opcode has no section | `no microprogram for opcode 0x48` |
| `signal-conflict` | a row breaks a rule | `rule 1: ACC_LOAD_ALU and ACC_INC both write ACC` |
| `stack-overflow` | `SP_DEC` runs with `SP` at 0 | `SP_DEC below the stack bottom` |
| `stack-underflow` | `SP_INC` runs with `SP` at `$0FFF` | `SP_INC above the stack top` |

The program counter checks run before each fetch. A `PC` equal to the
number of instructions halts, which is how falling off the end works.
A `PC` beyond it crashes. The stack checks run inside the row, so the
crash names the row and the signal. The conflict check runs when a set
loads, and the crash comes when the illegal row would have executed. A
set with an illegal row in `INW D2` runs every program that never reads
a word from a port.

One curiosity from the spec. `FETCH` is legal outside the `fetch`
section. A `FETCH` placed in an instruction reads the slot `PC` names
and replaces `IR`. The bounds check does not run for it, so past the
end it reads a `NOP`.

## Where to go next

docs/design/cpu-core-spec-v2.md is the whole design. It has the floor
for every family, the row semantics and the crash table.
docs/design/architecture.md is the shorter statement of the same
machine. src/core/signals.h is the signal table itself. It is the place
to learn what a signal writes. src/core/microcode.cpp is both shipped
sets in C++. The optimal set is there for a developer who wants to read
it. The game is better played without.

The next set to write is the whole thing. Take the fetch to `FETCH,
PC_INC`, drop every `PC_INC` row, fix `JSR`, `JSR D1` and `JSR D2` to
push the stepped `PC`, and merge what row semantics allows. The cycles
table shows the gap closing, family by family. The differential run
under `simplecpu-run` says whether the machine still tells the truth.
When every `MY` number equals its `OP` number, the two sets do the same
work in the same time, and you wrote one of them.
