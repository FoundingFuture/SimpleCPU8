# Peripheral port redesign

The GPU spends 30 of its 32 ports. Most are argument latches that programs
already rewrite before every command, so they buy nothing. This replaces them
with one command port, one modifier, and eight data ports. It also settles how
a peripheral learns the shape of the data it is pointed at.

Built, 2026-09-11. This is the design record, and the machine matches it.
Two things the design could not know are recorded below. See the sections on
the port map and the world mode.

## Contents

- [Why now](#why-now)
- [What the CPU knows about ROM](#what-the-cpu-knows-about-rom)
- [The port map](#the-port-map)
- [The command modifier](#the-command-modifier)
- [Text mode is the framebuffer](#text-mode-is-the-framebuffer)
- [Mapped transfers](#mapped-transfers)
- [Results come back on the data ports](#results-come-back-on-the-data-ports)
- [Every command that reads ROM](#every-command-that-reads-rom)
- [One pen, one position](#one-pen-one-position)
- [The blit](#the-blit)
- [The command map](#the-command-map)
- [The world mode](#the-world-mode)
- [Still open](#still-open)

## Why now

Two reasons.

The port space was the smaller one. The GPU claims 00 to 1F and fills 30 of
those slots. That is not scarcity yet, because 22 to 2F and 40 to FF are free.
It is duplication. `GPU_ARG` is rewritten before nearly every command, and the
cube demo rewrites `GPU_CART_HI` and `GPU_CART_LO` before all four of its
`CMD_DRAW_PATH3D` calls. These are arguments living in sticky latches.

The larger reason is that the machine holds two contradictory conventions for
the same job. `CMD_SPRITE_DEF` takes a sprite's size from ports, so the size is
a literal in the source. `CMD_SPRITE_STRIP` takes it from a header in the
cartridge. The second cannot go stale and the first can.

The redesign costs nothing in existing programs. Demo sources hold 406
name-based port references and zero raw numbers, so renumbering is a change to
`SYS_CONSTS` alone.

## What the CPU knows about ROM

The CPU cannot read the cartridge. This is structural, not policy.
`machine.ts` holds no reference to it, `cartByte` is private to the GPU, and no
read port returns a cartridge byte.

So the CPU knows where things are and never what is in them. The assembler
resolves a label to its cartridge address with `get_lowbyte`, `get_highbyte`
and `get_bankbyte`. The program passes those three bytes to a peripheral.

That splits the work in one place, and it is the rule the rest of this document
rests on.

- The CPU supplies the pointer.
- The blob supplies its own shape.

Every block of ROM handed to a peripheral therefore begins with a header
describing itself. A sprite blob leads with its frame count, width and height.
A sample leads with its length. A path leads with its point count.

The alternative is what `CMD_SPRITE_DEF` does today, and the invaders demo
shows the cost:

```asm
        OUT GPU_SPRITE, 19
        OUT GPU_X, 8
        OUT GPU_Y, 8         ; hand-typed, and nothing checks it
        OUT GPU_CMD, CMD_SPRITE_DEF
```

Those two bytes are the asset's dimensions copied into the source. Replace the
image with a wider one and the GPU reads 64 bytes of a 128-byte blob. The
sprite is wrong, and nothing reports it. A size that exists in two places
drifts, and the copy that drifts is the one written last.

The CPU gets no route to ROM data, and that is settled rather than deferred.
A read command plus a port would let programs keep tables in the 1MB
cartridge. It would also put cartridge access inside the CPU, which is the
wrong side of the magic box line. Code stays in program memory and data stays
in RAM.

The rule applies to the audio chip too. `CMD_DEF_SAMPLE` takes a length from an
argument pair, which is why a sample cannot exceed 65535 bytes. A header can
spend three bytes on the length and the cap goes away.

## The port map

```text
00  GPU_CMD        write a command byte to run it
01  GPU_CMD_MOD    modifier bits for the next command
02  GPU_DATA0      arguments in, results out
03  GPU_DATA1
04  GPU_DATA2
05  GPU_DATA3
06  GPU_DATA4
07  GPU_DATA5
08  GPU_DATA6
09  reserved
..
1D  reserved
1E  GPU_RAND       read: a random byte. write: reseed
1F  GPU_FRAME      read: the frame counter
```

Eleven ports in use where there were 30, and 21 slots left reserved. Each
command decides what each data port means, and the manual page for a command is
where that mapping lives.

Seven is the width of the widest command. That is `CMD_COPY`, taking a
cartridge address, a RAM address and a length. Nothing else reaches six.

The draft of this document stopped at six data ports, on the grounds that
`CMD_SPRITE_MOVE` at five was the widest. `CMD_COPY` in the command map below
already needed seven, and its own note said so. A raw copy has no blob to carry
a header, so its length has to be an argument. Seven it is.

The two direct reads sit at the ceiling and the command block grows from the
floor. Everything between is reserved, and nothing is promised. A seventh data
port takes 08 on the day a command needs one. A later direct-value port takes
1D. Neither disturbs the other.

That is why the layout is worth pinning now rather than packing tight. A packed
map has to be renumbered to grow. This one grows from both ends into the middle.

The GPU claims 00 to 1F, which is 32 slots, and names eleven of them. The other
21 run from 09 to 1D. The controller at 20 and the audio chip at 30 are
untouched.

`GPU_FRAME` and `GPU_RAND` stay direct because both are read in tight loops.
The frame poll is the idiom every animated program is built on. Making it a
command plus a read would double its cost.

## The command modifier

Data ports clear after every command. A port not written for the next command
reads as zero. Nothing carries over by accident.

That is a change from the machine as it stood when this was written, and it
is deliberate. This document argues for the redesign, so everything it calls
current describes the machine BEFORE it. The
`GPU_X_HI` and `GPU_Y_HI` latches were sticky then, so writing `GPU_X` left
the high byte at its old value. The knowledge base calls that the classic bug
and says it appears constantly. Clearing by default makes it unreachable.
That is now built, so the ports clear.

`GPU_CMD_MOD` then holds the opt-outs and the conveniences.

| Bit | Name | Effect |
|---|---|---|
| 0 | `MOD_STICKY` | Data ports keep their values after the command |
| 1 | `MOD_INC0` | DATA0 steps by one after the command |

`MOD_STICKY` is for repeating a command with one argument changed. Draw the
same sprite at ten positions, or plot a row of pixels in one colour.

`MOD_INC0` is for walking an indexed resource. DATA0 is the index on every
command that takes one. A sprite walk then sets it once, and the invaders demo
defines eighteen sprites in a loop.

The modifier holds until it is written again. It is a mode, not a one-shot.

## Text mode is the framebuffer

`CMD_SET_TEXTMODE` takes a 16-bit RAM address on DATA0 and DATA1. It switches
the video mode and points the display at that address. One command does both.

The GPU builds the video signal from those bytes every frame, so a CPU store is
visible at once. Nothing is copied and no command is needed.

This retires `CMD_TEXT_MAP` and the `MODE_TEXT` argument to `CMD_VIDEO_MODE`.
It also retires a failure. Text mode with no buffer mapped currently shows
noise. That state is unreachable once one command sets both.

Page flipping is the same command with the other address. Draw the next frame
at F800 and wait for `GPU_FRAME` to tick. Then point the display at F800 and
draw back at FC00.

Graphics mode has its own video memory and cannot work this way. A 32 by 32
character screen is 1024 bytes and fits in RAM. A 256 by 256 pixel screen is
65536 bytes, which is the whole address space. The numbers decide it, not
taste.

## Mapped transfers

`CMD_MEMMAP` takes a type on DATA0. A 16-bit RAM address follows on DATA1 and
DATA2. It remembers where a transfer reads from or writes to, and the mapping
holds until it is set again.

Types: `MAP_PALETTE_IN`, `MAP_PALETTE_OUT`, `MAP_PALETTE`, `MAP_COLLIDE`.

Nothing moves until a command asks. Reading the palette out, loading one in,
and filling the collision table are all commands. The mapping only says where.
That is what separates them from text mode, which the display reads
continuously.

Two palette types exist so the source and the dump destination can differ. Read
the live palette to one buffer while a prepared palette waits in another.
`MAP_PALETTE` sets both at once.

The palette gains a route it does not have today. `CMD_LOAD_PALETTE` reads the
cartridge, which is written once at assembly time. Reading and writing RAM lets
a program dump the live palette. It can then change the bytes with arithmetic
and load them back. The cartridge route stays, because authored palettes belong
there.

## Results come back on the data ports

A command that produces a small answer leaves it on a data port, and the
program reads it with `IN`.

That removes a wart. `GPU_COLLIDE` answers whenever it is read, whether or not
any collision command has run. A program can act on a table that was never
filled. Under this design a result exists because a command produced it.

Large answers go to RAM through `CMD_MEMMAP`. A collision table is 256 bytes
and a palette is 768. Moving those a byte at a time through a port would be
slow enough to be a trap.

The transfer is one cycle. That is the magic box rule, stated as always. The
CPU stays 8 bit and honest. The peripheral does the heavy work.

## Every command that reads ROM

Where each command learns the shape of what it is pointed at, today.

| Command | Shape from | Self-describing |
|---|---|---|
| `CMD_SPRITE_STRIP` | blob header, 2 bytes | yes |
| `CMD_PRINTF` | a zero byte ends the string | yes |
| `CMD_LOAD_MIDI` | the MIDI file's own header | yes |
| `CMD_SPRITE_DEF` | the X and Y ports | no |
| `CMD_DRAW_PATH` | `GPU_ARG` | no |
| `CMD_DRAW_PATH3D` | `GPU_ARG` | no |
| `CMD_DEF_SAMPLE` | the argument pair | no |
| `CMD_LOAD_PALETTE` | assumes 768 bytes | no |
| `CMD_LOAD_FONT` | assumes 2048 bytes | no |
| `CMD_MESH_LOAD` | the X and Y ports | no |

Three already follow the rule. The other seven change.

`CMD_SPRITE_DEF` and `CMD_SPRITE_STRIP` become one command. A blob leading with
frame count, width and height describes a single sprite when the count is one.
Animated sprites are square today. Under the header they stop being square, so
a 16 by 8 walking figure becomes possible.

## One pen, one position

Drawing commands disagree about where they draw, and the disagreement is
invisible.

`CMD_LINE_TO`, `CMD_RECT` and `CMD_CIRCLE` use the pen, which `CMD_MOVE_TO`
sets. `CMD_DRAW_PATH` and `CMD_DRAW_PATH3D` do not. They centre on the X and Y
latches, through `project2`. The cube demo works only because of that. It sets
the latches once and never issues a `CMD_MOVE_TO`.

The latches do not survive this redesign, so paths need an origin either way.
They take the pen, and every drawing command then shares one convention.

The pen carries position and colour. `CMD_MOVE_TO` moves it and
`CMD_SET_COLOR` colours it. A plotter works this way and so does a turtle, so
the model is one a student already has.

## The blit

`CMD_BLIT` draws a cartridge image at the pen. The blob leads with its width
and height, under the ROM rule above, so the command takes only a pointer.

That covers the background case, which has no command today. Point the pen at
0,0 and blit a 256 by 256 image. It also covers a case sprites handle badly:
stamping scenery from ROM without spending a sprite slot on it.

## The command map

Ports are scarce and command opcodes are not. The GPU owns 32 port slots and
256 command bytes. So every latch that was a port becomes an argument or a
command. The scarce resource stops being spent on the abundant one.

Multi-byte values go high byte first, because the CPU is big-endian
everywhere. A `dw` stores high first, a word load fills a D register high
first, and printf pushes its parameters high first. So a cartridge address is
bank, high, low, and a RAM address or a length is high, low. Coordinates are
signed 16-bit and follow the same order.

### Screen and pen

| Command | DATA | Note |
|---|---|---|
| `CMD_CLEAR` | 0: colour | Own colour, so clearing does not disturb the pen |
| `CMD_SET_COLOR` | 0: colour | The pen's colour |
| `CMD_MOVE_TO` | 0-3: x, y | Moves the pen |
| `CMD_LINE_TO` | 0-3: x, y | Draws from the pen, then moves it |
| `CMD_RECT` | 0-3: x, y | Other corner is the pen |
| `CMD_CIRCLE` | 0: radius | Centred on the pen |
| `CMD_RING` | 0: radius | Centred on the pen |
| `CMD_PLOT` | 0-3: x, y. 4: colour | Was the `GPU_PIXEL` write |
| `CMD_READ_PIXEL` | 0-3: x, y. Reads back on 0 | Was the `GPU_PIXEL` read |
| `CMD_SAVE_SCREEN` | none | |
| `CMD_RESTORE_SCREEN` | 0-3: x, y | Shift applied on restore |

### Palette

| Command | DATA | Note |
|---|---|---|
| `CMD_LOAD_PALETTE` | 0-2: cart address | Blob leads with its entry count |
| `CMD_COPY` | 0-2: cart address. 3-4: RAM address. 5-6: length | The first command to need a seventh data port |
| `CMD_STORE_PALETTE` | none | Goes to the `MAP_PALETTE_OUT` address |
| `CMD_FETCH_PALETTE` | none | Comes from the `MAP_PALETTE_IN` address |
| `CMD_RESET_PALETTE` | none | Back to 3-3-2 |
| `CMD_ROTATE_LEFT` | none | |
| `CMD_ROTATE_RIGHT` | none | |
| `CMD_ROTATE_SPEED` | 0: frames per step | 0 turns it off |
| `CMD_ROTATE_DIR` | 0: direction | |
| `CMD_ROTATE_RANGE` | 0: first. 1: last | Replaces two commands |

### Sprites

| Command | DATA | Note |
|---|---|---|
| `CMD_SPRITE_DEF` | 0: index. 1-3: cart address | Blob leads with frames, width, height |
| `CMD_SPRITE_MOVE` | 0: index. 1-4: x, y | The widest command, at five |
| `CMD_SPRITE_SHOW` | 0: index | |
| `CMD_SPRITE_HIDE` | 0: index | |
| `CMD_SPRITE_FRAME` | 0: index. 1: frame | Was the `GPU_SPRITE_FRAME` port |
| `CMD_STAMP` | 0: index. 1-4: x, y | Bakes the sprite into the screen |
| `CMD_BLIT` | 0-2: cart address | Draws a cartridge image at the pen |

`CMD_SPRITE_STRIP` is gone. A blob whose header says one frame is a single
sprite. The two commands were one command disagreeing with itself.

### Collision

| Command | DATA | Note |
|---|---|---|
| `CMD_HIT_TEST` | 0: index. 1: index. Reads back on 0 | Two named sprites |
| `CMD_HIT_SCAN` | 0: index. Reads back on 0 | Lowest sprite it overlaps |
| `CMD_COLLIDE_ALL` | none | Fills the `MAP_COLLIDE` address |

The `GPU_HIT` and `GPU_COLLIDE` ports are gone. A result now exists because a
command produced it.

### Paths and 3D

| Command | DATA | Note |
|---|---|---|
| `CMD_ROT_X` | 0: angle | |
| `CMD_ROT_Y` | 0: angle | |
| `CMD_ROT_Z` | 0: angle | |
| `CMD_SET_SCALE` | 0: scale | |
| `CMD_DRAW_PATH` | 0-2: cart address | Blob leads with its point count. Drawn at the pen |
| `CMD_DRAW_PATH3D` | 0-2: cart address | Same |

### Text

| Command | DATA | Note |
|---|---|---|
| `CMD_SET_TEXTMODE` | 0-1: RAM address | Sets the mode and the framebuffer |
| `CMD_SET_GRAPHICSMODE` | none | |
| `CMD_TEXT_STYLE` | 0: foreground. 1: background. 2: flags | Was three ports |
| `CMD_TEXT_AT` | 0: column. 1: row | Was two ports |
| `CMD_TEXT_CHAR` | 0: character | Draws at the cursor and advances |
| `CMD_TEXT_CLEAR` | none | |
| `CMD_PRINTF` | 0-2: template in cart. 3-4: arguments in RAM | |
| `CMD_LOAD_FONT` | 0-2: cart address | Blob leads with glyph count and height |

`CMD_VIDEO_MODE` and `CMD_TEXT_MAP` are gone, folded into the two mode
commands. So is `GPU_TEXT_ARG`.

That last one is the interesting change. Printf arguments were pushed one byte
at a time into a queue on a port. Their number was bounded by patience. Putting
them in RAM and passing a pointer makes them unbounded. It also matches the
rule the rest of this document follows. Large data moves through memory, never
through a port.

## The world mode

`MODE_WORLD` was built after this design was written, so none of its commands
appear above. All five fit, and three follow from rules already stated here.

**A mesh blob leads with its own counts.** `CMD_MESH_LOAD` needs a mesh id, an
address and two counts. That is eight data ports.
It does not fit, and it should not. The ROM rule above says the blob supplies
its own shape. With a two-word header the command drops to four, the same
correction `CMD_SPRITE_DEF` and `CMD_DRAW_PATH` take.

**`CMD_WORLD_RAMP` takes its colour directly.** Today it reads shade 0 back out
of palette entry `ramp * 16`. The program wrote that through `GPU_PAL_INDEX`
and the three component ports, which are gone. So the colour becomes an
argument, and the command stops depending on an earlier palette write.

**The scene is a framebuffer.** So the mode command points at it. The GPU reads
the scene record every frame, as the display reads the text buffer. So
`CMD_SET_WORLDMODE` sets the mode and the scene address together, the way
`CMD_SET_TEXTMODE` does. `CMD_WORLD_MAP` disappears into it.

The matrix table is different. It is read during a frame rather than defining
one, and a program may leave it unmapped. So it keeps its own command.

| Command | DATA | Note |
|---|---|---|
| `CMD_SET_WORLDMODE` | 0-1: scene address. 2-3: object count | Sets the mode and the scene |
| `CMD_MESH_LOAD` | 0: mesh id. 1-3: cart address | Blob leads with vertex and edge counts |
| `CMD_MESH_WRITE` | 0: byte | Streams into world RAM |
| `CMD_WORLD_RAMP` | 0: ramp. 1-3: red, green, blue | Fades shade 0 toward the background |
| `CMD_MATRIX_MAP` | 0-1: base address. 2-3: count | Matrices in data RAM, float64 |

The camera's two matrix ids live in the scene record, and so does every
object's transform. Neither costs a port.

## Sprite orientation

Decided 2026-09-07, to be built as part of this redesign rather than before it.

A sprite gains two attribute bits, a horizontal flip and a vertical flip. The
blit reads the source pixel from the mirrored coordinate. There is no per-pixel
cost beyond the blit it already does.

No rotation. Quarter-turn rotation was considered and rejected. It suits a
radially symmetric sprite like Pac-Man and breaks any sprite with a fixed
bottom. Rotate a ghost and its skirt ends up on the side. Every 2D console of
the era carries flip bits and almost none carry rotation. So a flip is also the
primitive a student meets again elsewhere.

The saving is not memory. Pac-Man's twelve frames become six and a ghost's four
become three, which is about 830 bytes of a megabyte. The reasons are that one
authored frame yields both facings, and that the feature matches what real
sprite hardware does.

Why it waits for this redesign. The Pac-Man sprite strips are built, tested and
reviewed against the current ports. This redesign already restructures how a
sprite is addressed. The attribute belongs in that work rather than bolted
beside it.

## Still open

- Whether `CMD_SET_COLOR` is worth a command, or whether colour rides DATA on
  each drawing command. The pen model argues for the command. Counting
  instructions in a real demo would settle it.
- What `MOD_STICKY` does to a command that writes results back onto the data
  ports. The two features want the same registers.
