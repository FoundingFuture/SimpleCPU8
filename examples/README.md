# Example programs

The examples sit in one folder for each kind of project: assembly/,
basic/, c/ and microcode/.

assembly/ holds the demos of the browser version as plain assembly, one
directory per program. Each directory holds `<name>.asm`, a README whose
first line is the ROM title, and any asset the source names. Edit a source
in any editor. The build assembles it again.

## Building the ROMs

The `roms` target runs simplecpu-asm on every example and writes
`build/roms/<name>.rom`. It is part of the default build:

```bash
cmake --preset headless && cmake --build --preset headless
build-headless/src/vm/simplecpu --rom build-headless/roms/pacman.rom
```

CTest runs every ROM through simplecpu-run for a bounded number of
instructions. A demo that halts must reach halted. A demo that reads the
keyboard or runs forever must not crash.

## Where the sources come from

The demos started as the browser project's, and then moved on. The
sources here use instructions the browser machine lacks. Those are CMP
and TST, the ALU through `[D1+n]`, address adds and the shifts. Each
rewrite was checked by running the old and the new ROM side by side. The
frames match, or the halted state does. mul8, div8 and div16 print their
answers in a new layout, and audio's notes land a few cycles apart.

The browser originals are kept in tests/asm/golden-src.
`extract-demos.mjs` copies the demo strings there out of the browser
project's TypeScript. So the text there is what its picker loads. Run it again
after a demo changes there:

```bash
/opt/node22/bin/node --experimental-strip-types examples/extract-demos.mjs ../SimpleCPU
```

No demo names an external file. The MIDI, mazes, sprites and samples were
generated into db lines by the browser project's scripts.
tests/asm/golden-bytes holds that project's assembled output per demo, and
sc8_asm_tests assembles golden-src and checks the bytes against it.

## Memory and the stack

- list: linked list sum. Halts.
- hanoi: towers of Hanoi, one move per frame. Halts.
- hello: the list demo with a banner in .data through .file. Halts.

## Arithmetic

- mul8: 8x8 multiply. Halts.
- div8: 8/8 divide. Halts.
- mul16: 16x16 multiply. Halts.
- div16: 16/16 divide. Halts.
- mandel: Mandelbrot on the coprocessor. Halts.

## Graphics

- gpu: gradient and palette. Halts.
- tunnel: rings and palette rotation. Endless.
- star: spinning star. Endless.
- cube: 3D wireframe cube. Endless.
- world: 3D world, spinning pyramid. Endless.
- fly: 3D world, fly through with the arrows. Endless.
- orbit: 3D world, ACP matrix orbit. Endless.
- circle: sin table circle test. Halts.
- random: random pixels from the GPU random port. Endless.

## Text

- text: printf over graphics. Halts.
- textmode: text mode from mapped RAM. Halts.
- matrix: matrix rain in text mode. Endless.
- font: a BASIC project with its own 8 by 8 font, LOADFONT and SETTEXT.
  Waits for a key between its steps.

## Sound

- audio: a tune on the audio chip. Endless.

## Input

- input: keyboard playground. Endless.

## Games

- sprite: Space Invaders. Endless.
- pacman: Pac-Man. Halts on game over.
- groups: sprite groups and collision. Endless.
- pong: pong with hit tests. Endless.
