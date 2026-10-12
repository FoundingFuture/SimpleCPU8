# The IDE

`simplecpu-ide` holds the editor, the build, the debugger and the
microcode tools around one running machine. This guide covers its panes
one at a time.

## Views

The IDE has four views, one for each language: BASIC, C, ASSEMBLY and
MICROCODE. The four tabs at the left of the menu bar pick one, and so do
F1 to F4. A view is the editor of its language with the panes that
language needs around it, and every view runs the machine.

Each view's editor has a tab for every open document of its kind. The
BASIC view holds the `.bas` files, the C view `.c` and `.h`, the
ASSEMBLY view `.asm`, and the MICROCODE view `microcode.txt`. A view
keeps its own pane layout, its own guide and its own speed.

`simplecpu-ide --view c` starts in a view: `basic`, `c`, `assembly` or
`microcode`. `--level` still takes the names of the four levels the
views replaced: `basic`, `project`, `run` and `cpu`.

## The project column

The project's files stand in a column at the left of the window. It
belongs to no view, so switching views leaves it alone. Its tree groups
the sources by kind, then the libraries a BASIC program includes, the
assets, and what the last build made. A click opens a file in the view
that owns its kind. New file, Add asset, New sprite and New font are the
buttons at its top.

The button at the column's top collapses it to a narrow bar, and a click
on the bar opens it again. Cmd-B does both, or Ctrl-B outside a Mac. A
drag on its right edge sets its width.

## The Run menu

The Run menu has the same items in every view. Each works at the level
of code the view shows.

| Item | BASIC | C | ASSEMBLY | MICROCODE |
|---|---|---|---|---|
| Run, F5 | types `RUN` for the program | builds if a document changed, then runs | the same | the same |
| Check, F7 | reads the program with BASIC's `CHECK` | builds and shows the messages | the same | parses the set and checks every row |
| Step, F10 | one BASIC line | one line of C | one instruction | one row |
| Pause, F5 | pauses | pauses | pauses | pauses |
| Stop | breaks the program | powers the machine on again | the same | the same |

In the ASSEMBLY and MICROCODE views the Listing is a tab beside the
Editor. Run and Step bring the Listing to the front, with the line the
processor runs next in green. A click on the Editor tab brings the
document back.

Speed is the view's own choice of the speed ladder. Microcode picks the
set the machine runs: the project's `microcode.txt`, naive or optimal.
Lock the running set keeps it whatever a later build or ROM brings.

## Messages

The Messages pane lists what the IDE did and what went wrong. A build
error stands on a dark red line, with the file and the line it names. It
comes from the compiler, the assembler or a microcode set. A build with errors
also plays two falling notes over the machine's sound. Settings,
Preferences, Sound on a build error turns them off.

## Line numbers and breakpoints

The editor numbers the lines of a C, assembly or microcode document in
a column left of the text. The column sits outside the text box, so a number is
never edited, selected or copied. A build error's line number is the one
in that column.

A click on a line's number sets a breakpoint there, a red dot in the
column. A second click clears it. The machine stops before the line's
first instruction runs, and Messages names the line:
`stopped at breakpoint, PC 0007, hello.c:14`. A line without code, such as
a comment, a blank line or a declaration, puts the dot on the next line
that has code. A line after the file's last instruction takes no
breakpoint.

A breakpoint moves with its line. Lines typed or pasted above it move it
down, and lines deleted above it move it up. Deleting its own line
removes it. A line joined onto the one above takes its breakpoint along.

Each build places the breakpoints again, so they stay on their lines
when the code moves. A click in a document edited since the last build
sets the dot, and the next build gives it an address.

A BASIC document has the column without the numbers, since its lines
carry their own. A breakpoint there belongs to the BASIC line number.
The program stops when that line starts, before it runs, and Messages
says `Stopped at line 1000 in GAME.BAS`. The IDE watches the
interpreter's system page for this. The program and the interpreter are
not changed. A line that leaves the document loses its breakpoint.

In `microcode.txt` a breakpoint belongs to a row. The machine stops
inside the instruction, before that row runs. The optimal set is sealed
and shows no row, so it refuses one.

The Breakpoints pane is in every view. It lists them all with their
document and line, beside the ones set in the Listing's margin. A click
on one shows its line in its view.

## Idle

A paused IDE draws nothing until a key, a click or a mouse move arrives,
so it uses almost no processor time. Three things keep it drawing every
frame: a running machine, a tune the sound chip still plays, and a
playing sprite preview. The text cursor does not blink, since a blink
would need a frame twice a second.

The project column reads the assets folder when a project opens and when the
IDE adds, removes or saves an asset. It reads it again when the window
comes back to the front, so a file another program changed shows up.

## The examples

File, Open example lists the examples in examples/ by kind: Assembly,
BASIC, C and Microcode. A name's tooltip is the first line of its README.
Picking one opens that folder as the project.

An example stays as it is. Its documents can be edited, built and run.
Build compiles the open documents with the example's own assets, and the
ROM stays in memory, so nothing lands in the example's folder. Save is
disabled, and Ctrl+S says so in Messages. Save project as writes the
documents, the assets and the README into a folder of your own and makes
that the project. Removing a file,
adding or removing an asset, and a sprite or font save are refused until
then, with a note in Messages. Save project as refuses a folder inside
examples/.

## The sprite editor

The sprite editor draws a sprite strip: the frames of one animated
sprite side by side in a PNG. `__sprite` in C and `.sprite` in assembly
read that file.

### Opening a sprite

- Double-click a picture under Assets in the project column.
- Press New sprite in the project column, and give a name, a frame size and a
  frame count.
- Name the picture on the command line.

```bash
simplecpu-ide game/assets/ship.png
```

A picture that is not a PNG opens as a new PNG beside it, since the
frame count lives in a PNG text chunk. The pane's title reads
`Sprite: ship.png`, with a star while the strip has unsaved changes. The
preview is a pane of its own, Sprite preview, beside the screen.

### Drawing

The canvas zooms, with a grid and an onion skin of the frame before. The
palette is the machine's 256 colours. Index 0 is transparent and shows as
a checker, since the GPU skips it.

A left click on the palette picks the drawing colour. A right click picks
the second colour, the one a gradient ends on. A right click on the canvas
picks up the colour under it.

| Key | Tool | Does |
|---|---|---|
| P | Pencil | sets a pixel, or draws a stroke while dragged |
| L | Line | from where the drag starts to where it ends |
| R | Rect | the box between two corners, outlined or filled |
| E | Ellipse | the ellipse that fits that box, outlined or filled |
| F | Fill | the area of one colour under the click |
| G | Gradient | shades that area from the first colour to the second |
| O | Roll | drags a row sideways or a column up and down |

Shift with any tool draws transparent, so Shift and a click clears a
pixel. With the roll tool, Shift moves the whole frame. Pixels that leave
one side come back on the other. The Roll and Flip buttons under the
palette move or mirror the frame by one step.

### Frames

The frames run along the bottom, up to 16. A frame can be added at the
end, inserted before or after the current one, duplicated, moved and
deleted. The frame size changes for every frame at once, up to 64 by 64.

`,` and `.` step through the frames. X swaps the two colours. Undo and
redo hold 200 steps, on Ctrl+Z and Ctrl+Shift+Z.

### The sprite preview

The preview plays the strip at its frames per second, or ping-pong. Space
plays and pauses it. It draws through the screen's own display look, so
a change there shows on the Screen pane too. Play on the Screen pane puts
the preview in place of the machine's picture.

The sprite sits in the middle of the 256 pixel picture at 4 x, or as large
as fits. 1 x is the size the machine draws it.

## The font editor

A font is a `.font` file under Assets: 256 glyphs of 8 by 8, and the cell
the GPU draws of each. The same pane that draws sprites opens it in font
mode. The file decides the mode, and the title says which:
`Font: small.font`.

### Opening a font

- Double-click a `.font` under Assets in the project column.
- Press New font in the project column and give it a name. The IDE writes a
  copy of the built-in font as `assets/<name>.font` and opens it.
- Name the font on the command line.

```bash
simplecpu-ide game/assets/small.font
simplecpu-ide --font-view block game/assets/small.font
```

A new font has the built-in glyphs at `$20` to `$7F` and every other code
blank. Its cell is 6 by 8. The block at `$7F` fills all eight columns, so
a cursor fills a cell of any width.

### What the pane shows

The canvas shows the glyph being edited, set pixels light and clear ones
dark. The part outside the cell is shaded red and outlined at the cell's
edge. The GPU never draws it, and the file keeps it.

On the right are the Set and Clear buttons, the cell, and the glyph sheet.

- Set and Clear choose what the tools draw. X swaps them.
- W and H set the cell, 4 to 8 each way. The grid it gives shows beside
  them: 6 by 8 is 42 columns and 32 rows.
- The glyph sheet shows all 256 codes, 16 to a row, with the code in hex
  under each. A click edits that glyph.

The toolbar has no palette, gradient or transparency. A font has one
colour, and a clear pixel is paper, not a hole.

### Tools

The tools shared with sprites keep their keys and their places on the
toolbar. The half circle takes the gradient's place.

| Key | Tool | Does |
|---|---|---|
| P | Pencil | draws a pixel, or a stroke while dragged |
| L | Line | from where the drag starts to where it ends |
| R | Rect | the box between two corners, outlined or filled |
| E | Ellipse | the ellipse that fits that box, outlined or filled |
| F | Fill | the area of set or clear pixels under the click |
| H | Half circle | half the ellipse in that box, its flat side on one edge |
| O | Roll | drags a row sideways or a column up and down |

Shift with any tool draws the other of set and clear. A right click on
the canvas picks up set or clear from the pixel under it.

The half circle has four turns: round side up, right, down or left. H
picks the tool, and H again turns it. The Turn button beside Filled does
the same. Filled fills the half circle, as it does the rectangle and the
ellipse.

Roll wraps round in all four directions. Its buttons, and Flip H, Flip V
and Clear, sit under the canvas and act on the glyph.

### Single, strip and 2 by 2 block

The buttons above the glyph sheet pick the view.

| View | Shows |
|---|---|
| Single | one glyph. `,` and `.` step the code |
| Strip of 4 | four consecutive codes as the frames of a strip. The preview plays them |
| 2 x 2 block | codes n, n+1, n+16 and n+17 as one 16 by 16 picture |

The strip is the sprite strip over codes n to n+3. The four frames show
under the canvas, and a click edits one. `,` and `.` step between them.
A code picked in the sheet outside the four moves the strip to it.

The block edits four glyphs as one picture, for large characters and
tiles. A stroke across the middle lands in every glyph it crosses. Each
stroke is split back into the four glyphs at once, so the sheet and the
file always hold four ordinary glyphs. `,` and `.` move the block.

Undo and redo cover every glyph, the cell and the view's place. An undo
goes back to the glyph it changed.

### The font preview

The preview sets text in the font the way the GPU does. Each glyph is cut
to the cell, and the cells sit side by side on the cell's grid, one
machine pixel to a font pixel. It shows three things:

- a sample line, which the box under the picture edits
- the glyph being edited, the strip with its first cell playing, or the
  block
- every code from `$00` to `$FF`

The font preview skips the screen's display look, since a font is judged
on its pixels. In the strip view, Play, Pause and fps set how the four
frames play. Space plays and pauses too.

### Saving

Save writes the font as text, the same text a `.font` file holds. A font
opened and saved with no change is not written at all, so the file on
disk stays as it was.
