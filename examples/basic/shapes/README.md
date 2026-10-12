# Shapes

A BASIC program that draws with a library. The library is
`examples/basic/libraries/shapes.bas`, a file of drawing routines that any
project can call. This project holds no copy of it.

`src/autorun.bas` names the library and calls into it:

```basic
20 INCLUDE SHAPES
50 GOSUB SHAPES:2300
```

`INCLUDE SHAPES` brings the file in beside the program before line 10
runs. `GOSUB SHAPES:2300` calls line 2300 of that file, and its `RETURN`
comes back to line 60 here.

A library and its caller share every variable. `shapes.bas` opens with a
`REM` block that lists the variables it reads and the ones it changes.
The program sets `X`, `Y`, `W`, `H`, `R` and `C` before each call, and
keeps `X0` to `X3` free.

The build looks for `SHAPES` in the project first, then in the folder
`libraries` beside the project's folder. It puts the library into the ROM,
so the ROM runs on a machine that has no libraries folder.

```bash
simplecpu-make examples/basic/shapes
simplecpu --rom examples/basic/shapes/build/shapes.rom
```

In the IDE the library shows under Libraries in the project column. It
opens read only. Edit library on its tab lets you change it, for every
project that includes it.
