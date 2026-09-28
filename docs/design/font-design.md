# Fonts

A font becomes an asset a program draws, loads and switches like any other.
The GPU learns a text cell smaller or larger than 6 by 8, and the grid
follows the cell. BASIC gets two words for it, and the sprite editor gets a
font mode. This note records the decisions Eddie took on 2026-09-28, with
their trade-offs, and what the code did before them.

## Contents

- [What exists today](#what-exists-today)
- [The font asset](#the-font-asset)
- [The GPU](#the-gpu)
- [Names on the cartridge](#names-on-the-cartridge)
- [BASIC](#basic)
- [The interpreter's terminal](#the-interpreters-terminal)
- [The font editor](#the-font-editor)
- [Order of work](#order-of-work)
- [Documents this changes](#documents-this-changes)
- [Decisions taken here](#decisions-taken-here)

## What exists today

This records the code before phase 1. 2866ea9, cd6a53a, 3a36824 and 38412d9 changed it.

### CMD_LOAD_FONT

`CMD_LOAD_FONT` ($57) takes a cartridge address in DATA0 to DATA2
(src/devices/gpu.cpp:698). The blob starts with a glyph count, 0 meaning
256, and a height byte. The GPU copies count times height bytes, back to
back, into its font RAM from code 0 up.

Font RAM is read at a stride of 8 bytes a glyph (`glyphRowOf`,
src/devices/gpu_text.cpp:36). So a height of 8 works. Any other height
packs the glyphs closer than they are read, and every glyph after the
first is misaligned. The height is not honored as a height.

The command appears by name in the assembly guide's command table
(docs/guides/assembly.md:898) and in the IDE manual's argument table. No
guide shows an example. One test loads a blob (tests/devices/text_test.cpp,
"loads a 256-glyph font from the cartridge"). A second test only checks the
name in the command table.

The two GPU design notes disagree about the blob. docs/design/gpu-ports.md
says 256 glyphs of 8 bytes. docs/design/port-redesign.md says the blob leads
with a glyph count and a height, which is what the code reads.

### The built-in font and the grid

src/devices/font.h holds the built-in font as art: one string a glyph,
six characters a row, rows split by `/`, X for a set pixel. It covers codes
$20 to $7F. The block at $7F fills all six columns, so a cursor leaves no
stripe. `FONT_W` is 6 and `FONT_H` is 8.

The grid is compile time. src/devices/gpu.h derives `TEXT_COLS` 42 and
`TEXT_ROWS` 32 from the font. The printf overlay is an array of 1344 cells
(`overlayChar`), and text mode reads 1344 bytes of RAM from `textBase` at a
stride of 42. `drawGlyph` draws `FONT_W` by `FONT_H` pixels of every glyph.
Power on seeds font RAM with the built-in font.

src/cc/layout.h holds its own copy of 42 and 32. It places the C stack top,
`C_STACK_TOP`, at $FAC0 below a text screen, in every C program.

### BASIC's screen

src/basic/basic.h, not term.c, defines `SCREEN` $FAC0, `COLS` 42 and
`ROWS` 32. term.c uses them. $FAC0 plus 1344 ends at $FFFF, the top of RAM.

The C heap stack, D3, starts at `C_STACK_TOP` and grows down to the end of
the interpreter's .ram, 20874 bytes today. So $F000 to $FABF is the top
2752 bytes of BASIC's heap stack. The hardware stack is separate memory and
is not involved.

### Names, slots and assets

simplecpu-make rewrites the name after CALL, JSR and JMP, and the target of
USR, into a slot number before the ROM is written (`resolveLabels`,
src/project/project.cpp:97). A name that is a BASIC variable is left alone.
The interpreter knows numbers only. A name at the prompt is error 26,
`name IS A ROUTINE NAME, WHICH ONLY A BUILT PROJECT KNOWS`.

The storage device finds BASIC program slots by name: STO_LOAD, STO_SAVE,
STO_DELETE and STO_CATALOG over the cartridge's BAS chunk. It reads nothing
else on the cartridge.

The assembler places an asset in the cartridge with `.image`, `.sample` or
`.file`, and the C compiler with `__image`, `__sprite`, `__palette`,
`__sample` and `__file`. Each placed asset gets a line in the cartridge's
ASET chunk: kind, the assembly label, offset and size. ASET lists only what
a directive placed, and by label rather than by file name. BASIC has no way
to reach an asset.

### The sprite editor

src/ide/sprite_editor.* edits a strip of frames on the machine palette. The
tools are Pencil, Line, Rect, Ellipse, Fill, Gradient and Roll, on the keys
P, L, R, E, F, G and O. X swaps the two colours, space plays, and comma and
period step frames. It has undo, onion skin and an animated preview through
the screen's display effects. The pane title is `Sprite ship.png`, with a
star when dirty.

The drawing tools are plain functions over `sprite::Frame` in
src/assets/sprite.h. The strip model caps a strip at 16 frames,
`SPRITE_FRAMES_MAX`, and 64 pixels a side. The editor saves its view as
key=value lines in the layout file, with no prefix, and skips a line it
does not know.

## The font asset

### What a font holds

A glyph is 8 bytes, one a row, bit 0 the left pixel. It is a true bitmap
with no colour. A font asset holds all 256 codes.

The header carries the cell, width and height, each 4 to 8. The cell is the
size the font was drawn for. It says how much of the 8 by 8 glyph the GPU
draws, from the top left corner. Pixels outside the cell are kept in the
asset and not drawn.

A new font starts as a copy of the built-in 6 by 8 font at $20 to $7F, cell
6 by 8, every other code blank. The copy widens the block at $7F to all
eight columns, so the cursor fills a cell up to 8 wide. src/devices/font.h
stays as it is.

### The file on disk

The extension is `.font`, beside `.image`, `.palette` and `.sample`.
The file is text drawn as art, the way src/devices/font.h keeps the
built-in font. It diffs line by line in git and opens in any editor.

```text
SimpleCPU-8 font
cell 6 8

$41 A
.XXX....
X...X...
X...X...
XXXXX...
X...X...
X...X...
X...X...
........
```

The rules, all checked on load with the line number in the message:

- The first line is `SimpleCPU-8 font`.
- The second is `cell`, a width and a height, each 4 to 8.
- Then 256 glyphs, codes $00 to $FF, each once and in order.
- A glyph starts with its code as `$` and two hex digits. For $21 to $7E the
  character follows after a space, as a label the loader ignores.
- Eight rows follow, each eight characters of `X` or `.`, the left one bit 0.
- Blank lines between glyphs are allowed. Nothing else is.

Every code is written, blank ones too, so one font has one text and a
change to one glyph is a change to nine lines. A font is about 2,560 lines
and 20 KB.

### On the cartridge

The blob `CMD_LOAD_FONT` reads is two header bytes, width then
height, and the 2048 glyph bytes, code 0 first. The glyph count goes. A
font always has 256 codes, so a count has nothing left to say.

The `.font` to blob converter is one function. simplecpu-make, the
assembler's `.font` directive and C's `__font` all call it.

## The GPU

Three commands own the cell.

`CMD_LOAD_FONT` reads the header, stores the 256 glyphs and sets the cell
from the header. The grid follows: columns are 256 divided by the width,
rows 256 divided by the height, rounded down. A 5 wide cell gives 51
columns and leaves one pixel unused at the right.

`CMD_TEXT_CELL` sets the cell alone, width and height 4 to 8 in DATA0 and
DATA1, and leaves the glyphs as they are. Its code is $58, the next free
text command.

`CMD_RESET_FONT` puts the built-in glyphs back and sets the cell to 6 by 8.
It takes no arguments. Its code is $59. It is what power on does, as one
command.

Every cell change, by any of the three, clears the overlay and puts the
overlay's cursor at column 0, row 0.

A width or height outside 4 to 8 crashes the machine with a named error,
the way a bad instruction does. That holds for a font header and for
DATA0 and DATA1. Its name is `bad-text-cell`. No device crashes the machine today,
so this adds the first crash kind a device raises.

Two direct reads give the current grid: `GPU_TEXT_COLS` at $1D and
`GPU_TEXT_ROWS` at $1C. The port map keeps its direct reads at the ceiling,
$1E and $1F today, and grows them down.

The printf overlay in graphics mode follows the cell too. Its array is
sized for 64 by 64, the 4 by 4 case, 4096 cells. Text mode reads columns
times rows bytes from `textBase` at a stride of the column count.
`CMD_TEXT_AT` wraps at the current columns and rows.

Power on stays 6 by 8, 42 by 32, with the built-in glyphs. There is no RAM
font: glyphs reach the GPU from the cartridge, or from the GPU itself
through `CMD_RESET_FONT`.

The existing blob format goes, since breaking changes are free until
release. Its one test is rewritten for the new header and joined by tests
for the cell.

## Names on the cartridge

BASIC finds an asset by name at run time, through the storage device.

### STO_FIND

A name in, the asset's kind, cartridge address and length out, in one
cycle. The name goes in the way STO_LOAD takes one, through STO_NAME_HI and
STO_NAME_LO. A name the directory lacks sets STO_STATUS to `STO_NOT_FOUND`.

The answer is seven bytes written at STO_ADDR_HI and STO_ADDR_LO,
high byte first. One byte of kind, three of cartridge address, three of
length. STO_LEN then reads 7, the bytes moved. The kinds are named constants in src/devices/storage_ports.h and in
the registry, `STO_KIND_FONT` among them.

The host hands the storage device the directory the way it hands it the BAS
slots today.

### The directory is ASET

In a project with BASIC, simplecpu-make places every file in assets/ into
the cartridge. Each gets a line in ASET, by kind and by the file's stem:
`font`, `small`, its offset and its size. A font is placed as the blob
above. Lines a directive placed keep their assembly labels.

A name matches ignoring case. The build refuses two stems that differ only
in case. It reports a stem BASIC cannot write as a note. BASIC's own rule
decides: a letter, then letters and digits, at most 10 of them. So a dash,
a `_`, a dot, a leading digit or a space each earns the note.

### Directives

The assembler gets `.font('small.font')` and C gets `__font("small.font")`.
Both place the blob through the same converter as simplecpu-make, and list
the font in ASET with the kind `font`.

## BASIC

The words must be as friendly as `CALL DOUBLE`. No ports, no IN or OUT, and
no number the user has to know except a size. Statements take no
parentheses in this BASIC.

| Statement | Does |
|---|---|
| `LOADFONT SMALL` | loads the font asset named small, takes its cell, clears the screen |
| `LOADFONT` | runs `CMD_RESET_FONT`: the built-in font and 42 by 32, clears the screen |
| `SETTEXT 8, 8` | sets the cell width and height in pixels, keeps the glyphs, clears the screen |

The label is the asset's file stem. `LOADFONT` asks STO_FIND and checks the
kind. A `small.png` in the project gives the same error as no `small` at
all. LIST shows the name, and the word works at the prompt.

`SETTEXT` checks each size against 4 to 8 before the GPU sees it. So a BASIC
user meets error 32, never the machine's crash.

| Code | Name | On the screen |
|---|---|---|
| 32 | E_TEXTSIZE | `TEXT SIZE IS OUT OF RANGE [4,8]` |
| 33 | E_NOFONT | `NO FONT CALLED name ON THE CARTRIDGE` |

The interpreter's C runs the GPU commands behind the words.

## The interpreter's terminal

`COLS` and `ROWS` become two bytes on the system page: `SYS_COLS` at $22 and
`SYS_ROWS` at $23. The interpreter fills them from the GPU's two reads at
boot and after every `LOADFONT` and `SETTEXT`. A driver reads them to know
the stride. $24 to $2F stay reserved.

The text buffer moves to a fixed 4 KB at $F000, so a 64 by 64 grid fits.
It ends at $FFFF as it does now.

What has to move is the C heap stack. Its top is `C_STACK_TOP`, $FAC0, in
src/cc/layout.h. The compiler gets an option to set it. The flag is
`--heap-stack-top ADDR`, beside the existing `--heap-stack-size`. BASIC's
build and BASIC and C projects pass $F000. Every other C program keeps
$FAC0. BASIC's heap stack then runs from $F000 down to the end of its .ram,
2752 bytes shorter and still about 40,500 bytes.

`SYS_COL` and `SYS_ROW` keep their addresses. Their documented range becomes
0 to `SYS_COLS` minus 1 and 0 to `SYS_ROWS` minus 1.

These use $FAC0, 64192 or a 42 column row for BASIC's screen, and move with
it:

- tests/basic/basic_test.cpp, which reads the screen at $FAC0, 42 wide.
- examples/basic-driver/driver.asm, which writes row 20 of BASIC's screen.
- docs/guides/basic.md, "Memory as numbered boxes", which POKEs 64192.
- docs/guides/assembly.md, which names BASIC's `POKE 64192,65`.
- src/ide/run_panes.cpp, the memory pane's "text screen" button at $FAC0.
- src/ide/datapath.cpp, whose text names $FAC0 and 1344 bytes.

examples/matrix and examples/textmode map their own screen at $FAC0. They
are programs of their own, keep 6 by 8, and keep working.

## The font editor

The sprite editor class gets a second mode. The asset decides the mode when
it opens, and the pane title says which: `Sprite: ship.png` or
`Font: small.font`. There is no toggle inside the pane.

Sprite mode keeps what it has: the palette, gradient, transparency, frame
size and count, and the preview through the screen effects.

Font mode has none of those. It has:

- set and clear as two buttons
- the glyph sheet, with the code under each cell
- the cell width and height fields
- the region outside the cell shaded
- a half circle tool, one tool with four orientations, filled or outlined
- a four-cell view in two forms, frames and block
- a preview that renders a sample line of text in the font at real size

The four-cell view as frames shows four consecutive codes side by side,
played as animation frames, which is the sprite strip. As a block it edits
a 2 by 2 group of codes as one 16 by 16 picture that splits back into four
glyphs.

Shared tools keep the same keys and toolbar positions in both modes. A tool
that belongs to one mode is absent in the other, not greyed out. Roll is
the rotate: pixels that leave one edge come back at the other, in four
directions.

Layout file keys are separate per mode. Today's keys carry no prefix, and
setView skips a line it does not know. So a layout saved before the change
loses the sprite editor's view once.

The drawing functions in src/assets/sprite.h are reused unchanged. A font
loads as 256 one-bit frames of 8 by 8. The strip caps of 16 frames and 64
pixels belong to sprites, so a font is read by its own loader, not by
`sprite::load`.

## Order of work

Each phase leaves master working.

### Phase 1, the asset format and the GPU

Done in 2866ea9.

The `.font` reader and writer, the blob converter, `CMD_LOAD_FONT` with the
cell, `CMD_TEXT_CELL`, `CMD_RESET_FONT`, the crash, the two reads, and the
grid in the overlay and in text mode. BASIC is untouched and still runs 42
by 32 at $FAC0.

Tests:

- A font file reads and writes back to the same text.
- Each malformed file is refused, naming its line.
- A new font equals the built-in glyphs at $20 to $7E, has $7F eight
  columns wide, and is blank elsewhere.
- The rewritten load test, and a load that sets each cell from 4 to 8.
- `CMD_TEXT_CELL` keeps the glyphs and changes the grid.
- `CMD_RESET_FONT` brings back the built-in glyphs and 6 by 8.
- A cell change clears the overlay and homes its cursor.
- A width or height of 3 or 9 crashes with `bad-text-cell`.
- The reads give 42 and 32 at power on, 32 and 32 at 8 by 8, 64 and 64 at 4
  by 4.
- A glyph draws only inside its cell.
- Text mode reads at the new stride.
- The overlay wraps and scrolls at the new width and height.
- The command and port tables name the new entries.

### Phase 2, the names

Done in cd6a53a and 3a36824.

STO_FIND, the ASET change, the placement rule in simplecpu-make, the name
notes, and the `.font` and `__font` directives.

Tests:

- The storage device finds a placed asset by name in any case, and gives
  its kind, address and length.
- A name the directory lacks gives `STO_NOT_FOUND`.
- simplecpu-make places every file in assets/ of a BASIC project and lists
  it by kind and stem.
- The build refuses `Small.font` beside `small.png`.
- The build notes `big-font.font` and a stem of 11 characters.
- A ROM without the new lines still loads.
- `.font` and `__font` place the same blob simplecpu-make does.

### Phase 3, BASIC and the terminal

Done in 38412d9.

The buffer at $F000, `--heap-stack-top`, `SYS_COLS` and `SYS_ROWS`,
`LOADFONT` and `SETTEXT`, errors 32 and 33, and the files listed under the
terminal.

Tests in tests/basic:

- BASIC boots at 42 by 32 with the screen at $F000, and `SYS_COLS` and
  `SYS_ROWS` read 42 and 32.
- `SETTEXT 8, 8` gives a 32 by 32 grid and `SYS_COLS` 32, and the cursor
  wraps at column 32.
- `SETTEXT 4, 4` gives 64 by 64 and scrolls at row 64.
- `SETTEXT 3, 8` and `SETTEXT 8, 9` give error 32, and the machine runs on.
- `LOADFONT SMALL` in a built project draws with the font's glyphs and cell.
- `LOADFONT` alone brings back 6 by 8, 42 by 32 and the built-in glyphs.
- `LOADFONT NOPE`, and `LOADFONT SHIP` beside a `ship.png`, give error 33.
- `SYS_COL` and `SYS_ROW` stay inside the grid.

tests/cc covers `--heap-stack-top` and the unchanged $FAC0 default.
tests/project covers a BASIC and C project built with $F000.

### Phase 4, the editor

Font mode, the half circle, the four-cell view as frames and as a block,
and the text preview. If the phase splits, the frames view comes first.

Tests: a `.font` opened as 256 frames and saved back is the same text. The
half circle in four orientations, filled and outlined, and the block split
and join are functions over `Frame`, tested without a window. The pane is
checked with `simplecpu-ide --screenshot` under Xvfb, in both modes.

## Documents this changes

Each is edited only when Eddie says so. Until then they describe today.

- docs/design/text-design.md, "Custom fonts" and "Ports and commands".
- docs/design/gpu-ports.md, the `CMD_LOAD_FONT` row and paragraph, the two
  new commands and the port table.
- docs/design/port-redesign.md, the `CMD_LOAD_FONT` row.
- docs/design/memory-map-design.md, the text screen at $FAC0.
- docs/design/c-compiler-design.md and docs/design/c-language.md, the stack
  top at $FAC0 and the new option.
- docs/storage-design.md, STO_FIND and the directory.
- docs/basic-system-page.md, `SYS_COLS` and `SYS_ROWS`, the ranges of
  `SYS_COL` and `SYS_ROW`, and errors 32 and 33.
- docs/guides/command-line.md, `--heap-stack-top`.
- docs/guides/basic.md and docs/guides/assembly.md, as listed under the
  terminal.

## Decisions taken here

Eddie, 2026-09-28.

### The font, the GPU and BASIC

- A font is an asset of 256 glyphs, 8 bytes each, bit 0 on the left, no
  colour. Its header carries the cell, 4 to 8 each way, drawn from the top
  left of the glyph. On disk it is text drawn as art. A new font copies the
  built-in font at $20 to $7F.
- `CMD_LOAD_FONT` sets the glyphs and the cell. `CMD_TEXT_CELL` sets the
  cell alone. The overlay follows the cell and holds 64 by 64. Power on is
  6 by 8. There is no RAM font.
- BASIC gets `LOADFONT label`, `LOADFONT` and `SETTEXT w, h`, each of which
  clears the screen. There are no ports and no numbers but a size. The
  interpreter's C does the GPU work.
- The terminal reads its columns and rows from the GPU. Its buffer is 4 KB
  at $F000. `SYS_COL` and `SYS_ROW` keep their addresses.
- The sprite editor gains a font mode chosen by the asset, with the title
  naming the mode. Shared tools keep their keys and places. Tools of one
  mode are absent in the other. The layout keys are per mode. The drawing
  functions are reused unchanged.

### LOADFONT SMALL finds the font at run time

Through STO_FIND over ASET, which simplecpu-make fills with every file in
assets/ of a BASIC project, by kind and stem. The name then survives LIST,
works at the prompt, and serves every later asset word.

Considered and not taken, build time, like `CALL DOUBLE`:

- simplecpu-make would write the address into the BASIC text. A cartridge
  address is three bytes and a BASIC number two. So the text would hold two
  numbers, `LOADFONT 0, 4096`, or the address divided by 256 for a font
  placed on a 256 byte boundary, `LOADFONT 16`.
- LIST would show the number, and the name would not work at the prompt.
- `resolveLabels` leaves a name that is a BASIC variable alone, so a font
  called `a` or `f1` would not resolve.

The run time way costs a device command, the placement rule and a lookup
in the interpreter. ASET already carried kind, name, offset and size, but
only for what a directive placed and by label, so it had to widen.

Names match ignoring case, because the lexer turns every name to capitals
and a file system may be case sensitive. Two stems that differ only in case
are refused, so the match is never a guess. A stem BASIC cannot write is a
note, not an error, since C and assembly can still use it.

`LOADFONT` checks the kind, so a picture never reaches the GPU as a font.
A wrong kind reads as no font at all, because to BASIC it is.

### The four-cell view is both

Four consecutive codes as frames animate a character, and the sprite strip
already does that. A 2 by 2 block edited as one 16 by 16 picture draws large
characters and tiles. The frames come first if phase 4 splits.

### The half circle is one tool

Four orientations, filled or outlined. A general arc tool would cover it
and more, and asks for more input per shape. That was not wanted.

### LOADFONT alone runs CMD_RESET_FONT

The built-in font lives in the GPU, so the GPU puts it back. Considered and
not taken: a built-in `.font` asset in BASIC's ROM, and a reserved
cartridge address that `CMD_LOAD_FONT` reads as built-in.

### The heap stack top is a compiler option

`--heap-stack-top`, passed by BASIC's build and by BASIC and C projects.
Every other C program keeps $FAC0 and its 2752 bytes. Moving the constant
for all would have changed every C program and three design documents for
the sake of one.

### SYS_COLS and SYS_ROWS on the system page

At $22 and $23, so a driver knows the stride without asking the GPU. C
globals in the interpreter were the alternative, and no driver could have
read them.

### Two direct reads for the grid

At $1D and $1C. A command answering on DATA0 and DATA1 was the alternative,
and would have kept the port map's rule that only the ceiling reads are
direct. The grid is read after every cell change, so a direct read earns
its place.

### A bad cell crashes the machine

The GPU has no error channel, and ignoring or clamping would draw something
the program did not ask for. BASIC checks the range first, so a BASIC user
only ever sees error 32.

### A cell change clears the overlay and homes the cursor

A new cell moves every overlay cell and can leave the cursor outside the
grid. Keeping the codes would show old text at new places.

### Errors 32 and 33

`TEXT SIZE IS OUT OF RANGE [4,8]` in the style of error 13, and
`NO FONT CALLED name ON THE CARTRIDGE` in the style of error 10.

### The new-font copy widens $7F

The block fills eight columns in a new font, so the cursor fills any cell.
font.h keeps its six column block for the built-in font.

### .font and __font are phase 2

They need the same converter as the directory, so they arrive with it.

### The names and layouts proposed in this note

Confirmed by Eddie on 2026-09-28, as proposed:

- `CMD_TEXT_CELL` at $58 and `CMD_RESET_FONT` at $59.
- The crash kind `bad-text-cell`.
- The flag `--heap-stack-top ADDR`.
- STO_FIND's answer as seven bytes at STO_ADDR: kind, three of address,
  three of length, with `STO_KIND_FONT` and its siblings as named kinds.
- `.font` as the file extension, and the blob as width, height and 2048
  glyph bytes.
- The error names `E_TEXTSIZE` and `E_NOFONT`.

### What exists today stays

It records the code before phase 1, and its first line says so.
