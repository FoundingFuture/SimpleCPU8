# MODE_WORLD reference

The record layouts, the memory sizes and the palette layout for the GPU's 3D
world mode. Every number here is pinned to a constant in
`packages/core/src/gpu.ts` by `packages/core/test/gpuworld-doc.test.ts`. Change
the machine and the test fails until the two agree.

The ports and commands themselves are in `docs/gpu-ports.md`, which is derived
from `GPU_PORTS` and `GPU_CMDS`. They are not repeated here.

## Contents

- [Switching the mode on](#switching-the-mode-on)
- [The memories](#the-memories)
- [The camera record](#the-camera-record)
- [The object record](#the-object-record)
- [Mesh geometry](#mesh-geometry)
- [Matrices](#matrices)
- [The palette layout](#the-palette-layout)
- [The depth cue](#the-depth-cue)
- [Conventions](#conventions)

## Switching the mode on

Two steps, in this order:

1. `CMD_MESH_LOAD` for each mesh. `GPU_MESH` names the mesh and
   `GPU_SRC_BANK`, `GPU_SRC_HI` and `GPU_SRC_LO` point at the geometry. The
   blob leads with its own vertex and edge counts.
2. `CMD_SET_WORLDMODE`. `GPU_ADDR_HI` and `GPU_ADDR_LO` carry the scene's
   data RAM base, `GPU_COUNT_HI` and `GPU_COUNT_LO` the object count.

One command sets the mode and points it at the scene. A world mode with
nothing mapped is not a state this machine can reach. There used to be three
steps and an order to get wrong.

The data ports clear after every command, so nothing needs clearing by hand.
The hazard is different now. Write one command's arguments and then run a
different command, and the bytes are read as whatever it expects there.

`CMD_SET_GRAPHICSMODE` switches back, and graphics is the power-on mode.

## The memories

| what | size | who reads it |
|---|---|---|
| world RAM | 262144 bytes | the GPU. Geometry lives here |
| mesh table | 256 meshes | the GPU |
| the scene | in data RAM, at the mapped base | the GPU, every frame |

Geometry lives in the GPU. The scene lives in the CPU's own data RAM. So
changing the world is an ordinary store, with no command and no upload.

## The camera record

32 bytes at the mapped base. Words are big-endian, like everywhere else.

| offset | field |
|---|---|
| 0-5 | position: x, y, z, three signed 16 bit |
| 6-11 | yaw, pitch, roll, three unsigned 16 bit |
| 12-13 | near plane |
| 14-15 | far plane |
| 16-17 | focal length. 256 means a 90 degree field of view |
| 18-23 | sun direction, three signed 16 bit. Reserved, and zero today |
| 24 | ambient floor, 0 to 255. Reserved, and zero today |
| 25 | flags. Bit 0 takes the view from a matrix, bit 1 the projection |
| 26-27 | view matrix id |
| 28-29 | projection matrix id |
| 30-31 | reserved |

A full turn is 65536, so an angle field wraps on its own. Adding to it forever
is legal and needs no test in the program.

The camera looks down -Z with Y up. Driving forward is subtracting from the
camera's z. Raising the camera moves the picture down.

## The object record

16 bytes each, starting at the base plus 32. Object N sits at
`base + 32 + N * 16`.

| offset | field |
|---|---|
| 0 | flags. Bit 0 is active, and the rest are reserved |
| 1 | mesh id |
| 2 | ramp, 0 to 15 |
| 3 | scale, where 64 means 1.0 |
| 4-9 | position, three signed 16 bit |
| 10-15 | yaw, pitch, roll, three unsigned 16 bit |

Sixteen bytes is forced, not chosen. The CPU has no multiply, so `N * 16` has
to be four doublings. A 24 byte record would need a real multiply on every
object the program touches.

Clearing bit 0 hides an object without removing it. More per-object data goes
in a second mapped table indexed by the same id, never in a wider record. An
object id stays two bytes.

## Mesh geometry

A mesh is geometry. An object is one placement of a mesh, at a position, in a
colour, at a size. Eight pyramids can be eight objects and one mesh.

`CMD_MESH_LOAD` reads the cartridge in this order:

| what | bytes each | format |
|---|---|---|
| vertices | 6 | x, y, z, three signed 16 bit |
| edges | 4 | two unsigned 16 bit vertex numbers |

Six bytes a vertex is a native `gl.SHORT` triple, which a graphics card reads
without conversion.

Faces are not stored, edges are. A pyramid's four sides are their eight edges,
with the shared ones kept once. An edge list cannot be turned back into faces.
So the mesh record reserves a face range, for the surfaces that come later.
Each mesh record is 20 bytes: 12 in use, then that reserved range.

`CMD_MESH_WRITE` streams bytes into world RAM for geometry a program builds at
run time, rather than reading the cartridge.

## Matrices

An object's transform can come from a 4x4 matrix instead of its own position,
angles and scale. `CMD_MATRIX_MAP` nominates the table. The base goes in
`GPU_ADDR_HI` and `GPU_ADDR_LO`, the count in `GPU_COUNT_HI` and
`GPU_COUNT_LO`. Matrix N sits at `base + N * 128`.

Set flag bit 1 on the object. Bytes 4 and 5 are then a matrix id. That is two
bytes, like every other id here. Bytes 3 and 6 to 15 say nothing.

A matrix already carries scale, position and rotation. A second source for any
of them would be a second source of truth.

| what | value |
|---|---|
| element | float64, big-endian |
| order | row major, 16 elements |
| size | 128 bytes a matrix |
| translation | elements 3, 7 and 11 |

The mesh's own scale exponent still applies. That is the unit its geometry is
written in, not a property of the placement.

**The camera takes two matrices of its own, a view and a projection.** Those
are separate things. `GL_MODELVIEW` and `GL_PROJECTION` were always separate
stacks, and a shadow-casting light will want both for the same reason. Byte 25
of the camera record carries a flag bit for each.

Set bit 0 and the view comes from the matrix named at bytes 26 and 27. The
camera's position and angles then say nothing. The matrix is the VIEW matrix, not the camera's
world transform. That is what a graphics card takes. Anyone holding a camera
transform runs `ACP_INVERSE`.

Set bit 1 and the projection comes from the matrix at bytes 28 and 29. The
focal length then says nothing. This is the only way to get an ORTHOGRAPHIC
view: the built projection is always a perspective.

Near and far keep working either way. The depth cue reads view space z against
them directly, never the projected z. That is what keeps the picture the same
under both depth conventions.

A camera id that names no matrix falls back to the record. That is different
from an object, which draws nothing. An object in matrix mode has no position
to fall back to, because its position bytes ARE the id. The camera's sit
elsewhere in the record and stay valid.

An id at or past the mapped count draws nothing, silently, the way an object
naming an undefined mesh does.

**float64 is forced, not chosen.** The ACP composes the transform. It is the
only device here that can, and it writes data RAM.

Its element types are `ACP_I64`,
`ACP_U64`, `ACP_F64` and `ACP_C64`, with no 16.16 among them. The CPU cannot
build a matrix at all, having no multiply. So the format the GPU reads has to
be the format the ACP writes.

Point the matrix table at the ACP's result slot and nothing copies anything.
The ACP block is A, then B, then the result. The result of a 4x4 product
therefore sits 256 bytes into the block. The orbit demo does exactly that.

The painter's sort reads an object's origin. In this mode that is the matrix's
translation, not the bytes at offset 4.

## The palette layout

One sentence: the high nibble is the ramp and the low nibble is the shade.

| bits | meaning |
|---|---|
| 7-4 | the ramp, 16 of them. An object's ramp field picks one |
| 3-0 | the shade, 16 of them. 0 is nearest and brightest |

Sixteen ramps of sixteen shades fills all 256 entries exactly, with nothing
left over. Ramp 1 is palette entries 16 to 31. Ramp 2 is 32 to 47. Ramp N starts at
N times 16.

Run `CMD_WORLD_RAMP` with the ramp on `GPU_RAMP` and the colour on
`GPU_RED`, `GPU_GREEN` and `GPU_BLUE`. That is shade 0. The GPU fills the
other fifteen, fading toward
the background. Shade 15 keeps a sixteenth of the base. So the farthest
geometry stays distinct rather than merging into the background.

**The background is palette entry 0.** Fading always to black is right only on
a black background. On a light sky, geometry that darkened as it
receded would stand out more, not less. Entry 0 is read when `CMD_WORLD_RAMP`
runs, so set the background before building the ramps.

Palette rotation still works, and works better here. `CMD_ROTATE_RANGE` takes
a sub-range on `GPU_FIRST` and `GPU_LAST`, so one ramp can cycle while others stay
still.

## The depth cue

The shade comes from view space depth:

```text
t     = (z - near) / (far - near)      clamped to 0 .. 1
shade = floor(t * 16)                  clamped to 0 .. 15
```

Set the far plane so it clears the furthest object. Everything past it clamps
to shade 15 together, and the depth cue stops separating those objects.

The shade is interpolated along an edge the way hardware does it. The value
is carried over w and divided back. Interpolating in
screen space is wrong on any edge running away from the viewer.

## Conventions

These are a graphics card's conventions, so a WebGL or WebGPU backend needs no
transposing and no sign flipping.

| what | choice |
|---|---|
| handedness | right handed, Y up |
| the camera | looks down -Z |
| matrices | row major |
| the fourth component | w equals -z |
| clipping | against the near plane, before the divide |
| the viewport | its own step, never in the projection matrix |

The depth range is a parameter. WebGL clips z to -1 to 1 and WebGPU clips it
to 0 to 1. The two differ in the projection matrix's third row and nothing
else.

Clip before the divide, always. Dividing by a negative w turns a vertex behind
the camera into a plausible coordinate in front of it.
