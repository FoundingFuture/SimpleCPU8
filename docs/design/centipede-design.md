# Centipede design

A Centipede game in BASIC for SimpleCPU-8, in examples/basic/centipede. It runs in
text mode, and everything on screen is a character of its own font. Objects
move on a grid of half cells. The font holds a glyph for every pair of halves
the rules can put in one cell.

Nothing is built. This is the design, written before any code, with the
interpreter's speed measured first. Eddie decides the open questions at the
end before phase 1 starts.

## Contents

- [What it is and is not](#what-it-is-and-is-not)
- [What BASIC can afford](#what-basic-can-afford)
- [The frame budget](#the-frame-budget)
- [The screen](#the-screen)
- [The half-cell grid](#the-half-cell-grid)
- [Objects and their states](#objects-and-their-states)
- [What never shares a cell](#what-never-shares-a-cell)
- [The pair table](#the-pair-table)
- [Computing a cell](#computing-a-cell)
- [The font](#the-font)
- [The centipede](#the-centipede)
- [Hits](#hits)
- [The player and the shot](#the-player-and-the-shot)
- [The spider](#the-spider)
- [The score and the status line](#the-score-and-the-status-line)
- [Waves, lives and screens](#waves-lives-and-screens)
- [Input, timing and DATA](#input-timing-and-data)
- [Memory](#memory)
- [Program structure](#program-structure)
- [Testing](#testing)
- [Phases](#phases)
- [Documents this changes](#documents-this-changes)
- [Open questions](#open-questions)

## What it is and is not

A BASIC project: src/autorun.bas and assets/centipede.font, built by
simplecpu-make into one ROM. No C, no assembly, no sprites, no graphics mode.
The playfield is written into the text buffer at SCREEN with POKE and DOKE,
never with PRINT.

In, in the brief's order of priority:

- The centipede descends and turns at mushrooms and edges.
- The player shoots, and a shot stops at the first thing it hits.
- A hit segment becomes a mushroom and cuts the chain there.
- Mushrooms take four hits.
- The player dies on contact.
- Waves.
- The spider.
- The arcade's scores, and three lives.

Out, and why:

- The flea. It is a fourth mover, estimated at 2 frames a step. Its job is
  to drop mushrooms into the player zone, and the budget below has no room.
- The poisoned mushroom. The arcade's scorpion makes them, and the scorpion
  is not in the brief. Poison also needs a dive state per chain and four more
  glyphs.
- Sound. BASIC has no sound word today. src/basic/keywords.h lists none, and
  BASIC has no OUT to reach the audio chip's ports.
- Colour. Text mode draws every character in one colour, and no BASIC word
  sets it. PAPER sets the background. The game is one colour on PAPER.
- A body that faces left or right. Both halves of a body segment are one
  drawing, which is what makes a step cheap. See
  [the centipede](#the-centipede).
- The bonus life, and the arcade's repair of damaged mushrooms after a death.
  Neither is in the brief.

## What BASIC can afford

Measured before any rule was settled.

### How it was measured

tests/support/basic_profile.cpp measures, built by the basic_profile
target. Its `--marks` mode boots BASIC on the machine tests/project runs
BASIC on, tests/support/basic_session.h, with the optimal microcode that
simplecpu-make picks for a BASIC project. It stores a program, holds PAD
with `--pad` and types RUN. The program writes a counter to $E7FF at each
checkpoint, and basic_profile prints the cycles between the changes.
`--per N` gives a loop's cost a pass, less the first loop's.

The programs are the prototypes in tests/support/centipede, one for each
measure below. Its README says which measures what.

One frame is 65,536 cycles at every speed setting, `CYCLES_PER_FRAME` in
src/vm/computer.h. The frame-locked speed runs one frame each 60th of a
second.

### Statement costs

Measured again after docs/design/basic-speed.md changed the interpreter,
with tests/support/basic_profile.cpp on the same programs. They are in
tests/support/centipede now:

```bash
build-headless/tests/basic/basic_profile --marks tests/support/centipede/statements.bas --per 500
```

Each statement stood alone on a line inside a FOR loop of 500 passes. The
cost of the same loop on a REM line is taken off. A line numbered below
256 costs `rt_run` 47 cycles more each time it runs, since its test for
the end of the program then reads a second byte. So each row is taken
against a REM loop in its own range. `A=5` and `A=X` are taken against
the loop at line 120, 2,342 cycles a pass, and the rest against the loop
at line 1200, 2,201. The first row is a whole pass of a loop written on
one line.

| Statement | Cycles | In one frame |
|---|---|---|
| `FOR I=0 TO 999:NEXT`, one pass of an empty loop | 691 | 94.8 |
| `A=5` | 1,151 | 56.9 |
| `A=X` | 1,529 | 42.9 |
| `LET A=X` | 1,746 | 37.5 |
| `A=PAD` | 1,415 | 46.3 |
| `A=X+Y` | 2,868 | 22.9 |
| `A=X*Y` | 2,932 | 22.4 |
| `A=X/Y` | 2,960 | 22.1 |
| `A=X MOD Y` | 2,948 | 22.2 |
| `A=RND(64)` | 2,222 | 29.5 |
| `A=S+Y*32+X` | 4,385 | 14.9 |
| `A=PEEK(S+X)` | 3,827 | 17.1 |
| `A=DEEK(S+X)` | 3,878 | 16.9 |
| `POKE S+X,C` | 3,168 | 20.7 |
| `POKE S+X,C,C` | 4,057 | 16.2 |
| `POKE S+X,C,C,C,C` | 5,835 | 11.2 |
| `DOKE S+X,C` | 3,217 | 20.4 |
| `POKE S+X,PEEK(S+Y)` | 5,466 | 12.0 |
| `IF X=Y THEN A=1`, false | 2,692 | 24.3 |
| `IF X<>Y THEN A=1`, true | 3,937 | 16.6 |
| `IF X=5 AND Y=7 THEN A=1` | 4,978 | 13.2 |
| `GOSUB` to the second line, and its `RETURN` | 1,122 | 58.4 |
| `GOSUB` to a line 221 lines down, and its `RETURN` | 1,198 | 54.7 |

What decides the design:

- A statement costs 1,100 to 5,900 cycles. A frame holds 11 to 58 of them.
- The first value of a POKE costs about 3,200 cycles and each further value
  890. One POKE with a list writes about 71 cells in a frame.
- GOTO, GOSUB and `IF ... THEN` with a line number walk the program from
  its first line once in a RUN. After that they read the line from the
  jump cache. A GOSUB 221 lines down costs 76 cycles more than one to the
  second line. NEXT goes back to its FOR without a walk.
- A line numbered below 256 costs 47 cycles more each time it runs.
- Spaces cost 4 percent. `A = X + Y` took 2,976 cycles and `A=X+Y`
  2,868.

### The numbers the brief asks for

From tests/support/centipede/loops.bas and redraw.bas.

| Measure | Cycles | Result |
|---|---|---|
| One POKE a pass, `FOR I=0 TO 999:POKE $F000+I,65:NEXT` | 3,605 a pass | 18.2 POKEs a frame |
| The same with the address in a variable, `POKE S+I,C` | 4,171 a pass | 15.7 POKEs a frame |
| A FOR loop over 12 segments, each read, checked and redrawn by the pair rule | 422,021 to 447,448 | 6.4 to 6.8 frames |
| One full centipede step with that loop | the same | 6.4 to 6.8 frames |

And the model this note uses, measured the same way:

| Measure | Cycles | Frames |
|---|---|---|
| One step of a 12 segment chain, head and tail only, chain held in variables | 65,375 to 75,697 | 1.00 to 1.16 |
| The same, with the chain loaded from a RAM record and stored back | 94,888 to 106,500 | 1.45 to 1.63 |
| The player taking a half step and a shot climbing 2 rows | 62,363 to 71,643 | 0.95 to 1.09 |
| A spider step, half a cell sideways and a row | 50,691 to 71,208 | 0.77 to 1.09 |
| 40 mushrooms placed with RND | 316,944 | 4.8 |
| The tick's own lines, each part a bare RETURN, one chain alive | 25,283 | 0.39 |
| The same with two chains or more | 27,969 | 0.43 |
| The status line's POKE of six digits, less the loop below | 17,554 | 0.27 |
| The step prototypes' own loop around a bare RETURN | 6,523 | 0.10 |

Each step alternates between two costs, because a half step alternates
between an aligned object and a straddling one. The first two chain steps
cost the most: they fill the jump cache. Every step row includes the
prototypes' own loop. spider.bas, tick.bas, status.bas and loop.bas are
new, so the whole tick is measured. spider.bas follows the rules of
[the spider](#the-spider).

### The verdict

A full-screen redraw is still out of reach of a frame. 31 rows of 32
cells, at 71 cells a frame, is 14 frames. The game updates only the cells
that change.

A centipede that redraws every segment costs 6.4 to 6.8 frames a step,
8.8 to 9.3 steps a second before anything else moves. A centipede whose
straight body looks the same after a half step costs 1.00 to 1.16 frames
a step, whatever its length, 5.5 to 6.8 times less. That design stays.

With the player, the shot and the spider, one tick takes 2.5 to 2.9
frames. The game runs 20 to 24 ticks a second. The centipede crosses the
30 columns in 2.5 to 2.9 seconds. A shot climbs the playfield in 0.6 to
0.7 seconds.

The first open question asks whether this pace is the game Eddie wants.

## The frame budget

No part of the game runs every frame. The unit is the tick, one pass of the
main loop.

Every tick:

- read PAD, and move the player a half step and a row at most
- move the shot 2 rows, when one is in flight
- step one centipede chain

Every other tick:

- step the spider

On an event only:

- a hit, to find the segment and cut the chain
- the status line, 0.27 frames, when the score or the lives change

| Chains alive | Chain steps a tick | Frames a tick | Ticks a second | Steps a second for each chain |
|---|---|---|---|---|
| 1 | 1, held in variables | 2.47 to 2.93 | 20.5 to 24.3 | 20.5 to 24.3 |
| 2 | 1, from a record | 2.96 to 3.44 | 17.4 to 20.3 | 8.7 to 10.1 |
| 4 | 1, from a record | 2.96 to 3.44 | 17.4 to 20.3 | 4.4 to 5.1 |
| 12 | 1, from a record | 2.96 to 3.44 | 17.4 to 20.3 | 1.5 to 1.7 |

Every figure in the table is measured. A tick is the tick's own lines,
one chain step, the player and the shot, and half a spider step. The
spider steps every other tick. The parts have the prototypes' loop,
6,523 cycles, taken off. A hit has no prototype, and it is not in the
table.

The game has no clock to fall behind. BASIC cannot read the frame counter,
since GPU_FRAME is a port and BASIC has no IN. A tick takes as long as its
work, and play never waits.

What falls behind is the centipede. One chain step a tick keeps the player
moving 17 to 24 times a second. After cuts, each chain waits its turn, so
the pieces slow down. A second chain step from a record adds 1.35 to 1.53
frames to a tick, 4.3 to 5.0 frames in all. The player would then move 12
to 14 times a second. That tick's own extra lines are not measured.

## The screen

`LOADFONT CENTIPEDE` loads assets/centipede.font. Its cell is 8 by 8, so the
grid becomes 32 by 32 without SETTEXT, and the screen clears. The game reads
SYS_COLS into W once, and stops with a message unless SYS_COLS and SYS_ROWS
both read 32.

A cell's address is SCREEN, $F000, plus the row times W plus the column.

```text
col    0  1                                 30  31
row 0     SCORE 000000                  lives        status line
row 1  |  the centipede enters here              |
rows 2 to 25   the mushroom field                |   playfield
rows 26 to 31  the player zone                   |
       wall                                   wall
```

- Row 0 is the status line.
- Rows 1 to 31 are the playfield.
- Rows 26 to 31 are the player zone. The player and the spider stay in it.
- Rows 2 to 25 are where the field's mushrooms start.
- Columns 0 and 31 of rows 1 to 31 hold the wall code, $9D, drawn blank.
  Movers see it as an obstacle, so no code tests for an edge. The playing
  width is columns 1 to 30, the arcade's 30.

The status line uses the ASCII glyphs. The playfield uses codes $80 to $DF,
and an empty playfield cell holds $80. LOADFONT fills the screen with 32, so
a new game writes $80 and the walls into rows 1 to 31. That is one POKE of 32
values a row, about 78,000 cycles, and 37 frames for the playfield.

## The half-cell grid

A position is a half position, H = row times 64 plus x. x runs 0 to 63, two
to a cell. Half x lies in cell x/2. x MOD 2 gives the side: 0 is the cell's
left four pixels, 1 its right four.

An object is 8 pixels wide, two halves. At an even x it fills one cell. At an
odd x it covers the right side of one cell and the left side of the next.

- Half steps: the centipede's heads and bodies, the player and the spider. A
  step moves x by 1.
- Whole cells: mushrooms, the shot and the walls.
- Every vertical move is a whole row, H plus or minus 64. A cell never holds
  parts of two rows, so the table stays two-dimensional.

The cell holding half H is at S+H/2, one division. A row in halves is 2W.

## Objects and their states

| Object | Moves | States | Codes |
|---|---|---|---|
| Mushroom | never | 4 damage states | $99 to $9C |
| Centipede head | half steps, rows when it turns | facing right, facing left | $A8, $B8, and halves in the pair table |
| Centipede body | half steps along the head's path | one, both halves the same bead | $91, and halves in the pair table |
| Player | half steps, rows in the zone | alive, hit | $C8, $C9, and halves in the pair table |
| Shot | whole cells, 2 rows a tick | in the left or the right side of its cell | $9E, $9F |
| Spider | half steps and rows, in the zone | one | $D8, and halves in the pair table |
| Wall | never | one, blank | $9D |

The flea and the poisoned mushroom are cut, see
[what it is and is not](#what-it-is-and-is-not).

## What never shares a cell

A cell holds at most two movers side by side, one half each. No half ever
holds two things. These rules keep it that way.

A shot over anything. The shot moves only into a cell that is entirely
empty, $80. Anything else in the cell is hit, whichever side it is in. So
the shot needs two glyphs, not a row and a column of the table. A mover
treats a cell holding the shot as an obstacle for that step.

A segment over a mushroom, sideways. A head checks the half it is about to
enter, and a mushroom there turns it first.

A segment over a mushroom, dropping. A head that turns drops a row onto
whatever is below. A mushroom there is crushed: the cell takes the head, and
nobody scores. The arcade lets the two overlap. Crushing keeps them out of
one cell without a glyph for the pair.

A mushroom made by a hit. The new mushroom takes the whole cell the shot
entered. A neighbouring segment can have a half in that cell. The drawing
order decides, and the mushroom shows. Every line that clears a half first
checks the cell holds a pair code, so a segment leaving never alters a
mushroom.

A segment over a segment. A head treats any occupied half as an obstacle
and turns. A drop into halves holding a segment or the spider does not
happen. The head reverses in its row and tries again next step.

The spider over a segment. The spider treats segment halves as walls and
reverses its vertical direction. A head treats spider halves as obstacles
and turns.

The spider over a mushroom. The spider eats it, as in the arcade.

The player over anything. A head or the spider entering a player half kills
the player. The player entering a segment or spider half dies. A mushroom,
the wall or the shot blocks the player.

Touching is not contact. A segment half beside a player half is two halves
of one cell, and the pair table has a glyph for it.

## The pair table

A playfield code is 128 + 16a + b. a is the kind covering the cell's left
side and b the kind covering its right side.

| Kind | Index | What the side shows |
|---|---|---|
| E | 0 | nothing |
| B | 1 | a body half, the same bead on either side |
| HR | 2 | half of a head facing right |
| HL | 3 | half of a head facing left |
| P | 4 | half of the player |
| S | 5 | half of the spider |

In a straddled cell the left side shows the right half of the object to the
left. The right side shows the left half of the object to the right. So a is
always a right half and b a left half. An aligned object fills its cell with
its whole glyph, which sits in column 8 of the sheet.

The glyph sheet shows 16 codes a row. Row $80 + 16a is left side a, and
column b is right side b. The sheet is the table. In the table below, rows
are the left side and columns the right side.

### Every pair the rules produce

| Left side | E | B | HR | HL | P | S |
|---|---|---|---|---|---|---|
| E | $80 | $81 | $82 | $83 | $84 | $85 |
| B | $90 | $91 | $92 | $93 | $94 | $95 |
| HR | $A0 | $A1 | $A2 | $A3 | $A4 | $A5 |
| HL | $B0 | $B1 | $B2 | $B3 | $B4 | $B5 |
| P | $C0 | $C1 | $C2 | $C3 | never | $C5 |
| S | $D0 | $D1 | $D2 | $D3 | $D4 | never |

Two cannot occur. $C4 would need the player straddling beside itself, and
$D5 the spider. There is one of each. They keep their slots so the
arithmetic stays regular, and they stay blank.

The 34 others all can:

- $80 is the empty cell.
- $81 and $90 are the ends of a body, and $91 its middle. $91 is also a whole
  body segment.
- A head beside nothing, beside a body, or beside another head. Two heads
  can meet, move apart, or follow one another.
- The player or the spider beside a segment, a head or each other, touching
  without contact.

### The other codes

| Code | Glyph |
|---|---|
| $99, $9A, $9B, $9C | mushroom, whole, three quarters, half, a quarter |
| $9D | wall, blank |
| $9E | shot, a line at the cell's pixel 0 |
| $9F | shot, a line at the cell's pixel 4 |
| $A8 | head facing right, whole |
| $B8 | head facing left, whole |
| $C8 | player, whole |
| $C9 | player hit |
| $D8 | spider, whole |
| $E0 to $ED, $F0 to $FD | the title's seven big letters, 2 by 2 cells each |

In play: 34 pairs, 4 whole movers, 4 mushrooms, the wall, 2 shots and the hit
player, 46 codes. The title's letters take 28 more, 74 in all.

### Codes kept and codes taken

| Codes | Use |
|---|---|
| $00 to $1F | blank, unused |
| $20 to $7F | the built-in glyphs, which a new font copies |
| $80 to $DF | the playfield, as above. Codes not listed stay blank |
| $E0 to $FF | the title's letters. $EE, $EF, $FE and $FF stay blank |

The status line, the title's text and the game over screen use $20 to $7F.
$7F stays the solid block, because BASIC draws its cursor with it.

## Computing a cell

No lookup. The layout turns both questions a mover asks into one comparison,
and every change into an addition.

The left side of cell X is free when X < $90. Row $80 holds exactly the codes
with nothing on the left. Every mushroom, wall, shot and whole glyph sits in
row $90 or below it.

The right side is free when X MOD 16 = 0. Column 0 holds exactly the codes
with nothing on the right. Every code outside the pairs sits in column 8 or
beyond.

A mover changes a side by adding or subtracting. Putting kind k into the
right side adds k, and taking it out subtracts k. The left side goes in
steps of 16.

An addition costs 1,339 cycles over a bare variable, `A=X+Y` against `A=X`.
A PEEK costs 2,298, `A=PEEK(S+X)` against `A=X`. A table built from DATA
would cost 959 cycles more per lookup than the arithmetic, so there is
none.

Where both cells of a write are constants, DOKE writes the pair for 3,217
cycles against 4,057 for a POKE with two values.

A hit decodes a code once, by column:

- 9 to 15 is a mushroom state, the wall or the shot
- 8 is a whole object of kind a, the row
- 0 to 5 is a pair, with left kind (X-128)/16 and right kind X MOD 16

## The font

assets/centipede.font, cell 8 8. It starts as the IDE's new font, which
copies the built-in glyphs at $20 to $7F. Every code from $80 is drawn in the
font editor:

```bash
simplecpu-ide examples/basic/centipede/assets/centipede.font
```

### What to draw first

Six drawings fix every other glyph:

- the body bead, 4 pixels wide and symmetric left to right. $91 is two of
  them side by side
- the head facing right, $A8
- the head facing left, $B8, the mirror of $A8
- the player, $C8, symmetric, with its gun on the line between its halves
- the spider, $D8, symmetric
- the mushroom, $99

Each pair glyph then follows. Its left four columns are the right four
columns of its left kind's whole glyph. Its right four columns are the left
four of its right kind's whole glyph. The body's whole glyph is $91. Bit 0 of
a row byte is the left pixel.

The pair table above is the glyph list for the pairs, row by row. The table
of other codes is the rest.

### Where the editor's views help

- The strip view plays four consecutive codes as frames. Consecutive pair
  codes share their left side, so the left half must stand still while the
  right half changes. Play $90 to $93, $A0 to $A3, $B0 to $B3, $C0 to $C3 and
  $D0 to $D3 to check each row. The strip also plays the mushroom's decay,
  $99 to $9C.
- The block view edits a 2 by 2 group as one 16 by 16 picture. It serves the
  title's letters. Letter n of C, E, N, T, I, P and D is the block at $E0 plus
  2n.
- The block view does not serve the pairs. A straddling object of kind t
  shows in $80 + t and $80 + 16t. For a head facing right that is $82 and
  $A0, 30 codes apart. The arithmetic needs the left side in the row.
- The half circle tool draws the mushroom's cap, the spider and the heads.
- Flip H mirrors a glyph in place. Draw the right-facing head at $B8 as well,
  then Flip H turns it to face left.

The editor has no way to copy a glyph, or half of one, to another code. So
each of the 34 pair glyphs is drawn by hand from its two halves. A phase 1
test checks every one against its halves, so a slip fails the build and not
the game. The open questions ask whether to add a copy.

## The centipede

### A chain and its path

A chain is a head and N-1 body segments behind it. Segments are two half
steps apart along the path the head took. The head decides every move, and a
body segment goes where the head went.

The path is a ring of 64 words at $E100, from R to E. Each head step writes
the head's new position into the next slot, I. Body segment k stands at the
slot 2k behind the head. The tail's slot is J, 2(N-1) behind I.

### Why a step touches three cells

Both halves of a body segment are the same bead, so a straight run of body
looks the same after any half step. Every cell of it holds $91 before and
after. A step rewrites only the cells at the ends:

- the head's two cells
- the cell the tail leaves

A 12 segment chain costs what a 2 segment chain costs, 3.04 to 3.09 frames
measured. The price is the look. The body reads as a string of 4 pixel beads,
and the boundaries between segments do not show. Distinct segments cost 19
frames a step.

### The head

H is the head's half position and D its direction, 1 or -1. Q is 1 when the
chain has a body and 0 for a lone head. A is S+H/2, the head's cell or the
left of its two cells.

Four cases, by direction and by whether H is even. A head facing right is
kind 2 and one facing left kind 3.

| Case | Enters | Free when | Writes |
|---|---|---|---|
| aligned, moving right | left side of A+1 | `PEEK(A+1)<144` | A becomes (Q, HR). A+1 gains 32 |
| straddling, moving right | right side of A+1 | `PEEK(A+1)=160` | A loses 2 and gains Q. A+1 becomes $A8 |
| aligned, moving left | right side of A-1 | `PEEK(A-1) MOD 16=0` | A-1 gains 3. A becomes (HL, Q) |
| straddling, moving left | left side of A | `PEEK(A)=131` | A becomes $B8. A+1 loses 48 and gains 16Q |

Each case is one PEEK, one IF and one POKE of two values. The prototype's
line for the first case:

```basic
40 X=PEEK(A+1):IF X<144 THEN POKE A,130+Q*16,X+32:H=H+1:GOTO 100
```

A blocked head turns. D becomes -D and the head drops a row, H plus V. V is
2W moving down and -2W moving up. Its old halves become body, or empty for a
lone head. Its new halves take the head's glyph for its new direction. That
crushes a mushroom below. When the halves below hold a segment or the
spider, the head reverses without dropping. When they hold the player, the
player dies.

At row 31, V becomes -2W and the head climbs. Climbing, it turns down again
at row 26. A chain that reaches the bottom stays in the player zone, as in
the arcade.

The ring slot is written after every move.

### The tail

The tail's slot moves on with the head's. T is the tail's old position and U
the step to its new one.

| U | The tail left | Write |
|---|---|---|
| 1 | half T | clear it |
| -1 | half T+1 | clear it |
| 2W or -2W | halves T and T+1 in the old row | clear both |

Clearing a half subtracts 1 from the right side or 16 from the left. The line
checks first that the cell holds a pair code. A lone head has no tail and no
ring.

### Corners

At a turn the path steps down a row without moving sideways. Segments
passing a corner cover its outer half on every other step. That half is not
redrawn each step. The head writes it as body when it turns, and the tail
clears it when it passes. So a corner shows one half of body more than the
segments cover at that moment. Drawing it exactly would cost two more cells a
step for every corner the chain spans.

### More than one chain

A second chain and every one after it live in 8 byte records at $E000: H, I
and V as words, then N and D+1. A tick steps one chain, the next in turn.
While one chain is alive it stays in the variables and skips the record. That
is the 3.0 against 4.3 frames measured above.

Chains cut from one chain share its ring. The front chain's slots lie ahead
of the back chain's, and each moves one slot per step. Taking turns keeps
every chain within one step of the others. So the back chain's head never
reaches the front chain's tail, across a gap of 4 slots after a cut. The
ring's 64 slots hold a wave's path of 23 slots. Taking turns adds at most one
slot per chain.

A lone head has no body and uses no ring slots.

## Hits

Each tick the shot climbs two rows. For each row it reads the cell above.
$80 lets it move. Anything else is a hit, and the shot is gone.

- A mushroom: the code goes up one, $99 to $9C. A hit on $9C empties the cell
  and scores 1.
- A head or a body half: the chain and the segment are found, and the chain
  is cut.
- The spider: it is gone, and scores by distance.
- Row 1: there is nothing above, and the shot is gone.

### Finding the segment

The hit half gives a half position h. Each chain's head is compared first,
since a head covers H and H+1. Then each chain's ring is walked back two
slots at a time, one segment each. At most 12 segments are compared. The
statement costs put that at about 5 frames, once per hit, a visible stall.

### The cut

Segment k of a chain of N is hit. The head is segment 0.

- The cell the shot entered becomes $99, a whole mushroom. The segment's
  other half, in the next cell, is cleared.
- The front part keeps segments 0 to k-1. With k = 0 there is no front part.
- The back part, segments k+1 to N-1, becomes a chain. Its head slot is
  I-2(k+1) and its length N-k-1. Its direction is its last sideways step in
  the ring, and its V is the old chain's. Its first segment is redrawn with a
  head's halves.
- A head scores 100 and a body segment 10.

The back part's new head meets the new mushroom within two steps and turns.
That is the arcade's behaviour, and it follows from the rules.

A hit on the head leaves one chain with a new head. A hit on a body segment
leaves two chains. That is how this note reads "a hit head splits the
chain", and an open question asks whether it is the reading meant.

## The player and the shot

The player stays in rows 26 to 31 and columns 1 to 30. It starts aligned at
row 30, column 15, H = 30*64+30. Each tick it moves a half step sideways and
a row up or down at most, from PAD.

Moving into a mushroom, the wall or the shot does nothing. Moving into a
segment or spider half kills the player.

Fire is any PAD value above 15: Z, Ctrl, space or Enter. While fire is held
and no shot is in flight, a shot starts in the cell above the player's
centre. The centre is the line between the player's halves, the left edge of
half H+1. So the shot is $9E when H+1 is even and $9F when it is odd. Each
draws its line at the left edge of that half.

One shot at a time, as in the arcade. It climbs 2 rows a tick, 15 rows a
second at 7.6 ticks.

## The spider

At most one spider. While there is none, each tick a `RND(64)=0` brings one
in at the left or the right wall, on a random row of the player zone.

A spider step, every other tick, is a half step sideways and a row up or
down. It bounces between rows 26 and 31 and leaves at the far wall.

- A mushroom half it enters is eaten.
- A segment half reverses its vertical direction.
- A player half kills the player.

A step clears up to two cells and draws up to two, reading each for its
other side. That estimates at 2.2 frames.

Shot, it scores by the rows between it and the player:

| Rows apart | Score |
|---|---|
| 0 or 1 | 900 |
| 2 or 3 | 600 |
| 4 or 5 | 300 |

## The score and the status line

The arcade's scores: a body segment 10, a head 100, a destroyed mushroom 1,
the spider 300, 600 or 900.

A BASIC number stops at 32,767, so the score is two numbers. L holds 0 to
9999 and G the ten thousands, which reaches 999,999. L past 9999 carries into
G.

Row 0 holds `SCORE`, six digits and the lives. `SCORE` prints once at the
start of a game, with the cursor home. The digits are one POKE of six
computed values, written only when the score changes:

```basic
470 POKE S+6,48+G/10,48+G MOD 10,48+L/1000,48+L/100 MOD 10,48+L/10 MOD 10,48+L MOD 10
```

The lives are player glyphs, $C8, in columns 28 to 30, written when they
change.

## Waves, lives and screens

A wave starts with its chains on row 1. The wave table, from DATA, gives the
main chain's length and the lone heads.

| Wave | Main chain | Lone heads |
|---|---|---|
| 1 | 12 | 0 |
| 2 | 11 | 1 |
| 3 | 10 | 2 |
| 4 | 9 | 3 |
| 5 and later | 8 | 4 |

The main chain enters aligned on row 1, columns 1 to N, facing right. Its
ring holds that straight path. Lone heads enter on row 1 at random columns
to its right, each facing a random way. Every lone head is a chain, so wave 5
starts with 5 chains, and each moves once in 5 ticks.

A wave ends when no chain is left. The next starts after `WAIT 30`.
Mushrooms stay from wave to wave.

Three lives. On contact the player's cells show $C9 for `WAIT 30`. Then the
shot, the spider and every chain are cleared. The centipede comes back at row
1 as one chain of the segments that were left, and the mushrooms stay. With
no lives left the game over screen follows.

The screens run in a ring, as Pac-Man's do: title, game, game over, title.
The title and the game over screen are not the playfield, so they use CLS
and PRINT. A key press leaves each. The machine never halts.

## Input, timing and DATA

PAD is read once a tick into B. It is a level, so a held key keeps the
player moving. AND and OR are logical in this BASIC, not bitwise, so tables
read the bits. B MOD 16 indexes two 16 byte tables at $E200 and $E210, the
sideways and the vertical step, each plus 1. Fire is B > 15.

KEY reads the title's and the game over screen's key press. It returns a
code when a key went down. Escape during play stops the program at the
prompt, since the interpreter checks for it between lines.

Pacing is WAIT, for the pauses only: a death, the end of a wave and the
title. The frame port cannot be read, because BASIC has no IN. Play never
waits, see [the frame budget](#the-frame-budget).

The tables come from DATA lines, placed at RUN:

```basic
9000 DATA $E200,1,1,1,1,0,0,0,0,2,2,2,2,1,1,1,1
9010 DATA $E210,1,0,2,1,1,0,2,1,1,0,2,1,1,0,2,1
9020 DATA $E220,12,0,11,1,10,2,9,3,8,4
9030 DATA $E230,40,2,24
```

Line 9030 is the field: 40 mushrooms, starting at row 2, over 24 rows. They
land at `RND(24)` rows and `RND(30)` columns from there, 14.7 frames for 40.

RND(n) takes a byte from the GPU's generator MOD n. BASIC cannot seed it,
since seeding is an OUT. The test machine starts the generator at its fixed
default seed, `RNG_DEFAULT_SEED`, so every test sees one field. simplecpu
seeds from the clock, so a player sees a new field each time.

## Memory

| Address | Bytes | Holds |
|---|---|---|
| $E000 | 96 | 12 chain records of 8 bytes |
| $E100 | 128 | the path ring, 64 words |
| $E200 | 16 | PAD to the sideways step |
| $E210 | 16 | PAD to the vertical step |
| $E220 | 10 | the wave table |
| $E230 | 3 | the field: count, first row, rows |
| $F000 | 1024 | the screen, BASIC's own |

BASIC's C stack grows down from $F000. Across the prototypes, measured
with basic_profile, the lowest byte it wrote was $EE96, 362 bytes down.
The tables end at $E232, 3,172 bytes below that.

The program holds 16,384 bytes, `PROGMAX` in src/basic/basic.h, DATA
lines included. It stores each keyword as one byte and each number with
its value. The chain step's 21 lines in chain.bas store in 804 bytes, and
the step from a record, 19 lines, in 891. The player and the shot, 8
lines, store in 506 bytes, and the spider, 12 lines, in 524. A prototype
stores in 0.89 to 1.08 of its listing. The whole game was estimated at
5.5 to 6 KB of listing, so it fits with room to spare. Spaces in a hot
line cost 4 percent, so the hot lines still carry none.

### Variables

A to Z and A0 to Z9 are 286 integers of 16 bits. BASIC has no arrays, so
every list lives in RAM behind PEEK and DEEK.

| Name | Holds |
|---|---|
| S | SCREEN, $F000 |
| W | the columns, from SYS_COLS |
| A | a cell address |
| X, X1 | cell codes |
| H | the stepping chain's head, a half position |
| D | its direction, 1 or -1 |
| V | its vertical step, 2W or -2W |
| I | its head's ring slot address |
| J | its tail's ring slot address |
| N | its length |
| Q | 1 when N > 1 |
| T, U | the tail's old position and its step |
| C | the address of the chain record being stepped |
| K | the chains alive |
| K0 | the next chain in turn |
| R, E | the ring's first address and the one past its end |
| P | the player's half position |
| B | PAD this tick |
| F | the shot's cell address, 0 with no shot |
| Z | the shot's code, $9E or $9F |
| Y | the spider's half position, 0 with no spider |
| Y1, Y2 | the spider's sideways and vertical steps |
| O | the wave |
| M | the lives |
| G, L | the score, in ten thousands and the rest |
| T9 | ticks since the game started, which the tests read |

## Program structure

examples/assembly/pacman is assembly, so this layout is new. One file,
src/autorun.bas.

Two measured costs order it. A jump walks from the first line, 280 cycles a
line. NEXT returns without a walk. So the hottest lines come first, and their
jump targets sit near the top.

| Lines | Part | Runs |
|---|---|---|
| 1 | `GOTO 7000`, the title | once |
| 10 to 19 | the tick | every tick |
| 20 to 29 | load and store a chain record, take the next chain | every tick with 2 chains or more |
| 30 to 99 | the chain step: head, ring and tail | every tick |
| 100 to 149 | the player | every tick |
| 150 to 199 | the shot | every tick with a shot |
| 200 to 299 | the spider | every other tick |
| 300 to 449 | hits: mushroom, finding the segment, the cut, the spider | on a hit |
| 450 to 499 | the score and the lives on row 0 | when they change |
| 500 to 599 | a death and the end of a wave | on the event |
| 6000 to 6099 | a new wave: chains and the ring | per wave |
| 6100 to 6199 | a new game: font, playfield, walls, mushrooms, row 0 | per game |
| 7000 to 7199 | the title and the game over screen | between games |
| 9000 to 9099 | DATA | placed at RUN |

The tick:

```basic
10 GOSUB 100
11 IF F THEN GOSUB 150
12 IF K>1 THEN GOSUB 20
13 IF K THEN GOSUB 30
14 IF K>1 THEN GOSUB 25
15 T9=T9+1:IF T9 MOD 2=0 THEN GOSUB 200
16 GOTO 10
```

Three rules of this BASIC shape every line:

- A false IF skips the rest of its line, so a line holds one decision.
- GOSUB returns to the line after its own, so a GOSUB ends its line.
- FOR loops nest 8 deep and GOSUBs 16.
- A line from AUTORUN holds up to 250 characters. A line typed at the prompt
  holds 79, so the long lines are edited in the IDE.

A hot line packs its statements with no spaces, as the head's line
above does.

## Testing

In tests/project, beside the examples/basic/font test. simplecpu-make builds the
example, and `testing::Session` boots the ROM headless at the fixed seed. A
test sets PAD through `Session::input.buttons`. It reads the screen at $F000
and the variables through SYS_VARS. T9 says where the game is.

A test keys its pad sequence to T9, not to frames. The game's logic runs per
tick, so a change that only makes a line faster leaves a run as it was.

The Release build runs the machine at about 68 million cycles a second,
1,037 frames, 17 times real time.

| Test | Frames | Cost |
|---|---|---|
| the project builds | none | the make run |
| the ROM boots to the title | about 100 | cheap |
| a fixed seed gives a known field, three cells read | about 150 | cheap |
| one centipede step gives the pair codes | about 200 | cheap |
| a shot turns a segment into a mushroom | about 1,000 | cheap |
| a full wave by a scripted pad sequence reaches a known score | 3,600 to 10,800 | expensive |

The step test's codes are known now. A wave starts with the main chain
aligned on row 1, cells 1 to 12, the head at cell 12. One step later cell 1
reads $81, cell 12 reads $92 and cell 13 reads $A0. Cells 2 to 11 still read
$91. A second step gives $80, $91 and $A8 in cells 1, 12 and 13.

The three field cells are pinned when phase 2 first runs, since the field is
whatever the default seed gives.

The shot test stages its board. At a known T9 it pokes the player's column
clear to $80, holds fire, and checks that a $99 appears on row 1 and the
score reads 10 or 100.

The full wave is the expensive one, and the speed numbers make it so. A wave
is an estimated 1 to 3 minutes of play. That is 4 to 11 seconds in the
Release build and about ten times that in the debug build.

## Phases

Each phase leaves master working: the project builds, its tests pass and the
ROM runs.

### Phase 1, the font and the pair table

assets/centipede.font with every code in the tables above. autorun.bas loads
it, lays out every game code on a still screen with each row labelled, and
waits for a key.

Tests:

- The project builds, and the ROM boots to the still screen.
- Every game code stands on screen at its place.
- Read from the .font file through src/assets/font.h: every pair glyph
  equals its two halves.
- The body bead is symmetric, and $91's two sides are the same bead.
- $B8 is $A8 mirrored. The player and the spider are symmetric.
- $C4 and $D5 are blank.

### Phase 2, the field, the player and the shot

The playfield, the walls, 40 mushrooms, row 0, the player and the shot.

Tests:

- The fixed seed gives the known field, read at three cells.
- A half step right from aligned at cell c gives $84 in c and $C0 in c+1.
- The player does not enter a mushroom.
- A shot takes a mushroom from $99 to $9A. Four shots empty the cell and the
  score reads 1.

### Phase 3, the centipede

Chains, the four head cases, turns, drops, the bounce, the ring, the tail,
lone heads, hits, cuts, waves and death on contact.

Tests:

- One step gives the codes above.
- A head blocked by a mushroom drops a row, reverses and shows its new
  facing.
- A drop onto a mushroom crushes it.
- A shot turns a body segment into a mushroom and leaves two chains.
- A shot at the head leaves one chain with a new head.
- A cleared wave starts the next with a chain of 11 and one lone head.
- No step writes $C4 or $D5.

### Phase 4, the spider and the score

Tests:

- The spider eats a mushroom it walks into.
- Shooting the spider scores 900, 600 or 300 by the rows apart.
- The spider's contact kills the player.
- Three deaths reach the game over screen.
- The score carries past 9999 into the fifth digit.

### Phase 5, polish

The title with the big letters, the game over screen, the ring of screens,
examples/basic/centipede/README.md, and the full wave test.

## Documents this changes

None. The README in examples/basic/centipede, its line in examples/README.md and
the CLAUDE.md repository map arrive with the code.

## Open questions

### The pace

One chain steps 20.5 to 24.3 times a second. The centipede crosses the
playfield in 2.5 to 2.9 seconds, and a shot climbs it in 0.6 to 0.7
seconds. After cuts, each chain moves once per turn round all of them.
Four chains move 4.4 to 5.1 times a second each.

The options, with their trade-offs:

- Accept the pace, with one chain step a tick. The player keeps 17 ticks
  a second or better. The pieces slow as they multiply.
- Two chain steps a tick. The pieces keep twice the speed, and the player
  drops to 12 to 14 ticks a second whenever two chains are alive.
- Slow the tick on purpose with `WAIT`, so the pace no longer follows the
  work. The waits are not measured.

### A faster interpreter

Done. docs/design/basic-speed.md made the changes this question asked
for. Stored keyword tokens replace the string compares, a switch on the
token picks a statement, and a cache of jump targets replaces the walk.
Numbers are stored with their value, and the lexer and the compiler's
peephole pass were tightened. The note measures each change.

On the prototypes a chain step went from 3.04 to 3.09 frames to 1.00 to
1.16. The player and the shot went from 3.0 to 3.3 frames to 0.95 to
1.09.

What is left: a line numbered below 256 costs 47 cycles more each time it
runs. [The program structure](#program-structure) puts the tick and the
chain step on such lines. The note's ceiling section has the rest.

### Program memory

Closed. `PROGMAX` in src/basic/basic.h is 16,384, and `TEXTMAX` in
store.c 22,528. The whole game was estimated at 5.5 to 6 KB of listing,
and a prototype stores in 0.89 to 1.08 of its listing, so the game fits
with room. The spider and the lone heads stay.

### Copying glyphs in the font editor

There is no copy, so 34 pair glyphs are drawn by hand. A copy and a paste
of the current glyph in font mode would take functions over `sprite::Frame`
in src/assets/sprite.h, tested headless, and buttons in
src/ide/sprite_editor.cpp. A paste of the left or the right four columns
alone would make each pair two pastes.

The other route is a script that writes the pairs from the six drawings, as
examples/assembly/pacman/gensprites.py writes Pac-Man's sprites. The brief asks for
the editor, so either route needs Eddie's word.

### Reading the frame counter

BASIC cannot read GPU_FRAME. A function returning it would take one entry
in `fn_call()` in expr.c, its word in keywords.h, and the guides. With it
the game could pad a light tick to an even pace. Without it a tick lasts 7
to 14 frames, as its work varies.

### Sound

BASIC has no sound word, so the game is silent. A word would reach the audio
chip's ports from run.c. It is a design of its own.

### Colour

The text is one colour, and no BASIC word sets it. The arcade changes its
colours each wave. A word for `GPU_TEXT_COLOR` would take one statement in
run.c, its entry in keywords.h and the guides.

### A hit head splits the chain

Read here as the arcade plays it. Any hit segment becomes a mushroom and cuts
the chain there. A hit on the head leaves the rest as one chain under a new
head. A hit on a body segment leaves two chains.
