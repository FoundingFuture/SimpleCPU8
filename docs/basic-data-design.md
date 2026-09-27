# DATA, READ and RESTORE

BASIC gains tables of numbers and strings in the program text, the way the
Oric and every Microsoft BASIC had them. Two additions go past the classic
form. `DATA(n)` gives the RAM address of line n's bytes, so the GPU, a C
routine or a driver reads a table where it lies. A line can also name the
address its bytes go to. POKE takes a list of bytes as well.

The first user is a text mode game with its own characters. Its glyphs live
in DATA lines, and the lines drop them into the font the GPU reads. The font
map is a separate design. This one knows nothing of fonts.

## Contents

- [The statements](#the-statements)
- [What a DATA line holds](#what-a-data-line-holds)
- [Placement at RUN](#placement-at-run)
- [The address of a line](#the-address-of-a-line)
- [READ and RESTORE](#read-and-restore)
- [POKE with a list](#poke-with-a-list)
- [Errors](#errors)
- [RENUM and the IDE](#renum-and-the-ide)
- [Tests](#tests)
- [Decisions taken here](#decisions-taken-here)
- [Later designs](#later-designs)

## The statements

```basic
10 FOR I = 0 TO 5: READ A: PRINT A;: NEXT I
20 PRINT DATA(100)
30 POKE $E010, 1, 2, 3
100 DATA $E000, 10, 20, 30
110 DATA 40, 50, 60
```

Line 10 prints `102030405060`. Line 20 prints -8192. A BASIC number runs
from -32768 to 32767, so an address past `$7FFF` prints negative. POKE,
PEEK and DATA(n) wrap it back to `$E000`. At RUN the six bytes 10 to 60
land at `$E000` to `$E005`.
Line 30 writes 1, 2 and 3 to `$E010` to `$E012`.

| Word | Form | Does |
|---|---|---|
| DATA | statement | Holds values. Does nothing when a program reaches it. |
| DATA | function, `DATA(n)` | The address of line n's first byte. |
| READ | statement, `READ A, B$` | Takes the next values into variables. |
| RESTORE | statement, `RESTORE` or `RESTORE n` | Moves READ back to the first value, or to line n. |
| POKE | statement, `POKE a, v1, v2` | Writes each value to the next address. |

DATA, READ and RESTORE go into `src/basic/keywords.h`. The IDE's
highlighter and the capitals rule read that list, so both pick the words up
with no other change.

## What a DATA line holds

DATA is the first word of its line. The rest of the line is a list of
values separated by commas. A value is one of these:

- a decimal number, `10` or `-3`
- a hex number, `$1F` or `-$10`
- a string in quotes, `"HELLO"`

Values are constants. An expression or a variable name is a syntax error
at RUN, when placement reads the line.

The first value of a line is an address instead of a value when it is a
`$` number written with three or four hex digits. `$E000` and `$0010` are
addresses. `$10` is a value. A decimal first value is always a value. The
way the number is written decides, so LIST shows what the line does.

An address is only an address in first place. `$E000` later in a line is a
value, and out of range as one.

## Placement at RUN

RUN places every DATA line's bytes before line 1 runs. A drop cursor walks
the DATA lines in line order.

- The cursor starts at the program's data area.
- A line that opens with an address moves the cursor there first.
- Each value then goes to the cursor as one byte, and the cursor steps on.
- The next line continues from where the cursor stands.

So an address holds for the lines after it until another address moves the
cursor again:

```basic
100 DATA $E000, 10, 20, 30
110 DATA 40, 50, 60
120 DATA $E100, 1, 2
130 DATA 3
```

That puts 10 to 60 at `$E000` to `$E005`, and 1, 2 and 3 at `$E100` to
`$E102`.

A value from 0 to 255 is its own byte. A value from -128 to -1 is its two's
complement, so -1 is 255. A string is its characters, with no length byte
and no terminator. An empty string places nothing.

Nothing guards an address. A line writes into the screen, a mapped font,
the system page or the program itself, the way POKE would. Writing into a
memory mapped area is what the address is for.

### The program's data area

Lines before the first address go to the data area. It is the free program
memory after the program text, from `SYS_PROG + SYS_PROG_LEN` up to the
6144 byte limit. A placed value never takes more bytes than its text in the
line, so the data area almost always has room. When it does not, RUN stops
with `THE PROGRAM MEMORY IS FULL`. A program without DATA uses none of it.

The data area moves when the program is edited. That is harmless, because
RUN places everything again.

## The address of a line

`DATA(n)` returns the address of line n's first byte. For a line that
opens with an address, that is the address itself.

```basic
100 DATA $E000, 10, 20, 30
110 DATA 40, 50, 60
```

`DATA(100)` is `$E000` and `DATA(110)` is `$E003`.

`DATA(n)` works out the address by walking the DATA lines the way
placement does. It writes nothing. So it returns the same answer at the
prompt, after an edit and before a RUN, as a RUN would place the bytes at.
The walk costs time in proportion to the DATA before line n. A program
reads its addresses once at the start rather than in a loop.

A line with no values has the cursor's address. `DATA(n)` of a line that
does not exist is `THERE IS NO LINE n`. Of a line that holds no DATA it is
`LINE n HOLDS NO DATA`.

## READ and RESTORE

`READ A, B$, C` takes the next value for each variable in turn. The values
run on across lines, so a line boundary means nothing to READ. An address
in first place is where the bytes go and never data, so READ skips it.
`READ A` of the line `DATA $E000, 10` gives 10.

READ returns a value as written. It reads the text of the program, not the
placed bytes. `DATA -1` reads as -1, though its byte is 255. A string value
goes into a string variable.

`RESTORE` moves READ back to the first value of the program. `RESTORE n`
moves it to the first value of line n, which must hold DATA.

RUN, NEW, LOAD and any change to the program move READ back to the first
value.

## POKE with a list

`POKE a, v` writes one byte, as it does now. `POKE a, v1, v2, v3` writes
each value to the next address: v1 to a, v2 to a + 1, v3 to a + 2. The values
are expressions, as POKE's value always was.

## Errors

Three messages are new. The codes follow code 27, `E_RENUM`.

| Code | Name | On the screen |
|---|---|---|
| 28 | E_NODATA | `READ FOUND NO MORE DATA` |
| 29 | E_NOTDATA | `LINE n HOLDS NO DATA` |
| 30 | E_DATABYTE | `A DATA VALUE IS ONE BYTE: -128 TO 255` |

The existing messages cover the rest:

| Case | Message |
|---|---|
| `DATA(n)` or `RESTORE n` of a line not there | `THERE IS NO LINE n` |
| `READ A` of a string value | `A STRING CANNOT BE USED AS A NUMBER` |
| `READ A$` of a number | `A NUMBER CANNOT BE USED AS A STRING` |
| an expression or a name in a DATA line | `SYNTAX ERROR` naming what it found |
| DATA after a colon or after THEN | `SYNTAX ERROR: EXPECTED A STATEMENT BUT FOUND DATA` |
| the data area full | `THE PROGRAM MEMORY IS FULL: 6144 BYTES AT MOST` |

An error found while placing names the DATA line, `IN LINE n`, and the
program does not start.

DATA typed at the prompt does nothing, as it does in a running program.

## RENUM and the IDE

RENUM changes the line number after `RESTORE` and inside `DATA(`, as it
does after GOTO, GOSUB and THEN. Two places carry that rule:

- `ed_renum` in `src/basic/edit.c`, for RENUM typed at the prompt.
- `rewriteReferences` in `src/basic/lines.cpp`, for the IDE's editor.

Both change together, and tests hold them to one result, as they do for
the capitals rule.

The IDE's BASIC editor needs nothing else. DATA lines are program text, and
`src/basic/program.cpp` moves text.

## Tests

The tests run the interpreter in the headless machine, in
`tests/basic/basic_test.cpp`, and the renumbering in
`tests/basic/lines_test.cpp`. Each behaviour above gets a test that fails
first:

- placement into the data area, and at an address, and the carry over to
  the next line
- a negative value and a string placed as bytes
- `DATA(n)` at an address, after an address, in the data area, and before
  any RUN
- READ across lines, READ skipping an address, READ of a string
- RESTORE and RESTORE n
- POKE with one value and with a list
- each error in the tables above, with its code in SYS_ERR
- RENUM at the prompt and in the IDE rewriting `RESTORE n` and `DATA(n)`

## Decisions taken here

- DATA(n) returns an address, asked for by Eddie on 2026-09-27. The GPU and
  C routines read RAM, and a table read in place needs no copy loop.
- The program stays text. Placement at RUN writes the bytes. LIST, SAVE
  and the IDE bridge stay as they are.
- A first value written as a three or four digit hex number is an
  address. Eddie's rule. Later lines continue from it.
- READ never returns an address. The address says where the bytes go.
- A value is one byte. A table for the GPU is bytes, and a value that does
  not fit is an error, never a silent wrap.
- `RESTORE n` needs line n to hold DATA. Microsoft BASIC moved to the next
  DATA line at or after n. Here the machine says what is wrong instead.
- DATA must open its line. Placement then finds every DATA line from the
  line starts alone.

## Later designs

- A font the GPU reads from RAM, so DATA lines can define characters.
- A text mode game with its own characters, the first program to use both.
- BASIC keywords for the other GPU functions, one family at a time. Each
  family needs its own answer to where its data comes from.
