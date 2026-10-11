# The BASIC system page

BASIC keeps its own state in the first 48 bytes of the zero page. Every
address is fixed. A program reads it with PEEK and DEEK and changes it with
POKE and DOKE. A driver in assembly finds it without a symbol table. The
old home computers kept their pointers this way. The Oric's page 2 held the
cursor, the last key, the timers and the vectors a disc system hooked. This
page follows that idea.

The compiler leaves the page alone. BASIC is compiled with `-zp-reserve
48`, which puts the compiler's own zero page traffic from $30 up. The
macros in src/basic/basic.h are the variables, so nothing here is a copy
that can drift.

Words are two bytes, high byte first, the way DOKE stores them and DEEK
reads them.

## The page

| Address | Name | Size | Holds |
|---|---|---|---|
| $00 | SYS_BANG_VEC | word | the instruction slot of the bang handler |
| $02 | SYS_BANG_TEXT | word | the address of the statement's text while a handler runs |
| $04 | SYS_RESULT | byte | the A register a bang handler or a CALL routine came back with |
| $05 | SYS_COL | byte | cursor column, 0 to SYS_COLS minus 1 |
| $06 | SYS_ROW | byte | cursor row, 0 to SYS_ROWS minus 1 |
| $07 | SYS_KEY | byte | the last key pressed, 0 before any |
| $08 | SYS_PROG | word | where the program store starts |
| $0A | SYS_PROG_LEN | word | its length in bytes, 158 for an empty program |
| $0C | SYS_VARS | word | the integer variables, 11 words per letter |
| $0E | SYS_HEAP | word | where the string heap starts |
| $10 | SYS_HEAP_TOP | word | bytes of the heap in use |
| $12 | SYS_ERR | byte | the last error code, cleared when the next command starts |
| $13 | SYS_ERR_LINE | word | the line it happened on, 0 in direct mode. SYS_FILE has its file |
| $15 | SYS_RUNNING | byte | 1 while a program runs |
| $16 | SYS_PC | word | the offset in the store of the line RUN goes to next |
| $18 | SYS_CALL | word | the instruction slot the last CALL or JMP went to |
| $1A | SYS_USR | 6 | USR's three parameter words on the way in, its answer on the way out |
| $20 | SYS_READ | word | the offset in the store of READ's next value, 0 for the first value |
| $22 | SYS_COLS | byte | the text grid's columns, 42 at power on |
| $23 | SYS_ROWS | byte | the text grid's rows, 32 at power on |
| $24 | SYS_PROMPT | byte | 1 while the prompt waits for a line, 0 otherwise |
| $25 | SYS_FILE | word | the index of the file the running line is in, 0 for the main file |
| $27 | SYS_EDIT | byte | the index of the current file, the one a typed line goes into |
| $28 | reserved | 8 | kept free for BASIC, $28 to $2F |

SYS_PROMPT is 0 from the start of BASIC's main until its first prompt.
It is 0 again from the Enter that ends a line. AUTORUN, LOAD and a typed
line store a program line by line with no program running. SYS_RUNNING
at 0 does not mean the program is whole. The IDE reads and writes the
program only while SYS_PROMPT is 1.

While a program runs, SYS_PC is one line ahead. RUN writes a line's
number to SYS_ERR_LINE, moves SYS_PC to the line after it, and then runs
the line. A jump sets SYS_PC to its target. So SYS_ERR_LINE and SYS_FILE
name the line that runs, and SYS_PC the line that comes next. The IDE
sees a line start by this: SYS_PC moves one record on while SYS_ERR_LINE
holds that record's number.

The integer variables sit in 11 word slots per letter: the bare name first,
then the digit forms 0 to 9. So `B` is slot 11 and `B7` is slot 19, and
`DEEK(DEEK(12) + 22)` reads `B`.

## Where BASIC's memory lies

The interpreter's globals start after the zero page and end at $BA23,
47,396 bytes. SYS_PROG, SYS_VARS and SYS_HEAP give a program the three a
program reads, so it never needs the addresses below, which are this
build's.

| From | To | Bytes | Holds |
|---|---|---|---|
| $0510 | $110F | 3,072 | the string heap, SYS_HEAP |
| $1130 | $692F | 22,528 | the text block SAVE, LOAD and CATALOG go through |
| $6930 | $6B6B | 572 | the integer variables, SYS_VARS |
| $6B6C | $AB6B | 16,384 | the program store, SYS_PROG, and its DATA bytes after it |
| $ADA8 | $B1A7 | 1,024 | the jump cache |
| $BA24 | the C stack | | free for a program's tables |
| $F000 | $FFFF | 4,096 | the text screen |

The C stack starts at $F000 and grows down. The Centipede prototypes took
it down to $EE96, 362 bytes. The program keeps each keyword as one byte
and each number with its value, docs/design/basic-speed.md.

## The program store

The store holds up to 8 files in its 16,384 bytes. One is the main file,
which RUN runs. INCLUDE, FILE and LOAD bring the others in beside it. A
table of 154 bytes at its head names them.

| Offset | Size | Holds |
|---|---|---|
| 0 | byte | how many entries the table has in use |
| 1 | byte | spare |
| 2 | 19 each | an entry per file, 8 of them |

An entry is the file's name, up to 16 characters in capitals, padded
with zeros to 17 bytes. Then comes the offset of the file's first record
as a word. An offset of 0 is a file a line names that is not loaded. A
file's index is its entry's place, and the main file is index 0, called
MAIN until a LOAD names it.

Each loaded file has one block of records, in line order.

| Byte | Holds |
|---|---|
| 0 | the index of the file the line is in |
| 1, 2 | the line number, high byte first |
| 3 | the record's length, this byte's four included |
| 4 on | the text, then a zero |

A record numbered 0 ends its file's block. It is 4 bytes. So the line
after a file's last is never another file's, and a program that runs off
the end of a file ends there. A new file's block goes after every
other, so loading one moves no record. An empty store is the table and
the main file's end record, 158 bytes.

The text holds a keyword as one byte, 128 plus its place in
src/basic/keywords.h. A number is the byte 191, its value as a word,
then its digits. A jump target, the line after GOTO, GOSUB, THEN or
RESTORE, is the byte 192, the target file's index, then the line as a
word. `GOSUB GRAPHICS:1000` and `GOTO 10` are both stored that way, the
second with the index of its own file. LIST prints the file's name and a
colon only for a target in another file.

## What a program can do with it

Read the cursor to draw at the prompt's position. Read the last key without
consuming one. Find its own text, stored crunched. A POKE into the text
while the program runs is not supported, since RUN keeps the line each
jump found. Reach a variable by number rather than by name, which
is how a driver hands a result back to BASIC. Read the error a `!` handler
left. `DOKE 32, 0` is RESTORE from a driver: READ starts again at the first
value.

```basic
10 PRINT PEEK(5), PEEK(6)
20 DOKE DEEK(12), 1234
30 PRINT A
```

## Installing a driver

A driver is a routine at an instruction slot, reached through
SYS_BANG_VEC. Install it from BASIC with DOKE, keeping the old vector as the
driver's next:

```basic
10 DOKE $C000, DEEK(0)
20 DOKE 0, 61440
```

From assembly, the same in four loads, as docs/storage-design.md shows.
examples/basic/basic-driver is the worked example.

## Calling a routine

`CALL n` calls the routine at instruction slot n, where n is any integer
expression. A hex number is written `$F000`. The routine comes back with
`RET`. BASIC parks the A register it came back with at SYS_RESULT, so
`PEEK(4)` reads it, and puts its own frame pointer back. The slot is
parked at SYS_CALL first. `JSR n` is the same word, spelled the way the
machine spells it. `JMP n` goes to slot n and never comes back: the
routine owns the machine from then on, and the interpreter is gone with
the program and its variables.

```basic
10 CALL $F000
20 PRINT PEEK(4)
```

A routine called this way has the contract a bang handler has. It may use
the hardware stack in balance. It may not touch the zero page past the
system page, which is BASIC's register file. The zero page from $30 to
$FF is the interpreter's, and a routine reads and writes nothing there.
$28 to $2F are reserved for future system page words, and they are not a
routine's either.

## Asking a routine for an answer

`USR(width, n, p1, p2, p3)` is a function. It calls the routine at slot
n the way compiled C calls a function, so a C function needs no glue.
The parameters are optional, up to three, and each is a word. BASIC
parks them at SYS_USR and copies them onto the heap stack, the C stack
in RAM under `D3`. The first lands at `[D3+0]`, high byte first, the
next two above it. Then it calls the routine. The routine leaves its answer in the
return cells, `__ret` and `__ret+1`, high byte first, where every C
function leaves its value. Width 0 is for a routine called for what it
does, and USR gives 0. Width 1 gives the low byte, 0 to 255. Width 2
gives the whole word. That word can be the address of a buffer the
routine filled, for `PEEK` and `DEEK` to read.

```basic
10 PRINT USR(2, TWICE, 21)
20 P = USR(2, FILL, 7) : PRINT PEEK(P), PEEK(P + 1)
```

BASIC puts `D3` back as it was after the call. A C function pops its
own parameters, and an assembly routine that ignores them leaves `D3` as
it found it anyway. An assembly routine reads its parameters at
`[D3+0]` to `[D3+5]` and writes its answer to `__ret`. `__ret` and
`__ret+1` are the only bytes past the system page it may touch, and `D3`
is the one register it must give back unchanged. A width other than 0, 1 or 2
is `RETURN VALUE WIDTH IS OUT OF RANGE [0,1,2]`.

In a project built by simplecpu-make, the number may be a name. `CALL
DOUBLE` and `USR(2, TWICE, 21)` name a C function or an assembly label.
The build writes the
label's slot into the program before the ROM is written. The interpreter
itself knows only numbers. A name typed at the prompt is a syntax error.
A C function called this way runs on the interpreter's own runtime. It may
keep globals and call other functions. It reads and writes the
variables A to Z through `<basicvars.h>`. examples/basic/basic-c is the worked
example. docs/standalone.md, under Making a program, has the project side.

## The error codes

This BASIC has room to explain itself, so a message says what went
wrong in words. A syntax error adds what it expected and what it found.
Every message ends with `IN LINE n` when a program was running. A line of
another file than the main one reads `IN NAME:n`, as in
`SYNTAX ERROR IN GRAPHICS:1010`. The code stays in SYS_ERR until the next
command. Codes 1 to 12 keep the numbers
they always had. Code 3 is no longer raised: codes 15 to 18 split it.

| Code | Name | On the screen |
|---|---|---|
| 0 | E_OK | no error |
| 1 | E_SYNTAX | `SYNTAX ERROR: EXPECTED what BUT FOUND token` |
| 2 | E_NOLINE | `THERE IS NO LINE n` |
| 3 | E_STACK | not raised |
| 4 | E_MEMORY | `THE PROGRAM MEMORY IS FULL: 16384 BYTES AT MOST` |
| 5 | E_TYPE | `A STRING CANNOT BE USED AS A NUMBER` |
| 6 | E_DIVZERO | `DIVISION BY ZERO` |
| 7 | E_RANGE | `NUMBER OUT OF RANGE` |
| 8 | E_BREAK | `BREAK` |
| 9 | E_BANG | `NO DRIVER KNOWS THE COMMAND !word` |
| 10 | E_NOTFOUND | `NO PROGRAM CALLED name ON THE CARTRIDGE` |
| 11 | E_STOFULL | `THE CARTRIDGE IS FULL` |
| 12 | E_BADNAME | `A PROGRAM NAME IS 1 TO 16 CHARACTERS WITHOUT SPACES` |
| 13 | E_USRWIDTH | `RETURN VALUE WIDTH IS OUT OF RANGE [0,1,2]` |
| 14 | E_UNKNOWN | `UNKNOWN WORD word` |
| 15 | E_GOSUBS | `TOO MANY GOSUBS INSIDE EACH OTHER: 16 AT MOST` |
| 16 | E_RETURN | `RETURN WITHOUT A GOSUB` |
| 17 | E_FORS | `TOO MANY FOR LOOPS INSIDE EACH OTHER: 8 AT MOST` |
| 18 | E_NEXT | `NEXT WITHOUT A FOR` |
| 19 | E_LINELONG | `THE LINE IS TOO LONG: 250 CHARACTERS AT MOST` |
| 20 | E_STRLONG | `THE STRING IS TOO LONG: 255 CHARACTERS AT MOST` |
| 21 | E_STRMEM | `OUT OF MEMORY FOR STRINGS` |
| 22 | E_SAVEBIG | `THE PROGRAM IS TOO LONG TO SAVE` |
| 23 | E_NEEDSTR | `A NUMBER CANNOT BE USED AS A STRING` |
| 24 | E_STRCMP | `STRINGS ARE COMPARED WITH = <> < > <= OR >=` |
| 25 | E_NOTVAR | `name IS NOT A VARIABLE: ...` |
| 26 | E_ROUTINE | `name IS A ROUTINE NAME, WHICH ONLY A BUILT PROJECT KNOWS: ...` |
| 27 | E_RENUM | `RENUM WOULD NUMBER A LINE PAST 65535` |
| 28 | E_NODATA | `READ FOUND NO MORE DATA` |
| 29 | E_NOTDATA | `LINE n HOLDS NO DATA` |
| 30 | E_DATABYTE | `A DATA VALUE IS ONE BYTE: -128 TO 255` |
| 31 | E_HEXWIDTH | `HEX WIDTH IS OUT OF RANGE [1,4]` |
| 32 | E_TEXTSIZE | `TEXT SIZE IS OUT OF RANGE [4,8]` |
| 33 | E_NOFONT | `NO FONT CALLED name ON THE CARTRIDGE` |
| 34 | E_NOFILE | `FILE NOT FOUND: name` |
| 35 | E_FILES | `TOO MANY FILES: 8 AT MOST` |

The codes are in src/basic/basic.h and the messages in run.c.
