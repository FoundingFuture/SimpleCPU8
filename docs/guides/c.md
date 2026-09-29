# C on SimpleCPU-8

A guide to writing C for the machine. It follows the BASIC guide at
docs/guides/basic.md and assumes you have read it. If you have never
written a program, start there. Every program printed here was built with
`simplecpu-make` and run on the machine before it went in. Read the
chapters in order. The reference behind this guide is
docs/design/c-language.md, which says what the compiler accepts in full.

## Contents

- [From BASIC to C](#from-basic-to-c)
- [The first program](#the-first-program)
- [A project and its build](#a-project-and-its-build)
- [Types and their sizes](#types-and-their-sizes)
- [Functions](#functions)
- [Arrays, pointers and strings](#arrays-pointers-and-strings)
- [Doubles](#doubles)
- [What the compiler refuses](#what-the-compiler-refuses)
- [Drawing with graphics.h](#drawing-with-graphicsh)
- [Keys with keys.h](#keys-with-keysh)
- [Sound with sound.h](#sound-with-soundh)
- [Maths with math.h](#maths-with-mathh)
- [Files with disk.h](#files-with-diskh)
- [BASIC and C together](#basic-and-c-together)
- [Pictures and sounds on the cartridge](#pictures-and-sounds-on-the-cartridge)
- [The three chips](#the-three-chips)
- [The layer below](#the-layer-below)
- [Errors you will meet](#errors-you-will-meet)
- [A small game](#a-small-game)
- [Where to go next](#where-to-go-next)

## From BASIC to C

BASIC reads your program one word at a time, every time round a loop.
That is why the star in Catch the Stars fell one row at a time. C works
the other way round. A compiler reads the whole program once, before it
runs, and turns it into machine instructions. The machine then runs those
instructions with nothing in between. The same game written in C moves
sprites sixty times a second.

Three habits from BASIC carry over. A variable is still a box with a
name. A loop still repeats a piece of program. A decision is still an
`if`. Four things change.

Lines have no numbers. The program runs from the top of a function
called `main`. Braces `{ }` group lines together, the way line
numbers grouped them before.

Every variable is declared before use, with a type. `int x;` makes a box
that holds a whole number. BASIC gave you 286 boxes with fixed names.
C gives you as many as fit in memory, with any name you like.

Programs are split into functions. A function is `GOSUB` with a name,
inputs and an answer. `square(7)` gives 49.

The program is a file on disk. You edit it in a text editor or the IDE,
build it into a cartridge, and run the cartridge. There is no prompt to
type at. That is the price of speed.

The C here is a subset of the language. It is the part a small machine
can run and a beginner can learn in a week. The chapter on what the
compiler refuses lists the rest.

## The first program

Make a folder called `hello` with a folder `src` inside it. Put this in
`hello/src/main.c`:

```c
#include <graphics.h>

int main(void)
{
    at(2, 2);
    print("HELLO FROM C");
    at(2, 4);
    printf("SIX TIMES SEVEN IS %d", 6 * 7);
    return 0;
}
```

Build it and run it:

```bash
build/release/src/tools/simplecpu-make hello
build/release/src/vm/simplecpu --rom hello/build/hello.rom
```

The screen shows the two lines, then the word `halted` in the corner. A
C program stops when `main` returns. BASIC went back to `READY`. C has
nowhere to go back to, so the machine halts.

Read the program from the top. `#include <graphics.h>` pulls in the
drawing library, which `at`, `print` and `printf` come from. `int
main(void)` is where the program starts. `at(2, 2)` puts the text cursor
at column 2, row 2, where BASIC's `PRINT` would have put it. `print`
writes a string. `printf` writes a string with holes in it. `%d` is a
hole for a whole number, filled by the next argument. `return 0` ends
`main`.

Every statement ends with a semicolon. Miss one and the compiler says
`expected ";"` with the line number. Capital and small letters are
different. `Main` is not `main`.

## A project and its build

A C program is a folder. `simplecpu-make` reads the folder and writes a
cartridge. Two layouts work. The small one keeps the `.c` files in the
folder itself and writes `hello/hello.rom`. The larger one, used in this
guide, has three folders:

```text
mygame/
  README.md
  src/main.c
  assets/
  build/mygame.rom
```

`src/` holds the C files. `assets/` holds pictures and sounds. `build/`
receives the cartridge, named after the folder. The first line of
README.md becomes the cartridge title. Every `.c` in `src/` is compiled
together, and a `.h` beside them can be included with quotes:
`#include "util.h"`.

`simplecpu-make new mygame --c` lays the folder out with a README, a
`.gitignore` for `build/` and a `src/main.c` that draws a circle you
steer with the arrow keys. Build it and run it before you change it:

```bash
build/release/src/tools/simplecpu-make new mygame --c
build/release/src/tools/simplecpu-make mygame
build/release/src/vm/simplecpu --rom mygame/build/mygame.rom
```

`simplecpu-make mygame --asm-out` also writes `build/mygame.asm`, the
assembly the compiler produced. It is worth a look once. Every block
names the C line it came from.

The IDE does the same thing with buttons. `./r --ide` opens it. The Edit
level has a Project tab beside Source and BASIC. Type a folder name and
press C beside `new project in that folder`. The layout appears.
Open a file from the list to edit it. Build compiles the folder and loads
the cartridge into the machine beside the editor. Build and Run starts
it. The generated assembly lands in the Source pane. Read your C and
its machine code side by side there.

A compile error names the file and the line, in the terminal or in the
IDE's message pane. Read the line it names. Most errors are a missing
semicolon, a missing brace or a name spelled two ways.

## Types and their sizes

The processor is 8 bit and the compiler's `int` is 16 bit. That decides
every size in the table.

| Type | Bytes | Holds |
|---|---|---|
| `char` | 1 | -128 to 127 |
| `unsigned char` | 1 | 0 to 255 |
| `int`, `short` | 2 | -32768 to 32767 |
| `unsigned int` | 2 | 0 to 65535 |
| any pointer | 2 | an address, 0 to 65535 |
| `double`, `float` | 8 | a real number, on the coprocessor |

`float` and `double` are the same type here. The coprocessor has one
real number format, and a program written elsewhere should not fail on
the spelling. `long` does not exist. The compiler names the two
replacements when you try it.

Arithmetic wraps, as it did in BASIC. Run this program and compare with
what you expect:

```c
#include <graphics.h>

int main(void)
{
    int a = 32767;
    unsigned int u = 60000;
    unsigned char c = 250;
    char s = 120;
    a = a + 1;
    u = u / 3;
    c = c + 10;
    s = s + 10;
    at(1, 1);
    printf("SIZES %d %d %d %d", sizeof(char), sizeof(int), sizeof(int *), sizeof(double));
    at(1, 3);
    printf("32767 + 1 IS %d", a);
    at(1, 5);
    printf("60000 / 3 IS %u", u);
    at(1, 7);
    printf("250 + 10 IN A BYTE IS %hhu", c);
    at(1, 9);
    printf("120 + 10 IN A CHAR IS %hhd", s);
    at(1, 11);
    printf("7 / 2 IS %d AND 7 %% 2 IS %d", 7 / 2, 7 % 2);
    at(1, 13);
    printf("-7 / 2 IS %d", -7 / 2);
    at(1, 15);
    printf("'A' IS %d AND %c IS 66", 'A', 66);
    return 0;
}
```

```text
SIZES 1 2 2 8
32767 + 1 IS -32768
60000 / 3 IS 20000
250 + 10 IN A BYTE IS 4
120 + 10 IN A CHAR IS -126
7 / 2 IS 3 AND 7 % 2 IS 1
-7 / 2 IS -3
'A' IS 65 AND B IS 66
```

Division throws the remainder away and `%` gives it, the way `/` and
`MOD` did. A negative quotient rounds towards zero. A character in single
quotes is its code, so `'A'` is 65 and you can add to it.

`printf` needs the right hole for each size. `%d` and `%u` read two
bytes, `%hhu` and `%hhd` read one, `%c` prints a byte as a character,
`%s` prints a string and `%x` prints in hex. `%%` prints a percent sign.
The compiler counts the holes against the arguments and refuses a
mismatch.

An `unsigned char` is the cheapest variable the machine has, one byte
that the processor handles in one instruction. Use it for anything that
fits in 0 to 255: a sprite number, a colour, a count of lives. Use `int`
for coordinates and scores.

Conditions compile straight to jumps. `if (lives > 0)` is a subtract
and one conditional jump, with no 0 or 1 worked out in between. Two
`unsigned char` values, or one and a constant that fits in a byte, are
compared in one byte. An `int` takes two. A pointer or an `int` tested
on its own, as in `while (p)` or `if (!next)`, is a single word load:
the load sets the zero flag from all 16 bits. A loop tests at its
bottom, so each pass costs one jump.

Declare variables at the top of a block or where they are first needed.
Both work. A variable declared outside every function is a global. It
starts at zero and every function can see it.

## Functions

A function has a return type, a name, a list of parameters and a body.
`void` means no answer, or no parameters. A function is declared before
the first call to it, so helpers go above `main`.

```c
#include <graphics.h>

/* The square of a number. */
int square(int n)
{
    return n * n;
}

/* n factorial, by calling itself. */
unsigned int factorial(unsigned char n)
{
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int calls;

/* Adds one to the count every time it is called. */
int tally(void)
{
    calls = calls + 1;
    return calls;
}

int main(void)
{
    int i;
    for (i = 1; i <= 5; i++) {
        at(1, i);
        printf("%d SQUARED IS %d, FACTORIAL IS %u", i, square(i), factorial(i));
    }
    tally();
    tally();
    at(1, 7);
    printf("TALLY CALLED %d TIMES", tally());
    return 0;
}
```

```text
1 SQUARED IS 1, FACTORIAL IS 1
2 SQUARED IS 4, FACTORIAL IS 2
3 SQUARED IS 9, FACTORIAL IS 6
4 SQUARED IS 16, FACTORIAL IS 24
5 SQUARED IS 25, FACTORIAL IS 120

TALLY CALLED 3 TIMES
```

`factorial` calls itself. That works because each call gets its own copy
of `n` on a stack in memory, the heap stack. It starts directly under the text
screen and grows downwards, and a call costs a few bytes of it.

The heap stack is the C runtime's, not the processor's. The processor's
own stack holds only return addresses, in a memory of its own. The heap
stack holds every parameter and local, in RAM, so a pointer to a local is
an ordinary address. The register D3 points at it, and the IDE shows D3
among the registers.

By default the heap stack gets all the RAM the program's globals leave
free. `#pragma heap_stack_size 4096` anywhere in the project asks for a
fixed size instead, and so does `simplecpu-cc --heap-stack-size 4096`.
The compiler adds up how deep a chain of calls goes wherever it can, and
refuses a `main` whose calls cannot fit. A function that calls itself
cannot be added up, so it checks for room each time it starts. When the
room runs out the program stops with `HEAP STACK OVERFLOW` at the top of
the screen.

`for (i = 1; i <= 5; i++)` is `FOR I=1 TO 5`. The three parts are the
start, the test that keeps the loop going and the step. `i++` adds one.
`while (test)` loops while the test holds. `do { } while (test)` runs
the body once before testing. `break` leaves a loop and `continue`
skips to the next turn. `switch` picks one of many `case` labels.

`tally` remembers between calls through a global. A `static` variable
inside a function does the same and keeps the name private to the
function: `static int calls = 0;` inside `tally` starts at zero once
and survives every return. The compiler lays it out in RAM as a
global with a private name, so a static in a function that calls itself
is one variable shared by every level. `static` on a global or a function
hides the name from other files in the project.

A function's locals must fit in 255 bytes together. A local array bigger
than that is refused with a message that says to make it a global.

## Arrays, pointers and strings

An array is a run of boxes under one name. `int scores[6]` is six ints
side by side, numbered 0 to 5. A string is an array of `char` ending in
a zero byte, and `"ADA"` makes one of four bytes.

A pointer holds an address. `&x` is the address of `x`, and `*p` is the
box that `p` points at. An array's name is the address of its first
box, so a function that takes `int *v` can be handed any array.

```c
#include <graphics.h>

int scores[6] = { 40, 12, 99, 7, 63, 25 };
char name[] = "ADA";

/* Sorts n ints into rising order, swapping neighbours. */
void sort(int *v, int n)
{
    int i;
    int j;
    int t;
    for (i = 0; i < n - 1; i++) {
        for (j = 0; j < n - 1 - i; j++) {
            if (v[j] > v[j + 1]) {
                t = v[j];
                v[j] = v[j + 1];
                v[j + 1] = t;
            }
        }
    }
}

/* The length of a string, walking a pointer to its zero byte. */
int length(char *s)
{
    int n = 0;
    while (*s) {
        n++;
        s++;
    }
    return n;
}

/* Swaps two ints through pointers to them. */
void swap(int *a, int *b)
{
    int t = *a;
    *a = *b;
    *b = t;
}

int main(void)
{
    int i;
    int x = 1;
    int y = 2;
    sort(scores, 6);
    at(1, 1);
    for (i = 0; i < 6; i++) printf("%d ", scores[i]);
    at(1, 3);
    printf("%s HAS %d LETTERS", name, length(name));
    name[0] = 'E';
    at(1, 5);
    printf("NOW IT IS %s", name);
    swap(&x, &y);
    at(1, 7);
    printf("X IS %d AND Y IS %d", x, y);
    at(1, 9);
    printf("THE ARRAY STARTS AT %u", scores);
    return 0;
}
```

```text
7 12 25 40 63 99
ADA HAS 3 LETTERS
NOW IT IS EDA
X IS 2 AND Y IS 1
THE ARRAY STARTS AT 17
```

The last line is the address of `scores`, which the compiler placed at
box 17. The first 256 boxes are the zero page, the only memory the
processor reaches in one instruction. The compiler puts the variables it
expects to be busiest there, judged by how many loops surround each
mention. `PEEK(17)` from BASIC would read the same byte.

`sort` changes the caller's array because it received the address, not
a copy. `swap` does the same for two single ints. That is what pointers
are for. `s++` moves a pointer to the next box, and `*s` in a test is
false at the zero byte that ends every string.

There is no `strlen`, `strcpy` or `strcmp`. Write the loop, as `length`
does. Arrays have one dimension. For a grid, use `cells[y * WIDTH + x]`
on a one dimensional array.

## Doubles

A `double` is a real number, eight bytes long, with about fifteen
significant digits. The processor cannot add two of them. The arithmetic
coprocessor can, and the compiler turns every `+`, `-`, `*`, `/` and
comparison on a double into one command to it. An int turns into a
double on its own. Going back needs a cast, `(int)d`, which cuts the
fraction off.

```c
#include <graphics.h>
#include <math.h>

/* The area of a circle. */
double area(double r)
{
    return PI * r * r;
}

int main(void)
{
    double d = 10.0 / 4.0;
    int deg;
    int x;
    int y;
    at(1, 1);
    printf("10.0 / 4.0 IS %.2f, CUT TO %d", d, (int)d);
    at(1, 2);
    printf("SQRT(2) IS %.2f", sqrt(2.0));
    at(1, 3);
    printf("AREA OF RADIUS 3 IS %.2f", area(3.0));
    setcolor(CYAN);
    for (deg = 0; deg < 360; deg = deg + 10) {
        x = 128 + (int)(cos(radians(deg)) * 80);
        y = 160 + (int)(sin(radians(deg)) * 80);
        fillcircle(x, y, 3);
    }
    return 0;
}
```

```text
10.0 / 4.0 IS 2.50, CUT TO 2
SQRT(2) IS 1.41
AREA OF RADIUS 3 IS 28.27
```

Below the text, 36 dots stand in a circle. `cos` and `sin` come from
`math.h` and run on the coprocessor too.

`printf` prints a double with `%f`, `%e` or `%g`, and `%.2f` sets the
decimals as in C. `%lf` is the same thing. An int given to `%f` is
turned into a double first.

The remainder `%` and the bit operators do not work on a double. The
error names the four operations that do. A double literal needs a
decimal point, so write `10.0 / 4.0`. `10 / 4` is an integer division
and gives 2.

## What the compiler refuses

The compiler takes a written subset of C. Each refusal is a compile
error that names the alternative. The ones a C programmer meets first:

| Written | Instead |
|---|---|
| `long n;` | `int` or `double` |
| `struct point { int x; int y; };` | parallel arrays, `xs[i]` and `ys[i]` |
| `enum colour { RED, GREEN };` | `#define RED 0` |
| `union`, `goto`, a function pointer | none, the parser stops at the token |
| `int grid[8][8];` | `int grid[64];` and `grid[y * 8 + x]` |
| `char buf[300];` inside a function | the same array as a global |
| `#include <stdio.h>` | `graphics.h` for `printf`, nothing for the rest |
| `ship[0]` on a `__ROM` object | `rom_copy(buf, ROM_ship, ROM_ship_SIZE)` |
| `void show(int n)` with `graphics.h` included | another name, since `show` is one of its macros |

The last one names the header in its message. `show`, `image`, `sample`
and `loop` are macros there, and a function cannot share a macro's
name while the header is in.

`typedef` works at file scope, so `typedef unsigned char byte;` is
fine. `#define`, `#if` and `#ifdef` work. `//` comments work beside
`/* */` ones. `sizeof`, casts, `?:`, compound assignment and `++` all
work. `switch` works. Recursion works.

## Drawing with graphics.h

`graphics.h` draws on the 256 by 256 picture. Coordinates are ints, x to
the right and y down, and anything off the screen is clipped. A colour
is a byte, `rgb(r, g, b)` with red and green from 0 to 7 and blue from
0 to 3. Eleven names are ready made: `BLACK`, `WHITE`, `RED`, `GREEN`,
`BLUE`, `YELLOW`, `CYAN`, `MAGENTA`, `ORANGE`, `GREY` and `DARKGREY`.

```c
#include <graphics.h>

int main(void)
{
    int i;
    clear(BLUE);
    setcolor(YELLOW);
    fillcircle(64, 64, 40);
    setcolor(WHITE);
    circle(64, 64, 48);
    setcolor(RED);
    fillrect(140, 30, 80, 60);
    setcolor(BLACK);
    rect(150, 40, 60, 40);
    setcolor(GREEN);
    fillellipse(64, 190, 50, 25);
    setcolor(rgb(7, 5, 3));
    for (i = 0; i < 256; i = i + 16) line(128, 128, i, 255);
    setcolor(CYAN);
    for (i = 0; i < 100; i++) plot(140 + random(100), 130 + random(60));
    textcolor(WHITE, BLACK);
    at(15, 30);
    printf("PIXEL AT 64,64 IS %hhu", pixel(64, 64));
    while (1) nextframe();
}
```

The pixel reads back 252, which is `rgb(7, 7, 0)`, yellow.

`setcolor` picks the pen for every shape after it. `cls()` clears to
black and `clear(c)` to a colour. `plot` sets one pixel and `pixel`
reads one. `line` takes both ends. `moveto` and `lineto` keep a pen
position between calls, as `MOVE` and `DRAW` did. Each shape has an
outline form and a `fill` form. A rectangle is its top left corner and
its size. A circle is its centre and radius, and an ellipse has two
radii.

`random(n)` gives 0 to n-1 and takes any n up to 65535. `at`, `print`,
`printf`, `textcolor` and `cleartext` are the text layer, 42 columns by
32 rows, which sits above every shape and sprite.

`nextframe()` waits for the next frame of the display, 60 a second.
The `while (1) nextframe();` at the end keeps the program running so the
picture stays. Without it `main` returns, the machine halts and the
picture stays anyway, with `halted` in the corner. A moving program
draws, calls `nextframe()`, and draws again. `frames()` counts frames,
wrapping at 256, which is how a program animates without a counter of
its own.

## Keys with keys.h

The keyboard reaches a program two ways. The arrow keys, space, Enter
and fire are levels. `left()` is true for as long as the key is held.
The rest of the keyboard is a queue. `key()` gives the next key pressed
or 0, and `waitkey()` waits for one. Both give ASCII codes, letters in
capitals, with `KEY_ENTER`, `KEY_ESCAPE` and `KEY_SPACE` named.

```c
#include <graphics.h>
#include <keys.h>

int main(void)
{
    int x = 128;
    int y = 128;
    unsigned char c;
    while (1) {
        if (left() && x > 10) x = x - 2;
        if (right() && x < 245) x = x + 2;
        if (up() && y > 10) y = y - 2;
        if (down() && y < 245) y = y + 2;
        c = key();
        if (c == 'Q' || c == KEY_ESCAPE) return 0;
        if (c == KEY_SPACE) { x = 128; y = 128; }
        cls();
        setcolor(fire() ? RED : YELLOW);
        fillcircle(x, y, 10);
        at(1, 31);
        printf("X %d Y %d   Q QUITS, SPACE RESETS", x, y);
        nextframe();
    }
}
```

The arrows and W, A, S, D both count as the controller. Fire is Z or
Ctrl. `shift()` and `ctrl()` say whether a modifier is held.
`flushkeys()` empties the queue. A game calls it before waiting for a
key to start. A press from the last game then cannot start the next.

## Sound with sound.h

The audio chip plays samples. `sound_init()` loads a built in square
wave, so a program can make a note with no sound file at all. Notes are
MIDI numbers, and `C3` to `C6` are named in the header, with `C4` middle
C at 60.

```c
#include <graphics.h>
#include <sound.h>

unsigned char song[8] = { C4, D4, E4, F4, G4, A4, B4, C5 };

int main(void)
{
    int i;
    sound_init();
    at(1, 1);
    printf("A SCALE, THEN A CHORD");
    for (i = 0; i < 8; i++) {
        at(1, 3 + i);
        printf("NOTE %hhu", song[i]);
        beep(song[i], 10);
    }
    noteon(0, C4);
    noteon(0, E4);
    noteon(0, G4);
    for (i = 0; i < 60; i++) nextframe();
    silence();
    at(1, 12);
    print("DONE");
    return 0;
}
```

`beep(note, frames)` plays a note and waits that many frames, so it is
a pause as well. A game wants sound without a pause. `noteon(track,
note)` starts a note on one of eight tracks and returns at once, and
`noteoff` stops it. A track sounds up to eight notes together, which is
how the chord works. `silence()` stops everything.

For a sound of your own, put a WAV file in `assets/`. The chapter on
the cartridge shows `sample` and `play`. A MIDI file plays the same way
through `tune`, `tune_stop` and `tune_playing`.

## Maths with math.h

`math.h` is the usual set on `double`: `sqrt`, `sin`, `cos`, `tan`,
`asin`, `acos`, `atan`, `atan2`, `pow`, `exp`, `log`, `log10`, `fabs`
and `hypot`. Angles are in radians and `radians(degrees)` converts. `PI`
is defined. For ints there are `abs`, `max` and `min`.

Each function is one command to the coprocessor. On most small machines
a sine is a table or a long loop. Here it costs about what an addition
costs, which changes what a program can afford. The circle of dots in
the doubles chapter calls `sin` and `cos` 72 times before the first
frame ends.

## Files with disk.h

The cartridge holds up to 64 named files, the same slots BASIC's `!SAVE`
writes. A name is up to 16 letters and digits. A file holds text.
`save(name, text)` writes a string, `load(name, buf, room)` reads one
back into a buffer of `room` bytes, and both answer 1 on success.
`erase(name)` deletes, `catalog(buf, room)` lists the names one per
line, and `files()` counts them.

```c
#include <graphics.h>
#include <disk.h>

char buf[64];

int main(void)
{
    unsigned char ok;
    at(1, 1);
    if (load("HIGHSCORE", buf, 64)) printf("LAST TIME: %s", buf);
    else print("NO SCORE SAVED YET");
    ok = save("HIGHSCORE", "1250 ADA");
    at(1, 3);
    printf("SAVED: %hhu", ok);
    at(1, 5);
    if (load("HIGHSCORE", buf, 64)) printf("READ BACK: %s", buf);
    catalog(buf, 64);
    at(1, 7);
    printf("%d FILES:", files());
    at(1, 8);
    printf("%s", buf);
    return 0;
}
```

The first run prints `NO SCORE SAVED YET`. `simplecpu` writes the
cartridge file back on every save, so the second run prints `LAST TIME:
1250 ADA`. A high score table survives because the `.rom` file on disk
changed. Rebuilding the project writes a fresh cartridge and the files
are gone, so keep a copy of a cartridge whose files matter.

A number has to become text to be saved. Write the digits into a buffer
with `/ 10` and `% 10`, save the buffer, and turn the digits back into a
number after `load`.

## BASIC and C together

A project that holds `.bas` and `.c` files builds one cartridge with the
interpreter and your C inside it. BASIC boots, runs `autorun.bas`, and
calls a C function by name with `CALL`. The C side reads and writes
BASIC's variables A to Z through `basicvars.h`.

`src/autorun.bas`:

```basic
10 REM BASIC ASKS C FOR A SQUARE ROOT
20 A=3
30 B=4
40 CALL HYPOT
50 PRINT "THE HYPOTENUSE OF "; A; " AND "; B; " IS "; C
60 A=200: B=300
70 CALL HYPOT
80 PRINT "AND OF 200 AND 300 IT IS "; C
90 END
```

`src/hyp.c`:

```c
/* HYPOT: called from BASIC with CALL HYPOT. C = the long side of the
 * right triangle with sides A and B, rounded to a whole number.
 */
#include <basicvars.h>
#include <math.h>

void HYPOT(void)
{
    double h = hypot(basic_get('A'), basic_get('B'));
    basic_set('C', (int)(h + 0.5));
}
```

```text
SimpleCPU-8 BASIC
READY
THE HYPOTENUSE OF 3 AND 4 IS 5
AND OF 200 AND 300 IT IS 361
>
```

`CALL` suits a function that works on BASIC's variables. A function that
takes numbers and gives one back is called with `USR` instead. BASIC
passes up to three numbers the way C passes arguments, and reads the
function's return value. The first number says how many bytes of the
answer to keep, 1 or 2. Every parameter is a word, so a function for
`USR` takes `int` or `unsigned int` parameters. A `char` parameter would
read the wrong byte. A function with more to give back fills a buffer
and returns its address. BASIC reads the buffer with `PEEK`.

`src/funcs.c`:

```c
int TWICE(int x) { return x + x; }
int ADD3(int a, int b, int c) { return a + b + c; }
int SEVEN(void) { return 7; }

unsigned char buf[4];

unsigned char *FILL(int n)
{
    buf[0] = n;
    buf[1] = n + n;
    return buf;
}
```

`src/autorun.bas`:

```basic
10 PRINT USR(2, TWICE, 21); " "; USR(1, ADD3, 1, 2, 3); " "; USR(1, SEVEN)
20 P = USR(2, FILL, 7)
30 PRINT PEEK(P); " "; PEEK(P + 1)
40 PRINT USR(1, TWICE, 200); " "; USR(2, TWICE, 200)
50 END
```

```text
42 6 7
7 14
144 400
```

Twice 200 is 400, which is `$0190`. Width 1 keeps the low byte, `$90`,
144.

The C files in a mixed project must not define `main`. That belongs to
the interpreter. BASIC calls a function by name, and the build writes the
function's address into the program. A name BASIC calls that no file
defines is a build error naming the line. `examples/basic/basic-c` is the
worked example, with an assembly routine beside the C one.

### Which functions are kept

The compiler leaves out every function nothing calls, so an unused one
costs no program memory. BASIC and assembly call C by name, which the C
cannot show. So the build reads them first:

1. It reads every `.bas` file for the names after `CALL`, `JSR`, `JMP`
   and `USR`.
2. It reads every `.asm` file for every name in it outside comments and
   strings, so `JSR name` and `LD D2 <- name` both count.
3. It compiles the C with those names as extra starting points, beside
   `main`. A C function they name is kept, and so is everything it calls.
   Everything else is left out.
4. It assembles the C's output with the `.asm` files into one ROM.

A function called from BASIC or assembly must be public. A `static`
function's label is private to its file, so the build refuses the call
with `NAME is static, so only its own file can call it`, and says to
remove `static`.

The calls go every way. BASIC calls C and assembly. Assembly calls C.
C calls assembly through a prototype with no body, `int twice(int n);`,
and the assembly defines the label `twice`. An assembly routine called
from C follows the C convention. It finds its parameters at `[D3+0]`
upward and leaves its answer in `__ret`. It drops the parameters with
`LD D3 <- D3+n` before `RET`, as a C function does.

## Pictures and sounds on the cartridge

The cartridge is memory the processor cannot read. The graphics chip and
the audio chip read it directly. That is where a picture, a sprite or a
sound lives. `__ROM` puts an array there, and six initializer forms
fill one from a file in `assets/`:

| Form | File | Used by |
|---|---|---|
| `__image("bg.png")` | a picture, up to 256 by 256 | `image` |
| `__sprite("ship.png")` or `__sprite("ship.png", 2)` | a strip of frames side by side, each up to 64 by 64 | `sprite` |
| `__sample("ping.wav")` | a WAV, converted to 8 bit mono at 8000 Hz | `sample` |
| `__palette("bg.png")` | the picture's 256 colours | `palette` |
| `__file("level.bin")` | any bytes as they are | `rom_copy` |
| `__font("small.font")` | a font, as `CMD_LOAD_FONT` reads it | `gpu_load_font` |

Each is allowed only on `__ROM const unsigned char name[]` with empty
brackets, because the file decides the length. For every `__ROM` object
the compiler defines `ROM_name` for its address and `ROM_name_SIZE` for
its size. Those names are what the library calls take.

Draw a ship of two frames, 16 by 16 each, as one PNG 32 wide with a
transparent background. Transparent pixels become colour 0, which the
chip skips. A black background is drawn, not skipped. Black becomes
colour 1 on the machine palette, a dark blue, and the build notes say
so. Use transparency for the parts that should not draw. Draw a small picture
called `hill.png` and record or generate `ping.wav`. Then:

```c
#include <graphics.h>
#include <sound.h>
#include <keys.h>

__ROM const unsigned char shippic[] = __sprite("ship.png", 2);
__ROM const unsigned char hillpic[] = __image("hill.png");
__ROM const unsigned char ping[] = __sample("ping.wav");

#define SHIP 1

int main(void)
{
    int x = 120;
    unsigned char f = 0;
    sound_init();
    sample(1, ROM_ping, ROM_ping_SIZE, A4);
    clear(DARKGREY);
    image(96, 200, ROM_hillpic);
    sprite(SHIP, ROM_shippic, 0);
    spriteat(SHIP, x, 100);
    show(SHIP);
    at(1, 1);
    printf("SHIP IS %u BYTES, PING IS %u", ROM_shippic_SIZE, ROM_ping_SIZE);
    while (1) {
        if (left()) x = x - 1;
        if (right()) x = x + 1;
        if (fire()) play(1, A4);
        if (space()) play(1, A5);
        f = f + 1;
        spriteframe(SHIP, (f >> 3) & 1);
        spriteat(SHIP, x, 100);
        nextframe();
    }
}
```

The IDE has a sprite editor for the strip. New sprite in the Files pane
makes one from a name, a frame size and a frame count.
A double click on a picture under Assets opens it. The editor draws with
the machine's own 256 colours, frame by frame. A preview beside the
screen plays the frames. Save writes the PNG into `assets/` with the
frame count inside it, so `__sprite("ship.png")` needs no count. A count
in the call still wins. A PNG from another editor has no count inside,
so it is one frame unless the call gives one.

`image(x, y, r)` copies a picture into the screen at a point. It is
part of the picture afterwards, and drawing over it changes it.

A sprite is different. `sprite(n, r, group)` loads the frames into
sprite number n, from 0 to 255, once. `spriteat` moves it, `show` and
`hide` switch it, and `spriteframe` picks a frame. The chip draws every
visible sprite over the picture on every frame, so moving one costs
nothing and the picture under it is untouched. Higher numbers draw in
front. `spriteflip(n, h, v)` mirrors one. `stamp(n, x, y)` bakes a
sprite into the picture for tiles and backdrops.

`hit(a, b)` answers whether two sprites overlap, by their boxes. A
hidden sprite hits nothing. The group byte given to `sprite` lets
`hitgroups` and `hitin` ask about a whole set of sprites at once, which
the game at the end does not need and docs/design/gpu-ports.md
describes.

`sample(slot, r, size, root)` loads a sound into one of 64 slots at
the note it was recorded at. Slot 0 holds the square wave, so start at 1. `play(slot, note)` plays it, and a higher
note plays it faster, so one recording gives a scale. The ship above
pings at A4 on fire and an octave up on space.

`ROM.h` is a generated file listing every `__ROM` object and its
address. Its names are in scope without an include. A `__ROM` array
cannot be read with `[]`, because the processor has no path to the
cartridge. `rom_copy(buf, ROM_name, ROM_name_SIZE)` copies it into RAM
through the graphics chip, and then `buf[]` reads it.

## The three chips

The processor is small. It adds, subtracts, shifts one bit, moves bytes
and jumps. It has no multiply and no divide. Three chips beside it do the
rest, each on its own ports, each answering a command the processor
writes. The friendly headers are thin covers over those commands. Knowing
what each chip is makes the headers make sense.

The GPU owns the screen. It keeps a picture of 256 by 256 bytes, and
each byte picks a colour from a palette of 256 entries. The default
palette is the 3-3-2 one, red and green in three bits each and blue in
two. `palette(r)` loads a different one from the cartridge and
`resetpalette()` puts the default back. Over the picture float 256
sprites, each up to 64 by 64, redrawn every frame by the chip. Over
those sits the text layer, 42 by 32 cells in a 6 by 8 font. The chip
draws lines, circles and rectangles itself, copies pictures from the
cartridge, tests sprites for overlap, and formats `printf`. A frame is
65536 processor cycles. The chip counts them, and `nextframe()` waits
for the count to change. It never reads a clock, so a program draws the
same pixels whatever speed the machine runs at.

The APU owns sound. It plays samples, 8 bit at 8000 a second, from the
cartridge. It has eight tracks and each track sounds eight notes at
once. A track carries an instrument, which is a sample and the note it
was recorded at. A note above the root plays the sample faster and a
note below plays it slower. That is how one square wave gives every
pitch. The chip also reads a MIDI file from the cartridge and plays it
on the tracks by itself. Unlike the GPU, the APU runs on real time, so
a tune keeps its tempo whatever the processor does.

The ACP owns arithmetic. Its name is short for arithmetic coprocessor. A
command points it at a block in RAM holding two operands, and it writes
the result after them. It works on 64 bit integers and on doubles, and
it does the trigonometry, roots, logs and powers of `math.h`. It also
does vector and matrix arithmetic. The compiler uses it for every
operation on a double. It also takes an int multiply, divide or shift
that the processor cannot do in a few lines. A multiply by 10 or a shift by 3
stays on the processor as shifts and adds. A double costs about fifteen
instructions whatever the operation, which is why this guide can say
doubles are cheap. `a * b` on two int variables is 54 instructions
through the chip. As a shift and add loop on the processor, 123 * 456 is
172.

The point of the three is the same. The processor is the lesson, and it
stays small so its every instruction can be understood. The work that
would drown the lesson goes to a chip. The chip is reached through
an `OUT` instruction that any program can see. Nothing is hidden. The
assembly guide shows the ports.

## The layer below

Each friendly header includes a machine header. `gpu.h`, `apu.h`,
`acp.h`, `io.h`, `storage.h`, `sys.h` and `rom.h` have one macro per
device command, named after the command in lower case, with one
argument per port byte. `CMD_SPRITE_DEF` is `gpu_sprite_def`. A macro
costs exactly the port writes it shows and no call.

```c
#include <gpu.h>

int main(void)
{
    gpu_clear(gpu_rgb(0, 0, 2));
    gpu_set_color(255);
    gpu_move_to(0, 100, 0, 100);
    gpu_circle(30);
    gpu_text_at(1, 1);
    gpu_printf("FRAME %hhu", gpu_frame());
    return 0;
}
```

A coordinate is two bytes on the GPU, high then low, so `gpu_move_to`
takes four arguments where `moveto` takes two. That is the difference
between the two layers. `graphics.h` splits the ints for you.

`sys.h` has `wait_frame`, `peek`, `poke`, `memcpy`, `memset`,
`rom_copy`, `rand` and `halt`. Two intrinsics reach any port at all:
`out(GPU_CMD, CMD_CLEAR)` and `in(GPU_FRAME)`. Every port and command
name the assembler knows is a C constant. The generated assembly from
`--asm-out` shows what each layer becomes. The assembly guide covers the
ports themselves, and `examples/c/hello-c` draws with `gpu.h` alone.

## Errors you will meet

The compiler prints the file, the line and a sentence. The ones that
come up most:

| Message | Meaning |
|---|---|
| `expected ";", found "return"` | the line before is missing its semicolon |
| `x is not declared` | a variable or function used before its declaration, or misspelled |
| `a declaration needs a type` | a word the compiler does not know where a type should be, such as `struct` |
| `unclosed {` | a brace without its partner |
| `a program needs a function called main` | no `main` in the project |
| `this format takes 2 arguments and 1 was given` | `printf` holes and arguments differ |
| `f is defined in other.c as well` | two files define one public name, so mark one `static` |
| `main needs a frame of 300 bytes and a frame reaches 255` | a local array too big, so make it global |
| `main's calls need 900 bytes of heap stack, and it has 512` | a `#pragma heap_stack_size` too small for the program |
| `pic is __ROM and cannot be read from C` | use `rom_copy` |
| `cannot find <stdio.h>` | the message lists the headers that exist |
| `show is a macro from graphics.h` | a function named like a macro, so rename it or leave the header out |

A program that compiles and shows nothing has usually returned from
`main` before drawing, or is waiting in `waitkey()`. A program that
draws once and freezes is missing `nextframe()` in its loop. A sprite
that never appears was not given `show`.

## A small game

The game is called Rocks. You are a ship at the bottom of the screen,
steering left and right. Rocks fall from the top. Every rock that
passes you is a point, and every rock that hits you costs a life. Three
hits end the game, and any key starts another. It is built in three
steps, each a program that runs.

Draw two PNG files into `rocks/assets/`. `ship.png` is 32 by 16, two
frames of a ship side by side, on a transparent background. The second
frame has an orange flame. `rock.png` is a 12 by 12 brown lump.

Step one draws the stars and steers the ship. Put it in
`rocks/src/main.c`.

```c
/* Rocks: steer the ship between the falling rocks. */
#include <graphics.h>
#include <keys.h>

__ROM const unsigned char shippic[] = __sprite("ship.png", 2);

#define SHIP 1
#define SHIP_Y 230

int shipx;

void stars(void)
{
    int i;
    clear(BLACK);
    setcolor(GREY);
    for (i = 0; i < 60; i++) plot(random(256), random(256));
}

int main(void)
{
    sprite(SHIP, ROM_shippic, 0);
    show(SHIP);
    stars();
    shipx = 120;
    while (1) {
        if (left() && shipx > 0) shipx = shipx - 2;
        if (right() && shipx < 240) shipx = shipx + 2;
        spriteframe(SHIP, (frames() >> 2) & 1);
        spriteat(SHIP, shipx, SHIP_Y);
        nextframe();
    }
}
```

`#define SHIP 1` names sprite number 1, so the rest of the program never
says 1. `stars` draws the background once, and the ship is a sprite, so
it moves over the stars without disturbing them. `(frames() >> 2) & 1`
is 0 for four frames and 1 for four, which swaps the flame on and off.
Build it, run it and hold an arrow key.

Step two adds the rocks. Five sprites share one picture. Each has a
column and a row in two arrays. A rock past the bottom scores a
point and starts again above the top.

```c
/* Rocks: steer the ship between the falling rocks. */
#include <graphics.h>
#include <keys.h>

__ROM const unsigned char shippic[] = __sprite("ship.png", 2);
__ROM const unsigned char rockpic[] = __sprite("rock.png");

#define SHIP 1
#define FIRST_ROCK 2
#define ROCKS 5
#define SHIP_Y 230

int shipx;
int rockx[ROCKS];
int rocky[ROCKS];
int speed;
unsigned int score;

void stars(void)
{
    int i;
    clear(BLACK);
    setcolor(GREY);
    for (i = 0; i < 60; i++) plot(random(256), random(256));
}

void hud(void)
{
    at(1, 0);
    printf("SCORE %u", score);
}

/* Puts rock n back above the screen at a new column. */
void newrock(unsigned char n)
{
    rockx[n] = random(244);
    rocky[n] = -12 - random(120);
    spriteat(FIRST_ROCK + n, rockx[n], rocky[n]);
}

int main(void)
{
    unsigned char n;
    sprite(SHIP, ROM_shippic, 0);
    show(SHIP);
    for (n = 0; n < ROCKS; n++) {
        sprite(FIRST_ROCK + n, ROM_rockpic, 0);
        show(FIRST_ROCK + n);
        newrock(n);
    }
    stars();
    shipx = 120;
    speed = 1;
    score = 0;
    hud();
    while (1) {
        if (left() && shipx > 0) shipx = shipx - 2;
        if (right() && shipx < 240) shipx = shipx + 2;
        spriteframe(SHIP, (frames() >> 2) & 1);
        spriteat(SHIP, shipx, SHIP_Y);
        for (n = 0; n < ROCKS; n++) {
            rocky[n] = rocky[n] + speed;
            spriteat(FIRST_ROCK + n, rockx[n], rocky[n]);
            if (rocky[n] > 255) {
                score = score + 1;
                if (score % 10 == 0 && speed < 6) speed = speed + 1;
                hud();
                newrock(n);
            }
        }
        nextframe();
    }
}
```

A rock starts between 12 and 132 pixels above the top, so the five do
not fall in a row. A sprite off the screen is clipped, not refused, so
negative rows are fine. Every tenth point raises the speed, up to 6
pixels a frame. `hud` redraws the score only when it changes.

Step three adds the collision, the lives and the game over screen. The
main loop moves into `step`, which answers 0 when the last life goes.
`main` becomes an outer loop that plays a game, shows the message, waits
for a key and starts again.

```c
/* Rocks: steer the ship between the falling rocks. */
#include <graphics.h>
#include <sound.h>
#include <keys.h>

__ROM const unsigned char shippic[] = __sprite("ship.png", 2);
__ROM const unsigned char rockpic[] = __sprite("rock.png");

#define SHIP 1
#define FIRST_ROCK 2
#define ROCKS 5
#define SHIP_Y 230

int shipx;
int rockx[ROCKS];
int rocky[ROCKS];
int speed;
unsigned int score;
unsigned char lives;

void stars(void)
{
    int i;
    clear(BLACK);
    setcolor(GREY);
    for (i = 0; i < 60; i++) plot(random(256), random(256));
}

void hud(void)
{
    at(1, 0);
    printf("SCORE %u   LIVES %hhu", score, lives);
}

/* Puts rock n back above the screen at a new column. */
void newrock(unsigned char n)
{
    rockx[n] = random(244);
    rocky[n] = -12 - random(120);
    spriteat(FIRST_ROCK + n, rockx[n], rocky[n]);
}

void newgame(void)
{
    unsigned char n;
    shipx = 120;
    speed = 1;
    score = 0;
    lives = 3;
    for (n = 0; n < ROCKS; n++) newrock(n);
}

/* One frame of play. Answers 1 while the ship is alive. */
unsigned char step(void)
{
    unsigned char n;
    if (left() && shipx > 0) shipx = shipx - 2;
    if (right() && shipx < 240) shipx = shipx + 2;
    spriteframe(SHIP, (frames() >> 2) & 1);
    spriteat(SHIP, shipx, SHIP_Y);

    for (n = 0; n < ROCKS; n++) {
        rocky[n] = rocky[n] + speed;
        spriteat(FIRST_ROCK + n, rockx[n], rocky[n]);
        if (rocky[n] > 255) {
            score = score + 1;
            if (score % 10 == 0 && speed < 6) speed = speed + 1;
            hud();
            newrock(n);
        }
        if (hit(SHIP, FIRST_ROCK + n)) {
            lives = lives - 1;
            beep(C3, 8);
            hud();
            if (lives == 0) return 0;
            newrock(n);
        }
    }
    return 1;
}

void gameover(void)
{
    at(16, 14);
    printf("GAME OVER");
    at(13, 16);
    printf("PRESS ANY KEY");
    flushkeys();
    waitkey();
    cleartext();
}

int main(void)
{
    unsigned char n;
    sound_init();
    sprite(SHIP, ROM_shippic, 0);
    for (n = 0; n < ROCKS; n++) sprite(FIRST_ROCK + n, ROM_rockpic, 0);
    show(SHIP);
    for (n = 0; n < ROCKS; n++) show(FIRST_ROCK + n);
    while (1) {
        stars();
        newgame();
        hud();
        while (step()) nextframe();
        gameover();
    }
}
```

`hit(SHIP, FIRST_ROCK + n)` asks the graphics chip whether the two
boxes overlap. The chip keeps every sprite's position, so the program
holds no ship box of its own. A hit costs a life, plays a low note for
eight frames and sends the rock back to the top. The `beep` is also the
pause that lets you see what happened.

`while (step()) nextframe();` is the whole game loop. `step` does one
frame and `nextframe` waits for the display. `gameover` calls
`flushkeys` before `waitkey`, so an arrow key held at the moment of the
crash does not restart the game at once.

Things to change once it runs. A rock that reaches the bottom could
speed up the ship instead of the rocks. A second sprite picture with
`spriteflip` could bank the ship as it turns. `save` from `disk.h` could
keep the best score across runs. Each is ten lines, and the compiler
tells you the line when one of them is wrong.

## Where to go next

The assembly guide beside this one goes one layer down. It shows the
instructions the compiler produced, the registers, and the ports the
three chips answer on. Build a project with `--asm-out` and read
`build/mygame.asm` next to `main.c` before you start it.

docs/design/c-language.md is the compiler's reference. It has the
memory map, the zero page rules and `__zp` for a variable you know is
busy. It also has the build line for many files. Its `-msoft-mul`
switch shows what a multiply costs without the coprocessor.

Every friendly header opens with its own reference. The text is in
src/cc/libs.cpp, and the IDE's manual pane shows the device tables.
`examples/c/bounce` is a bat and ball game on the same libraries as Rocks.
`examples/assembly/pacman` and `examples/assembly/cube` show what the chips can do from
assembly.
