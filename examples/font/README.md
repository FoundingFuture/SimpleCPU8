# Fonts

A BASIC project with a font of its own. assets/chunky.font is drawn for
an 8 by 8 cell. It holds a bold copy of the built-in letters and four
symbols at codes 128 to 131. A face spans the 2 by 2 block of codes 136,
137, 152 and 153. The build places every file in assets/ on the cartridge,
and BASIC finds the font by the file's name.

src/autorun.bas runs in three steps, with a key press between them:

```basic
20 LOADFONT CHUNKY
110 LOADFONT
150 SETTEXT 8,8
```

`LOADFONT CHUNKY` takes the font and its cell, so the grid becomes 32 by
32. CHR$(128) and the codes after it print the symbols and the face.
`LOADFONT` alone brings back the built-in font at 6 by 8, 42 columns.
`SETTEXT 8,8` then puts the built-in glyphs in 8 by 8 cells, 32 by 32.
PEEK(34) and PEEK(35) show the grid after each step.

    simplecpu-make examples/font
    simplecpu --rom examples/font/build/font.rom

The font is text, eight rows of `X` and `.` a glyph. The IDE opens it in
the font editor: `simplecpu-ide examples/font/assets/chunky.font`, or a
double click on chunky.font under Assets.
