# The standalone version

SimpleCPU-8 as a native program for Windows, macOS and Linux, replacing the
browser version. The machine is the one in docs/design/architecture.md. This
file records what the standalone version adds. The decisions appear in the
order they were settled.

## Contents

- [Programs](#programs)
- [The ROM](#the-rom)
- [Microcode in a ROM](#microcode-in-a-rom)
- [The screen](#the-screen)
- [BASIC](#basic)
- [The storage device](#the-storage-device)
- [The bang extension chain](#the-bang-extension-chain)
- [IDE levels](#ide-levels)
- [Source layout](#source-layout)
- [Port status](#port-status)

## Programs

Four executables come out of one build.

| Program | Job |
|---|---|
| simplecpu | The virtual computer. Boots a ROM in a window with keyboard and audio. |
| simplecpu-ide | Editor, assembler, debugger, microcode tools and ROM browser around the same computer. |
| simplecpu-asm | Assembles a source file and burns a ROM. |
| simplecpu-run | Runs a ROM or source headless and prints the machine state. For tests and scripts. |
| simplecpu-cc | Compiles the machine's C dialect to assembly. |
| simplecpu-make | Builds a directory of C, assembly, BASIC and assets into one ROM. |

simplecpu-cc compiles C to assembly. `simplecpu-cc main.c lib.c -o main.asm`,
with `-D NAME=VALUE`, `-msoft-mul`, `-zp-reserve N` and `--rom-header ROM.h`.

## Making a program

A directory is a program. `simplecpu-make mygame/` compiles every `.c` in
it together, with the `.h` files beside them reachable by `#include`. It
appends every `.asm` after the generated assembly, so a driver sits at its
`.org` slot. Every `.bas` goes into the ROM as a slot named after the file.
A picture or a sound named by `__sprite`, `__image` or `__sample` in C, or
by `.image` and `.sample` in assembly, resolves inside the directory. The
title is the first line of README.md. There is no manifest to learn.

Two layouts are read. The flat one keeps everything in one folder and
writes `mygame/mygame.rom`. The larger one has `src/` for the sources,
`assets/` for the pictures and sounds, and writes `build/mygame.rom`. A
folder with a `src/` directory is the larger layout. examples/hello-c is
the flat case and examples/bounce the larger one. The build makes every
example that holds C or a `src/` folder this way. src/project is the code
behind both `simplecpu-make` and the IDE.

What the sources hold decides the kind. A `.c` makes a C project. A
`.bas` with no `.c` makes a BASIC project: the ROM is the interpreter,
every `.bas` is a slot named after the file, and a slot named `AUTORUN`
runs at power on, so the ROM boots into the program. An `.asm` in a BASIC
project is a driver appended to the interpreter. Only `.asm` files make
an assembly project. A `microcode.txt` among the sources is the ROM's
microcode set.

A `.bas` beside a `.c` makes a mixed project. The ROM is one C program:
the interpreter's own sources compiled together with the `.c` files, so
`main` is the interpreter's and the user's files may not define one. BASIC
boots and runs `AUTORUN` as before. Every public function in the user's
files is kept whether or not anything calls it, because BASIC calls it by
instruction slot: `JSR DOUBLE` in a `.bas` names a C function or an
assembly label, and the build writes the label's slot into the program
before it goes into the ROM. An unknown name is a build error naming the
file and line. `<basicvars.h>` gives the C side `basic_get` and
`basic_set` on the variables A to Z. examples/basic-c is the worked
example, and docs/basic-system-page.md has the JSR and JMP statements.

`simplecpu-make new mygame --c` lays a project out, and so do `--basic`,
`--assembly` and `--microcode`: the folders, a README, a `.gitignore`
for `build/` and a first program that builds and runs. The IDE's Project
tab has the same four buttons. The microcode kind starts from the naive
set written out as `microcode.txt`, and its program shows what every
instruction costs under that set and under the optimal one, side by side.
The build counts the rows and writes the table into the ROM, so a changed
row shows as a changed number at the next run. examples/cycles is that
project.

`simplecpu --rom game.rom` prints the frame rates when it ends: the
window's and the machine's, average, worst second and best second.
`--seconds N` ends a run by itself.

In the IDE, the Edit level has a Project tab beside Source and BASIC. It
opens a folder, lists its files, edits one at a time and has one Build
button. Build is `simplecpu-make` on the folder: the ROM lands in `build/`
and the machine. The assembly it made goes to the Source pane, so the
generated code can be read next to the C. Opening a `.c` file from the
command line opens its project and builds it, and `--run` starts it.

The Edit level also shows the machine's screen, so a program runs where it
is written. The editor and the interpreter's program memory are two views
of one program. Run in BASIC boots the interpreter, writes the program
where the system page says it lives and types RUN. Push to machine writes
it without running. While BASIC waits at READY, the IDE reads the memory
back every frame. A line typed or changed on the small screen shows up in
the editor. When the editor holds edits of its own, the pane says the
machine changed and offers Pull instead. `simplecpu-ide game.bas --run` does the same from the
command line. The Run level, reached from the Level menu or F2, puts
registers, memory, breakpoints and the listing around that screen.

Command lines that are settled:

```text
simplecpu --rom game.rom --fps 60
simplecpu --rom game.rom --max
simplecpu --rom game.rom --microcode naive
simplecpu --rom game.rom --crt 0.6
simplecpu --rom game.rom --no-crt
simplecpu --basic
simplecpu --basic hello.bas --run
simplecpu --rom basic.rom
simplecpu-asm main.asm -o game.rom --microcode optimal --title "Pac-Man"
simplecpu-run game.rom --ram 0 16
```

## The ROM

A `.rom` file is the cartridge. It is one file that goes into the machine.
It needs no other file beside it. Program memory stays read-only and
separate from RAM. FETCH is its only reader. RAM holds data and memory mapped IO.

The file is chunked. A reader skips what it does not know, and the IDE can
list what a ROM holds. src/core/cartridge.h has the byte layout. The chunks:

| Chunk | Holds |
|---|---|
| PROG | Machine code, 3 bytes per instruction. The only required chunk. |
| RAM | The initial data RAM image from `.ram`. |
| DATA | The `.data` section: every resource, bytes inside the file. |
| ASET | The asset table: kind, name, offset and size per resource. |
| UCOD | The microcode set, by name or as full text. |
| META | Title, author, and whatever a tool adds. |
| BAS | Saved BASIC programs, one per named slot. |

A program may be laid out by instruction slot. `.org 4096` in the code
section places what follows at slot 4096, and labels follow. Slots between
runs are unloaded. A fetch from one crashes with an illegal program address,
the way a fetch past the end does. The ROM stores one PROG chunk per run,
each with its first slot. So a driver sits at a known slot apart from the
program that calls it. A BASIC bang handler is one such driver, reached
through a vector set with DOKE. simplecpu-asm takes more than one source
on its command line. Each starts in the code section, and `.code`, `.ram`
and `.data` may each open again, with offsets carrying on. So
`simplecpu-asm basic.asm driver.asm` burns the interpreter and a driver into
one ROM. examples/basic-driver is the worked example: the interpreter at
slot 0, a driver at slot $F000, and a BASIC program in the BAS chunk that
DOKEs the vector and calls the driver.

During development `.file('x.bin')`, `.image('x.png')` and `.sample('x.wav')`
read from the directory of the source file. Burning copies the bytes into
the DATA chunk, so a distributed ROM carries every resource.

## Microcode in a ROM

The two built-in sets live in the computer, and a ROM names them:
`@optimal` or `@naive`. The optimal set never travels in a file. The encoder
replaces its text with the reference, so it stays hidden in the computer. The
naive set may travel as text or by reference. A user's own set is stored as
its full text. It is the only set a ROM carries in full. A ROM that
names the optimal set runs at full speed. The IDE refuses to show, step or
export its rows. To trace or edit, select `@naive` or a custom set. The machine runs
slower and every row is visible. The command line and the IDE override the
ROM's choice without changing the file.

## The screen

The GPU composes 256 x 256 pixels. The display scales that to the window
through a CRT shader with scanlines, curvature, blur, bloom and vignette.
Each effect has its own strength from 0 to 1. The whole look has an on/off
switch. Presentation only: the shader reads the bytes the GPU composed, and
no program can see which effect is on. The settings belong to the person,
not the ROM. They live in user settings and never in a project or a
cartridge.

## BASIC

The interpreter runs on the CPU, written in the machine's C dialect. Editing
a program on the 256 x 256 screen stays. Two things are new. Programs can be
saved to and loaded from the ROM by name, so one ROM carries a library. And
the IDE gets a BASIC editor pane. It saves into the ROM or pushes the text
into the interpreter's RAM, so `RUN` picks it up.

BASIC boots two ways. `simplecpu --basic` runs the interpreter ROM built
into the executable, so a SAVE lives only for that session. `simplecpu
--rom build/roms/basic.rom` boots the same interpreter from a file, and a
SAVE or DELETE writes that file back. Copy basic.rom to make a workspace
of your own, and keep as many as you like.

## The storage device

A fifth device on the bus, on ports $50 to $5F. It gives the running
machine a way to read and write the ROM's BAS chunk. The commands are LOAD,
SAVE, DELETE and CATALOG over named slots. simplecpu writes the ROM file back on SAVE and DELETE.
The port map, the commands and the status byte are settled in
docs/storage-design.md. src/devices/storage.h is the device.

## The bang extension chain

BASIC gets a bang statement, spelled with an exclamation mark, modeled on
the Oric-1 and Atmos. The statement hands the rest of its text to a handler
through a documented RAM vector. D1 points at the NUL-terminated text. The default handler raises
an unknown command error. A driver installs itself by writing its entry address
into the vector. It keeps the old address as its next and passes anything
it does not recognise down the chain. The storage driver is the first link:

```basic
!LOAD "NAME"
!SAVE "NAME"
!DELETE "NAME"
!CATALOG
```

On the Oric, every disc system before Sedoric required the bang prefix. The
Sedoric manual documents the sibling `]` vector at #2F9, set with DOKE. The
ROM disassemblies reachable online do not show the bang handler. So the
Oric vector address itself is not confirmed from a primary source.

Here the vector is the word at $0000, big-endian, holding the handler's
instruction slot. It is the first entry of the BASIC system page, the
first 32 bytes of the zero page, described in docs/basic-system-page.md. docs/storage-design.md has the handler contract, the
dispatch and a driver written in assembly. BASIC gained DOKE, DEEK and
PEEK beside POKE so a driver can install itself from the prompt.

## IDE levels

One screen cannot hold everything, so the IDE has levels. Each level is a
saved pane layout, and switching level swaps the layout. Three to start:

- Edit: the editor, the assembler messages and the manual.
- Run: the screen large, registers, memory, breakpoints, and the code
  listing read-only. No microcode rows at all. This level names the set in
  use, naive, optimal or the user's own, and shows nothing else of it.
- Microcode: the datapath schema with the lanes that fire on each row, the
  flow view and the microcode rows. The listing is the same read-only one.
  Breakpoints can be set and cleared here too. No editor.

The Microcode level respects the seal. On `@optimal` the lanes still light
from the bus events and register writes the machine reports, but the rows
stay hidden.

The levels are built. Each level owns a Dear ImGui dockspace with a fixed
id. imgui.ini keeps three layouts, so a rearrangement inside one level
survives a switch. The hidden levels are submitted with KeepAliveOnly. A
pane both Run and Microcode show, the listing and the registers, has one
window name per level. F1, F2 and F3 switch the level, as does the Level
menu. The CRT toggle sits in the Display menu and has no key.

The Run level's speed choice is the browser's ladder. Trace microcode
comes first, then 0.5, 2, 10, 60, 1k and 100k instructions per second.
Then 30, 60 and 120 frames per second, then MAX. The pacing folds worker.ts into the host
frame loop. Tracing is on for the trace speed and up to 1k instructions
per second, off above. The fast-frame latch is a double click on the Frame
button and applies to the paced speeds up to 1k.

The Edit level carries a BASIC pane beside the source. Save into ROM adds
or replaces a named slot in the cartridge's basic list. A session opened
from a .rom rewrites that file. Any other session carries the slots into
the next burn. Push to RAM waits for the storage device. Until then it
types the text through the input device's key queue, two events per
character. It pauses while more than 32 are queued. The machine has to run
for the interpreter to read the lines.

The manual pane ports the instruction pages and the device reference
tables. Cycle counts come from the shipped sets, plus a column for the
loaded set when it is the user's own. The section prose of the browser's
manual is not ported.

## Source layout

Everything under src/. One library per layer, one executable per program.

| Directory | Target | Depends on |
|---|---|---|
| src/core | sc8_core | nothing |
| src/devices | sc8_devices | core |
| src/asm | sc8_asm | core, devices |
| src/cc | sc8_cc | asm |
| src/basic | sc8_basic, the interpreter's ROM from its C sources | cc and asm at build time |
| src/tools | simplecpu-asm, simplecpu-run, simplecpu-cc, simplecpu-make | asm, cc, assets |
| src/vm | sc8_vm, simplecpu | asm, raylib, miniaudio |
| src/ide | simplecpu-ide | vm, Dear ImGui, rlImGui |

tests/ mirrors the libraries with one doctest executable each.

Dependencies come through CMake FetchContent, pinned in
cmake/Dependencies.cmake. The pins: raylib 5.5, miniaudio 0.11.25, Dear
ImGui 1.92.9b docking, rlImGui at a fixed commit, doctest 2.4.12. raylib's
own audio module is switched off. miniaudio is compiled once, in
src/vm/audio.cpp.

## Port status

Ported from the TypeScript project, with their tests:

- the ISA, the control signals and the eight conflict rules
- the microcode parser and both shipped sets
- the machine
- the expression evaluator and the assembler
- the golden cycle table and the 250 program differential check

Written new:

- the ROM format and the device constant registry
- the virtual computer with frame pacing, its session tests ported from
  session.test.ts into tests/vm
- the keyboard map from main.ts
- the display, the CRT shader and the audio output
- the command line tools
- the IDE with its panes

The C compiler is ported. Its tests run doubles and rom_copy through a
stub GPU in the test rig that answers CMD_RAM_MOVE and CMD_COPY only.

The 25 demos sit in examples/ as assembly. The build burns each into a
ROM under build/roms and CTest runs every ROM headless. The assembler's
output for each demo matches the browser project's golden bytes.

The BASIC interpreter is ported as a ROM built from the C sources in
src/basic. The function sc8::basicRom() hands the bytes to the virtual
computer. The tests in tests/basic boot the ROM into a headless machine.
The GPU, the ACP, the APU, the storage device and the input device sit on
its bus in the tests. The bang statement and the storage driver are in
src/basic/bang.c and src/basic/store.c.

Pending, in the order they unblock each other:

- the GPU: screen, text, sprites, world
- the input device, the APU and the ACP
- the image and audio decoders for `.image` and `.sample`
- the --basic switch of simplecpu, booting sc8::basicRom()
- the storage device in the virtual computer, attached to the cartridge
