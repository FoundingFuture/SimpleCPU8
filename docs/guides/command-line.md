# Working from the command line

Everything the IDE does can be done from a terminal. Start a project,
write it in any editor, build it into a ROM and run it. This guide walks
through that for each language: BASIC, C, assembly and microcode. The
guides beside it teach the languages. This one teaches the tools.

Every command below was run as printed, and every output shown is what
it printed.

## Contents

- [The six programs](#the-six-programs)
- [A project is a folder](#a-project-is-a-folder)
- [Starting a project](#starting-a-project)
- [Building a ROM](#building-a-rom)
- [Running a ROM](#running-a-rom)
- [BASIC](#basic)
- [C](#c)
- [Assembly](#assembly)
- [Microcode](#microcode)
- [Pictures and sounds](#pictures-and-sounds)
- [The ROM file](#the-rom-file)
- [The tools one step lower](#the-tools-one-step-lower)
- [Working in the repository](#working-in-the-repository)

## The six programs

| Program | Does |
|---|---|
| `simplecpu-make` | starts a project, builds a folder into a ROM, unpacks a ROM into a folder |
| `simplecpu` | runs a ROM in a window or on the whole screen, or boots BASIC |
| `simplecpu-ide` | the IDE, which the IDE guide covers |
| `simplecpu-cc` | compiles C files into assembly |
| `simplecpu-asm` | assembles one or more `.asm` files into a ROM |
| `simplecpu-run` | runs a ROM or an `.asm` file with no window and prints the machine's state |

`simplecpu-make` is the one to learn first. It calls the compiler and
the assembler for you, so most people never type `simplecpu-cc` or
`simplecpu-asm` at all.

A distribution keeps the programs in `bin/`. A build from the repository
keeps them in `build/release/src/tools/` and `build/release/src/vm/`.
The examples below assume the folder is on your `PATH`:

```bash
export PATH="$PWD/bin:$PATH"
```

## A project is a folder

There is no project file to write. The folder holds the sources, and
what they are decides what gets built.

The larger layout, which `simplecpu-make new` lays out:

```text
mygame/
  README.md        the first line is the ROM's title
  src/             .c .h .asm .bas and microcode.txt
  assets/          pictures and sounds the sources name
  build/           the ROM lands here
```

The flat layout keeps everything in one folder and writes
`mygame/mygame.rom` beside the sources. A folder with a `src/`
directory is the larger layout, and any other folder is flat.

What the sources hold decides the kind:

| The folder holds | The ROM is |
|---|---|
| `.c` files | a C program: every `.c` compiled together, every `.asm` appended |
| `.bas` files and no `.c` | the BASIC interpreter with every `.bas` as a stored program |
| `.bas` and `.c` files | one program: the interpreter and your C together |
| `.asm` files only | an assembly program |
| a `microcode.txt` as well | any of the above, running on that microcode set |

## Starting a project

`simplecpu-make new` lays a project out with a first program that builds
and runs. Say which kind:

```bash
simplecpu-make new hello --basic
simplecpu-make new ball --c
simplecpu-make new blink --assembly
simplecpu-make new cyc --microcode
```

```text
wrote hello/.gitignore
wrote hello/README.md
wrote hello/src/autorun.bas
build it with: simplecpu-make hello
```

The folder must not exist yet, or must be empty. The `.gitignore` keeps
`build/` out of version control, since the ROM can always be made again.

## Building a ROM

```bash
simplecpu-make hello
```

```text
wrote hello/build/hello.rom
```

The folder is the only argument it needs. The options:

| Option | Does |
|---|---|
| `-o out.rom` | writes the ROM somewhere else |
| `--listing` | also prints the program's size and the files that went in |
| `--asm-out` | also keeps the generated assembly beside the ROM |
| `--title T`, `--author A` | the names the ROM carries, instead of the README's first line |
| `-D NAME` or `-D NAME=VALUE` | a C preprocessor definition |
| `--microcode naive`, `optimal` or a file | the microcode set, instead of the project's own |
| `--no-sources` | a ROM without the project inside it |

An error names the file and the line, the way a compiler does. The ROM
is not written until everything builds:

```text
main.c:15: y is not declared
```

A warning says something the build did anyway, such as a picture moved
onto the machine's palette.

### Calls between languages

A project can mix BASIC, C and assembly in one ROM. BASIC calls C and
assembly. Assembly calls C. C calls assembly. The compiler leaves out
every C function nothing calls. The C cannot show a call from BASIC or
assembly, so `simplecpu-make` builds in this order:

1. It reads every `.bas` file for the names after `CALL`, `JSR`, `JMP`
   and `USR`.
2. It reads every `.asm` file for every name outside comments and
   strings.
3. It compiles the C with those names as starting points beside `main`.
   A C function they name is kept, with everything it calls.
4. It assembles the C's output and the `.asm` files into one ROM.

A C function called from BASIC or assembly must not be `static`, since
its label is private to its file. The build refuses such a call:

```text
maths.c:2: SQUARE is static, so only its own file can call it. BASIC and assembly call it by name: remove static.
```

C calls an assembly routine through a prototype with no body, such as
`int twice(int n);`, and an `.asm` file defines the label `twice`. The
routine follows the C calling convention. The C guide and the assembly
guide describe it.

## Running a ROM

```bash
simplecpu --rom hello/build/hello.rom
```

A ROM fills the whole screen, the way a game console does. `--window`
asks for a window instead, and `--scale 3` for a window three times the
machine's 256 by 256 pixels. F11 switches between the two while it runs.

| Option | Does |
|---|---|
| `--window`, `--fullscreen`, `--scale N` | how big the picture is |
| `--fps 60` | paces the machine to 60 frames a second, which is the default |
| `--max` | runs the machine as fast as the host allows |
| `--microcode naive` or `optimal` | runs the ROM on another set |
| `--crt 0.6`, `--no-crt` | the strength of the display look, or none |
| `--crt-effect lcd` | another look: `sharp`, `sharp-smooth`, `classic`, `shadow-mask`, `aperture-grille`, `slot-mask`, `lcd`, `composite` or `smooth` |
| `--seconds 10` | quits by itself after ten seconds |
| `--stack-size 16K` | the hardware stack's size, 256 bytes to 64K. 2K is the default |
| `--type 'RUN\n'` | types text into the machine after it boots, `\n` being Enter |
| `--screenshot shot.png` | saves the window after one second and quits |

While it runs, F1 turns the display look on and off. F2 and F3 turn it
down and up, and F4 steps to the next look. F5 powers the machine on
again. Every other key goes to the
machine. Quitting is the operating system's own gesture, Command-Q on
macOS or the close button elsewhere. So Escape and Ctrl-C reach BASIC,
which uses both to stop a program.

When a run ends, `simplecpu` prints two frame rates:

```text
window: 36.9 fps average, 38 min, 39 max (111 frames in 3.0 s)
GPU_FRAME: 59.5 fps average, 59 min, 62 max (179 frames in 3.0 s)
```

The first is how often the host drew the window. The second is how often
the machine's frame counter ticked, which is what a program that waits
for `GPU_FRAME` saw. A machine that keeps up shows 60 there, whatever the
window managed. The numbers above come from a slow test machine.

## BASIC

A BASIC project holds `src/autorun.bas`:

```basic
10 REM hello
20 CLS
30 FOR I = 1 TO 10
40 PRINT "HELLO FROM hello "; I
50 NEXT I
60 PRINT "TYPE LIST TO SEE ME, RUN TO RUN ME AGAIN"
70 END
```

Edit it in any text editor, then build and run:

```bash
simplecpu-make hello
simplecpu --rom hello/build/hello.rom
```

The ROM is the BASIC interpreter with your program stored inside it. A
stored program called `AUTORUN` runs as soon as the machine powers on.
Every other `.bas` file becomes a stored program named after the file,
which `!LOAD "NAME"` fetches while the machine runs.

A program can also run without a project. `simplecpu --basic` boots the
interpreter, loads a file and, with `--run`, starts it:

```bash
simplecpu --basic three.bas --run
```

```text
SimpleCPU-8 BASIC
READY
>!LOAD "THREE"
READY
>RUN
HELLO 1
HELLO 2
HELLO 3
READY
```

BASIC and C together: put a `.c` file beside the `.bas` files and the
ROM becomes one program, the interpreter and your C compiled together.
BASIC calls a C function by name with `CALL` or `USR`.

`src/maths.c`:

```c
/* SQUARE: called from BASIC with USR(2, SQUARE, n). */
int SQUARE(int n)
{
    return n * n;
}
```

`src/autorun.bas`:

```basic
10 PRINT "12 SQUARED IS "; USR(2, SQUARE, 12)
```

```bash
simplecpu-make hello --listing
```

```text
27107 instructions, 20028 bytes of .ram, 0 bytes of .data, 1 BASIC slot(s)
  the BASIC interpreter
  maths.c
  autorun.bas
wrote hello/build/hello.rom
```

The machine prints `12 SQUARED IS 144`. The build turns the name
`SQUARE` into the function's address before the program goes into the
ROM. Names work only in a built project. At the machine's prompt, a
routine is called by its number, which `LIST` shows. An `.asm` file in a
BASIC project is a driver appended to the interpreter, called the same
way. The BASIC guide's chapter on machine code has the details. The
build reads the BASIC program for those names before it compiles the C,
as Calls between languages describes.

## C

A C project holds `src/main.c`, and the build compiles every `.c` in
`src/` together. A header beside them is reachable with
`#include "name.h"`. The machine's own libraries come with the compiler:

| Header | Gives |
|---|---|
| `graphics.h` | drawing, text on the screen, sprites and pictures |
| `keys.h` | the arrow keys, fire and the keyboard |
| `sound.h` | notes, beeps and samples |
| `math.h` | square roots, trigonometry and the rest, in doubles |
| `disk.h` | the cartridge's stored files |
| `basicvars.h` | BASIC's variables, in a project with BASIC |

A library is compiled in only when its header is included.

```bash
simplecpu-make ball --listing
```

```text
897 instructions, 50 bytes of .ram, 43 bytes of .data, 0 BASIC slot(s)
  main.c
wrote ball/build/ball.rom
```

`--asm-out` keeps the assembly the compiler made, as
`build/ball.asm`, to read what a line of C became. `-D DEBUG` defines
`DEBUG` for every file, as with any C compiler.

An `.asm` file beside the C goes into the same ROM. It follows the
compiler's output. The C calls a routine in it through a prototype with
no body. The assembly calls a C function by its name.

The C guide covers the language and each library.

## Assembly

An assembly project holds `src/main.asm`. Every `.asm` in `src/` goes
into one ROM, in name order, and each file starts in the code section.
A file that is not the first can place itself with `.org` at a fixed
instruction slot.

```bash
simplecpu-make blink
```

A single file can also skip the project and go straight to the
assembler:

```bash
simplecpu-asm add.asm
```

```text
wrote add.rom (149 bytes)
```

`simplecpu-run` runs a ROM, or an `.asm` file directly, with no window.
It prints where the machine ended up, which is how a small routine is
tested. `--ram 0 3` shows three bytes of RAM from address 0:

```asm
sum:    LD     A <- [n1]
        ADD    A <- [n2]
        LD     [total] <- A
        HLT
.ram
n1:     db     20
n2:     db     22
total:  db     0
```

```bash
simplecpu-run add.asm --ram 0 3
```

```text
status: halted
instructions: 3  cycles: 16
A=2a D1=0000 D2=0000 D3=0000 SP=07ff PC=0003 flags=----
0000: 14 16 2a
```

20 and 22 went in, and `$2a`, 42, came out. `--max N` stops a program
that never halts after N instructions. A program that writes to the
machine's ports gets its port writes listed, since there is no screen.

The IDE lays assembly out in columns when it saves: mnemonics in column
8, operands in column 15. A new project starts that way. The assembler
reads any layout, so any editor will do.

The assembly guide covers the instructions, the assembler and every
port.

## Microcode

A microcode project is an assembly project with a `src/microcode.txt`.
That file is the naive set written out. Each instruction has a section,
and each row of signals has a line.

```text
LD A <- [D1]+:
  RAM_TO_B, ADDR_D1
  D1_INC
  ALU_PASS_B, ACC_LOAD_ALU
  ALU_PASS_B, FLAGS_LOAD
  PC_INC
```

Its program shows a table: every instruction's cost in microcycles under
your set and under the optimal one. The fetch is included, which is why
`LD A <- [D1]+` costs 6 with the five rows above. Merge the third and
fourth rows into one:

```text
  ALU_PASS_B, ACC_LOAD_ALU, FLAGS_LOAD
```

Build and run again, and the table says 5. The build counts the rows of
the set it puts in the ROM. So the table always tells the truth about
that set.

A row that breaks one of the machine's eight rules still builds, with a
warning that names the line:

```text
cyc/src/microcode.txt:97: warning: LD A <- [D1]+, row 1, breaks rule 8: more than one address base select. The machine stops with a signal conflict when it runs this row.
```

The machine runs the set anyway and stops on that row when it gets
there, which is its own lesson. A line that is not microcode at all is
an error, such as a misspelt signal name. Then nothing is built.

Any ROM can run on another set without a rebuild:
`simplecpu --rom game.rom --microcode naive`. `simplecpu-asm` and
`simplecpu-make` take `--microcode` too, with `naive`, `optimal` or the
name of a set file.

The microcode guide explains the rows, the signals and the eight rules.

## Pictures and sounds

Pictures and sounds go in `assets/`. The sources name them, and the
build converts them and puts them in the ROM.

In C:

```c
__ROM const unsigned char shippic[] = __sprite("ship.png", 2);
__ROM const unsigned char hillpic[] = __image("hill.png");
__ROM const unsigned char ping[] = __sample("ping.wav");
```

In assembly:

```asm
.data
ship:   .sprite('ship.png')
hill:   db 32, 16
        .image('hill.png')
```

| C | Assembly | Takes |
|---|---|---|
| `__image("x.png")` | `.image('x.png')` | a picture up to 256 by 256 |
| `__sprite("x.png", n)` | `.sprite('x.png', n)` | a strip of n frames side by side, each up to 64 by 64. Without n, the count the sprite editor saved in the PNG |
| `__palette("x.png")` | `.palette('x.png')` | a picture's 256 colours |
| `__sample("x.wav")` | `.sample('x.wav')` | a sound, made 8 bit mono at 8000 a second |
| `__file("x.bin")` | `.file('x.bin')` | any bytes as they are |

PNG, JPEG, BMP and GIF pictures are read, and WAV, FLAC and MP3 sounds. A picture in other colours is moved onto the machine's palette,
and the build says so. Transparent pixels become colour 0, which the
graphics chip skips. `examples/bounce` is a C project with two sprites in
`assets/`.

## The ROM file

A ROM holds everything the machine needs. That is the program, the
starting RAM, the pictures and sounds, the microcode set and any stored
BASIC programs. It also carries the project that made it: the sources, the
assets and the README. A ROM is therefore a project in one file. It is
compressed.

`simplecpu-make unpack` turns a ROM back into a project folder:

```bash
simplecpu-make unpack ball.rom
```

```text
wrote ball/src/main.c
wrote ball/README.md
wrote ball/.gitignore
build it with: simplecpu-make ball
```

The folder defaults to the ROM's name, and a second argument names
another. It must not exist yet:

```text
ballcopy exists already
```

`--no-sources` builds a ROM without the project inside, for a program
handed out without its source. Such a ROM runs the same and cannot be
unpacked:

```text
this ROM carries no project
```

## The tools one step lower

`simplecpu-make` is the compiler and the assembler run for you. They can
be run by hand as well.

`simplecpu-cc` compiles C files into one assembly file:

```bash
simplecpu-cc main.c -o main.asm
```

| Option | Does |
|---|---|
| `-o file` | where the assembly goes, `out.asm` otherwise |
| `-D NAME[=VALUE]` | a preprocessor definition |
| `-msoft-mul` | multiplies and divides on the CPU instead of the coprocessor |
| `-zp-reserve N` | leaves the first N bytes of the zero page to the program |
| `--heap-stack-size N` | the C stack's size in RAM, as 4096 or 4K. `#pragma heap_stack_size` does the same from a source |
| `--rom-header file` | also writes `ROM.h`, the map of what the cartridge holds |

Pictures and sounds named in the C are looked up beside the first file.

`simplecpu-asm` assembles one or more files into a ROM:

```bash
simplecpu-asm main.asm -o ball.rom --title "Ball" --listing
```

```text
897 instructions, 50 bytes of .ram, 43 bytes of .data
  __L0_while               code     17
  __L10_wend               code    371
```

| Option | Does |
|---|---|
| `-o out.rom` | the ROM's name, the source's name with `.rom` otherwise |
| `--title T`, `--author A` | the names the ROM carries |
| `--microcode naive`, `optimal` or a file | the ROM's microcode set |
| `--listing` | prints the sizes and every label |
| `--no-sources` | leaves the sources out of the ROM |
| `--bas NAME=file.bas` | stores a BASIC program in the ROM under NAME |

Two or more files make one ROM, each starting in the code section:
`simplecpu-asm basic.asm driver.asm`. Pictures and sounds are looked up
beside the source.

`simplecpu-run` takes a ROM or an `.asm` file:

| Option | Does |
|---|---|
| `--max N` | stops after N instructions |
| `--microcode naive` or `optimal` | the set to run on |
| `--ram ADDR COUNT` | prints COUNT bytes of RAM from ADDR at the end |
| `--stack-size BYTES` | the hardware stack's size, as for `simplecpu` |

## Working in the repository

The repository has three scripts that wrap CMake:

```bash
./c                # build the release version into build/release
./c --debug        # a debug build into build/debug
./c --test         # build, then run the tests
./c --clean        # configure from scratch
./r                # boot BASIC
./r pacman         # run build/release/roms/pacman.rom
./r game.rom       # run any ROM
./r --make hello   # simplecpu-make, and --asm, --cc and --run likewise
./r --ide          # the IDE
./d                # a distribution for this machine under dist/
```

`./r` passes everything after its own options on to the program, so
`./r --make hello --listing` works. The build also turns every example
in `examples/` into a ROM in `build/release/roms/`.
