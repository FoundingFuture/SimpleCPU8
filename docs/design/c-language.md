# C on SimpleCPU-8

What a programmer may write, and what the compiler does with it. The design
behind it is docs/c-compiler-design.md. The reference is below.

## Contents

- [The subset](#the-subset)
- [Types](#types)
- [Doubles](#doubles)
- [Statements](#statements)
- [Ports](#ports)
- [The libraries](#the-libraries)
- [ROM](#rom)
- [Many files](#many-files)
- [The memory map](#the-memory-map)
- [The entry point](#the-entry-point)
- [Breakpoints](#breakpoints)
- [What it refuses](#what-it-refuses)

## The subset

A defined subset, not all of C. The target is small, and a subset with a
written boundary is teachable where a large language with holes is not.

## Types

| type | bytes | notes |
|---|---|---|
| `char`, `unsigned char` | 1 | |
| `int`, `unsigned int` | 2 | an int is 16 bits here |
| `short` | 2 | the same as `int` |
| any pointer | 2 | |
| arrays | length times the element | |
| `double`, `float` | 8 | IEEE 754, on the coprocessor |

`float` and `double` are the same type. The coprocessor has one float format,
and a program written for another compiler should not fail on the spelling.

`long` is parsed and refused by name. The coprocessor could run it and the
compiler does not build it yet.

## Doubles

The CPU cannot add two of these bytes together. So every operation on a
double is a coprocessor command, and a double lives in RAM rather than in a
register. There is no register here that could hold one.

Add, subtract, multiply, divide, negate, all six comparisons, and conversion
both ways. An int widens on its own. Narrowing a double needs a cast, and it
truncates toward zero the way C does.

```c
double area(double r) { return 3.14159 * r * r; }
```

The bitwise operators and the remainder are integer only, and say so.

An operation is about fifteen instructions. The temps are laid out so the
device reads them where they already are. Its block is A, then B, then the
result, so two operands in consecutive slots need no copying at all.

An integer literal takes the first type it fits, the way C does. So `60000`
is unsigned and `60000 / 3` is an unsigned divide.

## Statements

`if`, `else`, `while`, `for`, `do`, `switch`, `case`, `default`, `return`,
`break`, `continue`, blocks, and declarations inside a block. Functions take
byte and word arguments and return one value. Recursion works: C frames live
on a software stack in data RAM.

Every operator in the table below works, at both widths.

| group | operators |
|---|---|
| arithmetic | `+ - * / %` |
| bitwise | `& \| ^ ~ << >>` |
| comparison | `< > <= >= == !=` |
| logical | `&& \|\| !`, and they short circuit |
| assignment | `=` and every compound form |
| other | `++ -- ?: , sizeof` and casts |

Multiply, divide, remainder and every right shift run on the coprocessor. A
left shift by a small constant is adds, which the CPU does itself.

Inline assembly is a statement:

```c
asm("HLT");
```

## Ports

Two intrinsics reach the hardware, and every built-in name the assembler
knows is a C constant too.

```c
out(GPU_CMD, CMD_CLEAR);
unsigned char f = in(GPU_FRAME);
```

A port has to be a name or a constant the compiler can work out. That is
what lets the assembler see `GPU_CMD` rather than a number.

## The libraries

Two layers. The friendly one is where a program starts:

`#include <graphics.h>`, `<sound.h>`, `<keys.h>`, `<math.h>`, `<disk.h>`,
`<basicvars.h>`.

Plain int coordinates, the usual names, and no port in sight. `graphics.h`
draws: `setcolor`, `cls`, `plot`, `line`, `rect`, `circle`, `ellipse`, each
shape with a `fill` twin, `at` and `printf` for text, `image`, `sprite`,
`spriteat`, `show`, `hide`, `hit`, `nextframe` and `random`. `sound.h`
beeps with a built in square wave and plays samples and MIDI tunes:
`sound_init`, `beep`, `noteon`, `sample`, `play`, `tune`. `keys.h` reads
the arrows and the keyboard: `left`, `fire`, `key`, `waitkey`. `math.h` is
`sqrt`, `sin`, `cos`, `atan2`, `pow` and the rest on `double`, run by the
coprocessor. `disk.h` is `save`, `load`, `erase` and `catalog` on the named
files BASIC uses. `basicvars.h` is `basic_get` and `basic_set` on BASIC's
integer variables A to Z, for a C function that BASIC calls with `CALL` in
a project that holds both. The interpreter's own header is `"basic.h"`,
included with quotes, and in such a project a C file may include it too.

```c
#include <graphics.h>
#include <keys.h>
int main(void) {
    int x = 128;
    while (1) {
        if (left()) x = x - 1;
        if (right()) x = x + 1;
        cls();
        setcolor(YELLOW);
        fillcircle(x, 128, 20);
        nextframe();
    }
}
```

Each header has a library unit behind it, compiled in when the header is
included. A function the program never reaches is dropped, so an unused
library costs nothing. The names are short and common on purpose. A
program that wants `line` for itself leaves `graphics.h` out. Every header
in src/cc/libs.cpp opens with its own reference.

Below them sit the machine's own headers:

`#include <gpu.h>`, `<apu.h>`, `<acp.h>`, `<io.h>`, `<sys.h>`, `<rom.h>`.

There is one wrapper per device command, named after it in lower case, so
the manual's command table maps one to one onto the header. `CMD_SPRITE_DEF`
is `gpu_sprite_def`.

A wrapper is a macro, so it costs exactly the port writes it shows:

```c
gpu_clear(7);          /* two OUTs, no call, no frame */
```

`gpu_printf` is the exception. It is variadic and belongs to the compiler:
the template goes on the cartridge, and the arguments are marshalled into
RAM sized by the format.

```c
gpu_printf("SCORE %u LIVES %hhu", score, lives);
```

An int is 16 bits, so `%d` reads two bytes, `%hhu` one, `%lu` four and
`%llu` eight. The compiler counts the conversions against the arguments.

`sys.h` also has `memcpy`, `memmove`, `memset`, `peek`, `poke`, `rom_copy`,
`rand`, `srand`, `wait_frame` and `halt`. The block moves are one GPU
command each rather than a loop.

## ROM

The cartridge is memory the CPU cannot read. `__ROM` is the storage class
for it:

```c
__ROM const unsigned char ship[] = { 1, 8, 8 };
__ROM const char fmt[] = "SCORE %u";
```

Four rules make it airtight.

- `__ROM` implies `const`.
- A `__ROM` object needs a constant initializer. The cartridge is built at
  assembly time.
- A `__ROM` object cannot be read from C. Its address and size are all the
  CPU gets, and the error says so.
- `const` without `__ROM` is ordinary RAM, readable as usual.

`ROM.h` is generated on every build and lists the cartridge as built. Its
names are in scope without the include, which exists for reading:

```c
#include "ROM.h"

rom_copy(work, ROM_maze, ROM_maze_SIZE);
```

Every object gets `ROM_<name>` for its address, `_SIZE`, and `_BANK`, `_HI`
and `_LO` for the three bytes a device port wants. Two objects with
identical bytes share one address, and `ROM.h` says so.

An asset file goes on the cartridge through one of five initializer forms.
Each is allowed only on `__ROM const unsigned char name[]`, with the
brackets empty, because the file decides the length:

| form | the bytes | read by |
|---|---|---|
| `__image("bg.png")` | width, height, then the pixels row-major | `CMD_BLIT` |
| `__sprite("ship.png", 4)` | frames, frame width, frame height, then each frame | `CMD_SPRITE_DEF` |
| `__palette("bg.png")` | a count byte of 0, then 256 RGB triples | `CMD_LOAD_PALETTE` |
| `__sample("beep.wav")` | unsigned 8 bit mono at 8000 Hz | `CMD_DEF_SAMPLE` |
| `__file("level.bin")` | the file as it is | anything |

A width or height of 256 is written as 0, since a byte cannot hold 256.
`__sprite` reads a horizontal strip with the frames side by side. The
frame width is the image width divided by the count, which defaults to 1.
The compiler refuses a width that does not divide, a count of zero, and a
frame past 64 pixels a side. The file is looked up beside the source, the
way `.image` is beside an assembly file. A missing file is a compile error
that names the file and the form.

```c
#include <gpu.h>
#include <rom.h>
#include "ROM.h"

__ROM const unsigned char bg[] = __image("bg.png");
__ROM const unsigned char ship[] = __sprite("ship.png", 4);

int main(void) {
  gpu_blit(ROM_BANK(ROM_bg), ROM_HI(ROM_bg), ROM_LO(ROM_bg));
  gpu_sprite_def(0, 0, ROM_BANK(ROM_ship), ROM_HI(ROM_ship), ROM_LO(ROM_ship), 0);
  return 0;
}
```

The bytes land in `.data` as plain `db` lines. The generated assembly
needs no asset beside it, and `ROM.h` carries the sizes as usual.

## Many files

A project holds many files, each a tab. A `static` function or variable is
private to its file. A public name defined twice is an error naming the
other file. Nothing `main` cannot reach is emitted.

The build line is a comment in the file that holds `main`:

```c
// build: cc -o game main.c menu.c -lgpu -lio
```

It says which files are compiled and in what order, so the first name holds
slot 0. Exactly one file may carry it. None is fine and means "compile every
file in tab order". Two is an error naming both, because a program with build
lines in two places cannot say which one built it.

The two step shape works as well, for anyone who wants to see objects exist:

```c
// build: cc -c main.c menu.c
// build: ld -o game main.o menu.o -lgpu
```

The Files tab shows the resolved build.

## The other runtime

`-msoft-mul` on the build line turns integer multiply, divide, remainder and
shift back into software: adds, on the CPU, with no coprocessor. The same
`a * b` compiles both ways and the Generated tab shows the difference.

Measured on this machine, per operation, including the call:

| operation | coprocessor | software |
|---|---|---|
| `int * int` | 50 instructions | 361 |
| `int / int` | 50 | 648 |
| `int >> n` | 56 | 306 |

The software routines all work from the top bit down, because this machine
has no right shift. Doubling is an add, and the top bit of a word is the N
flag after a load of its high byte. Those two facts are the whole trick, and
they are worth reading once.

The switch reaches integers only. There is no software float to fall back to,
so `double` would use the coprocessor whichever way it is set. A CPU with no
multiply has no business pretending to have a double.

## The memory map

```text
$0000-$00FF   zero page: the compiler's reservations, then variables
$0100-        globals, the coprocessor scratch, the printf arguments
              ... the C stack grows down from $FAC0 ...
$FAC0-$FFFF   room for a text screen
```

The zero page is the only memory the ALU reaches directly, so it is this
machine's register file, and it holds 256 bytes. Who gets it is decided in
three steps.

1. The compiler's own reservations. The software stack pointer, the return
   value, the compare byte and the runtime operands. Then one temp slot per
   level of the deepest expression in the program.
2. Everything marked `__zp`, in declaration order. If that does not fit, that
   is the error, and it names the bytes asked for, the bytes free and what
   the reservations took.
3. Whatever is left, by weighted count.

The count is mentions, weighted by the loops around them. A mention inside a
loop is worth ten, and ten again for each loop outside that one. So a
variable touched once in a loop beats one touched five times in setup code.
When a `for` has a literal bound the compiler reads it and uses it, capped so
one long loop cannot swamp the program.

What is compared is the work per BYTE, not the total. An 80 byte buffer
touched 33 times asks for eighty times the room a counter does, for a sixth
of the work, so the counter wins.

A variable that does not fit in what is left is passed over. It does not push
everything after it out, because a 400 byte array would then take the whole
page and give it to nobody.

```c
__zp unsigned char frame;   /* I know this one is hot */
```

When a program has run, the tool can do better than a count. The machine
keeps a counter for every address in data RAM, and no program can reach them.
They are the host watching, not the machine telling. Press Measure and the
zero page is placed from what the program actually did.

A count is mentions and a measurement is executions, and the two are not the
same thing. A variable named once inside a loop is mentioned once and touched
a hundred thousand times.

`__zp` on a local is an error naming `static`: a zero page local is a static
by definition. The build pane's Zero page tab shows every byte, what has it,
the reason it won, and how many are left.

The C stack starts at `$FAC0` in every program, whether or not it maps a
text screen. That is the top of RAM less the whole 42 by 32 character grid.
The 1344 bytes buy never having to explain why adding one call to
`gpu_set_textmode` corrupted the display.

## The entry point

`main` is a subroutine. The startup stub calls it and halts when it comes
back, so a program ends by returning:

```asm
__start:
        LD D2 <- 64512
        LD [__sp] <- D2
        JSR main
        HLT
```

Falling off the end of `main` works too. `halt()` from `sys.h` is for
stopping early from somewhere deep, not for ending a program: a `main` that
halts itself makes its own `RET` dead code.

## Breakpoints

The Breakpoints tab in the build pane shows the open file, line numbered. A
circle marks every line that became code. Click one and the machine stops
there.

It stops before the line runs, on the first instruction that line produced.
The variables that line writes still hold what they held. Marks survive a
build, so the workflow is to mark the line, press Load, and stop there.

A line with no circle produced no code. A comment, a declaration with no
initializer, and a closing brace are the usual ones.

## What it refuses

Each of these is a compile error that names the alternative.

| written | why | instead |
|---|---|---|
| `ship[0]` on a `__ROM` object | the CPU cannot read the cartridge | `rom_copy` |
| `__zp` on a local | a zero page local is a static by definition | `static` |
| `long n;` | the coprocessor type is not built yet | `int` or `double` |
| a frame past 255 bytes | a local is reached through `[D1+n]` | a global |
| `out(p, 1)` with a variable port | a port is fixed at assembly time | a name |
| a second public definition | the linker cannot choose | `static` |
