# BASIC

The BASIC interpreter runs on the CPU. It is written in the machine's own C
dialect and compiled by simplecpu-cc. So this directory holds C sources, not
C++. Seven of the files are the browser project's, extracted from
packages/ui/src/basic/ with extract.mjs. The script writes each TypeScript
string constant to a file with its exact value. bang.c, store.c and data.c
are new in this tree.
This BASIC differs from the browser's in its drawing words, in RENUM, in
DATA, in HEX$, in its fonts and in its jump cache. `INK` replaces `COLOR` in run.c, and `PIXEL` replaces `POINT` in
expr.c. `PLOT` draws in the INK colour, where it drew white. `CIRCLE rx,
ry, fill` draws a ring in INK with an optional fill, where `CIRCLE r`
filled a disc. main.c sets INK to white at boot. `RENUM start, step` is
new: ed_renum in edit.c, called from rt_line in run.c.
DATA, READ, RESTORE and DATA(n) are new in data.c. POKE takes a list of
values in run.c. lex.c records where each token starts, `lx_tokpos`, and
has a raw mode for strings, `lx_raw`, that data.c reads DATA lines with.
`HEX$(n, w)` is new in expr.c, beside STR$, with its width error in run.c.
`LOADFONT name`, `LOADFONT` and `SETTEXT w, h` are new in run.c. The
screen moved to 4 KB at $F000 in basic.h, and term.c reads the grid from
the GPU into SYS_COLS and SYS_ROWS on the system page.
run.c keeps the line each GOTO, GOSUB and `THEN n` found while a program
runs, docs/design/basic-speed.md. A POKE into the program text while it
runs is not supported.
A refresh from upstream undoes all of this, so redo it after one.

## Files

| File | Holds |
|---|---|
| basic.h | Shared declarations, limits, error codes. Includes gpu.h, io.h and sys.h. |
| main.c | The prompt loop and the build line. Slot 0, so main runs from here. |
| term.c | The text mode terminal: cursor, scrolling, readline over IO_KEY. |
| lex.c | The tokenizer. |
| expr.c | Expression evaluation, numbers and strings. |
| strings.c | The string heap and the string functions. |
| edit.c | The program store: insert, replace, delete, list and renumber lines. |
| run.c | The statements: RUN, GOTO, GOSUB, FOR, IF, PRINT, POKE, DOKE, CALL, JMP, READ, RESTORE, the break check. DATA's own reading lives in data.c. |
| bang.c | The bang statement and its vector at $8000. See docs/storage-design.md. |
| store.c | The storage driver: LOAD, SAVE, DELETE and CATALOG through the storage device. |
| data.c | DATA lines: placement at RUN, DATA(n), READ and RESTORE. See docs/basic-data-design.md. |
| basic_rom.h, basic_rom.cpp | sc8::basicRom(), the embedded ROM bytes, basicAsm() the assembly and basicSources() the C, for a project that holds BASIC and C. |
| extract.mjs | The extraction script. Needs Node 22 with --experimental-strip-types. |

## Build

CMakeLists.txt runs three custom commands in the build tree, in order:

1. simplecpu-cc compiles the ten C files, passed by bare name from this
   directory so `#include "basic.h"` resolves, into basic.asm and ROM.h.
2. simplecpu-asm burns basic.asm into basic.rom with the optimal microcode
   and the title BASIC.
3. cmake/EmbedBinary.cmake turns basic.rom into basic_rom_data.h, a byte
   array plus its size.

The sc8_basic library compiles basic_rom.cpp against that header and
exports `std::span<const uint8_t> sc8::basicRom()`. The virtual computer
boots those bytes for `simplecpu --basic`, and tests/basic boots them into a
headless machine. Both tools come from the same build, so the ROM needs
SC8_BUILD_TOOLS on. The headless preset builds it.

To refresh the sources after an upstream change:

```bash
cd src/basic
node --experimental-strip-types extract.mjs ../../../ref/packages/ui/src/basic .
```
