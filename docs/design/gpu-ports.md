# SimpleCPU-8 GPU reference

The GPU is a peripheral on the IO bus. It is not part of the CPU. The CPU
talks to it with OUT, OUTA, and IN. The GPU claims ports 0x00 to 0x1F.
Writes to other ports go to the rest of the bus.

The GPU is deterministic. Every visible effect follows from the values
written and the machine cycle counter. The GPU never crashes the CPU.
Off screen drawing clips at the edges. Unknown commands do nothing.

## Screen model

The screen is 256 by 256 pixels. Each pixel holds one byte. That byte
indexes a palette of 256 colors. Each palette entry holds red, green, and
blue as one byte each. The default palette is 3-3-2. The top three bits
pick red, the next three pick green, the last two pick blue.

Device time runs on frames. One frame lasts 65536 machine cycles. The
frame counter drives palette rotation and is readable by programs.

## Ports

Eleven ports, where there were thirty. One command port, one modifier, seven
data ports, and the two direct reads.

A data port means whatever the running command says it means. The command's
own row below says which.

The two direct reads sit at the ceiling and the command block grows from the
floor. Ports 0x09 to 0x1D are reserved. An eighth data port and a later
direct-value port can be added without renumbering anything.

| Port | Name | Direction | Meaning |
| --- | --- | --- | --- |
| 0x00 | GPU_CMD | write | write a command byte to run it |
| 0x01 | GPU_CMD_MOD | both | modifier bits for the next command |
| 0x02 | GPU_DATA0 | both | arguments in, results out |
| 0x03 | GPU_DATA1 | both | arguments in, results out |
| 0x04 | GPU_DATA2 | both | arguments in, results out |
| 0x05 | GPU_DATA3 | both | arguments in, results out |
| 0x06 | GPU_DATA4 | both | arguments in, results out |
| 0x07 | GPU_DATA5 | both | arguments in, results out |
| 0x08 | GPU_DATA6 | both | arguments in, results out |
| 0x1E | GPU_RAND | both | read: a random byte. write: reseed |
| 0x1F | GPU_FRAME | read | read: the frame counter |

GPU_FRAME and GPU_RAND stay direct because both are read in tight loops. A
frame poll is the idiom every animated program is built on. Making it a
command plus a read would double its cost.

## The command modifier

Data ports clear after every command. A port not written for the next command
reads as zero, so nothing carries over by accident. The sticky high byte on
the old coordinate latches was this machine's classic bug. Clearing by
default makes it unreachable.

A result is not an argument. A command that leaves an answer on a data port
keeps it. Clearing it would make reading it back impossible.

| Bit | Name | Effect |
| --- | --- | --- |
| 0 | MOD_STICKY | Data ports keep their values after the command |
| 1 | MOD_INC0 | DATA0 steps by one after the command |

MOD_STICKY repeats a command with one argument changed. MOD_INC0 walks an
indexed resource: DATA0 is the index on every command that takes one.

The modifier holds until it is written again. It is a mode, not a one-shot.

## Commands

Write the byte to GPU_CMD. The data ports carry the arguments, high byte
first, because the CPU is big-endian everywhere. A cartridge address is bank,
high, low.

| Byte | Name | Data ports | Meaning |
| --- | --- | --- | --- |
| 0x01 | CMD_CLEAR | 0: colour | Its own, so clearing leaves the pen alone |
| 0x02 | CMD_SET_COLOR | 0: colour | The pen's colour |
| 0x03 | CMD_MOVE_TO | 0-3: x, y | Moves the pen |
| 0x04 | CMD_LINE_TO | 0-3: x, y | Draws from the pen, then moves it |
| 0x05 | CMD_RECT | 0-3: x, y | The other corner is the pen |
| 0x06 | CMD_CIRCLE | 0: radius | A filled disc, centred on the pen |
| 0x07 | CMD_RING | 0: radius | An outline, centred on the pen |
| 0x08 | CMD_PLOT | 0-3: x, y. 4: colour | One pixel, in its own colour |
| 0x09 | CMD_READ_PIXEL | 0-3: x, y | Reads back on DATA0 |
| 0x0A | CMD_SAVE_SCREEN | none |  |
| 0x0B | CMD_RESTORE_SCREEN | 0-3: x, y | A shift applied on restore |
| 0x10 | CMD_RESET_PALETTE | none | back to 3-3-2 |
| 0x11 | CMD_LOAD_PALETTE | 0-2: cart address | The blob leads with its count |
| 0x12 | CMD_STORE_PALETTE | none | to the MAP_PALETTE_OUT address |
| 0x13 | CMD_FETCH_PALETTE | none | from the MAP_PALETTE_IN address |
| 0x14 | CMD_ROTATE_LEFT | none |  |
| 0x15 | CMD_ROTATE_RIGHT | none |  |
| 0x16 | CMD_ROTATE_SPEED | 0: frames per step, 0 turns it off | 0: frames per step, 0 turns it off |
| 0x17 | CMD_ROTATE_DIR | 0: direction | 0: direction |
| 0x18 | CMD_ROTATE_RANGE | 0: first | 1: last |
| 0x20 | CMD_SPRITE_DEF | 0: index. 1-3: cart address. 4: group mask | The blob carries frames, width and height |
| 0x21 | CMD_SPRITE_MOVE | 0: index | 1-4: x, y |
| 0x22 | CMD_SPRITE_SHOW | 0: index | 0: index |
| 0x23 | CMD_SPRITE_HIDE | 0: index | 0: index |
| 0x24 | CMD_SPRITE_FRAME | 0: index | 1: frame |
| 0x25 | CMD_SPRITE_FLIP | 0: index | 1: bit 0 horizontal, bit 1 vertical |
| 0x26 | CMD_STAMP | 0: index | 1-4: x, y. Bakes the sprite into the screen |
| 0x27 | CMD_BLIT | 0-2: cart address | Draws an image at the pen |
| 0x30 | CMD_HIT_TEST | 0: index | 1: index. Reads back on DATA0 |
| 0x31 | CMD_HIT_SCAN | 0: index | Reads back on DATA0 |
| 0x32 | CMD_COLLIDE_ALL | none | fills the MAP_COLLIDE address |
| 0x33 | CMD_SPRITE_HITS | 0: index | Groups touching it, back on DATA0 |
| 0x34 | CMD_GROUP_HITS | 0: group mask | Groups touching it, back on DATA0 |
| 0x35 | CMD_HIT_IN_GROUP | 0: index. 1: group mask. 2: search from | The sprite, back on DATA0 |
| 0x36 | CMD_COLLIDE_GROUP_ALL | none | fills the MAP_GROUPS address |
| 0x40 | CMD_ROT_X | 0: angle | 0: angle |
| 0x41 | CMD_ROT_Y | none |  |
| 0x42 | CMD_ROT_Z | none |  |
| 0x43 | CMD_SET_SCALE | 0: scale | 0: scale |
| 0x44 | CMD_DRAW_PATH | 0-2: cart address | The blob leads with its count |
| 0x45 | CMD_DRAW_PATH3D | 0-2: cart address | Same |
| 0x50 | CMD_SET_TEXTMODE | 0-1: RAM address | 0-1: RAM address |
| 0x51 | CMD_SET_GRAPHICSMODE | none |  |
| 0x52 | CMD_TEXT_STYLE | 0: foreground | 1: background. 2: flags |
| 0x53 | CMD_TEXT_AT | 0: column | 1: row |
| 0x54 | CMD_TEXT_CHAR | 0: character, drawn at the cursor | 0: character, drawn at the cursor |
| 0x55 | CMD_TEXT_CLEAR | none |  |
| 0x56 | CMD_PRINTF | 0-2: template in cart | 3-4: arguments in RAM |
| 0x57 | CMD_LOAD_FONT | 0-2: cart address | 0-2: cart address |
| 0x60 | CMD_SET_WORLDMODE | 0-1: scene address | 2-3: object count |
| 0x61 | CMD_MESH_LOAD | 0: mesh id | 1-3: cart address |
| 0x62 | CMD_MESH_WRITE | 0: byte | 0: byte |
| 0x63 | CMD_WORLD_RAMP | 0: ramp | 1-3: red, green, blue |
| 0x64 | CMD_MATRIX_MAP | 0-1: base address | 2-3: count |
| 0x70 | CMD_COPY | 0-2: cart address | 3-4: RAM address. 5-6: length |
| 0x71 | CMD_MEMMAP | 0: what | 1-2: RAM address |
| 0x72 | CMD_RAM_MOVE | 1-2: from. 3-4: to | 5-6: length |

## Paths

Paths are vector shapes stored on the cartridge. Each point is one byte
per axis, normalized: 0 is the shape's left or top, 255 is almost 1, and
128 is the center. The assembler writes these bytes for you. db 0.75
stores 192. db sin(0.25) and db cos(t) store trig values centered on
128, with t in turns. One turn is 1.0, matching the GPU's 256 angle
steps.

The GPU has a sine table in hardware. CMD_ROT_X, CMD_ROT_Y, and
CMD_ROT_Z set rotation angles as bytes, 256 steps per turn. CMD_SET_SCALE
sets how many pixels the 0..1 range spans. The (x, y) latches place the
shape's center on screen.

CMD_DRAW_PATH reads pairs and applies the z rotation, the scale, and the
translation. CMD_DRAW_PATH3D reads triples, rotates around all three
axes, and projects with perspective: vertices closer to the eye draw
larger. All the math is integer, so the same path draws the same pixels
on every machine. A rotating shape costs the CPU one OUT per angle per
frame. The GPU does the rest.

## Cartridge

The cartridge holds up to 1MB of read only asset data. Only the GPU can
read it. The CPU cannot address it at all. Addresses are flat. The bank
byte is the address bits 16 to 19.

The assembler builds the cartridge from the .data section. Use db and dw
for raw bytes. Use .file('name') for raw file content. Use .image('name')
for decoded pixels. Use .palette('name') for the 768 palette bytes of an
image. The assembler stores equal content once. Two labels for the same
bytes point at one copy.

Labels in .data are cartridge addresses. They do not fit in one byte, so
three macros split them. get_lowbyte(label), get_highbyte(label), and
get_bankbyte(label) each produce one byte. They work as immediates and as
OUT values.

## Sprites

There are 256 sprites. Each is up to 64 by 64 pixels. CMD_SPRITE_DEF
copies pixel data from the cartridge into the sprite. The blob leads with
its frame count, width and height. Nothing in the program says how big the
sprite is. The copy happens at definition time.

### Groups

A sprite carries a mask of eight group bits, set by CMD_SPRITE_DEF on data
port 4. Zero means no group, which is what a program written before groups
gets, and it answers exactly as it did.

A mask rather than a number, so one sprite can be an enemy and a solid at
once. There is no command to change it. Redefining the sprite is how a
sprite changes groups, and a redefine resets the animation frame to 0.

Four commands read the groups. CMD_SPRITE_HITS gives every group touching
one sprite, all of them and not the first. CMD_GROUP_HITS gives every group
touching a group, and a group reports itself when two of its own overlap.
CMD_HIT_IN_GROUP names the sprite, searching from data port 2. Feeding back
the last answer plus one walks all of them. CMD_COLLIDE_GROUP_ALL writes all
256 masks to the MAP_GROUPS address in one command.

The test is the same one every other collision uses. Bounding boxes, not
pixels. A hidden sprite collides with nothing. An ungrouped sprite never
appears in a group answer, and no sprite collides with itself.

Sprites float above the background. They do not change video RAM. Pixel
value 0 is transparent. Sprites with higher numbers draw in front.
Scroll moves the background under them. Sprites hold still in screen
space. CMD_STAMP is different. It writes the sprite pixels into video
RAM once, for tiles and backdrops.

## Text

The GPU draws characters with a 6 by 8 font, which gives 42 columns by 32
rows, 1344 cells. The font is five columns of face with a one pixel gap, so
letters never touch.

Codes 0x20 to 0x7F have a glyph. The last is a solid block, which is what a
text cursor wants. It fills the whole cell rather than the face, so a cursor
leaves no stripe down its right hand side.

CMD_LOAD_FONT loads a custom font, 256 glyphs of 8 bytes, from the
cartridge. A glyph row is a byte and the cell reads its low six bits.
Restart brings the built-in font back.

printf draws over the graphics. Put a template in the cartridge, push the
parameters into RAM, point GPU_TEXT_ARG_HI and GPU_TEXT_ARG_LO at them,
and run CMD_PRINTF. The format follows C, so
%03.4f and %#x work. An int is 16 bits here. So %hhu reads one byte, %d
two, %ld four, %lld eight. Push multi-byte values high byte first. The
text color is GPU_TEXT_COLOR, its own register.

The %s and %r conversions take a pointer into RAM. The template is in
ROM, but the strings it points at live in RAM. %s reads to a zero byte.
%r prints an exact count set by the width, so %5r draws five raw bytes.

Text mode draws characters read from data RAM over the VRAM. A pixel no
glyph lights shows the picture under it. The text background fills a cell
only when the style's opaque flag is set, as it does for the overlay.
CMD_SET_TEXTMODE switches the mode and sets the base together, reading
GPU_ADDR_HI and GPU_ADDR_LO. One command, so a mapped mode with no buffer
under it cannot be reached. The GPU reads 1344 bytes every frame, so a store
shows at once. CMD_SET_GRAPHICSMODE switches back, and graphics is
the power-on mode.

## Copying cartridge data into RAM

The CPU cannot read the cartridge. CMD_COPY is how ROM data reaches it. The
CPU names the addresses and the GPU moves the bytes. The CPU then reads RAM
the way it always does.

Seven bytes describe the move, and every multi-byte value goes high byte
first.

```asm
        OUT GPU_CART_BANK, get_bankbyte(level1)
        OUT GPU_CART_HI, get_highbyte(level1)
        OUT GPU_CART_LO, get_lowbyte(level1)
        OUT GPU_DEST_HI, $10   ; destination $1000
        OUT GPU_DEST_LO, $00
        OUT GPU_LEN_HI, 3      ; length 868 bytes
        OUT GPU_LEN_LO, $64
        OUT GPU_CMD, CMD_COPY
```

The copy finishes in one cycle, like every GPU command. A destination past
the top of RAM wraps to zero, because no data address is illegal. A source
past the end of the cartridge reads as zero. A length of zero copies nothing.

Every data port clears once the command has run. So each copy writes all
seven bytes, and a long copy followed by a short one cannot copy too much.

This is what puts level layouts and lookup tables in the 1MB cartridge. They
no longer have to sit in the assembled RAM image.

## Moving a block of RAM

CMD_RAM_MOVE moves bytes from one place in data RAM to another. The CPU has
no block move. Without this command a copy is a load and a store per byte.
That is what every struct assignment and every memcpy would cost.

It is a move and not a copy, in the sense the C function memmove has. The
two blocks may overlap, and the GPU picks the direction that survives the
overlap. A collector pulling a string heap down over itself is the case
that asked for the command.

```asm
        OUT GPU_FROM_HI, $20   ; from $2000
        OUT GPU_FROM_LO, $00
        OUT GPU_DEST_HI, $10   ; to $1000
        OUT GPU_DEST_LO, $00
        OUT GPU_LEN_HI, 1      ; 256 bytes
        OUT GPU_LEN_LO, $00
        OUT GPU_CMD, CMD_RAM_MOVE
```

The ports are CMD_COPY's with the bank slot left empty, so a program that
knows one knows the other. Both ends wrap at 16 bits, the way every other
address on this machine wraps. The source is left as it was.

## Random numbers

The GPU has a random generator on a dedicated port, so no command is needed.
Read GPU_RAND for the next byte. Each read advances the generator. Write
GPU_RAND to reseed it from that byte.

The generator is a 32-bit xorshift, seeded at power-on from the wall clock. So
a program that reads GPU_RAND without seeding gets a different sequence every
run. Power-on happens on Restart and on program load.

Write a seed first to get the other behaviour. The same seed always gives the
same stream, so a seeded run repeats exactly. That is what you want for a test,
a replay, or a bug you need to reproduce.

Both are useful and the choice is the program's. Read first for real
randomness. Seed first for randomness you can run again.

Read a word with INW. IN GPU_RAND -> A reads one random byte into A. IN
GPU_RAND -> D2 reads two bytes into D2, a 16-bit random. Store it with
LD [D1] <- D2. The byte opcode is INB and the word opcode is INW. Plain IN
picks by the target, the way PUSH picks PUSHB or PUSHW.

## Reset rules

The Reset button clears the CPU only. The screen is outside the CPU, so
it survives. Restart reloads the .ram image and powers the GPU on fresh.
Power on state: black screen, default palette, all latches zero, no
sprites, rotation off, graphics mode, the built-in font. The random seed
resets to a fixed value, so the random stream repeats after a Restart.
