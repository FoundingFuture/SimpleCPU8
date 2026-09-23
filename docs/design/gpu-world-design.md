# The GPU world renderer

Eddie's design, 2026-09-10. A video mode in which the GPU holds a 3D world.
It draws that world from a camera, by itself, every frame. The CPU moves the
camera and edits the scene, and does no drawing at all.

## Contents

- [What it is](#what-it-is)
- [The compatibility contract](#the-compatibility-contract)
- [World RAM, and the meshes in it](#world-ram-and-the-meshes-in-it)
- [The scene, in data RAM](#the-scene-in-data-ram)
- [Object ids and changing the world](#object-ids-and-changing-the-world)
- [The palette](#the-palette)
- [Depth cue](#depth-cue)
- [Lighting, when surfaces arrive](#lighting-when-surfaces-arrive)
- [The pipeline, stage by stage](#the-pipeline-stage-by-stage)
- [The depth convention](#the-depth-convention)
- [Commands](#commands)
- [What it does not do](#what-it-does-not-do)
- [The demo](#the-demo)
- [Testing](#testing)
- [Settled](#settled)
- [Still open](#still-open)

## What it is

`MODE_WORLD` is a third video mode, beside `MODE_GRAPHICS` and `MODE_TEXT`.
In it the GPU renders a wireframe world every composed frame, from a camera
the program moves. Nothing the CPU does per frame is a drawing command.

**`MODE_GRAPHICS` is untouched.** `CMD_DRAW_PATH3D`, the `CMD_ROT_X` angle
latches, `CMD_SET_SCALE` and the 8 bit sine ROM all keep working exactly as
they do. The cube and star demos are unchanged. That is the low floor, and
this mode is the high ceiling above it.

The offload is the usual deliberate cheat. An 8 bit CPU could not transform
and draw a whole world in the time the display takes to compose. The manual
says so.

## The compatibility contract

Eddie's instruction, 2026-09-10. Build the software renderer, use it for
testing and rendering, and keep a hardware backend as a later option. If that
swap happens, the software renderer stays as the reference. The hardware path
is not tested, because a browser vendor owns it.

That is only cheap if the model matches what a GPU already speaks. These were
measured in the browser rather than recalled.

| checked | result | what it settles |
|---|---|---|
| WebGL2 present | yes, GLSL ES 3.00 | z clips to -1..1, not WebGPU's 0..1 |
| `uniformMatrix4fv` with transpose true | accepted | row major matrices upload as they are. WebGL1 refuses this |
| `gl.SHORT` vertex attribute | accepted | signed 16 bit positions are a native format |
| `ALIASED_LINE_WIDTH_RANGE` | 1 to 1 | hardware draws one pixel lines and nothing else |
| WebGPU present | yes | its z range is 0..1, and that is isolated. See below |

The line width result decided a feature. Thick lines are not portable, so
wireframe here is one pixel wide, which is what the hardware does anyway.

**The conventions, which are OpenGL's.** Right handed, Y up, the camera looks
down -Z. Clip space is a four component vector with w, and w is -z. Matrices
are row major, as on the ACP. After the divide, x and y lie in -1 to 1 under
both APIs. The z range is the one thing they disagree on.

**Three ways a software renderer usually diverges.** Each would break the
swap. Each is also easy to get wrong and invisible until the picture is.

- **Clip before the divide, not after.** Divide by a negative w, and a vertex behind the camera lands in front. The current `MODE_GRAPHICS` code
  clamps the denominator instead, which works only because its eye cannot
  move.
- **Interpolate a varying the way hardware does.** The depth shade is a
  varying. Interpolating it linearly in screen space is wrong on any edge
  running away from the viewer. Interpolate `value/w` against `1/w`, then
  divide back.
- **Keep the viewport transform out of the projection matrix.** Hardware
  keeps them separate. A projection with the viewport folded in is not one a
  GPU can use.

## World RAM, and the meshes in it

The GPU gains 256K of world RAM. The CPU cannot address it. Nor can it
address VRAM, the palette, the sprites, the font or the cartridge.
Geometry is large and static, and the CPU's own 64K is its working memory.

**A mesh is one whole shape, however many pieces it took to draw.** A pyramid
is a mesh. A house with a door, four windows and a roof is a mesh. So is a
whole tree. Nothing limits it but the vertex and edge counts.

**An object id names one placed copy of a mesh.** The mesh is the shape. The
object is where it stands, how big it is, and what colour. Ten houses along a
street are one mesh and ten object ids. The geometry is stored once.

**Faces are not stored. Edges are.** This is a wireframe renderer. A triangle
is three edges and a rectangle is four. A pyramid on a square base is five
vertices and eight edges, not five faces.

**A shared edge is stored once.** A cube has six faces. Between them those
faces name twenty four edges, of which only twelve are distinct. Storing faces
would keep every edge twice and draw every line twice.

Two shapes live there, both in the formats a GPU takes natively:

| what | format | bytes |
|---|---|---|
| a vertex | x, y, z as signed 16 bit | 6 |
| an edge | two unsigned 16 bit vertex indices | 4 |

A mesh is stored as a range of each. The mesh table holds 256 entries of 20
bytes:

| offset | field |
|---|---|
| 0-3 | first vertex, unsigned 32 bit offset into world RAM |
| 4-5 | vertex count |
| 6-9 | first edge |
| 10-11 | edge count |
| 12-15 | first face. Reserved, and zero today |
| 16-17 | face count. Reserved, and zero today |
| 18 | scale exponent, signed. A vertex unit is 2 to this power |
| 19 | reserved |

**The face range is reserved for surfaces, which will come.** Eddie,
2026-09-10. The room costs six bytes a mesh and nothing at run time.

An edge list cannot be turned back into faces. Nothing in it says which loops
of edges are surfaces. A mesh authored without that field could never be lit,
and every piece of world data would need authoring again.

**Faces will be stored beside the edges, never instead of them.** A wireframe
drawn from triangles shows every triangulation diagonal. A cube would draw
eighteen lines rather than twelve. The edge list stays what the wireframe
draws.

**The scale exponent stops 16 bit vertices becoming a ceiling.** A vertex
unit is two to that power, in world units. A finely detailed mesh sets
a negative exponent and a huge one sets a positive exponent. Both use the
whole 16 bit range, and both stay exact.

Without it a mesh is caught between two limits at once. Vertices reach 32767,
and an object's scale byte only spans 0 to 4. A detailed chair and a mountain
could not share a world at good precision.

It costs one byte and one shift when the model matrix is built. Every
quantised vertex format does this, and it is why they afford 16 bit
positions.

`CMD_MESH_LOAD` copies geometry from the cartridge into world RAM and defines
a mesh, in one command. That is how a static world arrives. `CMD_MESH_WRITE`
streams bytes through the argument latches for geometry a program builds at
run time, the way palette streaming already works.

## The scene, in data RAM

The camera and the objects live in the CPU's own data RAM, at a base the
program nominates with `CMD_WORLD_MAP`. The GPU reads them every composed
frame. This is the text mode idiom. `CMD_TEXT_MAP` points the character plane
at data RAM and the GPU reads it each frame, and this works the same way.

**That choice is the whole design.** Changing the world is writing bytes. No
command, no port, no upload. The ACP writes data RAM too, so it can compute a
transform straight into an object record.

The camera is 32 bytes at the base:

| offset | field |
|---|---|
| 0-5 | position, three signed 16 bit |
| 6-11 | yaw, pitch, roll, three unsigned 16 bit |
| 12-13 | near plane |
| 14-15 | far plane |
| 16-17 | focal length, 256 means a 90 degree field of view |
| 18-23 | sun direction, three signed 16 bit. Reserved, and zero today |
| 24 | ambient floor, 0 to 255. Reserved, and zero today |
| 25-31 | reserved |

Objects follow, starting at base plus 32, sixteen bytes each:

| offset | field |
|---|---|
| 0 | flags. Bit 0 is active, and the rest are reserved |
| 1 | mesh id |
| 2 | ramp, 0 to 15 |
| 3 | scale, where 64 means 1.0 |
| 4-9 | position, three signed 16 bit |
| 10-15 | yaw, pitch, roll, three unsigned 16 bit |

**More per-object data goes in a second table.** A material or a parent would
go there. So would a velocity, or a pointer to a matrix. The growth path is another mapped region,
indexed by the same id. Object N's extra data sits at `detailBase + N * 16`.
The id stays two bytes.

That path costs nothing today and needs no code now. Widening the record to
32 bytes was the alternative. It would double what every scene pays, today,
for something nobody has asked for.

**Sixteen bytes is forced, not chosen.** Object N sits at
`base + 32 + N * 16`, and 16 is four doublings. The CPU has no multiply. A 24
byte record would need a real multiply, or a lookup table, on every object
access. This is the reasoning that shaped `[D1+A]` and the demos that walk
pointers instead of indexing.

## Object ids and changing the world

**An object id is two bytes and never more.** Eddie's constraint, 2026-09-10.
Sixteen bits is the whole id space, so no record, no command and no program
ever has to widen one.

An id is the object's index. How many are live is the program's choice,
through `CMD_WORLD_MAP`. 256 objects cost 4K of data RAM and 1024 cost 16K.
A world is as big as the program is willing to pay for.

All 65536 at once would want 1MB of records, which the CPU does not have.
That is fine. The id space being wider than the practical count is the point.
The ceiling moves when data RAM does, and the format does not notice.

Everything the world can do is a memory write:

| to do this | write this |
|---|---|
| add an object | a whole record, with bit 0 of flags set |
| change one | the same record again, at the same id |
| move it | two bytes per axis |
| spin it | two bytes, the yaw |
| resize it | one byte, the scale |
| remove it | clear bit 0 of flags |

An inactive slot is skipped. So is a slot naming a mesh that was never
defined. Drawing nothing visible in that case would leave a reader wondering
which of the two happened.

## The palette

One sentence covers the whole layout: **the high nibble is the colour and the
low nibble is the distance.**

| bits | meaning |
|---|---|
| 7-4 | the ramp, 16 of them. An object's `ramp` field picks one |
| 3-0 | the shade, 16 of them. 0 is nearest and brightest, 15 is farthest and darkest |

That fills all 256 entries exactly, with nothing left over to explain. An
object is one colour, and how far away a piece of it is picks the shade.

`CMD_WORLD_RAMP` fills one ramp's sixteen shades from a base colour, fading to
black, so nobody writes 256 palette entries by hand. `GPU_ARG` is the ramp and
the colour goes in through the palette latches.

**Palette rotation still works, and works better here.** `CMD_ROTATE_START`
and `CMD_ROTATE_END` already take a sub-range. A program can cycle one ramp's
sixteen shades while every other ramp stays still.

## Depth cue

The shade is a function of view space depth, written down so that a fragment
shader could reproduce it exactly:

```text
t     = (z - near) / (far - near)      clamped to 0..1
shade = floor(t * 16)                  clamped to 0..15
colour = ramp * 16 + shade
```

`z` is the distance along the view axis, which is `-z` in view space because
the camera looks down -Z. The shade is computed per vertex, then
interpolated along the edge the way hardware interpolates a varying.

An object closer than `near` is clipped away. An object past `far` draws at
shade 15 rather than vanishing. A world that pops out of existence looks like
a bug, even when it is a feature.

## Lighting, when surfaces arrive

Eddie's call, 2026-09-10. One sun. Parallel rays, one direction, the same
everywhere in the world.

**That is a directional light rather than an ambient one, and the difference
shows.** Ambient light has no direction. Every surface takes the same amount,
so a shape reads as a flat silhouette. Parallel rays give a surface its shape,
because brightness follows the angle it makes with the light.

A small ambient floor rides along with it. Without one, a surface facing away
from the sun is pure black rather than dim.

**One sun fits in the camera record.** There is never a light table. It needs
a direction and a floor. That is seven of the fourteen bytes already
reserved there. `CMD_WORLD_MAP` keeps its two arguments, and the scene stays
a camera followed by objects.

**The palette layout does not move.** The low nibble still means how bright.
Distance decides it today and the sun would decide it then. Nothing about the
16 by 16 split has to change.

Nothing here is built. Lighting needs surfaces, and surfaces are the reserved
face range. This section exists so the reserved bytes have a shape, rather
than being a hole someone fills differently later.

## The pipeline, stage by stage

Each stage is named for the WebGL stage it stands in for. The two lists can
then be compared line by line.

1. **Vertex fetch.** Read the mesh's vertices out of world RAM as signed 16
   bit triples.
2. **Vertex stage.** Apply `MVP`, giving a four component clip position.
   `M` comes from the object's position, rotation and scale. `V` comes from
   the camera. `P` comes from the focal length, the near and far planes and
   the screen's aspect. The depth shade is computed here as a varying.
3. **Primitive assembly.** Read the edge indices and pair the vertices.
4. **Clip.** Against the near plane in homogeneous coordinates. An edge with
   both ends behind it is dropped, and one with a single end is split at the
   plane.
5. **Perspective divide.** x, y and z each divided by w.
6. **Viewport transform.** From -1..1 to the 256 by 256 screen.
7. **Rasterise.** One pixel wide lines, with the shade interpolated
   perspective correctly.
8. **Fragment stage.** The palette index is `ramp * 16 + shade`, written
   straight into VRAM.

Objects are drawn back to front by the depth of their origin, so nearer
wireframes overwrite farther ones. That is a painter's order and there is no
depth buffer.

**The transform maths is 16.16 fixed point.** Vertices cross the boundary as
signed 16 bit, because that is a native attribute format. Matrices stay 16.16
inside the GPU, which is the GPU's own business. Angles are 16 bit, and
`MODE_WORLD` builds its own 16.16 sine table at power on. The 8 bit ROM stays
where it is, for `MODE_GRAPHICS`. Two sine sources, two precisions, two jobs,
and the manual says which is which.

## The depth convention

WebGL clips z to -1..1 and WebGPU clips it to 0..1. Eddie asked that switching
later stay cheap, so the difference is isolated here rather than spread
through the renderer.

**It is one setting and three places.** The perspective matrix differs in its
third row alone. Nothing else in it changes:

```text
                 row 2 of the projection matrix
WebGL, -1..1     0  0  -(f+n)/(f-n)  -2fn/(f-n)
WebGPU, 0..1     0  0     -f/(f-n)     -fn/(f-n)
```

The clip test changes with it. WebGL rejects a vertex outside `-w <= z <= w`,
and WebGPU rejects one outside `0 <= z <= w`. The viewport's depth mapping
changes the same way, and this renderer has no depth buffer to map into.

**The depth cue is deliberately not one of those places.** The shade comes
from view space z. That is before the projection matrix is applied at all. It reads
the same under either convention. Taking it from the value after the divide
would have been the obvious shortcut. It would also have tied the picture to
one API.

**That gives a test rather than a promise.** Render a scene under both
conventions and compare the two framebuffers. They must be identical, because
x and y are untouched and nothing but clipping reads z. A change that leaks a projected z into the shading fails it the day it is
written. That is years before anyone tries the swap.

## Commands

| name | what |
|---|---|
| `CMD_WORLD_MAP` | point the scene at a data RAM base, with an object count |
| `CMD_MESH_LOAD` | copy geometry from the cartridge into world RAM, and define a mesh |
| `CMD_MESH_WRITE` | stream geometry bytes into world RAM for a mesh built at run time |
| `CMD_WORLD_RAMP` | fill one ramp's sixteen shades from a base colour |

`CMD_VIDEO_MODE` with `GPU_ARG` set to `MODE_WORLD` switches the mode, the way
`MODE_TEXT` already works. Map the scene before switching, or the first frame
renders whatever the mapped bytes happened to be.

## What it does not do

Named so nobody looks for them.

- No filled polygons, no shading and no textures. Wireframe with depth cued
  ramps is the whole look.
- No depth buffer. Wireframes overlap, and a painter's order by object is
  enough for that.
- No per-edge colour. An object is one ramp, and distance does the rest.
- No thick lines. The hardware reports a width range of 1 to 1. A thick line
  would have to be faked as triangles, and would not port.
- No object hierarchy. An object has a transform, not a parent. A jointed
  model is two or more objects the program moves together.
- No culling. Wireframe has no facing, so there is nothing to cull.

## The demo

A spinning pyramid, then a small world to fly through.

The pyramid is the advertisement for the scene living in data RAM. Its whole
frame loop is one 16 bit add, to the yaw field of one object record. The GPU
does everything else. Five vertices and eight edges.

The world adds objects at different distances, in different ramps. The depth
cue is then visible as something other than an assertion in a test.

## Testing

The software renderer is the reference implementation, so it is tested. The
hardware path, if it ever exists, is not.

- The pipeline stages are separable, so each is tested on its own. A vertex
  through a known `MVP`, an edge clipped against the near plane, a varying
  interpolated perspective correctly.
- The near plane clip is the one that earns its own tests. An edge entirely
  behind the camera, an edge crossing it, and a vertex exactly on it.
- The depth cue is arithmetic, so it is checked against the formula written
  above. Nearer than `near`, exactly `near`, exactly `far`, and past it.
- A whole frame is checked the way the Pac-Man tests check a maze. Count the
  lit pixels and check named ones. Assert the picture is what the geometry
  says rather than what a recording says.
- `MODE_GRAPHICS` must be unchanged. The golden bytes and the cube and star
  demos are the evidence.

## Settled

Eddie ruled on all four open questions, 2026-09-11. Three are built.

**A ramp fades toward the background.** That is palette entry 0. Eddie: "Yes
the background color (mostly black but can be anything at palette[0])." Entry 0
defaults to black. So every program written before the change renders the
same. It is read when `CMD_WORLD_RAMP` runs, not when the frame is drawn.

**`MODE_WORLD` carries the printf overlay.** The plane is already composited
over whatever was drawn. So the mode only ever lacked the call. Text mode is
still left out, because that mode IS characters.

**An object may name a matrix.** "Let's give matrices also an id."
`CMD_MATRIX_MAP` nominates a table in data RAM. Flag bit 1 says bytes 4 and 5
are a matrix id.

The elements are float64, which is forced rather than chosen. The ACP composes
the transform, and its types are I64, U64, F64 and C64. The CPU cannot build a
matrix at all. So the format the GPU reads has to be the format the ACP
writes. See `docs/gpu-world-ports.md`.

**The sun multiplies into the distance cue.** Decided, not built. There is
nothing to light yet: an edge has no normal and faces do not exist. The
multiply keeps all sixteen shades. Splitting the nibble would leave eight
distances and two light levels.

**The camera takes two matrices, a view and a projection.** Eddie recalled
that cameras and lights carry their own. His words: "Well it must be able to
use matrices for its calculations right?"

They are separate things. `GL_MODELVIEW` and `GL_PROJECTION` were separate
stacks, and a shadow-casting light will want both.

The view half lets the ACP aim the camera. It can then track a target the
three angles cannot name without an atan2 the CPU does not have.

The projection half makes an orthographic view possible at all. Before it,
`mat4Perspective` was the only projection this machine had.

## Still open

- How the object record grows past sixteen bytes, if it ever needs to. The
  growth path is a second mapped table indexed by the same id. Nothing needs
  it, and six free flag bits are room enough to add one later.
