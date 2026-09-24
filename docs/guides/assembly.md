# Assembly on SimpleCPU-8

A guide to programming the machine in its own instructions. It follows
the BASIC guide at docs/guides/basic.md and the C guide at
docs/guides/c.md. If you have never written a program, start with BASIC.
If you have never built a project, read the C guide's first two chapters.
Every program printed here was assembled and run on the machine before
it went in. The reference behind this guide is the IDE's manual pane,
which shows the same flag facts and port tables. The microcode inside
each instruction is the next guide's subject.

## Contents

- [From C to the machine](#from-c-to-the-machine)
- [The first project](#the-first-project)
- [The Run level](#the-run-level)
- [A first program, traced](#a-first-program-traced)
- [The registers](#the-registers)
- [The flags](#the-flags)
- [The memories](#the-memories)
- [The instructions](#the-instructions)
- [Addressing forms](#addressing-forms)
- [The stack and subroutines](#the-stack-and-subroutines)
- [Frames and drawing](#frames-and-drawing)
- [Reading the pad](#reading-the-pad)
- [A data table](#a-data-table)
- [The assembler](#the-assembler)
- [Ports](#ports)
- [The GPU](#the-gpu)
- [A sprite](#a-sprite)
- [Input](#input)
- [The audio chip](#the-audio-chip)
- [The coprocessor](#the-coprocessor)
- [Storage](#storage)
- [How C becomes assembly](#how-c-becomes-assembly)
- [A routine for BASIC](#a-routine-for-basic)
- [A small game](#a-small-game)
- [Where to go next](#where-to-go-next)

## From C to the machine

The C compiler turned your program into instructions. This guide is about
writing those instructions yourself. There is no compiler between you
and the processor, so every byte moved is a line you wrote. The reward
is that nothing is hidden. The cost is that a multiply is a loop, and a
line of C is ten lines here.

The processor has one byte register for arithmetic, two pointer
registers, a stack and four flags. It has 80 instructions. Everything
else is a chip on the bus, reached through `OUT` and `IN`. The chips are
the ones the C libraries covered. They are the GPU, the input device,
the audio chip, the coprocessor and storage. In C a chip was a header. Here
it is a port number and a command byte, and both have names the
assembler knows.

## The first project

`simplecpu-make` lays out an assembly project the way it laid out a C
one:

```bash
build/release/src/tools/simplecpu-make new mine --assembly
build/release/src/tools/simplecpu-make mine
build/release/src/vm/simplecpu --rom mine/build/mine.rom
```

The folder has `src/main.asm`, an empty `assets/` folder and a README
whose first line is the cartridge title. Every `.asm` in `src/` goes
into the ROM in name order, with `main.asm` first. The first program is
a bouncing dot that leaves a coloured trail. Run it before you change
it.

A single file works too. `simplecpu-asm prog.asm -o prog.rom` writes a
cartridge, and `simplecpu-run prog.rom` runs it with no window and
prints the registers at the end. That is the tool this guide uses for
programs that halt. `--ram 0 16` adds a dump of 16 bytes of RAM from
address 0.

## The Run level

`./r --ide` opens the IDE. `simplecpu-ide mine/src/main.asm` opens a
file into its Source tab, and the Project tab opens a folder and lists
its files. F7 assembles the source and the Messages pane reports the
count: `assembled: 4 instructions, 3 bytes of .ram, 0
bytes of .data`. An error names the line instead.

F3 switches to the Run level. Six panes surround the screen.

The Listing shows the source with a column of numbers on the left. For
an instruction line the column holds its slot number, then the opcode
byte and the operand word. For a `.ram` line it holds the RAM address
the label names. The line the processor will run next is green.

The Registers pane shows `PC`, `A`, `D1`, `D2` and `SP`, then the four
flags and the two latches the microcode guide will explain. Under them
are the cycle count, the instruction count and the frame count. The
last line is the bus: what the last memory access read or wrote.

Run and Pause are one button, also F5. Step, also F10, runs one
instruction. Frame, F6, runs until the GPU's frame counter ticks. Power
on, Shift+F5, reloads the program and its RAM image. The speed box
starts at a trace of single microcycles. It climbs to 100000
instructions a second, then to frame rates, then to the fastest the
host allows.

The Memory pane shows 256 bytes from an address you type. The byte the
last bus event touched is lit. Click a label in the Listing and it joins
the watch list at the top of the pane. The list shows each label's byte
and its word.

Click the margin left of a line and a red dot appears. That is a
breakpoint. Run stops when the processor reaches that instruction,
with every register intact, and Run again carries on from there.

## A first program, traced

Put this in a file called `first.asm`:

```asm
; Adds two numbers and keeps the sum.
        LD A <- [x]
        ADD A <- [y]
        LD [sum] <- A
        HLT
.ram
x:      db 5
y:      db 7
sum:    db 0
```

A line is a label, an instruction, a comment, or a mix of them. A label
ends in a colon. A comment starts with a semicolon and runs to the end
of the line. `.ram` starts the part of the file that describes memory.
`db 5` places one byte holding 5, and the label in front of it names
that address. So `x` is address 0, `y` is 1 and `sum` is 2.

`LD` is the one instruction that moves data. The arrow points at the
destination. Square brackets mean the contents of an address. So `LD A
<- [x]` reads the byte at address 0 into the register `A`. `ADD A <-
[y]` adds the byte at address 1 to it. `LD [sum] <- A` writes `A` to
address 2. `HLT` stops the clock.

Open it in the IDE and press F3, then Step four times. After the first
step `A` shows `05`, the bus line says `ram read 0000 <- 05`, and the
first byte in the Memory pane is lit. The flags stay dark, because 5 is
not zero and its top bit is clear. After the second step `A` shows `0C`,
which is 12. After the third the byte at address 2 turns to `0C`. After
the fourth the status says halted.

The cycle count reads 16. An assembly project runs on the naive
microcode set. Under it the load costs 5 cycles, the add 6, the store 3
and the halt 2. The optimal set does the same work in 10. Both sets are
selectable from the Run menu, and the microcode guide is about the
difference.

The same program without the window:

```bash
build/release/src/tools/simplecpu-asm first.asm -o first.rom
build/release/src/tools/simplecpu-run first.rom --ram 0 3
```

```text
status: halted
instructions: 3  cycles: 16
A=0c D1=0000 D2=0000 SP=0fff PC=0003 flags=----
0000: 05 07 0c
```

`HLT` stops the clock before the instruction counter reaches it, so
three instructions are counted. `PC` is 3, the slot of the `HLT`.

## The registers

| Register | Bits | Holds |
|---|---|---|
| `A` | 8 | the accumulator. Every byte load, every sum and every port read lands here |
| `D1`, `D2` | 16 | pointers into data RAM. A word load fills one, and `[D1]` reads through it |
| `PC` | 16 | the slot number of the next instruction |
| `SP` | 16 | the next free cell of the stack. Starts at `$0FFF` and grows down |
| flags | 4 | `N`, `Z`, `C` and `V`, set by arithmetic and by loads |

`A` is the only register the arithmetic instructions touch. To add two
bytes in memory, load one into `A`, add the other, store `A`. There is
no instruction that moves a register to a register. `LD D1 <- D2` is
refused with `register to register moves do not exist`.

`D1` and `D2` are the reach into memory beyond the first 256 bytes. A
byte instruction can name an address only up to 255. Above that, a
pointer register holds the address and the instruction says `[D1]`.

## The flags

| Flag | Set when |
|---|---|
| `Z` | the result is zero. For a word load, when the whole 16 bit value is zero |
| `N` | bit 7 of the byte result is set, which is the sign bit of a signed byte |
| `C` | an add carried out of bit 7, or a subtract needed a borrow |
| `V` | a signed add or subtract overflowed, so the sign of the result is wrong |

Three short programs show them. Run each with `simplecpu-run` and read
the last line.

```asm
        LD A <- 200
        SUB A <- 201
        HLT
```

```text
A=ff D1=0000 D2=0000 SP=0fff PC=0002 flags=N--C
```

200 minus 201 needs a borrow, so `C` is set, and the result 255 has its
top bit set, so `N` is set.

```asm
        LD A <- 100
        ADD A <- 100
        HLT
```

```text
A=c8 D1=0000 D2=0000 SP=0fff PC=0002 flags=NV--
```

200 fits in a byte, so no carry. Read as signed bytes, 100 plus 100
cannot be minus 56, so `V` says the signed answer is wrong.

```asm
        LD A <- 255
        ADD A <- 1
        HLT
```

```text
A=00 D1=0000 D2=0000 SP=0fff PC=0002 flags=--ZC
```

The sum wrapped to zero and carried. `Z` and `C` together mean the byte
overflowed to exactly zero.

The four jump instructions read one flag each. `JZ` jumps when `Z` is
set, `JC` on `C`, `JN` on `N` and `JV` on `V`. There is no jump on a
clear flag, so a loop tests for the set case and jumps over. To compare
`A` with a value, subtract the value. `Z` then means equal and `C`
means `A` was smaller.

A byte load into `A` sets `Z` and `N` from the byte. A word load into
`D1` or `D2` sets `Z` only. Stores and pointer steps set nothing. A port
read into `A` sets nothing either, so test the value with `AND A <-
$FF` before a `JZ`.

## The memories

The machine has four memories, and a program sees them in four ways.

Data RAM is 65536 bytes, addresses `$0000` to `$FFFF`. The `.ram`
section of a program is loaded into it at power on, from address 0
upwards. The first 256 bytes are the zero page. A byte instruction can
name a zero page address directly, in one byte of operand. `LD A <-
[counter]` works when `counter` is under 256, and the assembler refuses
it otherwise with `byte direct addressing is zero page only`. The
arithmetic instructions take only a zero page address or a constant. So
the variables a program uses most go in the zero page, and everything
else is reached through a pointer.

The text screen lives at `$FAC0` when the GPU is in text mode, 42 by
32 bytes. That is the block BASIC's `POKE 64192,65` wrote to. In
graphics mode, which is the power on mode, those bytes are free RAM. No
data address is illegal. An address past `$FFFF` wraps to zero, so a
pointer walked off the end reads address 0.

The stack is a separate memory of 4096 bytes. Only `SP` reaches it. A
push writes at `SP` and moves `SP` down. A pop moves `SP` up and reads.
`JSR` pushes the return address and `RET` pops it. There is no
instruction that reads the stack at an address, so a program cannot
peek into it. Pushing when it is full crashes with `stack-overflow`.
Popping when it is empty crashes with `stack-underflow`.

Program memory holds the instructions. Each is one slot of three bytes,
an opcode and a 16 bit operand, and `PC` counts slots. The processor
fetches from it and can do nothing else with it. A program cannot read
its own code as data or change it. A jump to the slot after the last
instruction halts, the way falling off the end does. A jump further
crashes with `illegal-program-address`.

The cartridge holds up to 1MB of read only data, built from the `.data`
section. The processor cannot read it at all. The GPU and the audio chip
read it directly, so it is where pictures, sprites, sounds and printf
templates live. To bring cartridge bytes into RAM, the GPU's `CMD_COPY`
moves them.

## The instructions

The 80 opcodes fall into six families. One mnemonic covers every load
and store. So the surface is 26 mnemonics, plus four spellings that
map one to one onto them. Each table lists the syntax, what the
instruction does and the flags it writes. The IDE's manual pane shows
the same facts. It adds the cycle count of each under both microcode
sets.

Control and jumps:

| Syntax | Does | Flags |
|---|---|---|
| `NOP` | nothing | none |
| `HLT` | stops the clock | none |
| `JMP target` | `PC <- target` | none |
| `JZ target` | jumps when `Z` is set | none |
| `JC target` | jumps when `C` is set | none |
| `JN target` | jumps when `N` is set | none |
| `JV target` | jumps when `V` is set | none |
| `JMP D1`, `JMP D2` | `PC <- D1` or `D2`, the jump table instruction | none |
| `JSR target` | pushes the next slot number, then jumps | none |
| `JSR D1`, `JSR D2` | the same through a register | none |
| `RET` | pops two bytes into `PC` | none |

A target is a code label or a slot number. `JMP [D1]` is refused,
because brackets mean contents-of and `[D1]` is one byte in RAM. The
register is written bare.

Byte loads and stores, all through `A`:

| Syntax | Does | Flags |
|---|---|---|
| `LD A <- 5` | a constant | `Z N` |
| `LD A <- [addr8]` | the byte at a zero page address | `Z N` |
| `LD A <- [A]` | the byte at the zero page address in `A` | `Z N` |
| `LD A <- [D1]` | the byte `D1` points at | `Z N` |
| `LD A <- [D1]+` | the same, then `D1` steps by one | `Z N` |
| `LD A <- [D1+n]` | the byte `n` past `D1` | `Z N` |
| `LD A <- [D1+A]` | the byte `A` past `D1` | `Z N` |
| `LD [addr8] <- A` | store to a zero page address | none |
| `LD [D1] <- A` | store where `D1` points | none |
| `LD [D1]+ <- A` | store, then step `D1` | none |
| `LD [D1+n] <- A` | store `n` past `D1` | none |

Every form with `D1` exists with `D2` as well. The arrow may point
either way. `LD [D1] -> A` is the same instruction as `LD A <- [D1]`.

Word loads and stores, through `D1` and `D2`:

| Syntax | Does | Flags |
|---|---|---|
| `LD D1 <- 1000` | a constant, or the address of a label | `Z` |
| `LD D1 <- [addr16]` | the word at any address | `Z` |
| `LD D1 <- [D2]` | the word `D2` points at | `Z` |
| `LD D1 <- [D2]+` | the same, then `D2` steps by two | `Z` |
| `LD D1 <- [A]` | the word at the zero page address in `A` | `Z` |
| `LD D1 <- [A]+` | the same, then `A` steps by two | `Z` |
| `LD D1 <- [D2+n]` | the word `n` past `D2` | `Z` |
| `LD D1 <- [D2+A]` | the word `A` past `D2` | `Z` |
| `LD [addr16] <- D1` | store a word at any address | none |
| `LD [D2] <- D1` | store where `D2` points | none |
| `LD [D2+n] <- D1` | store `n` past `D2` | none |

A word is two bytes, high byte at the lower address. The register
outside the brackets decides the width. So `[D1]+` steps by one after
a byte and by two after a word. A word load into the register that
holds the address is refused, with the message `corrupts its own
address`. The first byte written would spoil the address before the
second byte was read.

Arithmetic and logic, all into `A`:

| Syntax | Does | Flags |
|---|---|---|
| `ADD A <- v` | `A <- A + v` | `N V Z C` |
| `SUB A <- v` | `A <- A - v` | `N V Z C` |
| `ADC A <- v` | `A <- A + v + C` | `N V Z C` |
| `SBC A <- v` | `A <- A - v - C` | `N V Z C` |
| `AND A <- v` | bitwise and | `N Z` |
| `OR A <- v` | bitwise or | `N Z` |
| `XOR A <- v` | bitwise exclusive or | `N Z` |
| `INC A` | `ADD A <- 1`, the same opcode | `N V Z C` |
| `DEC A` | `SUB A <- 1`, the same opcode | `N V Z C` |
| `INC D1`, `INC D2` | steps a pointer by one | none |

`v` is a constant or a zero page address in brackets. There is no
multiply, no divide, no shift and no `DEC D1`. A multiply is a loop of
adds, or a command to the coprocessor. `ADC` and `SBC` chain bytes into
wider numbers: add the low bytes with `ADD`, then each higher byte with
`ADC`. Loads write only `Z` and `N`, so the carry survives the load and
store between the two.

Stack:

| Syntax | Does | Flags |
|---|---|---|
| `PUSH A` | writes `A` to the stack | none |
| `POP A` | reads the top byte into `A` | `N Z` |
| `PUSH D1`, `PUSH D2` | writes a word, high byte first | none |
| `POP D1`, `POP D2` | reads a word | `Z` |

`PUSHB`, `POPB`, `PUSHW` and `POPW` are the opcode names, and the
assembler accepts them too. So does `LD [SP]- <- A` for a push and `LD
A <- [SP]+` for a pop, the same opcodes in the load spelling.

Ports:

| Syntax | Does | Flags |
|---|---|---|
| `OUT port, value` | writes a constant byte to a port | none |
| `OUTA port` | writes `A` to a port | none |
| `OUT A -> port` | the same instruction as `OUTA` | none |
| `IN port -> A` | reads a byte from a port into `A` | none |
| `IN port` | the same, `A` being the default | none |
| `IN port -> D1` | reads two bytes from one port, high first | `Z` |

`INB` and `INW` are the opcode names. A port and a value both take the
names from the port tables, so `OUT GPU_CMD, CMD_CLEAR` assembles with
no number in sight.

## Addressing forms

Six shapes appear inside the brackets, and every one is an address
computed at run time from what is written.

| Written | Address |
|---|---|
| `[label]` or `[$1C]` | the number itself. A byte instruction needs it under 256 |
| `[D1]` | the value in `D1` |
| `[D1]+` | the value in `D1`, and `D1` steps afterwards |
| `[D1+7]` | `D1` plus a constant from 0 to 255 |
| `[D1+A]` | `D1` plus the value in `A` |
| `[A]` | the value in `A`, a zero page address |

Without brackets a name means its value. `LD D1 <- table` puts the
address of `table` in `D1`. `LD A <- [table]` reads the byte stored
there. An `&` in front, as in `dw &node`, means the same as no `&` and
is kept for readers who like to see it.

The displacement form is how a record is read. Point `D1` at a record
of three bytes, and `[D1]`, `[D1+1]` and `[D1+2]` are its fields. The
indexed form is how an array is read. Point `D1` at the array, put the
index in `A`, and `LD A <- [D1+A]` is the element. There is no indexed
store, because `A` would be both the index and the byte to write. Store
through a stepped pointer instead.

Round brackets never dereference. They group arithmetic, as in `LD A <-
(2 + 3) * 4`, and hold a call's argument, as in `get_lowbyte(pic)`.

## The stack and subroutines

`JSR label` pushes the slot number of the next instruction, high byte
first, and jumps to the label. `RET` pops those two bytes into `PC`.
The stack is 4096 bytes, so a program can nest about 2000 calls before
`stack-overflow`, and a `RET` with no `JSR` before it crashes at once.

A subroutine needs an agreement with its callers. It says where the
inputs go, where the answer comes back, and what it may spoil. The
machine fixes none of this, so the program must. This guide's routines use one
convention and say so in a comment above each one. Inputs go in `A`,
`D1` or named zero page cells. A byte answer comes back in `A`. The
comment lists the registers the routine changes. A caller that needs a
register the routine changes pushes it before the call and pops it
after.

```asm
; Adds three 16 bit numbers with one subroutine.
        LD D1 <- &n1
        JSR add16
        LD D1 <- &n2
        JSR add16
        LD D1 <- &n3
        JSR add16
        HLT

; add16: adds the word D1 points at to the word at total.
; In: D1. Out: total. Uses: A, the flags. Keeps: D1, D2.
add16:  LD A <- [D1+1]                ; low byte first
        ADD A <- [total+1]
        LD [total+1] <- A
        LD A <- [D1]                  ; then the high byte, with the carry
        ADC A <- [total]
        LD [total] <- A
        RET

.ram
total:  dw 0
n1:     dw 1000
n2:     dw 2000
n3:     dw 40000
```

```text
status: halted
instructions: 27  cycles: 134
A=a7 D1=0006 D2=0000 SP=0fff PC=0006 flags=N---
0000: a7 f8 03 e8 07 d0 9c 40
```

`dw` places a word, high byte first, so `n1` is the bytes `03 e8`. The
total `a7 f8` is 43000. Read the routine as the schoolbook sum: the low
bytes first, then the high bytes with the carry from the low ones.
`total+1` in brackets is an expression the assembler folds to the
address of the low byte.

Set a breakpoint on the `RET` and run. `SP` reads `0FFD`, two below its
start, and the Memory pane cannot show why: the stack is not in RAM.
Step once and `SP` is back at `0FFF`.

## Frames and drawing

The GPU counts frames at port `GPU_FRAME`. One frame is 65536
processor cycles, and the counter wraps at 256. A program that draws
once a frame reads the counter and waits for it to change. That is the
whole of animation on this machine.

```asm
; A disc that crosses the screen, one pixel a frame.
loop:   OUT GPU_COLOR, $03            ; blue
        OUT GPU_CMD, CMD_CLEAR
        OUT GPU_COLOR, $FC            ; yellow
        OUT GPU_CMD, CMD_SET_COLOR
        LD A <- [x]
        OUTA GPU_X
        OUT GPU_Y, 128
        OUT GPU_CMD, CMD_MOVE_TO
        OUT GPU_RADIUS, 20
        OUT GPU_CMD, CMD_CIRCLE
        LD A <- [x]
        INC A
        LD [x] <- A
wait:   IN GPU_FRAME
        SUB A <- [frame]
        JZ wait
        IN GPU_FRAME
        LD [frame] <- A
        JMP loop
.ram
x:      db 0
frame:  db 0
```

Put it in `mine/src/main.asm`, build and run. A yellow disc slides
across a blue screen, once round in a little over four seconds.

Every GPU command is the same shape. Write the arguments to the data
ports, then write the command byte to `GPU_CMD`. `GPU_COLOR`, `GPU_X`,
`GPU_Y` and `GPU_RADIUS` are names for the data ports, chosen to say
what the next command reads there. The data ports clear after every
command, so nothing carries over from the command before. A coordinate
is a signed 16 bit number in two ports, and the high byte reads as zero
when a program writes only the low one.

The wait loop reads the counter until it differs from the saved copy.
Then it saves the new value. `SUB A <- [frame]` sets `Z` when the two
are equal. `JZ wait` then goes round again. Under the naive microcode the
loop body costs 12 cycles, so the program polls about 5000 times a
frame. A program that draws more does the same wait at the end of its
frame. Everything it drew then shows at once.

The colour byte is the 3-3-2 palette: three bits of red, three of
green, two of blue. `$FC` is red and green full, blue off, which is
yellow. `$03` is blue alone.

## Reading the pad

The input device is on ports `$20` to `$22`. `IO_CONTROLLER` is one
byte of seven buttons, one bit each, and reading it costs nothing. A
held button stays set. The names `BTN_UP`, `BTN_DOWN`, `BTN_LEFT`,
`BTN_RIGHT`, `BTN_FIRE`, `BTN_SPACE` and `BTN_ENTER` are the bits, so
`AND A <- BTN_LEFT` leaves zero unless left is held.

```asm
; A disc steered with the arrow keys.
loop:   IN A <- IO_CONTROLLER
        LD [pad] <- A
        AND A <- BTN_LEFT
        JZ noleft
        LD A <- [x]
        DEC A
        LD [x] <- A
noleft: LD A <- [pad]
        AND A <- BTN_RIGHT
        JZ noright
        LD A <- [x]
        INC A
        LD [x] <- A
noright: LD A <- [pad]
        AND A <- BTN_UP
        JZ noup
        LD A <- [y]
        DEC A
        LD [y] <- A
noup:   LD A <- [pad]
        AND A <- BTN_DOWN
        JZ nodown
        LD A <- [y]
        INC A
        LD [y] <- A
nodown: OUT GPU_COLOR, 0
        OUT GPU_CMD, CMD_CLEAR
        LD A <- [pad]
        AND A <- BTN_FIRE
        JZ calm
        OUT GPU_COLOR, $E0            ; red while fire is held
        JMP paint
calm:   OUT GPU_COLOR, $FC            ; yellow otherwise
paint:  OUT GPU_CMD, CMD_SET_COLOR
        LD A <- [x]
        OUTA GPU_X
        LD A <- [y]
        OUTA GPU_Y
        OUT GPU_CMD, CMD_MOVE_TO
        OUT GPU_RADIUS, 10
        OUT GPU_CMD, CMD_CIRCLE
wait:   IN GPU_FRAME
        SUB A <- [frame]
        JZ wait
        IN GPU_FRAME
        LD [frame] <- A
        JMP loop
.ram
x:      db 128
y:      db 128
pad:    db 0
frame:  db 0
```

The pad byte is read once and kept in `pad`, because each `AND` spoils
`A`. Each test is the same four lines. Reload the byte and mask one
bit. Skip when zero, or change a coordinate. The arrows and W, A, S, D both
count. Fire is Z or Ctrl. The disc leaves the screen at one edge and
comes back at the other. A byte wraps, and the GPU clips.

`IO_KEY` is the other half of the keyboard. Each read pops one key
event, or zero when none is waiting. The low seven bits are the key
code, capitals for letters, 13 for Enter and 27 for Escape. The top bit
is set for a release. `IO_MODS` reads the modifier keys as a level.

## A data table

A table is bytes in `.ram` under one label, walked with a stepped
pointer. This one draws a shape from a list of points.

```asm
; Draws a shape from a table of points.
        OUT GPU_COLOR, $10            ; dark green paper
        OUT GPU_CMD, CMD_CLEAR
        OUT GPU_COLOR, $FF
        OUT GPU_CMD, CMD_SET_COLOR
        LD D1 <- &shape
        LD A <- [D1]+                 ; the count leads the table
        LD [left] <- A
        LD A <- [D1]+                 ; the first point starts the pen
        OUTA GPU_X
        LD A <- [D1]+
        OUTA GPU_Y
        OUT GPU_CMD, CMD_MOVE_TO
next:   LD A <- [left]
        DEC A
        LD [left] <- A
        JZ done
        LD A <- [D1]+
        OUTA GPU_X
        LD A <- [D1]+
        OUTA GPU_Y
        OUT GPU_CMD, CMD_LINE_TO
        JMP next
done:   HLT

.ram
left:   db 0
shape:  db 6                          ; six points, x then y
        db 128, 40
        db 180, 200
        db 50, 100
        db 206, 100
        db 76, 200
        db 128, 40
```

A white star on green, then `halted` in the corner. `LD D1 <- &shape`
loads the address of the table. Each `LD A <- [D1]+` takes the next
byte and moves on, so the loop never computes an address. The count is
kept in the zero page because `DEC A` needs `A`, and `A` is busy with
the bytes. Add a point to the table and raise the count, and the shape
grows without a change to the code.

## The assembler

A source file has three sections. `.code` is where a file starts, and
holds instructions. `.ram` holds the bytes loaded into data RAM at
power on. `.data` holds the bytes burned into the cartridge. Each
keyword can appear again, and the section carries on where it left
off. A second file appended to a first adds to all three.

Labels end with a colon. In `.code` a label is a slot number. In `.ram`
it is a RAM address. In `.data` it is a cartridge address. A jump to a
RAM label is refused with `not a code label`. A byte instruction that
names a data label is refused with `not a RAM label`. A label defined
twice gives `duplicate label`, and a label never defined gives
`undefined label`. Mnemonics are case insensitive. Labels are not.

The data directives:

| Line | Places |
|---|---|
| `db 5, 6, 7` | bytes |
| `db "text", 0` | the characters, then a zero. Escapes: `\n`, `\t`, `\r`, `\0`, `\\`, `\"`, `\xHH` |
| `dw 1000, &label` | words, high byte first |
| `ds 64` | 64 zero bytes |
| `name: .addr($10)` | a `.ram` label at a chosen address, placing nothing |
| `.org 4096` | in `.code`, the next instruction goes in slot 4096 |
| `.equ SHIP, 3` | names a number, placing nothing |

`db` and `dw` switch the width for the rest of the line, so `db 5, dw
&next` places a byte and a word. A `.org` leaves a gap of unloaded
slots, and fetching one crashes. That is how a driver sits at a known
slot apart from the program that calls it. examples/basic-driver is
one, at slot `$F000`.

A number is decimal, hex with `$` or `0x`, or binary with `0b`.
Anywhere a number goes, an expression goes, with C's operators and C's
precedence: `+ - * / % & | ^ ~ << >>` and round brackets. The
assembler folds it before a byte is emitted, so `LD D2 <- $FAC0 + 20 *
42` costs the same as a constant. A label in an expression contributes
its address. `&&` and `||` are refused by name. A count that decides
addresses, in `ds`, `.org`, `.addr` and a port operand, must fold in
the first pass, so it cannot hold a label.

A value that does not fit is an error, with the number in the message:
`value 300 does not fit in a byte`. `.equ NAME, expr` names a number
used in many places. The expression folds where it is written, so it
can hold numbers and registry names but not a label. The name then
goes anywhere a number goes, a port and a count included. A `.equ`
name defined twice, or defined as a label as well, gives `duplicate
label`.

Five macros split a value into the bytes a port wants. `get_lowbyte(x)`
is the low byte, `get_highbyte(x)` the next and `get_bankbyte(x)` bits
16 to 19, which is the cartridge bank. They take any expression.
`get_sizelo(x)` and `get_sizehi(x)` give the byte count of a blob a
directive placed, and refuse anything else.

Four directives place a file from `assets/` in `.data`:

| Line | Places |
|---|---|
| `.file('level.bin')` | the bytes as they are |
| `.image('pic.png')` | the picture's pixels, one palette index per pixel, row by row |
| `.palette('pic.png')` | its 768 palette bytes |
| `.sample('ping.wav')` | the sound as 8 bit mono at 8000 a second |

The label in front binds to where the blob landed. Equal bytes are
stored once, so two labels on the same file share one copy. `.image`
places the pixels alone, with no size in front. The GPU's sprite and
image commands read a header first. Write it with `db` on the line
before, as the sprite chapter shows.

Every port name, command name, button bit and mode number lives in one
registry. The devices publish it and the assembler reads it. The same
names are C constants in the machine headers. Every name resolves
anywhere a number does, in a port operand, an immediate, a data byte or
an expression. `ACP_ADDR_HI + 1` is `ACP_ADDR_LO`, `LD A <- CMD_CLEAR`
loads the command's number, and `CMD_CLEAR + 1` works after the comma.
The IDE's manual pane lists them all by device.

## Ports

`OUT` and `IN` are the whole of the machine's contact with the outside.
A port is a number from 0 to 255. The five devices each claim a range:

| Ports | Device |
|---|---|
| `$00` to `$1F` | the GPU |
| `$20` to `$22` | input |
| `$30` to `$3F` | the audio chip |
| `$40` to `$4F` | the coprocessor |
| `$50` to `$5F` | storage |

A write to a port takes one instruction and lands at once. A device
that runs a command does it inside the same cycle, so a program never
waits for a device. The one exception is the audio chip, which plays in
real time on its own clock.

## The GPU

The GPU owns the 256 by 256 screen, 256 sprites, the character plane
and the cartridge reader. It has eleven ports:

| Port | Name | Meaning |
|---|---|---|
| `$00` | `GPU_CMD` | write a command byte to run it |
| `$01` | `GPU_CMD_MOD` | modifier bits for the commands that follow |
| `$02` to `$08` | `GPU_DATA0` to `GPU_DATA6` | arguments in, results out |
| `$1E` | `GPU_RAND` | read a random byte, or write a seed |
| `$1F` | `GPU_FRAME` | the frame counter |

The data ports mean whatever the running command says. The alias names
say what each command reads there. `GPU_COLOR`, `GPU_RADIUS`,
`GPU_SPRITE` and `GPU_X_HI` are all port `$02`. `GPU_X` is `$03`,
`GPU_Y_HI` is `$04` and `GPU_Y` is `$05`. A command that takes an index
puts it in `GPU_DATA0` and everything else moves up one, which is why
`GPU_SPRITE_X_HI` is `$03` where `GPU_X_HI` is `$02`. The manual pane
lists every alias against its port.

The commands, by family:

| Family | Commands |
|---|---|
| drawing | `CMD_CLEAR`, `CMD_SET_COLOR`, `CMD_MOVE_TO`, `CMD_LINE_TO`, `CMD_RECT`, `CMD_CIRCLE`, `CMD_RING`, `CMD_PLOT`, `CMD_READ_PIXEL`, `CMD_SAVE_SCREEN`, `CMD_RESTORE_SCREEN` |
| palette | `CMD_RESET_PALETTE`, `CMD_LOAD_PALETTE`, `CMD_STORE_PALETTE`, `CMD_FETCH_PALETTE`, the four `CMD_ROTATE_` commands |
| sprites | `CMD_SPRITE_DEF`, `CMD_SPRITE_MOVE`, `CMD_SPRITE_SHOW`, `CMD_SPRITE_HIDE`, `CMD_SPRITE_FRAME`, `CMD_SPRITE_FLIP`, `CMD_STAMP`, `CMD_BLIT` |
| collision | `CMD_HIT_TEST`, `CMD_HIT_SCAN`, `CMD_COLLIDE_ALL`, `CMD_SPRITE_HITS`, `CMD_GROUP_HITS`, `CMD_HIT_IN_GROUP`, `CMD_COLLIDE_GROUP_ALL` |
| transforms | `CMD_ROT_X`, `CMD_ROT_Y`, `CMD_ROT_Z`, `CMD_SET_SCALE`, `CMD_DRAW_PATH`, `CMD_DRAW_PATH3D` |
| text | `CMD_SET_TEXTMODE`, `CMD_SET_GRAPHICSMODE`, `CMD_TEXT_STYLE`, `CMD_TEXT_AT`, `CMD_TEXT_CHAR`, `CMD_TEXT_CLEAR`, `CMD_PRINTF`, `CMD_LOAD_FONT` |
| the 3D world | `CMD_SET_WORLDMODE`, `CMD_MESH_LOAD`, `CMD_MESH_WRITE`, `CMD_WORLD_RAMP`, `CMD_MATRIX_MAP` |
| memory | `CMD_COPY`, `CMD_MEMMAP`, `CMD_RAM_MOVE` |

The drawing commands work from a pen. `CMD_MOVE_TO` puts it at `GPU_X`,
`GPU_Y`. `CMD_LINE_TO` draws to a point and moves the pen there.
`CMD_RECT` fills from the pen to the point. `CMD_CIRCLE` fills a disc
of `GPU_RADIUS` around the pen and `CMD_RING` draws its outline.
`CMD_PLOT` sets one pixel in the colour at `GPU_PIXEL`. `CMD_CLEAR`
fills the screen with `GPU_COLOR` and leaves the pen's colour alone.
`CMD_READ_PIXEL` answers on `GPU_DATA0`, and a result survives the
clearing that wipes the arguments.

The data ports clear after every command. `MOD_STICKY` written to
`GPU_CMD_MOD` keeps them instead, and `MOD_INC0` steps `GPU_DATA0` by
one after each command. Both hold until the modifier is written again.

The character plane is 42 columns by 32 rows in a 6 by 8 font, drawn
over the pixels. `CMD_TEXT_STYLE` takes the foreground colour, the
background colour and style bits. `CMD_TEXT_AT` moves the cursor to a
column and a row. `CMD_TEXT_CHAR` draws one character there.
`CMD_PRINTF` takes a template in the cartridge and arguments in RAM.
Point `GPU_CART_BANK`, `GPU_CART_HI` and `GPU_CART_LO` at the template,
and `GPU_TEXT_ARG_HI` and `GPU_TEXT_ARG_LO` at the arguments. The
template follows C: `%hhu` takes one byte, `%d` and `%u` two, high byte
first, `%s` a two byte pointer to a string in RAM. The game at the end
prints its score this way.

`CMD_SET_TEXTMODE` switches the screen to read characters from RAM at
the address in `GPU_ADDR_HI` and `GPU_ADDR_LO`. BASIC runs in that
mode with the buffer at `$FAC0`, so a store to `$FAC0` shows a
character at the top left. `CMD_SET_GRAPHICSMODE` switches back.

`CMD_COPY` moves bytes from the cartridge into RAM: the cartridge
address in `GPU_CART_BANK`, `GPU_CART_HI` and `GPU_CART_LO`, then
`GPU_DEST_HI`, `GPU_DEST_LO`, `GPU_LEN_HI` and `GPU_LEN_LO`. That is
the only way cartridge bytes reach the processor. `CMD_RAM_MOVE` copies
RAM to RAM with the same ports and the bank left out, and the two
blocks may overlap.

`IN GPU_RAND -> A` gives a random byte and `IN GPU_RAND -> D2` a random
word. Write a byte to `GPU_RAND` first and the sequence repeats from
that seed on every run.

docs/design/gpu-ports.md is the full reference, with the argument ports
of every command.

## A sprite

A sprite is a picture of up to 64 by 64 pixels that the GPU draws over
the screen every frame. Moving one costs one command and disturbs
nothing under it. Its pixels live in the cartridge as a blob. The blob
holds the frame count, the width, the height, then the pixels of each
frame in turn. Colour 0 is transparent.

```asm
; A two frame sprite that walks across the screen.
        OUT GPU_COLOR, $03
        OUT GPU_CMD, CMD_CLEAR
        OUT GPU_SPRITE, 1
        OUT GPU_SRC_BANK, get_bankbyte(bug)
        OUT GPU_SRC_HI, get_highbyte(bug)
        OUT GPU_SRC_LO, get_lowbyte(bug)
        OUT GPU_CMD, CMD_SPRITE_DEF
        OUT GPU_SPRITE, 1
        OUT GPU_CMD, CMD_SPRITE_SHOW
loop:   IN GPU_FRAME
        LD [frame] <- A
        OUT GPU_SPRITE, 1
        OUTA GPU_SPRITE_X             ; x is the frame count, so it walks
        OUT GPU_SPRITE_Y, 120
        OUT GPU_CMD, CMD_SPRITE_MOVE
        LD A <- [frame]
        AND A <- 8                    ; bit 3: swap frames every 8 frames
        JZ still
        LD A <- 1
still:  OUT GPU_SPRITE, 1
        OUTA GPU_SPRITE_FRAME
        OUT GPU_CMD, CMD_SPRITE_FRAME
wait:   IN GPU_FRAME
        SUB A <- [frame]
        JZ wait
        JMP loop
.ram
frame:  db 0
.data
; frames, width, height, then the pixels of each frame. 0 is transparent.
bug:    db 2, 8, 8
        db $00,$00,$FC,$FC,$FC,$FC,$00,$00
        db $00,$FC,$FC,$FC,$FC,$FC,$FC,$00
        db $FC,$FC,$00,$FC,$FC,$00,$FC,$FC
        db $FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC
        db $00,$FC,$FC,$FC,$FC,$FC,$FC,$00
        db $00,$00,$FC,$FC,$FC,$FC,$00,$00
        db $00,$FC,$00,$00,$00,$00,$FC,$00
        db $FC,$00,$00,$00,$00,$00,$00,$FC
        db $00,$00,$FC,$FC,$FC,$FC,$00,$00
        db $00,$FC,$FC,$FC,$FC,$FC,$FC,$00
        db $FC,$FC,$00,$FC,$FC,$00,$FC,$FC
        db $FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC
        db $00,$FC,$FC,$FC,$FC,$FC,$FC,$00
        db $00,$00,$FC,$FC,$FC,$FC,$00,$00
        db $00,$00,$FC,$00,$00,$FC,$00,$00
        db $00,$FC,$00,$00,$00,$00,$FC,$00
```

A yellow bug walks from left to right, legs swapping every eight
frames. `CMD_SPRITE_DEF` copies the blob into sprite 1 once, at
definition time. The cartridge address is three bytes, and the three
`get_` macros split the label into them. `CMD_SPRITE_MOVE` places the
sprite, `CMD_SPRITE_FRAME` picks a frame, and `CMD_SPRITE_SHOW` makes it
visible. Every command names the sprite in `GPU_SPRITE` first, because
the data ports were cleared by the command before.

The `AND A <- 8` reads one bit of the frame count. It is 0 for eight
frames and 8 for the next eight, and the `JZ` turns that into a frame
number of 0 or 1.

The picture can come from a paint program instead. Save a PNG with a
transparent background into `assets/` and write the header by hand:

```asm
.data
ball:   db 1, 16, 16                  ; one frame, 16 by 16
        .image('ball.png')            ; its pixels, straight after
```

The pixels land right after the header, so `ball` names a complete
blob. A picture in other colours is moved onto the 3-3-2 palette, and
the build says `colors quantized to the current palette`.

`CMD_HIT_TEST` takes two sprites in `GPU_SPRITE` and `GPU_SPRITE_B` and
answers on `GPU_HIT` with a nonzero byte when their boxes overlap. A
hidden sprite hits nothing. The game at the end uses it.

## Input

| Port | Name | Read gives |
|---|---|---|
| `$20` | `IO_CONTROLLER` | the seven buttons as bits, a level |
| `$21` | `IO_KEY` | the next key event, or zero |
| `$22` | `IO_MODS` | the shift, control, alt and meta bits, a level |

The pad chapter read the controller. A key event holds the code in
its low seven bits and the release flag in bit 7. The queue holds 64
events, and the oldest is dropped when it is full. A game reads
`IO_KEY` until it answers zero before it waits for a key to start. A
press from before the game then cannot start it.

## The audio chip

The audio chip plays samples from the cartridge. A sample is 8 bit
unsigned mono at 8000 a second, with silence at 128. It has 64 sample
slots and 8 tracks, and each track sounds up to 8 notes at once. Unlike
the GPU it runs in real time. A note lasts the same whether the
processor is stepped or run flat out.

| Port | Name | Meaning |
|---|---|---|
| `$30` to `$32` | `APU_CART_LO`, `APU_CART_HI`, `APU_CART_BANK` | the cartridge address for the next define or load |
| `$33` | `APU_TRACK` | the track, 0 to 7 |
| `$34` | `APU_NOTE` | a MIDI note, 0 to 127 |
| `$35`, `$36` | `APU_ARG`, `APU_ARG2` | a length high then low, a velocity, an octave or a mode |
| `$37` | `APU_SLOT` | the sample slot, 0 to 63 |
| `$38` | `APU_CMD` | write a command byte to run it |
| `$39` | `APU_STATUS` | read: bit 0 while a tune plays |
| `$3A` | `APU_VOICES` | read: how many notes sound |

The ports latch, so a program sets what changed and writes the command.
`CMD_DEF_SAMPLE` defines `APU_SLOT` from the cartridge address, with the
length in `APU_ARG` and `APU_ARG2` and the note it was recorded at in
`APU_NOTE`. `CMD_LOOP_SLOT` with `APU_ARG` at 1 makes it repeat.
`CMD_SET_INSTRUMENT` gives a track a slot. `CMD_NOTE_ON` starts
`APU_NOTE` on `APU_TRACK` at the velocity in `APU_ARG`, and a higher
note plays the sample faster. `CMD_NOTE_OFF` stops it, or every note on
the track when `APU_ARG` is 0. `CMD_TRIGGER` plays a slot once at a
note, for a sound effect. `CMD_LOAD_MIDI` and `CMD_PLAY` play a MIDI
file from the cartridge on the tracks by themselves.

```asm
; A scale on the audio chip, one bar drawn per note.
        OUT APU_SLOT, 0
        OUT APU_CART_BANK, get_bankbyte(wave)
        OUT APU_CART_HI, get_highbyte(wave)
        OUT APU_CART_LO, get_lowbyte(wave)
        OUT APU_ARG, 0                ; length 32, high byte then low
        OUT APU_ARG2, 32
        OUT APU_NOTE, 59              ; the note the wave was recorded at
        OUT APU_CMD, CMD_DEF_SAMPLE
        OUT APU_ARG, 1                ; loop it, so a note lasts
        OUT APU_CMD, CMD_LOOP_SLOT
        OUT APU_TRACK, 0
        OUT APU_ARG, 0                ; 0: melodic, one sample at every pitch
        OUT APU_CMD, CMD_SET_INSTRUMENT
        OUT GPU_COLOR, $1C
        OUT GPU_CMD, CMD_SET_COLOR
        LD D1 <- &scale
        LD A <- 8
        LD [left] <- A
        LD A <- 20
        LD [x] <- A
note:   LD A <- [D1]+
        LD [cur] <- A
        OUTA APU_NOTE
        OUT APU_ARG, 100              ; velocity
        OUT APU_CMD, CMD_NOTE_ON
        LD A <- [x]
        OUTA GPU_X
        OUT GPU_Y, 200
        OUT GPU_CMD, CMD_MOVE_TO
        LD A <- [x]
        ADD A <- 20
        LD [x] <- A
        OUTA GPU_X
        LD A <- 255
        SUB A <- [cur]                ; a taller bar for a higher note
        SUB A <- [cur]
        SUB A <- [cur]
        OUTA GPU_Y
        OUT GPU_CMD, CMD_RECT
        LD A <- 15
        LD [hold] <- A
wait:   JSR frame
        LD A <- [hold]
        DEC A
        LD [hold] <- A
        JZ off
        JMP wait
off:    LD A <- [cur]
        OUTA APU_NOTE
        OUT APU_ARG, 1                ; 1: this note only
        OUT APU_CMD, CMD_NOTE_OFF
        LD A <- [left]
        DEC A
        LD [left] <- A
        JZ done
        JMP note
done:   HLT

; frame: returns when the next frame has started. Uses: A.
frame:  IN GPU_FRAME
        LD [fr] <- A
again:  IN GPU_FRAME
        SUB A <- [fr]
        JZ again
        RET

.ram
left:   db 0
cur:    db 0
x:      db 0
hold:   db 0
fr:     db 0
scale:  db 60, 62, 64, 65, 67, 69, 71, 72
.data
; one cycle of a square wave: 16 high, 16 low
wave:   db 224,224,224,224,224,224,224,224,224,224,224,224,224,224,224,224
        db 32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32
```

Eight green bars rise across the screen while a scale from middle C
plays, a quarter second a note. The wave is 32 bytes, one cycle of a
square wave. Looped, at 8000 a second, it sounds at 250 Hz, which is
the B below middle C, note 59. That is what `APU_NOTE, 59` tells the
define. Note 60 then plays it a semitone faster.

The first draft of this program called the counter `hold` and the loop
`hold` as well, and the assembler said `duplicate label: hold`. A
label is one name for one address, in whichever section it appears.

A sound file replaces the `db` lines with `.sample('ping.wav')` and
the length with `get_sizehi(ping)` and `get_sizelo(ping)`.

## The coprocessor

The processor has no multiply. The coprocessor on ports `$40` to `$4F`
does 64 bit integer and floating point arithmetic on a block of RAM the
program names. It is the only device that writes RAM. A command reads
operand A from the block and operand B after it. It writes the result
after both, all within one cycle.

| Port | Name | Meaning |
|---|---|---|
| `$40`, `$41` | `ACP_ADDR_HI`, `ACP_ADDR_LO` | where the block starts |
| `$42` | `ACP_FMT` | the element type: `ACP_I64`, `ACP_U64`, `ACP_F64` or `ACP_C64` |
| `$43` | `ACP_RFMT` | the result type, when it differs |
| `$44` | `ACP_CMD` | write a command to run it |
| `$45` | `ACP_FLAGS` | read: `ACP_ZERO`, `ACP_NEGATIVE`, `ACP_DIVZERO`, `ACP_OVERFLOW` and the rest |
| `$46` to `$48` | `ACP_ROWS`, `ACP_COLS`, `ACP_COLS_B` | the shapes, all 1 at power on |
| `$49` to `$4B` | `ACP_ARG`, `ACP_ARG2`, `ACP_FUNC` | for `ACP_GEN_TABLE` |

An element is eight bytes, high byte first, and a complex number is
sixteen. The commands are `ACP_ADD`, `ACP_SUB`, `ACP_MUL`, `ACP_DIV`,
`ACP_REM`, `ACP_NEG`, `ACP_ABS`, `ACP_CMP`, `ACP_CVT` and `ACP_SCALE`
on any type, `ACP_SQRT`, `ACP_SIN`, `ACP_COS` and the other functions
of `math.h` on floats, vector and matrix commands, and the shifts and
bit operations on integers.

```asm
; 200 times 300 on the coprocessor, printed by the GPU.
        OUT ACP_ADDR_HI, get_highbyte(block)
        OUT ACP_ADDR_LO, get_lowbyte(block)
        OUT ACP_FMT, ACP_I64
        OUT ACP_CMD, ACP_MUL
        OUT GPU_TEXT_COL, 1
        OUT GPU_TEXT_ROW, 1
        OUT GPU_CMD, CMD_TEXT_AT
        OUT GPU_CART_BANK, get_bankbyte(fmt)
        OUT GPU_CART_HI, get_highbyte(fmt)
        OUT GPU_CART_LO, get_lowbyte(fmt)
        OUT GPU_TEXT_ARG_HI, get_highbyte(block + 30)
        OUT GPU_TEXT_ARG_LO, get_lowbyte(block + 30)
        OUT GPU_CMD, CMD_PRINTF
        HLT
.ram
block:  db 0, 0, 0, 0, 0, 0, 0, 200   ; A: 200 as eight bytes, high first
        db 0, 0, 0, 0, 0, 0, 1, 0x2C  ; B: 300
        ds 16                         ; R: a 1 by 1 multiply writes 16 bytes
.data
fmt:    db "200 * 300 = %u", 0
```

The screen shows `200 * 300 = 60000`. Two 64 bit integers multiply to
128 bits, so the result is 16 bytes. Its low word is at `block + 30`. `%u` reads two bytes there, high byte first, which is how every
word on this machine is stored. The first draft of this program gave B
seven bytes instead of eight, and printed 24576. The block is laid out
by hand, and a byte too few shifts everything after it.

docs/design/acp-ports.md has every command with its operand shapes.

## Storage

The storage device on ports `$50` to `$5F` reads and writes the named
slots in the cartridge that BASIC's `!SAVE` and `!LOAD` use. A slot
holds text, up to 65535 bytes, under a name of up to 16 characters.

| Port | Name | Meaning |
|---|---|---|
| `$50`, `$51` | `STO_ADDR_HI`, `STO_ADDR_LO` | the text block in RAM |
| `$52`, `$53` | `STO_NAME_HI`, `STO_NAME_LO` | a zero terminated name in RAM |
| `$54`, `$55` | `STO_LEN_HI`, `STO_LEN_LO` | write: the room at the block. Read: bytes moved |
| `$56` | `STO_CMD` | `STO_LOAD`, `STO_SAVE`, `STO_DELETE` or `STO_CATALOG` |
| `$57` | `STO_STATUS` | read: `STO_OK`, `STO_NOT_FOUND`, `STO_FULL`, `STO_BAD_NAME` |
| `$58` | `STO_COUNT` | read: how many slots the cartridge holds |

```asm
; Saves a note into the cartridge, then lists the slots.
        OUT STO_NAME_HI, get_highbyte(name)
        OUT STO_NAME_LO, get_lowbyte(name)
        OUT STO_ADDR_HI, get_highbyte(text)
        OUT STO_ADDR_LO, get_lowbyte(text)
        OUT STO_CMD, STO_SAVE
        IN A <- STO_STATUS
        LD [args] <- A
        OUT STO_ADDR_HI, get_highbyte(list)
        OUT STO_ADDR_LO, get_lowbyte(list)
        OUT STO_LEN_HI, 0
        OUT STO_LEN_LO, 64            ; the room at list
        OUT STO_CMD, STO_CATALOG
        IN A <- STO_COUNT
        LD [args+1] <- A
        LD A <- get_highbyte(list)
        LD [args+2] <- A
        LD A <- get_lowbyte(list)
        LD [args+3] <- A
        OUT GPU_TEXT_COL, 1
        OUT GPU_TEXT_ROW, 1
        OUT GPU_CMD, CMD_TEXT_AT
        OUT GPU_CART_BANK, get_bankbyte(fmt)
        OUT GPU_CART_HI, get_highbyte(fmt)
        OUT GPU_CART_LO, get_lowbyte(fmt)
        OUT GPU_TEXT_ARG_HI, get_highbyte(args)
        OUT GPU_TEXT_ARG_LO, get_lowbyte(args)
        OUT GPU_CMD, CMD_PRINTF
        HLT
.ram
name:   db "NOTE", 0
text:   db "REMEMBER THE MILK", 0
args:   db 0, 0, 0, 0
list:   ds 64
.data
fmt:    db "SAVE STATUS %hhu, %hhu SLOTS: %s", 0
```

The screen shows `SAVE STATUS 0, 1 SLOTS: NOTE`. The save writes the
`.rom` file on disk, so the slot is still there on the next run, and a
rebuild of the project writes a fresh cartridge without it. `%s` takes
the address of the catalog text as two bytes, and the program stores
the two halves of `list` into the argument block by hand.

## How C becomes assembly

Build a C project with `--asm-out` and the compiler's output lands in
`build/name.asm`. In the IDE, Build puts the same text in the Source
pane. Every block names the C line it came from. This is the compiler's
translation of a function that doubles a byte:

```c
unsigned char twice(unsigned char n)
{
    return n + n;
}
```

```asm
twice:
        LD A <- [__sp+1]
        ADD A <- 252
        LD [__sp+1] <- A
        LD A <- [__sp]
        ADC A <- 255
        LD [__sp] <- A
        LD D1 <- [__sp]
        LD A <- [D1+4]
        LD [__t0+1] <- A
        LD A <- 0
        LD [__t0] <- A
```

The first six lines subtract 4 from a 16 bit word in the zero page,
`__sp`, with `ADD` and `ADC` of the two's complement of 4. That word is
the compiler's stack pointer. C locals live in data RAM on a stack the
compiler keeps itself, because the hardware stack cannot be read at an
address. `LD D1 <- [__sp]` makes `D1` the frame pointer, and the
argument `n` is `[D1+4]`. The compiler widens it to a 16 bit `int` in
the zero page word `__t0`, because C arithmetic is done in `int`. The
add follows as `ADD` on the low bytes and `ADC` on the high ones, and
the answer goes to `__ret`. The function ends by adding the frame back
to `__sp` and `RET`.

The call site puts 21 in the frame it has computed, writes the new
`__sp`, and does `JSR twice`. The chapter on the stack wrote the same
16 bit add by hand in six instructions. The compiler spends about
thirty, and every one is visible. That is the trade the C guide
described from the other side.

The compiler's own names start with two underline characters, as in
`__sp`, `__ret` and `__t0`. A global named `count` in C is the label `count` in the
assembly, so an assembly file in the same project can read it. Every
`.asm` in a C project is appended after the generated code, with its
own `.ram` and `.data` following the compiler's.

## A routine for BASIC

A project with `.bas` files and an `.asm` file builds the interpreter
with the routine appended. `CALL name` in the BASIC program calls the
label, and the build writes the label's slot number into the program
before the ROM is written. The routine comes back with `RET`. BASIC
parks the `A` register it came back with at address 4, so `PEEK(4)`
reads it.

`twice/src/autorun.bas`:

```basic
10 REM BASIC CALLS ASSEMBLY
20 A=21
30 CALL DOUBLE
40 PRINT "TWICE 21 IS "; A
50 PRINT "AND THE ROUTINE ANSWERED "; PEEK(4)
60 END
```

`twice/src/double.asm`:

```asm
; DOUBLE: called from BASIC with CALL DOUBLE. Doubles the variable A.
; The word at $0C points at the variables, and A is the first word.
; $04 is where BASIC parks the A register on return, so the routine
; borrows it as a scratch byte until then.
DOUBLE: LD D1 <- [$000C]
        LD A <- [D1+1]                ; low byte
        LD [$04] <- A
        ADD A <- [$04]
        LD [D1+1] <- A
        LD A <- [D1]                  ; high byte, plus the carry
        LD [$04] <- A
        ADC A <- [$04]
        LD [D1] <- A
        LD A <- 7
        RET
```

```text
SimpleCPU-8 BASIC
READY
TWICE 21 IS 42
AND THE ROUTINE ANSWERED 7
>
```

docs/basic-system-page.md lists the first 32 bytes of the zero page,
where BASIC keeps its pointers. The word at `$0C` is where the integer
variables start, 11 words per letter, `A` first. A routine called from
BASIC may use the hardware stack in balance. It may not touch the zero
page past those 32 bytes, because the interpreter's own variables are
there. So this routine borrows `$04`, which BASIC overwrites on return
anyway, as the one zero page byte an `ADD` can read.

`USR` asks a routine for an answer instead. It calls the routine the way
C calls a function. Up to three parameter words sit on the software
stack, the first at the address `__sp` holds, high byte first. The
answer goes in the return cells, `__ret` then `__ret+1`, high byte
first. Those two labels are the interpreter's own and are the only zero
page bytes past the system page a routine may use. BASIC puts the stack
back after the call, so the routine does not pop its parameters.

```asm
; PLUS1: USR(1, PLUS1, n) answers n + 1. Only the low byte of n is read.
PLUS1:  LD D1 <- [__sp]
        LD A <- [D1+1]                ; the low byte of the first word
        INC A
        LD [__ret+1] <- A
        LD A <- 0
        LD [__ret] <- A
        RET
```

`PRINT USR(1, PLUS1, 41)` prints 42.

## A small game

The game is called Catch. A basket at the bottom of the screen moves
left and right. A star falls from a random column. Catch it for a
point, miss it and the miss is counted. Each catch plays a note. Build
it in three steps, and run each.

Step one draws the basket and steers it. Put it in `catch/src/main.asm`.

```asm
; Catch: steer the basket under the falling star.
        OUT GPU_COLOR, $01
        OUT GPU_CMD, CMD_CLEAR
        OUT GPU_SPRITE, 1
        OUT GPU_SRC_BANK, get_bankbyte(basket)
        OUT GPU_SRC_HI, get_highbyte(basket)
        OUT GPU_SRC_LO, get_lowbyte(basket)
        OUT GPU_CMD, CMD_SPRITE_DEF
        OUT GPU_SPRITE, 1
        OUT GPU_CMD, CMD_SPRITE_SHOW

loop:   IN A <- IO_CONTROLLER
        LD [pad] <- A
        AND A <- BTN_LEFT
        JZ noleft
        LD A <- [bx]
        SUB A <- 2
        JC noleft                     ; a borrow: it was already at the edge
        LD [bx] <- A
noleft: LD A <- [pad]
        AND A <- BTN_RIGHT
        JZ noright
        LD A <- [bx]
        SUB A <- 239                  ; C is set while bx is under 239
        JC right
        JMP noright
right:  LD A <- [bx]
        ADD A <- 2
        LD [bx] <- A
noright: OUT GPU_SPRITE, 1
        LD A <- [bx]
        OUTA GPU_SPRITE_X
        OUT GPU_SPRITE_Y, 230
        OUT GPU_CMD, CMD_SPRITE_MOVE
        JSR frame
        JMP loop

; frame: returns when the next frame has started. Uses: A.
frame:  IN GPU_FRAME
        LD [fr] <- A
again:  IN GPU_FRAME
        SUB A <- [fr]
        JZ again
        RET

.ram
bx:     db 120
pad:    db 0
fr:     db 0

.data
basket: db 1, 16, 8
        db $FC,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$FC
        db $FC,$FC,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$FC,$FC
        db $00,$FC,$FC,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$FC,$FC,$00
        db $00,$00,$FC,$FC,$00,$00,$00,$00,$00,$00,$00,$00,$FC,$FC,$00,$00
        db $00,$00,$00,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$00,$00,$00
        db $00,$00,$00,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$00,$00,$00
        db $00,$00,$00,$00,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$00,$00,$00,$00
        db $00,$00,$00,$00,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$00,$00,$00,$00
```

The edge tests use the carry. `SUB A <- 2` borrows when `bx` is 0 or
1, so `JC` skips the move and the basket stops at the left edge. `SUB
A <- 239` borrows while `bx` is under 239, so `JC right` allows the
move and the basket stops at 238, where 16 pixels still fit on the
screen. The subtract is done to read the flag, and `A` is reloaded
afterwards.

Step two adds the star, sprite 2. It starts above the screen at a
random column and falls two pixels a frame. Below the basket row it
starts again. Add the definition after the basket's `CMD_SPRITE_SHOW`,
a `newstar` routine, its variables and its picture:

```asm
        OUT GPU_SPRITE, 2
        OUT GPU_SRC_BANK, get_bankbyte(star)
        OUT GPU_SRC_HI, get_highbyte(star)
        OUT GPU_SRC_LO, get_lowbyte(star)
        OUT GPU_CMD, CMD_SPRITE_DEF
        OUT GPU_SPRITE, 2
        OUT GPU_CMD, CMD_SPRITE_SHOW
        JSR newstar
```

```asm
; newstar: puts the star above the screen at a random column. Uses: A.
newstar: IN A <- GPU_RAND
        AND A <- $7F
        ADD A <- 60
        LD [sx] <- A
        LD A <- 0
        LD [sy] <- A
        RET
```

```asm
sx:     db 0
sy:     db 0
```

```asm
star:   db 1, 8, 8
        db $00,$00,$00,$FF,$FF,$00,$00,$00
        db $00,$00,$00,$FF,$FF,$00,$00,$00
        db $00,$FF,$00,$FF,$FF,$00,$FF,$00
        db $00,$00,$FF,$FF,$FF,$FF,$00,$00
        db $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        db $00,$00,$FF,$FF,$FF,$FF,$00,$00
        db $00,$FF,$00,$FF,$FF,$00,$FF,$00
        db $FF,$00,$00,$00,$00,$00,$00,$FF
```

`AND A <- $7F` keeps seven bits of the random byte, 0 to 127. The add
moves the range to 60 to 187, away from the edges. In the main loop,
after the basket has moved, the star falls and is redrawn. Past row
240 it is a miss:

```asm
        LD A <- [sy]
        ADD A <- 2
        LD [sy] <- A
        OUT GPU_SPRITE, 2
        LD A <- [sx]
        OUTA GPU_SPRITE_X
        LD A <- [sy]
        OUTA GPU_SPRITE_Y
        OUT GPU_CMD, CMD_SPRITE_MOVE
        LD A <- [sy]
        SUB A <- 240                  ; past the basket row: a miss
        JC wait
        JSR newstar
wait:   JSR frame
        JMP loop
```

Step three adds the hit test, the score and the note. The whole
program, with the pieces in place:

```asm
; Catch: steer the basket under the falling star.
        OUT GPU_COLOR, $01
        OUT GPU_CMD, CMD_CLEAR
        OUT GPU_SPRITE, 1
        OUT GPU_SRC_BANK, get_bankbyte(basket)
        OUT GPU_SRC_HI, get_highbyte(basket)
        OUT GPU_SRC_LO, get_lowbyte(basket)
        OUT GPU_CMD, CMD_SPRITE_DEF
        OUT GPU_SPRITE, 1
        OUT GPU_CMD, CMD_SPRITE_SHOW
        OUT GPU_SPRITE, 2
        OUT GPU_SRC_BANK, get_bankbyte(star)
        OUT GPU_SRC_HI, get_highbyte(star)
        OUT GPU_SRC_LO, get_lowbyte(star)
        OUT GPU_CMD, CMD_SPRITE_DEF
        OUT GPU_SPRITE, 2
        OUT GPU_CMD, CMD_SPRITE_SHOW
        JSR newstar
        OUT APU_SLOT, 0
        OUT APU_CART_BANK, get_bankbyte(wave)
        OUT APU_CART_HI, get_highbyte(wave)
        OUT APU_CART_LO, get_lowbyte(wave)
        OUT APU_ARG, 0
        OUT APU_ARG2, 32
        OUT APU_NOTE, 59
        OUT APU_CMD, CMD_DEF_SAMPLE
        OUT APU_ARG, 1
        OUT APU_CMD, CMD_LOOP_SLOT
        OUT APU_TRACK, 0
        OUT APU_ARG, 0
        OUT APU_CMD, CMD_SET_INSTRUMENT
        OUT GPU_TEXT_COLOR, $FF
        OUT GPU_CMD, CMD_TEXT_STYLE
        JSR score

loop:   IN A <- IO_CONTROLLER
        LD [pad] <- A
        AND A <- BTN_LEFT
        JZ noleft
        LD A <- [bx]
        SUB A <- 2
        JC noleft                     ; a borrow: it was already at the edge
        LD [bx] <- A
noleft: LD A <- [pad]
        AND A <- BTN_RIGHT
        JZ noright
        LD A <- [bx]
        SUB A <- 239                  ; C is set while bx is under 239
        JC right
        JMP noright
right:  LD A <- [bx]
        ADD A <- 2
        LD [bx] <- A
noright: OUT GPU_SPRITE, 1
        LD A <- [bx]
        OUTA GPU_SPRITE_X
        OUT GPU_SPRITE_Y, 230
        OUT GPU_CMD, CMD_SPRITE_MOVE

; the star falls two pixels a frame
        LD A <- [sy]
        ADD A <- 2
        LD [sy] <- A
        OUT GPU_SPRITE, 2
        LD A <- [sx]
        OUTA GPU_SPRITE_X
        LD A <- [sy]
        OUTA GPU_SPRITE_Y
        OUT GPU_CMD, CMD_SPRITE_MOVE

; did it land in the basket?
        OUT GPU_SPRITE, 1
        OUT GPU_SPRITE_B, 2
        OUT GPU_CMD, CMD_HIT_TEST
        IN A <- GPU_HIT
        AND A <- $FF                  ; IN sets no flags, so test A
        JZ nohit
        LD A <- [caught]
        INC A
        LD [caught] <- A
        OUT APU_NOTE, 72
        OUT APU_ARG, 100
        OUT APU_CMD, CMD_NOTE_ON
        LD A <- 6
        LD [ring] <- A
        JSR score
        JSR newstar
        JMP tick
nohit:  LD A <- [sy]
        SUB A <- 240                  ; past the basket row: a miss
        JC tick
        LD A <- [missed]
        INC A
        LD [missed] <- A
        JSR score
        JSR newstar

; the note plays for six frames, then stops
tick:   LD A <- [ring]
        JZ wait
        DEC A
        LD [ring] <- A
        JZ stop
        JMP wait
stop:   OUT APU_NOTE, 72
        OUT APU_ARG, 1
        OUT APU_CMD, CMD_NOTE_OFF
wait:   JSR frame
        JMP loop

; newstar: puts the star above the screen at a random column. Uses: A.
newstar: IN A <- GPU_RAND
        AND A <- $7F
        ADD A <- 60
        LD [sx] <- A
        LD A <- 0
        LD [sy] <- A
        RET

; score: prints the two counts on the top row. Uses: A.
score:  OUT GPU_TEXT_COL, 1
        OUT GPU_TEXT_ROW, 0
        OUT GPU_CMD, CMD_TEXT_AT
        LD A <- [caught]
        LD [args] <- A
        LD A <- [missed]
        LD [args+1] <- A
        OUT GPU_CART_BANK, get_bankbyte(fmt)
        OUT GPU_CART_HI, get_highbyte(fmt)
        OUT GPU_CART_LO, get_lowbyte(fmt)
        OUT GPU_TEXT_ARG_HI, get_highbyte(args)
        OUT GPU_TEXT_ARG_LO, get_lowbyte(args)
        OUT GPU_CMD, CMD_PRINTF
        RET

; frame: returns when the next frame has started. Uses: A.
frame:  IN GPU_FRAME
        LD [fr] <- A
again:  IN GPU_FRAME
        SUB A <- [fr]
        JZ again
        RET

.ram
bx:     db 120
sx:     db 0
sy:     db 0
pad:    db 0
fr:     db 0
caught: db 0
missed: db 0
ring:   db 0
args:   db 0, 0

.data
fmt:    db "CAUGHT %hhu  MISSED %hhu", 0
basket: db 1, 16, 8
        db $FC,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$FC
        db $FC,$FC,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$FC,$FC
        db $00,$FC,$FC,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$FC,$FC,$00
        db $00,$00,$FC,$FC,$00,$00,$00,$00,$00,$00,$00,$00,$FC,$FC,$00,$00
        db $00,$00,$00,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$00,$00,$00
        db $00,$00,$00,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$00,$00,$00
        db $00,$00,$00,$00,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$00,$00,$00,$00
        db $00,$00,$00,$00,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$FC,$00,$00,$00,$00
star:   db 1, 8, 8
        db $00,$00,$00,$FF,$FF,$00,$00,$00
        db $00,$00,$00,$FF,$FF,$00,$00,$00
        db $00,$FF,$00,$FF,$FF,$00,$FF,$00
        db $00,$00,$FF,$FF,$FF,$FF,$00,$00
        db $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        db $00,$00,$FF,$FF,$FF,$FF,$00,$00
        db $00,$FF,$00,$FF,$FF,$00,$FF,$00
        db $FF,$00,$00,$00,$00,$00,$00,$FF
wave:   db 224,224,224,224,224,224,224,224,224,224,224,224,224,224,224,224
        db 32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32
```

The top row reads `CAUGHT 0  MISSED 0` and the counts change as stars
land or fall past. `CMD_HIT_TEST` asks the GPU whether the two boxes
overlap. The GPU keeps every sprite's position, so the program holds no
box of its own. `IN A <- GPU_HIT` sets no flags, which is why the `AND
A <- $FF` follows it: the `AND` leaves the byte alone and sets `Z`.

The note is not a pause. `CMD_NOTE_ON` returns at once, `ring` counts
six frames in the main loop, and `CMD_NOTE_OFF` follows. The game keeps
moving while the note sounds. `score` redraws the top row only when a
count changes, and prints both counts from a two byte block that
`printf` reads with two `%hhu`.

Things to change once it runs. A caught star could raise the speed by
keeping the step in a variable and adding it instead of 2. A second
star needs a second sprite, two more variables and a copy of the
falling block. That is a case for a routine that takes the sprite
number in `A`. A miss limit and a game over message need `CMD_PRINTF`
with a second template. Each is a page of assembly, and the machine
tells you the slot when one of them is wrong.

## Where to go next

The microcode guide beside this one goes one layer down again. Every
instruction in this guide is a short list of control signals, one row
per cycle. The naive set that an assembly project runs on is open to
read and change. The IDE's CPU level, F4, shows the rows firing
on the datapath as a program steps. `simplecpu-make new mine
--microcode` lays out a project with the naive set as a file. Its
program prints what every instruction costs.

docs/design/cpu-core-spec-v2.md is the processor's reference, with the
opcode of every instruction and the cycle count of both sets. The
device references are docs/design/gpu-ports.md,
docs/design/acp-ports.md and docs/design/audio-design.md. The IDE's
manual pane shows the same tables. The 25 programs in examples/ are all
assembly. examples/sprite is Space Invaders and examples/pacman is
Pac-Man. Both run on the ports this guide covered.
