# BASIC for beginners

A book for someone who has never written a program. It uses the BASIC
built into SimpleCPU-8. Every program in it was typed into the machine
and run before it was printed here. Read it in order. Each chapter adds
one idea and one small program to type in.

## Contents

- [What a computer does](#what-a-computer-does)
- [Starting BASIC](#starting-basic)
- [Talking to the machine](#talking-to-the-machine)
- [Numbers and words](#numbers-and-words)
- [A stored program](#a-stored-program)
- [Remembering things](#remembering-things)
- [Memory as numbered boxes](#memory-as-numbered-boxes)
- [Making decisions](#making-decisions)
- [Doing things again](#doing-things-again)
- [Working with words](#working-with-words)
- [Drawing](#drawing)
- [Reading keys](#reading-keys)
- [Saving and loading](#saving-and-loading)
- [The IDE](#the-ide)
- [Longer programs](#longer-programs)
- [A small game](#a-small-game)
- [Where to go next](#where-to-go-next)
- [Every word](#every-word)

## What a computer does

A computer follows a list of instructions. The list is called a program.
The computer reads one instruction, does it, and moves to the next. It
never gets bored and never skips a step. It also never guesses. If an
instruction makes no sense to it, it stops and says so.

SimpleCPU-8 is a small computer that lives inside a window on your
screen. It has four parts you will meet in this book.

The processor reads the instructions and does the arithmetic.

The memory is where the computer keeps things. It is a long row of
boxes. This machine has 65536 boxes, which people call 64K. Each box
has a number, from 0 to 65535, and each box holds one number from 0 to
255. A program, its words and its pictures all live in these boxes.

The screen shows you what the computer is doing. It is 256 dots wide and
256 dots tall. A dot is called a pixel. When the screen shows text, each
letter takes a cell 6 pixels wide and 8 tall. So the screen holds 42
letters across and 32 lines down.

The keyboard is how you talk back. A key you press goes into a small
queue. A program takes keys out of that queue when it wants them.

BASIC is a language for giving the computer instructions in words that
look like English. `PRINT "HELLO"` puts HELLO on the screen. You type an
instruction, the machine does it, and you see the result at once.

## Starting BASIC

Build the project first, following README.md. Then start the machine
with BASIC in it:

```bash
./r
```

That is the same as `build/release/src/vm/simplecpu --basic`. A window
opens showing the machine's screen. It says:

```text
SimpleCPU-8 BASIC
READY
>
```

`READY` means BASIC is waiting for you. The `>` is the prompt, and the
white block after it is the cursor. Whatever you type appears at the
cursor. Press Enter to send a line to the machine. Backspace rubs out the
last letter. Small letters are turned into capitals as you type.

F5 restarts the machine. Everything you typed is gone afterwards, so
read the chapter on saving before you rely on it.

## Talking to the machine

Type this and press Enter:

```basic
PRINT "HELLO"
```

The screen shows:

```text
>PRINT "HELLO"
HELLO
READY
>
```

`PRINT` is an instruction. It means put this on the screen. The quotes
mark where the text starts and stops. The machine prints the text, then
says `READY` again, because it has nothing more to do.

Now make a mistake on purpose:

```basic
PRUNT "HELLO"
```

```text
? UNKNOWN WORD PRUNT
READY
>
```

The machine did not understand the line, and it names the word it
tripped on. It is not angry and nothing is broken. Fix the typo and type
the line again. You will see messages like this often. Everyone does.

When the words are right but in the wrong order, the message is a syntax
error. It says what the machine expected and what it found instead:

```basic
PRINT (1 + 2
```

```text
? SYNTAX ERROR: EXPECTED ) BUT FOUND THE END OF THE LINE
```

## Numbers and words

`PRINT` can do arithmetic. Type each line and read what comes back.

```basic
PRINT 6*7
PRINT 2+3*4
PRINT (2+3)*4
```

```text
42
14
20
```

The star means multiply, because the keyboard has no times sign. The
machine multiplies before it adds, the way a maths teacher does.
Brackets change the order.

Division has a surprise:

```basic
PRINT 7/2, 7 MOD 2
```

```text
3 1
```

This machine works only in whole numbers. `7/2` is 3, and the half is
thrown away. `MOD` gives the remainder instead, which is 1 here. The
comma between the two prints a space.

Numbers run from -32768 to 32767. Go past the end and the count wraps
round to the other end:

```basic
PRINT 32767+1
```

```text
-32768
```

Text in quotes is called a string. A semicolon joins two things with no
gap. A comma puts one space between them.

```basic
PRINT "SIX TIMES SEVEN IS"; 6*7
PRINT "A";"B"
PRINT "A","B"
```

```text
SIX TIMES SEVEN IS42
AB
A B
```

The first line has no space before 42. The machine prints numbers with
nothing in front of them. Put the space inside the quotes when you want
one.

## A stored program

So far each line ran as soon as you pressed Enter. A program is a list
of lines the machine keeps and runs later. To store a line, put a number
in front of it:

```basic
10 PRINT "HELLO"
20 PRINT 6*7
```

Nothing is printed except `READY`. The lines are stored, not run. Now
type:

```basic
RUN
```

```text
HELLO
42
READY
>
```

`RUN` runs the stored lines in number order, lowest first. `LIST` shows
them:

```basic
LIST
```

```text
10 PRINT "HELLO"
20 PRINT 6*7
READY
>
```

The numbers are the reason lines can be kept in order however you type
them. They also give you room to change your mind. People count in tens
so a new line can go between two old ones. Type `15 PRINT "AND"` and it
lands between 10 and 20.

`LIST` can also show part of a program. `LIST 20` shows line 20.
`LIST 20-40` shows lines 20 to 40. `LIST -40` shows everything up to
line 40, and `LIST 20-` shows everything from line 20 on. A long program
no longer scrolls away before you find the line you want.

After many lines squeezed in between others, the numbers get crowded.
`RENUM 100,50` numbers the program again: the first line becomes 100,
the next 150, the one after that 200. `RENUM` on its own starts at 10 and counts in
tens. Every `GOTO`, `GOSUB` and `THEN` changes with the line it names,
so the program runs as before. A number inside quotes or after `REM`
stays as you typed it.

To replace a line, type it again with the same number. To delete a line,
type its number and nothing else. `NEW` throws the whole program away.

```basic
10 PRINT "OLD"
10 PRINT "NEW"
LIST
10
LIST
```

```text
10 PRINT "NEW"
READY
>10
READY
>LIST
READY
>
```

A program holds up to 6144 bytes of lines, and a line holds up to 79
characters.

## Remembering things

A variable is a box with a name. You put a number in it, and later you
take the number out. The name is one capital letter, or a letter and a
digit. `A`, `B`, `Z`, `A0` and `T9` are all variable names. That gives
you 286 boxes for numbers.

```basic
10 A=5
20 B=A*2
30 PRINT A, B
```

```text
5 10
```

`A=5` means put 5 in box A. `B=A*2` means look in box A, double what you
find, and put that in box B. The old word `LET` still works, so
`LET C=3` is the same as `C=3`.

A box for text has a dollar sign after its letter. There are 26 of them,
`A$` to `Z$`, and each holds up to 255 characters.

```basic
10 A$="HELLO"
20 PRINT A$
```

```text
HELLO
```

`INPUT` stops the program and waits for you to type something. Whatever
you type goes into the variable.

```basic
10 INPUT "YOUR NAME"; N$
20 PRINT "HELLO, "; N$
30 INPUT "YOUR AGE"; A
40 PRINT "IN TEN YEARS YOU WILL BE "; A+10
```

```text
YOUR NAME? ADA
HELLO, ADA
YOUR AGE? 12
IN TEN YEARS YOU WILL BE 22
```

The question mark is printed by `INPUT`. If you type letters where a
number was expected, the variable gets 0.

## Memory as numbered boxes

The variables are boxes with names. Underneath, every box in the machine
has a number instead. `PEEK` looks into a box by its number and `POKE`
puts a value in.

```basic
10 POKE 40000,77
20 PRINT PEEK(40000)
```

```text
77
```

Box 40000 now holds 77. A box holds one byte, which is a number from 0
to 255.

The screen is made of boxes too. Box 64192 is the top left cell of the
text screen. The 42 boxes after it are the rest of the top row, and the
next 42 are the second row. A cell shows the character whose code is in
its box. The code for `A` is 65.

```basic
10 POKE 64192,65
20 POKE 64192+41,66
30 POKE 64192+42,67
```

An `A` appears in the top left corner, a `B` in the top right, and a `C`
at the start of the second row. `PRINT` does the same thing for you: it
puts codes into these boxes, one after another.

Two more boxes are worth knowing. Box 5 holds the column of the cursor
and box 6 holds its row. Change them, and the next `PRINT` starts there.

```basic
10 CLS
20 POKE 5,10: POKE 6,5
30 PRINT "HERE";
```

`CLS` clears the screen. The colon lets two instructions share one line.
The semicolon at the end of `PRINT` stops it moving to a new line. The
word HERE appears at column 10, row 5.

`DEEK` and `DOKE` do the same for a pair of boxes at once, so a number up
to 65535 fits. `DOKE 40000,1000` puts 3 in box 40000 and 232 in box
40001, because 3 times 256 plus 232 is 1000. `DEEK(40000)` reads it back
as 1000. You will not need them for a long while.

## Making decisions

`IF` runs an instruction only when something is true.

```basic
10 A=7
20 IF A>5 THEN PRINT "BIG"
30 IF A<5 THEN PRINT "SMALL"
40 IF A=7 THEN PRINT "SEVEN"
50 IF A<>7 THEN PRINT "NOT SEVEN"
60 IF A>0 AND A<10 THEN PRINT "ONE DIGIT"
```

```text
BIG
SEVEN
ONE DIGIT
```

`>` means greater than, `<` less than, `=` equal, and `<>` not equal.
`<=` and `>=` mean less or equal and greater or equal. `AND` needs both
sides true. `OR` needs one. `NOT` turns true into false.

A number after `THEN` sends the program to that line:

```basic
70 IF A=1 OR A=7 THEN 90
80 PRINT "SKIPPED"
90 PRINT "DONE"
```

Add these lines to the program above and run it. `SKIPPED` never
appears, because line 70 jumps straight to 90.

## Doing things again

`GOTO` sends the program to a line. Send it backwards and it goes round
for ever.

```basic
10 PRINT "HELLO ";
20 GOTO 10
```

The screen fills with HELLO and keeps filling. Press Escape, or hold
Ctrl and press C, to stop it.

```text
HELLO HELLO HELLO ? BREAK IN LINE 20
READY
>
```

`BREAK IN LINE 20` tells you which line was running when you stopped it. The
program is still stored, so `LIST` and `RUN` work as before.

Most loops should end by themselves. `FOR` counts for you.

```basic
10 FOR I=1 TO 5
20 PRINT I; " TIMES 3 IS "; I*3
30 NEXT I
```

```text
1 TIMES 3 IS 3
2 TIMES 3 IS 6
3 TIMES 3 IS 9
4 TIMES 3 IS 12
5 TIMES 3 IS 15
```

Line 10 puts 1 in box I. The lines down to `NEXT` run, then `NEXT` adds
1 to I and goes back. When I passes 5 the program carries on past
`NEXT`. `STEP` changes the amount added, and a negative step counts
down. `WAIT 30` pauses for 30 frames, which is half a second.

```basic
10 FOR I=5 TO 1 STEP -1
20 PRINT I
30 WAIT 30
40 NEXT I
50 PRINT "GO"
```

A short loop fits on one line. The colon separates the instructions,
as it did for `POKE`.

```basic
10 FOR I=1 TO 5: PRINT I;: NEXT I
```

```text
12345
```

A loop can hold another loop. The inner one runs all the way through
for every step of the outer one. Each `NEXT` closes the loop that was
opened last.

```basic
10 FOR I=1 TO 3
20 FOR J=1 TO 3
30 PRINT I*J; " ";
40 NEXT J
50 PRINT
60 NEXT I
```

```text
1 2 3
2 4 6
3 6 9
```

`GOSUB` jumps to a line and remembers where it came from. `RETURN` goes
back. That lets a piece of program be used from more than one place.

```basic
10 GOSUB 100
20 GOSUB 100
30 PRINT "END"
40 END
100 PRINT "SUB"
110 RETURN
```

```text
SUB
SUB
END
```

`END` stops the program. Without it, line 100 would run a third time and
`RETURN` would have nowhere to go.

## Working with words

Strings can be joined with `+`, measured, and cut into pieces.

```basic
10 A$="SIMPLE"
20 B$="CPU"
30 C$=A$+B$
40 PRINT C$
50 PRINT LEN(C$)
60 PRINT MID$(C$,3,2)
70 PRINT MID$(C$,7)
80 PRINT STR$(42)+"!"
90 PRINT VAL("12")+1
100 PRINT CHR$(65), ASC("A")
110 IF A$="SIMPLE" THEN PRINT "SAME"
```

```text
SIMPLECPU
9
MP
CPU
42!
13
A 65
SAME
```

`LEN` counts the characters. `MID$(C$,3,2)` takes 2 characters starting
at the third. Leave the count out and it takes the rest. `STR$` turns a
number into text and `VAL` turns text back into a number. `CHR$` gives
the character with a code and `ASC` gives the code of a character.
Strings compare with the same signs as numbers, in alphabet order.

There is no `LEFT$` or `RIGHT$`. `MID$(A$,1,3)` is the first three
characters, and `MID$(A$,LEN(A$)-2)` is the last three. There are no
lists of variables either. This BASIC has no `DIM` and no arrays. When a
program needs a row of numbers, `POKE` them into a run of boxes and
`PEEK` them back.

## Drawing

Six words draw on the 256 by 256 screen. `PAPER` fills it with a colour.
`INK` picks the pen, and everything drawn after it takes that colour.
The pen starts white. `MOVE` puts the pen at a point without drawing.
`DRAW` draws a line from the pen to a point. `PLOT` puts one dot at a
point. `CIRCLE` draws a ring around the pen. `PIXEL(X,Y)` reads the
colour of a pixel back.

`CIRCLE` takes up to three numbers: the radius across, the radius down
and a fill colour. `CIRCLE 40` is a round ring. `CIRCLE 40,20` is an
oval, twice as wide as it is tall. `CIRCLE 40,40,224` is a round ring
filled with red. The ring is in the `INK` colour and the fill in the
third number, so a disc can have a rim of another colour. The three may
sit in brackets, as `CIRCLE(40,40,224)`.

A colour is a number from 0 to 255. Multiply the red by 32 and the green
by 4, then add the blue. Red and green go from 0 to 7, blue from 0 to
3. So 224 is red, 28 is green, 3 is blue, 255 is white and 0 is black.
X runs from 0 on the left to 255 on the right. Y runs from 0 at the top
to 255 at the bottom.

The picture sits behind the text. Both show at once, so a program can
draw and print in the same run. Type this and see for yourself:

```basic
10 PAPER 3
20 INK 255
30 MOVE 128,128
40 CIRCLE 40,40,224
50 INK 28
60 MOVE 0,0
70 DRAW 255,255
80 PRINT PIXEL(100,128)
```

```text
224
```

A red disc with a white rim appears on blue, with a green line from
corner to corner. The listing stays readable on top. The pixel left of
the middle is red, so `PIXEL` says 224. The middle itself is under the
line, so it is green. The text is white, so a white paper hides it.

`CLS` clears the text and paints the picture in the `PAPER` colour
again. `PAPER` on its own repaints the picture and leaves the text.

## Reading keys

`INKEY$` gives the key pressed since the program last looked, or an
empty string when there was none. It does not wait.

```basic
10 PRINT "PRESS A KEY"
20 K$=INKEY$
30 IF K$="" THEN 20
40 PRINT "YOU PRESSED "; K$; " WHICH IS CODE "; ASC(K$)
```

```text
PRESS A KEY
YOU PRESSED Q WHICH IS CODE 81
```

Line 30 goes back to line 20 until a key arrives. `KEY()` does the same
job with numbers. It gives the code of the key, or 0. The arrow keys
have codes 17, 18, 19 and 20 for up, down, left and right. Enter is 13
and Escape is 27.

A game wants to know which keys are held down right now. `PAD()`
answers that. Each key has a value, and `PAD()` adds up the values of
the keys that are down. Up is 1, down 2, left 4, right 8, fire 16,
space 32 and Enter 64. The arrow keys
and W, A, S, D both count. Fire is Z or Ctrl. `PAD()` gives 0 when
nothing is held.

## Saving and loading

Restarting the machine loses the program. The machine has a cartridge,
called the ROM, and a program can be saved into it under a name. The
commands that do this start with an exclamation mark.

```basic
10 PRINT "HI"
20 PRINT "THERE"
!SAVE "FIRST"
NEW
LIST
!CATALOG
!LOAD "FIRST"
LIST
```

```text
>!SAVE "FIRST"
READY
>NEW
READY
READY
>LIST
READY
>!CATALOG
FIRST
READY
>!LOAD "FIRST"
READY
>LIST
10 PRINT "HI"
20 PRINT "THERE"
READY
>
```

`!SAVE` writes the program under a name of up to 16 characters.
`!CATALOG` lists the names. `!LOAD` replaces the program in memory with
the saved one. `!DELETE "FIRST"` removes it. Loading a name that is not
there says `NO PROGRAM CALLED FIRST ON THE CARTRIDGE`.

When you started with `./r`, the cartridge lives only in memory. Close
the window and the saved programs go with it. To keep them on disk,
copy the cartridge file and start from the copy:

```bash
cp build/release/roms/basic.rom mine.rom
build/release/src/vm/simplecpu --rom mine.rom
```

Now every `!SAVE` and `!DELETE` writes `mine.rom`. The file is a
cassette with your programs on it.

A program can also live in a plain text file, which any editor can
open. The file holds the lines the way `LIST` prints them. Start the
machine with the file and it is loaded and run:

```bash
build/release/src/vm/simplecpu --basic hello.bas --run
```

The third way is a project folder. A
folder with a `src` folder holding `.bas` files is a BASIC project.
`simplecpu-make` builds it into a cartridge in `build/`. A file called
`autorun.bas` runs when the cartridge boots, so the cartridge is a
finished game that starts by itself. `simplecpu-make new mygame --basic`
lays out a new project with a first program in it.

## The IDE

`./r --ide` opens the IDE, a bigger window with more panes. File, New
project, BASIC lays out a BASIC project with one file, `autorun.bas`.
The IDE opens it on the BASIC level. The editor is on the left, the
machine's screen on the right, and this guide under the screen. Nothing
else is in the way. The Level menu shows the files and the messages
when you want them, and F2 opens the full Project level.

Type a program in the editor. Press Run in BASIC. The IDE boots BASIC,
puts the program in the machine's memory and types `RUN` for you.

From then on the editor and the machine hold one program. Change a line
in the editor and type `LIST` on the small screen: the change is there.
Type `30 REM A NOTE` on the small screen and line 30 appears in the
editor, in its place. There is nothing to send across and nothing to
forget. While a program runs, the two wait. The changes go across when
the program stops at `READY`.

The editor keeps the line numbers for you. Press Enter at the end of a
numbered line and the next line starts with a number already. That
number is ten more, or halfway to the line after. Press Enter on a line
that holds only a number and the number goes away again. Shift+Enter
opens a numbered line under the line you are on, wherever the cursor is
in it. Cmd-I does the same, or Ctrl-I outside a Mac. When no number is
free between two lines, the editor numbers the whole program again in
tens. `GOTO`, `GOSUB` and `THEN` change with it, and the Messages pane
says so. A line typed with a number out of order moves to its place when
you leave it. A file opened out of order is put in order at once.

`NEW` on the small screen empties the editor as well. The file on disk
keeps the old program until you save, and the Messages pane says so.
Save writes the editor's text to the file.

## Longer programs

A few habits make a program of fifty lines readable.

`REM` marks a remark. The machine ignores the rest of the line, so a
remark is a note for you. Put one at the top saying what the program
does, and one before each part.

Count lines in tens, and keep a gap of a hundred or more between the
parts. The main loop can start at 100, the subroutines at 1000. A
`GOSUB 1000` then says something about where it goes.

Keep one idea per line. Colons let two or three instructions share a line,
which is handy for a short `POKE 5,X: POKE 6,Y` pair. A long line of
colons is hard to read back.

When the machine stops with an error, the message ends with the line it
was on, as in `IN LINE 30`. `LIST 30` shows that line. The errors you
will meet most:

| Message | Meaning |
|---|---|
| `SYNTAX ERROR: EXPECTED ... BUT FOUND ...` | the words are in the wrong order, and it says what should have come |
| `UNKNOWN WORD ...` | a word that is not a statement, often a typo |
| `... IS NOT A VARIABLE` | a variable name is one letter, or a letter and a digit |
| `THERE IS NO LINE ...` | a `GOTO`, `GOSUB` or `THEN` named a line that is not there |
| `A STRING CANNOT BE USED AS A NUMBER` | a string where a number was wanted |
| `A NUMBER CANNOT BE USED AS A STRING` | a number where a string was wanted |
| `DIVISION BY ZERO` | a `/` or `MOD` by 0 |
| `RETURN WITHOUT A GOSUB` | the program reached a `RETURN` it did not `GOSUB` to |
| `NEXT WITHOUT A FOR` | a `NEXT` with no `FOR` open |
| `THE STRING IS TOO LONG` | a string longer than 255 characters |
| `THE PROGRAM MEMORY IS FULL` | the program no longer fits |
| `BREAK` | you pressed Escape or Ctrl-C |

## A small game

The game is called Catch the Stars. A star falls from the top of the
screen. You are a `^` on the bottom row, moving with the left and right
arrows. Catch the star for a point. Miss three and the game is over.
Build it in three steps and run it after each one.

Step one draws you and moves you. Box 5 and box 6 place the cursor, so
`PRINT` puts the `^` where P says.

```basic
10 REM CATCH THE STARS
20 CLS
50 P=20
90 POKE 5,P: POKE 6,30: PRINT "^";
100 WAIT 3
110 B=PAD()
120 POKE 5,P: POKE 6,30: PRINT " ";
130 IF B=4 AND P>0 THEN P=P-1
140 IF B=8 AND P<40 THEN P=P+1
170 GOTO 90
```

Line 90 draws you. Line 100 waits three frames, so the loop runs about
twelve times a second. Line 110 reads the keys. Line 120 rubs you out by
printing a space over the `^`. Lines 130 and 140 move P, and stop it at
the edges. Line 170 goes round again. Run it, hold an arrow key, and
watch the `^` slide. Press Escape to stop.

Step two adds the star. X and Y are its column and row. It starts at a
random column on row 0 and moves down one row per turn. `RND(41)` gives
a number from 0 to 40.

```basic
60 X=RND(41)
70 Y=0
80 POKE 5,X: POKE 6,Y: PRINT "*";
150 POKE 5,X: POKE 6,Y: PRINT " ";
160 Y=Y+1
170 IF Y<30 THEN 80
180 GOTO 60
```

Line 170 replaces the old one. The loop now runs from 80 to 170 until
the star reaches row 30, and line 180 starts a new star.

Step three keeps score. S counts stars caught and L counts lives. When
the star reaches row 30 it is caught if X equals P.

```basic
30 S=0
40 L=3
180 IF X=P THEN S=S+1
190 IF X<>P THEN L=L-1
200 POKE 5,0: POKE 6,31: PRINT "SCORE "; S; "  LIVES "; L;
210 IF L>0 THEN 60
220 CLS
230 PRINT "GAME OVER. YOU CAUGHT "; S; " STARS."
240 END
```

The whole program, as `LIST` shows it:

```basic
10 REM CATCH THE STARS
20 CLS
30 S=0
40 L=3
50 P=20
60 X=RND(41)
70 Y=0
80 POKE 5,X: POKE 6,Y: PRINT "*";
90 POKE 5,P: POKE 6,30: PRINT "^";
100 WAIT 3
110 B=PAD()
120 POKE 5,P: POKE 6,30: PRINT " ";
130 IF B=4 AND P>0 THEN P=P-1
140 IF B=8 AND P<40 THEN P=P+1
150 POKE 5,X: POKE 6,Y: PRINT " ";
160 Y=Y+1
170 IF Y<30 THEN 80
180 IF X=P THEN S=S+1
190 IF X<>P THEN L=L-1
200 POKE 5,0: POKE 6,31: PRINT "SCORE "; S; "  LIVES "; L;
210 IF L>0 THEN 60
220 CLS
230 PRINT "GAME OVER. YOU CAUGHT "; S; " STARS."
240 END
```

Save it with `!SAVE "STARS"` before you change anything. Then change
things. `WAIT 2` makes it faster. A second star needs two more variables
and a copy of lines 60 to 170. A `GOSUB` for drawing would shorten the
copy. That is how every game grows, one line at a time.

## Where to go next

BASIC reads every word of every line again each time round a loop,
which is why the star falls slowly. The machine's C compiler turns a
program into machine code once, before it runs. C also reaches the whole
graphics chip, with sprites and sound. docs/design/c-language.md
describes the dialect, and the C guide beside this one carries on from
here.

`CALL` runs a routine for what it does. `USR` asks a routine for an
answer. `PRINT USR(2, TWICE, 21)` hands 21 to a routine named TWICE and
prints what it gives back. The first number says how big the answer is.
A 1 means one box, 0 to 255. A 2 means two boxes, up to 65535. A routine
with a longer answer fills a row of boxes and gives back where the row
starts. `PEEK` and `DEEK` then read it. The routine can be written in C
or in assembly, and the call looks the same from BASIC. How the other
side works matters once you move on to C, and later to assembly. The C
guide and the assembly guide pick it up there.

The system page at docs/basic-system-page.md lists every box BASIC keeps
its own state in, from 0 to 31. Box 5 and box 6 came from there.
examples/basic-c and examples/basic-driver show BASIC calling C and
assembly.

## Every word

Commands you type at the prompt:

| Word | Does |
|---|---|
| `RUN` | runs the stored program from its lowest line |
| `LIST` | prints the stored program, or part of it: `LIST 20`, `LIST 20-40`, `LIST -40`, `LIST 20-` |
| `NEW` | throws the stored program away |
| `RENUM s,i` | numbers the lines again from s in steps of i, 10 and 10 when left out |

Statements, in a program or at the prompt:

| Word | Does |
|---|---|
| `PRINT a; b, c` | prints things, `;` with no gap, `,` with a space |
| `INPUT "text"; V` | prints the text and a `?`, waits for a line, stores it |
| `V=expr` or `LET V=expr` | puts a value in a variable |
| `IF cond THEN stmt` | runs the statement when cond is not 0 |
| `IF cond THEN 100` | goes to line 100 when cond is not 0 |
| `GOTO 100` | goes to line 100 |
| `GOSUB 100` | goes to line 100 and remembers where it was |
| `RETURN` | goes back to the line after the last `GOSUB` |
| `FOR I=a TO b STEP s` | starts a counted loop, `STEP` optional |
| `NEXT` | adds the step and goes back while I is in range |
| `END` or `STOP` | stops the program |
| `REM text` | a remark, ignored |
| `CLS` | clears the text and paints the picture in the `PAPER` colour |
| `WAIT n` | pauses n frames, 60 to a second |
| `POKE a,v` | puts byte v in box a |
| `DOKE a,v` | puts word v in boxes a and a+1, high byte first |
| `PAPER c` | fills the picture with colour c |
| `INK c` | sets the pen colour, which every drawing word uses |
| `MOVE x,y` | puts the pen at x,y |
| `DRAW x,y` | draws a line from the pen to x,y |
| `CIRCLE rx,ry,f` | a ring around the pen in the pen colour, ry and the fill colour f optional |
| `PLOT x,y` | puts a dot at x,y |
| `CALL n` or `JSR n` | runs machine code at slot n, the A register goes to box 4 |
| `JMP n` | jumps to machine code at slot n and never comes back |
| `!SAVE "N"` | saves the program into the cartridge as N |
| `!LOAD "N"` | loads N from the cartridge |
| `!DELETE "N"` | removes N from the cartridge |
| `!CATALOG` | lists the names in the cartridge |

Functions, used inside an expression:

| Word | Gives |
|---|---|
| `RND(n)` | a random number from 0 to n-1 |
| `ABS(n)` | n without its sign |
| `PEEK(a)` | the byte in box a |
| `DEEK(a)` | the word in boxes a and a+1 |
| `PIXEL(x,y)` | the colour of the pixel at x,y |
| `KEY()` | the code of the key pressed since the last look, or 0 |
| `PAD()` | the keys held down, added up as bits |
| `LEN(s$)` | the length of a string |
| `ASC(s$)` | the code of its first character |
| `VAL(s$)` | the number the string spells |
| `STR$(n)` | the number as a string |
| `CHR$(n)` | the character with code n |
| `MID$(s$,i,n)` | n characters from position i, count optional |
| `USR(w,n,p1,p2,p3)` | the answer of the routine at slot n, given up to three numbers: nothing when w is 0, a byte when 1, a word when 2 |
| `INKEY$` | the key pressed since the last look, or `""` |

Operators, in order from tightest to loosest: `-` and `NOT` on one
value, then `*`, `/` and `MOD`, then `+` and `-`, then `=`, `<>`, `<`,
`>`, `<=` and `>=`, then `AND`, then `OR`. A number may be written in
hex with a dollar sign, so `$F000` is 61440. Strings join with `+`.
