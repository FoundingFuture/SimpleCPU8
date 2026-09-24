# Text and printf design

Two text features for the GPU of SimpleCPU-8. Eddie asked for a printf that
draws over graphics, and a real text mode read from memory. This doc is
the built design. It records the decisions and the reasoning.

Both features share one font and one glyph renderer. The CPU stays 8 bit and
honest. It writes bytes to ports or to memory. The GPU does the drawing.

## Contents

- [The font and the grid](#the-font-and-the-grid)
- [Feature 1, printf overlay](#feature-1-printf-overlay)
- [The format grammar](#the-format-grammar)
- [Parameters and endianness](#parameters-and-endianness)
- [Cursor, newline, and scroll](#cursor-newline-and-scroll)
- [Colors](#colors)
- [Feature 2, text mode](#feature-2-text-mode)
- [Video modes](#video-modes)
- [Custom fonts](#custom-fonts)
- [Ports and commands](#ports-and-commands)
- [String literals in the assembler](#string-literals-in-the-assembler)
- [The 2KB RAM](#the-2kb-ram)

## The font and the grid

The screen is 256 by 256 pixels. The font is 6 by 8 pixels per glyph. So the
screen holds 42 columns by 32 rows. That is 1344 character cells. Both features
use this 42 by 32 grid.

A glyph is five columns of face and one blank column, so a line of text needs
no spacing added. `TEXT_COLS` and `TEXT_ROWS` divide the screen by `FONT_W` and
`FONT_H`, so the grid cannot disagree with the font.

A font is 256 glyphs, so every byte value has a glyph. Each glyph is 8 bytes,
one byte per row, with the low bit on the left. The GPU ships with a built-in
font for printable ASCII. Other codes start blank until a font is loaded.

## Feature 1, printf overlay

printf draws formatted text on top of the graphics. The graphics keep their own
pixels. The text rides above them on a separate plane.

The template string lives in ROM, in the cartridge. An example is
`Add %hhu + %hhu = %hd\n`. A program points the cartridge latches at the
template, pushes the parameter bytes to a port, then issues one command. The GPU
reads the template, formats it, and draws it at the cursor.

The overlay is a plane of 1344 cells. Each cell holds one character code. Empty
cells are transparent, so the graphics show through. A command clears the plane.

## The format grammar

printf follows the C grammar. A format is a percent sign, then optional flags, a
width, a precision, a length, and a conversion. The bits carry no type. The
format says how to read them.

The length modifier sets how many bytes the format reads. On this machine an int
is 16 bits.

| Length | Bytes | C meaning |
|---|---|---|
| hh | 1 | char |
| h | 2 | short |
| none | 2 | int, 16 bit here |
| l | 4 | long |
| ll | 8 | long long |

The integer conversions. Signed reads two's complement. Unsigned does not.

| Conversion | Reading |
|---|---|
| d, i | signed decimal |
| u | unsigned decimal |
| o | unsigned octal |
| x, X | unsigned hex, lower or upper |
| b | unsigned binary |

The other conversions.

| Conversion | Bytes | Meaning |
|---|---|---|
| f, F | 4, or 8 with l | float, IEEE 754 single or double |
| e, E | 4, or 8 with l | float in scientific form |
| g, G | 4, or 8 with l | float, shortest of f or e |
| c | 1 | one byte as its glyph |
| s | 2 | a RAM pointer to a zero-terminated string |
| r | 2 | a RAM pointer to a raw string of an exact length |
| %% | 0 | a literal percent |

The flags are `-` for left justify, `+` for a leading sign, space for a leading
space on positives, `0` for zero padding, and `#` for the alternate form. So
`%03.4f`, `%#x`, and `%-8s` all work. `%#x` prefixes `0x`. The width is the
minimum field width. The precision is digits after the dot.

`%s` and `%r` take a 2 byte pointer into data RAM. The template is in ROM, but
the strings it points at live in RAM. `%s` reads bytes until a zero. `%r` is the
sized form. Its width is an exact byte count, not a field to pad. So `%5r` reads
exactly 5 bytes from the address, terminator or not. Standard C has no exact
length string, and `%S` is the wide string there, so the letter is `r` for raw.

So one byte holding 200 prints as `200` with `%hhu`, `-56` with `%hhd`, `c8`
with `%hhx`, and `11001000` with `%hhb`. Same bits, different reading.

## Parameters and endianness

A program pushes parameter bytes to the `GPU_TEXT_ARG` port. The bytes queue in
order. The command consumes them as the template asks.

Multi-byte values are big-endian, the same as the CPU. Push the high byte first.
A `%hd` with value 0x1234 takes a push of 0x12 then 0x34. This matches how the
CPU stores a word, so a program pushes a `D` register high byte first.

Too few parameters. A format that cannot fill its bytes draws the text
`-no parameter-` in its place. The rest of the template still draws.

Too many parameters. Bytes left in the queue after the template ends are
dropped. The queue clears after every printf.

## Cursor, newline, and scroll

The cursor is a column and a row, measured in characters. A program sets it with
the `GPU_TEXT_COL` and `GPU_TEXT_ROW` ports. Each drawn character advances the
column. Past the last column the cursor wraps to the next row.

A newline byte, `\n`, moves the cursor to column 0 of the next row. Past the last
row the overlay scrolls up by one row. The top row is lost. The bottom row
clears. The graphics do not scroll. Only the text plane moves.

## Colors

Text has one foreground and one background color, not one per character. Text
color is separate from the graphics draw color. Graphics use `GPU_COLOR`. Text
uses `GPU_TEXT_COLOR`. The two never clash, because they are different registers.
Both hold a palette index.

The background rides in `GPU_TEXT_BG`. A flag byte, `GPU_TEXT_FLAGS`, has one bit
for an opaque background. The default background is transparent, so text sits
over graphics with only its glyph pixels. The rule is the same in both
features. An opaque style fills each cell with the background first. A
transparent one leaves the pixels under the cell alone.

## Feature 2, text mode

Text mode shows characters read from memory. The screen is the 42 by 32 grid,
drawn over the graphics VRAM. It is one of the video modes below.

A pixel that is not a glyph pixel shows the VRAM pixel under it. So a program
can draw with the graphics commands and print in the same mode. BASIC is the
program that wanted this. It runs in text mode, and its PLOT and CIRCLE drew
into a VRAM nobody saw. The background follows the style's opaque flag, as the
overlay does. A style without the flag is transparent. Black text on black is
still what a fresh machine shows, because VRAM starts black. BASIC clears the
VRAM to its PAPER colour on CLS, so its screen stays one colour. Text mode
shows the VRAM as it is. The scroll registers and the sprites belong to
graphics mode.

The character buffer is a region of data RAM. A command maps it. The GPU gets a
read-only second port into data RAM. So the CPU writes a byte with
`LD [addr] <- A`, and the GPU shows that byte as a glyph on the next frame. This
is the double-ported memory Eddie described. The CPU port and the GPU port never
appear to collide.

`CMD_TEXT_MAP` sets a 16 bit base address from the `GPU_CART_LO` and
`GPU_CART_HI` latches. The screen is always the full 42 by 32, so it reads 1344
bytes from that base every frame. A write to the mapped region shows up with no
command. The colors come from `GPU_TEXT_COLOR` and `GPU_TEXT_BG`, and the
background is drawn only when `GPU_TEXT_FLAGS` says opaque.

## Video modes

The GPU has a video mode. `CMD_VIDEO_MODE` switches it, reading `GPU_ARG` for the
mode. `MODE_GRAPHICS` is the power-on mode and the one every drawing command
uses. `MODE_TEXT` shows the mapped character screen. A program switches back and
forth as it likes.

Map before you switch. `CMD_TEXT_MAP` is the setup step and does not change mode.
Switch to text mode with no map first and the screen fills with crawling snow.
The snow is the display telling you the mode has no memory to read yet. The
graphics mode is never affected, so the default program keeps working untouched.

## Custom fonts

`CMD_LOAD_FONT` loads a full 256-glyph font from the cartridge latches. Each
glyph is 8 bytes, so a font is 2048 bytes. Every code from 0 to 255 can carry a
glyph. A game can point high codes at tiles and draw a map in text mode.

A custom font does not erase the built-in one. The built-in font is the source
of truth. A Restart reloads it, so the default font always comes back.

## Ports and commands

New ports in the GPU range, 0x00 to 0x1F. The names are built-in assembler
constants, like the other GPU ports.

- GPU_TEXT_COL, 0x17: cursor column, 0 to 31.
- GPU_TEXT_ROW, 0x18: cursor row, 0 to 31.
- GPU_TEXT_COLOR, 0x19: text foreground palette index.
- GPU_TEXT_BG, 0x1A: text background palette index.
- GPU_TEXT_FLAGS, 0x1B: bit 0 is an opaque overlay background.
- GPU_TEXT_ARG, 0x1C: push one parameter byte into the printf queue.
- GPU_TEXT_CHAR, 0x1D: draw one byte at the cursor and advance, no formatting.

New commands for `GPU_CMD`.

- CMD_PRINTF, 0x30: format the template at the cartridge latches, draw it.
- CMD_TEXT_CLEAR, 0x31: clear the overlay plane and home the cursor.
- CMD_TEXT_MAP, 0x32: point text mode at a data RAM base.
- CMD_VIDEO_MODE, 0x33: switch video mode from GPU_ARG.
- CMD_LOAD_FONT, 0x34: load a 256-glyph font from the cartridge.

New assembler constants for `GPU_ARG` at a mode switch. MODE_GRAPHICS is 0 and
MODE_TEXT is 1.

## String literals in the assembler

printf needs template strings in ROM. The assembler gained a string literal. A
`.data` or `.ram` line can hold `db "text"`. The bytes are the ASCII codes.

Escapes inside a string: `\n` is 10, `\t` is 9, `\0` is 0, `\\` is a backslash,
`\"` is a quote, and `\xHH` is a hex byte. A string is a list of bytes. It sits
among other `db` values on the same line.

## The 2KB RAM

Data RAM grew from 1024 to 2048 bytes. A full text screen was 1024 bytes then,
on the 8 by 8 font this machine started with. So it fit in the upper 1KB. Map
the screen at $400 and keep the lower 1KB for data. Writing to the upper 1KB
needs a 16 bit address, so a D register walks the screen. RAM grew again later,
to the full 65536 bytes a 16 bit address can already reach. No data address
crashes now, at $800 or anywhere else.
