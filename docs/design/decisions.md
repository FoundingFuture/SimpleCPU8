# Design decisions

Every design question that came up while building SimpleCPU-8, and how it was
settled. Each entry records the question, the decision, and the reason.
Eddie decided.

## Contents

- [Hex prefix over the 6502 hash](#hex-prefix-over-the-6502-hash)
- [The effective address adder stays stateless](#the-effective-address-adder-stays-stateless)
- [Fetch shown as an immutable preamble](#fetch-shown-as-an-immutable-preamble)
- [ALU input latches named A and B](#alu-input-latches-named-a-and-b)
- [Microcode row cap of 16](#microcode-row-cap-of-16)
- [Optimal microcode stays sealed](#optimal-microcode-stays-sealed)
- [Arrows as the data-flow language](#arrows-as-the-data-flow-language)
- [Editor parameter column gap](#editor-parameter-column-gap)
- [Flag display order N V Z C](#flag-display-order-n-v-z-c)
- [Fast-frame throttling](#fast-frame-throttling)
- [MAX speed](#max-speed)
- [Tracing off at speed](#tracing-off-at-speed)
- [Watch and edit microcode layouts](#watch-and-edit-microcode-layouts)
- [Reference docs welcome before they list](#reference-docs-welcome-before-they-list)
- [No shift instructions, no indexed stores](#no-shift-instructions-no-indexed-stores)
- [The magic box](#the-magic-box)
- [Input as a bus device](#input-as-a-bus-device)
- [Wrap instead of crash on RAM overflow](#wrap-instead-of-crash-on-ram-overflow)
- [High byte first, on every port](#high-byte-first-on-every-port)
- [Square brackets dereference, round brackets group](#square-brackets-dereference-round-brackets-group)
- [Breaking changes are free until release](#breaking-changes-are-free-until-release)
- [A migration is a one-off, not a feature](#a-migration-is-a-one-off-not-a-feature)
- [ACP, and why not FPU, MPU or NPU](#acp-and-why-not-fpu-mpu-or-npu)
- [The shape belongs in its own ports](#the-shape-belongs-in-its-own-ports)
- [Generic arithmetic here, the 3D chain in the GPU](#generic-arithmetic-here-the-3d-chain-in-the-gpu)
- [Deferred instructions](#deferred-instructions)

## Hex prefix over the 6502 hash

Question: how to write hex and immediate values.

Decision: $1C and 0x1C for hex, no prefix for immediates.

Reason: the 6502 uses # for an immediate, which collides with the reader's
intuition that # means a number. Using $ for hex and a bare number for an
immediate keeps LD A <- 5 and LD A <- $1C both obvious. The & prefix takes a
label's address.

## The effective address adder stays stateless

Question: should a memory pointer be a register that remembers, or an adder
that recomputes every cycle. Eddie expected a stateful pointer and read a bug
where none existed.

Decision: the EA adder is combinational and stores nothing. A word load leaves
no address behind because there is no address register.

Reason: a stateful address register would cost sixteen flip-flops to save one
wire, for zero cycles gained. The 8080 latched an address, the 6502
recomputed. SimpleCPU-8 sides with the 6502. The [D1]+ post-increment is the one
place a value persists. There the register D1 remembers, not the adder. This
decision produced the component reference feature and the live EA display, so
the model would teach itself. See docs/lessons.md for the full walkthrough.

## Fetch shown as an immutable preamble

Question: fetch is not row zero of an instruction. The machine fetches, then
dispatches, then runs rows. How should the UI show it.

Decision: show fetch as a grayed, checked line numbered 00 above every
instruction's rows. During the fetch stage, show fetch alone with no
instruction rows revealed.

Reason: physically the dispatch happens after fetch, so fetch cannot be row
zero. But a flow view that jumps straight from fetch to the first instruction
row loses the reader. The compromise is display truth. The drawing simplifies
by placing a labeled fetch line in context. The simulation keeps the honest
fetch-then-dispatch split. Reveal still follows execution: no instruction
row appears until the machine reaches it.

## ALU input latches named A and B

Question: what to call the two ALU input latches. A candidate was LATCH_ACC
and LATCH_MEM.

Decision: A and B, loaded by ACC_TO_A, IMM_TO_B, RAM_TO_B, STK_TO_B.

Reason: LATCH_MEM would lie. The B latch takes an immediate and a stack byte
as well as a memory byte. Naming the latches A and B and naming the loaders by
source keeps the signal names truthful.

## Microcode row cap of 16

Question: how many microcode rows per instruction.

Decision: 16.

Reason: Eddie first said 12, then corrected to 16. The editor gutter numbers
01 to 16 and turns red past the cap. Sixteen rows is enough for the widest
instruction with headroom for a student's unoptimized version.

## Optimal microcode stays sealed

Question: should the optimal microcode be visible.

Decision: it can be selected and raced, never read, stepped, exported, or
saved in a project.

Reason: the optimization game is the point. Seeing the answer removes the
challenge. Selecting optimal disables the microcode editor, the microstep
button, and the datapath view. Project files carry naive or the user's own
custom set, never optimal.

## Arrows as the data-flow language

Question: LD reads both directions with an arrow. Should OUT and IN.

Decision: OUT A -> port and OUT port <- A assemble to OUTA. IN port -> A and
IN A <- port assemble to INB, the byte read. IN port -> D1 or D2 assembles to
INW, the word read.

Reason: the arrow shows data flow, and OUT A -> GPU_COLOR reads as exactly
what happens. Extending the arrow to every value-moving instruction keeps the
language consistent. The target picks the width, so IN alone works like PUSH
picking PUSHB or PUSHW. The source of an arrow OUT must be A. An arrow IN reads
into A, D1, or D2. Anything else is a parse error.

## A random port on the GPU

Question: how a program gets a random number.

Decision: a dedicated read port, GPU_RAND, not a command. Reading it returns a
byte and advances a deterministic xorshift. Writing it reseeds. INW reads a word
from it in two reads.

Reason: a port read is one instruction, so no command dance for a common need.
Determinism is the machine's rule, so the generator is a seeded PRNG, not
entropy. The same program gives the same stream, and a program reseeds from
GPU_FRAME or a key press to vary it.

## Editor parameter column gap

Question: how far right the parameter column sits.

Decision: mnemonic column plus the longest mnemonic plus one space. Column 14.

Reason: Eddie first specified plus four, then changed to plus one for a
tighter listing. The formatting is display only. The stored source keeps what
was typed. Tab and Space both jump to the column.

## Flag display order N V Z C

Question: what order to show the four flags, and how to show cleared ones.

Decision: always all four letters in the order N V Z C. Grey when clear,
accent green when set.

Reason: N V Z C is the classic status-register order. Eddie asked for O for
overflow first, then agreed on V. V is overflow's canonical letter and matches
the JV mnemonic. Showing all four always, with color for state, beats showing
only the set ones. The field never changes width.

## Fast-frame throttling

Question: a latched Frame button that makes GPU_FRAME reads return fresh
frames, so poll loops fall through. At what speeds should it apply.

Decision: a GPU_FRAME read forces the counter forward, rationed to one forced
frame per 120ms, about the real device rate. The latch is ignored above 1000
instructions per second.

Reason: the first design ignored the latch below 60 instructions per second,
which was backwards. It disabled the aid exactly where the waits hurt. A
GPU_FRAME poll at 1000 instructions per second can take 12 seconds, and Eddie
found that intolerable. Rationing by wall clock instead of by a speed
threshold fixes every speed at once. Slow speeds force on every poll. Fast
speeds get real frames on their own. The switch survives Restart and load
because it belongs to the person, not the machine. This is the one sanctioned
lie in a machine that otherwise never lies, and it sits behind a lit button.

## MAX speed

Question: a speed that does not wait at all.

Decision: a MAX option that counts no instructions. It runs the core flat out
on a 24ms wall budget per 33ms display tick. About a million instructions per
second.

Reason: every other speed paces by instruction count. MAX paces by wall clock,
so the display updates while the core runs as fast as it can. It replaced a
stale option labeled fast budgeted, a leftover from before the speed ladder.

## Tracing off at speed

Question: the machine records what it did on every microcycle. Six panels
draw from that record. At speeds too fast to follow, is the record worth its
cost?

Decision: no. Tracing is off at 30 device frames per second and above, and at
MAX. The machine writes no bus event and builds no microcycle record. It
stamps no RAM heat. The worker sends a five field snapshot instead of a full
one. Every panel drawn from a recording goes blank. The screen keeps drawing,
and so do the status badge, the two counters and the measured frame rate. The
line is TRACE_OFF_FPS in packages/ui/src/trace.ts, written in one place.
Moving that number moves the line.

Eddie: "I think every mode from 30fps and above we should stop tracing CPU
functionality. Just turn it off completely. No stack, No source tracing.
Nothing. We only run the code and we display what needs to be displayed. At
that speed you can't follow it anyway, it is too fast."

He then gave the reason to prefer. "We want all available CPU to go to
executing the program itself not updating the CPU or tracing the program."

Reason: the cost is a record object and an opcode name lookup per microcycle.
Add a timestamp write per RAM access. Add a full RAM copy and two 256KB
timestamp maps per display tick. Measured on the invaders demo, the core runs
about 11.6 million cycles per second traced. Untraced it runs about 14.3
million. At MAX that is about a quarter more program inside the same wall
clock budget. At a frame locked rate it hands back about a fifth of each
frame. That is headroom on a slower host. Nothing the program can observe
changes. A core test runs one program both ways and compares RAM, stack,
flags, cycles and instruction count.

Blank panels rather than frozen ones, on Eddie's call.

"We can make all panels blank to show they are hidden."

A frozen panel reads as live, which would be the machine lying about its own
state. The panels blank once, on the way into
an untraced run, never per tick. Pausing, stepping or dropping to a traced
speed turns the recording back on, and the next snapshot repaints. Breakpoints,
halts and crashes never read a recording. They still stop the machine at any
speed.

## Watch and edit microcode layouts

Question: the microcode editor was always visible below the flow view. The
flow view jumped up and down as instructions of different lengths ran.

Decision: two layouts. Watching shows the flow view at a fixed height with
room for all 16 rows, editor hidden behind an Edit button. Editing collapses
the flow view and shows the editor. Running forces watching.

Reason: a running CPU is for watching, so the editor folds away and gives the
room to the flow. The fixed height stops the jumping. Editing is a paused,
deliberate act, reachable by one button.

## Reference docs welcome before they list

Question: the manual sections read like terse reference pages with no
introduction.

Decision: every section opens with a welcoming orientation paragraph, then the
glanceable reference below it.

Reason: this is a learning tool. A student meeting assembly for the first time
needs a door before a table. The rule is welcome first, list second, answers
never buried in prose. Flag facts stay in a table so is-carry-affected is one
glance, never a paragraph to read.

## No shift instructions, no indexed stores

Question: the ISA lacks shifts and indexed stores. Should they be added.

Decision: no. Programs use lookup tables through [D1+A] and pointer walks
through INC D1 instead.

Reason: hardware must pay rent, and additions must beg. The hanoi and circle
demos show the workarounds stay readable and teach real technique. Shifts and
indexed addressing have not justified their cost yet.

## The magic box

Question: an 8 bit CPU with no multiply cannot do real graphics or 3D math on
its own. How should the project handle heavy work.

Decision: the CPU stays small and honest. Heavy work goes to magic
peripherals. The GPU draws graphics in one cycle. Future coprocessors do
matrices and floats the same way.

Reason: the goal is to teach the basics of a CPU. A student must see fetch,
decode, execute, and microcode clearly. That means the CPU stays plain. The
fun comes from the results, so graphics and math offload to a box that does
them at once. That offload is a deliberate cheat, stated as one. It keeps the
CPU teachable and the output fun. This is why the GPU is honest about the
CPU's time even though its own drawing is free. One frame is 65536 cycles. The
roadmap in docs/roadmap.md follows this rule for the matrix unit, the vector
processor, and the FPU.

## Input as a bus device

Question: the machine had no inputs, so no interactive programs. How to add a
keyboard.

Decision: a controller device on the IO bus with two ports. IO_CONTROLLER is
one byte of seven game buttons. IO_KEY pops key events from a device buffer,
each a 7 bit code with a release bit.

Reason: input is a peripheral, so it belongs on the bus beside the GPU. The
GPU forwards ports it does not claim, so the controller drops in as its
fallback with no core change. Two ports serve two needs. The packed button
byte suits games, read in one IN and tested with AND. The buffered key poll
suits text and any key. The buffer lives on the device, so fast typing
survives a slow program. Buttons and buffer are hardware state, so they
outlive load and restart.

## Audio on a real-time clock

Question: the GPU frame counter ticks on cycles, never wall-clock time. Should
the audio chip follow that rule, or play in real time.

Decision: the APU renders in real time on its own clock, decoupled from CPU
cycles. This is open for Eddie's veto. See docs/audio-design.md.

Reason: audio is real time by nature. A tune has a tempo in seconds. A sound
effect must play at the right pitch the instant a game fires it. Tying the
sample rate to cycles would slow music at slow speeds and raise its pitch at
MAX. The chip is a peripheral, so its own work is free and the CPU clock stays
honest. The GPU is a picture you scrub at any speed. The APU is a tape that
plays now. The two devices answer different questions, so they use different
clocks.

## Wrap instead of crash on RAM overflow

Question: data RAM grew from 2048 bytes to the full 64KB the CPU can already
address. Should an effective address past the top still crash, or wrap.

Decision: it wraps at 16 bits. This retires the illegal-data-address crash.
No data address is illegal any more. See docs/memory-map-design.md.

Reason: Eddie's stated position: "0-65535 is all legal and when unexpected
things happen the more fun it is."

This is a real loss and worth stating. A program that walks a pointer off
the end used to stop with a clear message. It now quietly reads address 0
instead. The trade is that the machine stops pretending an address it can
build is one it refuses to use.

## High byte first, on every port

Question: a peripheral that takes a 16 bit value receives it as two writes.
Which half goes first.

Decision: the high byte. A cartridge address is bank, high, low. A RAM
address or a length is high, low.

Reason: the CPU is big-endian everywhere. A `dw` stores high first, a word
load fills a D register high first, and printf pushes its parameters high
first. The GPU's cartridge latches were already written that way by every
demo. The audio chip was the one exception, reading its sample length as
`(arg2 << 8) | arg`. So a program wrote the low byte first there, and the
high byte first everywhere else.

Eddie, on finding the mix: "I don't like the mixing of hi+lo lo+hi we must
make up our mind. This CPU is big endian right? So that should also be the
order to pass in addresses."

The audio chip changed rather than the rest, because one exception is
cheaper to move than three conventions. The cost was three demo sources and
the generator that writes one of them.

## Square brackets dereference, round brackets group

Question: the assembler is about to gain constant expressions, and expressions
need a grouping bracket. Round brackets were already the dereference. Which
bracket gives way.

Decision: dereferencing moves to `[ ]`. Round brackets keep a call's argument
and take arithmetic grouping, which landed the same day.

Reason: Eddie, 2026-09-10:

```text
maybe [] is the better notation for dereferencing [5] means content of
address 5 [A] means the contents of the memory location pointed to by [A].
We now have a lot of code to rewrite and a lot of documentation but to be
honest I think we have to do this. The arithmetic grouping really is ().
```

The convention agrees. Intel and ARM both write memory as `[bx]` and `[r0]`.
Only the 6502 family writes indirection as `(addr)`, and this machine is not a
6502. Every language a student already knows groups arithmetic with `( )`.
Taking that bracket for addressing would have read wrong forever after.

The cost was 1,922 rewritten sites in the programs and 449 in the
documentation. All of it mechanical, and every program still assembles to the
byte it did before. Doing it after the expression work would have cost the
same rewrite plus the unlearning.

Round brackets group, as of the same day. The evaluator is expr.ts. A round
group carrying an operator is arithmetic. One holding an addressing shape is
the old spelling and is refused. On LD and the ALU mnemonics that error
names the square form. A port operand or a jump target never reaches the
operand parser, so there it is a different error.

## Breaking changes are free until release

Asked, 2026-09-10, when dereferencing moved from `(D1)` to `[D1]`. A plan had
grown a fifth task, to migrate project zips saved under the old spelling.

Dropped. Eddie:

```text
THERE IS NO OLD CODE OUT THERE IN ZIPS. WE NEVER RELEASED THIS PROGRAM TO
THE WORLD SO WE CAN STILL DO BREAKING CHANGES LIKE THIS. Once we do release
this we can't do those breaking changes anymore. That's why we are holding
back the release until we are completely satisfied with this tool.
```

Nothing is deployed. No file exists that the change would break. A migration
would serve a hypothetical reader, and leave a compatibility path in the code
for good.

**The rule this sets.** Weigh a design on whether it is right. What the move
costs is not the measure. Getting the interface correct now is worth more than
any amount of smoothing later. Later is the one thing that cannot be undone.

**And the rule afterwards.** The day this ships, four things become promises.
Those are the port numbers, the syntax, the file formats and the microcode
section names. That is why the release waits for a finished tool, not a
working one.

## A migration is a one-off, not a feature

Asked, 2026-09-10, right after the bracket migration shipped. Eddie loaded the
app and saw `(A)` in the editor. The cause was not the code.

`main.ts` restores the autosaved source from browser storage on every load. A
session saved before the migration comes back in the old spelling, and then
fails to assemble. That is a real file on a real machine, unlike the project
zips the migration task was dropped for.

Decision: do not convert it. Eddie: "The conversion logic should not be part
of the product. It was a one off."

The rule this sets. A migration is work done once, by hand or by a script.
The script is committed and then never runs again. Shipping it puts a
permanent reader for a dead format into the product. Every future reader then
has to understand a spelling that no longer exists. The code that translates
it can never be removed either, because nobody can prove the last old file is
gone.

`scripts/tobrackets.mjs` is the right shape. It ran once, its diff is in the
history, and the product knows nothing about it. The assembler's refusal
message is the whole support the old spelling gets, and it names the fix.

## ACP, and why not FPU, MPU or NPU

Asked, 2026-09-10, when the third magic box needed a name. Eddie: "A FPU is
not correct because ours does integer arithmetic as well. ACP (Arithmetic
Co-Processor)?"

Decision: ACP, and one prefix for everything it owns.

Two alternatives were weighed and dropped. MPU reads naturally beside GPU and
APU. It has also meant microprocessor unit since the 6502. In a project about
the CPU, a student would ask whether the MPU is the CPU.
NPU is the historically correct term. Intel called the 8087 a numeric
processing unit, and that chip is close to this one. Today NPU reads as neural
processing unit and would teach the wrong thing.

The prefix matters as much as the name. An earlier draft used `CO_` for ports
and `COPRO_` for flags. That is two spellings for one device, where the GPU
and the APU each use one. `ACP_` covers the ports, the types, the commands and
the flags. That leaves the plain word coprocessor free as the category word, which
the vector processor on the roadmap will need.

One collision came out of it and is worth recording. The spec named the
complex angle `ACP_ARG`, which is also the port carrying a table's entry
count. The machine calls the command `ACP_ARG_OF`. A coverage test now compares the
spec's names against the machine's tables, in both directions.

## The shape belongs in its own ports

Asked, 2026-09-10. The first draft gave `ACP_FMT` ten values: three scalar
types, three vector lengths, three matrix sizes and complex.

Eddie: "The ACP has generic matrix calculations of any type and of any size."

Decision: `ACP_FMT` carries the element type alone. The shape moved to
`ACP_ROWS`, `ACP_COLS` and `ACP_COLS_B`.

This is better than the draft rather than different from it. Ten values collapse into
four types and two dimensions. A scalar is 1 by 1 and a vector of N is N by 1,
so no separate vector type is needed. Every element-wise command then works
over any shape, so one opcode adds two numbers or two 4 by 4 matrices. A
matrix product works for any shapes that agree, at any size RAM holds. A fixed
list of sizes cannot express that at all.

The cost is that a block's layout depends on the command as well as the type.
A program therefore computes three offsets. Constant expressions had landed the day
before, which is what makes `BLOCK + 8*R*C` one line instead of three
hand-worked numbers.

## Generic arithmetic here, the 3D chain in the GPU

Asked, 2026-09-10, when the ACP's matrix work raised the question of where a
3D pipeline belongs.

Eddie: "I want the GPU to have matrix calculations specific to 3D modelling
and rendering. Optimized for just that. The ACP has generic matrix
calculations of any type and of any size."

Decision: two devices, two jobs. The ACP does generic mathematics at double
precision, of any type and any size. The GPU takes the 3D chain. That is the
model, view and projection matrices, the matrix stack, the perspective divide
and the viewport. It is built for that one chain.

The split has a natural handoff. The ACP composes a transform once per object
in float64, and the GPU applies it per vertex at speed. Neither device has to
be good at the other's job.

## The GPU's 3D world

Eddie, 2026-09-10, asked for two things at once. The first, in his words:
"I want simple programs like cube still be possible".

The second was the offload: "setup a 3D world in the GPU and the GPU can
render that directly". The main program updates the camera every frame.

Decision: MODE_WORLD, a third video mode. MODE_GRAPHICS is untouched, so the
cube and the star demos still draw the way they always did. The new mode is
additional, never a replacement.

Eddie, on the backend: "Let's try without and use that for our testing and
rendering. The only thing we have to do: make sure our model and software
renderer is compatible with the hardware renderer."

Decision: a software renderer, written to a graphics card's conventions.
Right handed, Y up, the camera down -Z, row major, w equal to -z. Clip before
the divide. Interpolate a varying by carrying it over w. Keep the viewport
out of the projection matrix. Every one of those is what hardware does. A WebGL
or WebGPU backend later needs no transposing and no sign flipping.

Eddie: "Make sure we can switch to WebGPU later." So the depth range is a
parameter. WebGL clips z to -1 to 1 and WebGPU clips it to 0 to 1. The two
differ in the projection's third row and nothing else.

Eddie: "The id can not be bigger than 2 bytes." So an object id is two bytes
and stays two bytes. More per-object data goes in a second mapped table
indexed by the same id, never in a wider record.

Eddie: "Lets just use ambient lighting for now. Like the sun. Parallel rays
of light illuminating the whole world equally." The camera record reserves
the sun direction and an ambient floor. They are zero today. Faces are not
stored, and an edge list cannot be turned back into faces. So the mesh record
reserves a face range now rather than later.

The palette layout was the part Eddie asked to be clear to the user. The high
nibble is the ramp and the low nibble is the shade. Sixteen ramps of sixteen
shades fills all 256 entries exactly, with nothing left to explain.

## The four world-mode rulings

Eddie, 2026-09-11, answering the four questions the world design left open.
His words, then what was built.

"Yes the background color (mostly black but can be anything at palette[0])."

A world ramp fades toward palette entry 0 rather than toward black. Entry 0 is
read when `CMD_WORLD_RAMP` runs. It defaults to black, so nothing written
before the change looks different.

On the printf overlay in `MODE_WORLD`: "YES."

On whether an object may name a matrix: "YES. let's give matrices also an id."

`CMD_MATRIX_MAP` nominates a table in data RAM, the same shape as
`CMD_WORLD_MAP`. Flag bit 1 on an object says bytes 4 and 5 are a matrix id.

The element type is float64, which is forced rather than chosen. The ACP is
the only device here that can compose a transform, and it writes data RAM. Its
types are I64, U64, F64 and C64, with no 16.16. The CPU cannot build a matrix
at all, having no multiply. So the format the GPU reads has to be the format
the ACP writes.

On how the sun and the distance cue share one shade: "multiply."

Recorded, not built. An edge has no normal and faces do not exist yet, so
there is nothing to light. The multiply keeps all sixteen shades. Splitting
the nibble would leave eight distances and two light levels.

## The camera's two matrices

Eddie, 2026-09-11, told the camera could not name a matrix:

"If I remember correctly from long ago certain things like lights and camera's
have their own matrices."

He remembered right, and it widened the design. Fixed-function OpenGL kept
`GL_MODELVIEW` and `GL_PROJECTION` as separate stacks. A shadow-casting light
has a view and a projection of its own, because the scene is rendered from it.

Decision: the camera takes TWO matrix ids, not one. A view at bytes 26 and 27,
a projection at 28 and 29, behind two flag bits in byte 25.

The view half was the proposal. The projection half is what the memory added,
and it is the half that unlocks something. The GPU built its projection from a
focal length and nothing else. That made a perspective the only projection
this machine had. An orthographic view was impossible. Now it is a matrix like
any other.

The supplied matrix is the VIEW matrix, not the camera's world transform. That
is what a graphics card takes, so the GPU uses it as given. Anyone holding a
camera transform runs `ACP_INVERSE`.

A light, when it comes, gets the same treatment and needs no new mechanism.

## Jumps on a clear flag

The machine had four flags and one jump for each, on the set state. A test
that went the other way was a jump over a jump: `JZ done` and then `JMP
loop`. That pair was 149 of the 447 conditional jumps in the demos. It was
132 of the 1003 in the BASIC interpreter. It costs 5 naive cycles and 4 optimal
where one jump costs 3 and 2.

So the machine gained JNZ, JNC, JP and JNV, one for each flag's clear state.
They cost one decode each and four signals of the same shape as the set
jumps. The datapath gains nothing: the flag is read, inverted, and gates
the same PC load. The hardware pays its rent in every loop.

The null pointer test came with them and needed no hardware. A word load
into D1 or D2 already set Z from all sixteen bits. So `LD D2 <- [p]` and a
jump is the test. The C compiler now uses it for every word and pointer
condition. It also compiles a condition straight to jumps on the flags of
its subtract. No condition is turned into a 0 or 1 first. The BASIC
interpreter came out a fifth smaller. A loop benchmark ran in 30% fewer
cycles.

## Deferred instructions

Each of these waits behind the same test, must justify its hardware:

- DEC for D registers. Walk pointers forward or reload instead.
- CMP. Subtract and read flags instead.
- Indirect ALU operands.
- A second byte register.
- D to A transfers.
- LDS.

Spec v2 also carries 12 assumed defaults still open for Eddie to veto.
