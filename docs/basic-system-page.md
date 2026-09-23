# The BASIC system page

BASIC keeps its own state in the first 32 bytes of the zero page. Every
address is fixed. A program reads it with PEEK and DEEK and changes it with
POKE and DOKE. A driver in assembly finds it without a symbol table. The
old home computers kept their pointers this way. The Oric's page 2 held the
cursor, the last key, the timers and the vectors a disc system hooked. This
page follows that idea.

The compiler leaves the page alone. BASIC is compiled with `-zp-reserve
32`, which puts the compiler's own zero page traffic from $20 up. The
macros in src/basic/basic.h are the variables, so nothing here is a copy
that can drift.

Words are two bytes, high byte first, the way DOKE stores them and DEEK
reads them.

## The page

| Address | Name | Size | Holds |
|---|---|---|---|
| $00 | SYS_BANG_VEC | word | the instruction slot of the bang handler |
| $02 | SYS_BANG_TEXT | word | the address of the statement's text while a handler runs |
| $04 | SYS_BANG_RESULT | byte | what the handler left in A |
| $05 | SYS_COL | byte | cursor column, 0 to 41 |
| $06 | SYS_ROW | byte | cursor row, 0 to 31 |
| $07 | SYS_KEY | byte | the last key pressed, 0 before any |
| $08 | SYS_PROG | word | where the stored program starts |
| $0A | SYS_PROG_LEN | word | its length in bytes, 3 for an empty program |
| $0C | SYS_VARS | word | the integer variables, 11 words per letter |
| $0E | SYS_HEAP | word | where the string heap starts |
| $10 | SYS_HEAP_TOP | word | bytes of the heap in use |
| $12 | SYS_ERR | byte | the last error code, cleared when the next command starts |
| $13 | SYS_ERR_LINE | word | the line it happened on, 0 in direct mode |
| $15 | SYS_RUNNING | byte | 1 while a program runs |
| $16 | SYS_PC | word | the offset in the program of the line being run |
| $18 | reserved | 8 | free for what comes next |

The integer variables sit in 11 word slots per letter: the bare name first,
then the digit forms 0 to 9. So `B` is slot 11 and `B7` is slot 19, and
`DEEK(DEEK(12) + 22)` reads `B`.

## What a program can do with it

Read the cursor to draw at the prompt's position. Read the last key without
consuming one. Find its own text to write a self modifying program, which
the machine allows. Reach a variable by number rather than by name, which
is how a driver hands a result back to BASIC. Read the error a `!` handler
left.

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
10 DOKE 40000, DEEK(0)
20 DOKE 0, 61440
```

From assembly, the same in four loads, as docs/storage-design.md shows.
examples/basic-driver is the worked example.

## The error codes

| Code | Name | On the screen |
|---|---|---|
| 0 | E_OK | no error |
| 1 | E_SYNTAX | `SYNTAX ERROR` |
| 2 | E_NOLINE | `NO SUCH LINE ERROR` |
| 3 | E_STACK | `TOO DEEP ERROR` |
| 4 | E_MEMORY | `OUT OF MEMORY ERROR` |
| 5 | E_TYPE | `TYPE ERROR` |
| 6 | E_DIVZERO | `DIVIDE BY ZERO ERROR` |
| 7 | E_RANGE | `OUT OF RANGE ERROR` |
| 8 | E_BREAK | BREAK |
| 9 | E_BANG | `UNKNOWN ! COMMAND ERROR` |
| 10 | E_NOTFOUND | `NOT FOUND ERROR` |
| 11 | E_STOFULL | `STORAGE FULL ERROR` |
| 12 | E_BADNAME | `BAD NAME ERROR` |

The codes are in src/basic/basic.h and the messages in run.c.
