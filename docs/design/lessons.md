# Worked lessons

Longer explanations that came out of building SimpleCPU-8. Each one started as a
question and ended as a demo or a feature. These are the teaching pieces the
project is built to deliver.

## Contents

- [The stateless effective address adder](#the-stateless-effective-address-adder)
- [Word loads and the missing address register](#word-loads-and-the-missing-address-register)
- [Frame timing and the poll loop](#frame-timing-and-the-poll-loop)
- [The sine table and turns](#the-sine-table-and-turns)
- [Circle accuracy and the quarter-wave fold](#circle-accuracy-and-the-quarter-wave-fold)
- [Recursion on the hardware stack](#recursion-on-the-hardware-stack)
- [Working without shifts or indexed stores](#working-without-shifts-or-indexed-stores)

## The stateless effective address adder

EA means effective address, the address that takes effect on RAM this cycle,
after the addressing math. The adder is combinational. It computes base plus
optional offset plus optional carry-in, fresh, every cycle, and stores
nothing.

- Base, exactly one signal: ADDR_OP8, ADDR_OP16, ADDR_D1, ADDR_D2, ADDR_A.
- Offset, optional: EA_OFF_OP8 or EA_OFF_A.
- Carry-in, optional: EA_CIN adds one, this cycle only.

EA_CIN alone does nothing. With no base selected, nothing addresses RAM. It
must ride with a base select. That pairing is legal cooperation, not a
conflict. The component reference feature came from exactly this confusion.
A yellow EA_CIN signal seemed to do nothing on its own.

The lesson holds for the whole machine. Registers remember. The adder forgets.
Next cycle it recomputes from whatever its inputs say then.

## Word loads and the missing address register

Loading D1 from a two byte word looks like it should leave an address behind.
It does not, because there is no address register.

Trace LD D1 <- [1] on a table where address 1 holds the high byte:

```asm
; row 01: RAM_TO_D1H, ADDR_OP16   -> EA = 1, read high byte into D1 high
; row 02: RAM_TO_D1L, ADDR_OP16, EA_CIN -> EA = 2, read low byte into D1 low
```

Row 02 selects the same base again and adds one with EA_CIN. It does not carry
an address forward from row 01. Nothing does. Address 3 is never read, because
only two bytes were wanted. Between the two rows, D1 is half loaded, and the
listing shows it. That is correct, not a bug.

Eddie expected the machine to remember the pointer between cycles. He read a
bug where none existed. The fix was not to change the machine. It was to make
the model teach itself. Hover any datapath box for what it does. Click it for
its inputs and outputs. Read the live EA value recomputed each cycle.

## Frame timing and the poll loop

One GPU frame is 65536 cycles, which is 2 to the 16th, so the frame counter
is the cycle counter shifted right 16. One free-running divider, no extra
hardware.

At slow speeds a real frame almost never arrives. A typical instruction is
about 5 to 6 cycles, so at 2 instructions per second a frame takes roughly
1.6 hours. At 1000 instructions per second it still takes about 12 seconds.
This is why animation loops that poll GPU_FRAME are painful to watch.

Two features answer this. The Frame button runs to the next frame tick in one
click. Fast-frame mode, latched by a double click, makes each GPU_FRAME read
return a fresh frame so the poll falls through. See docs/decisions.md for the
throttling design.

## The sine table and turns

The assembler takes floats in db and dw. A bare float 0..1 scales to the full
width and saturates at the top. sin(t) and cos(t) take turns, one full circle
per 1.0, matching the GPU's 256 steps per turn. The byte encoding is centered:

```text
byte = 128 + round(127 * sin(2 * pi * t))
```

This encoding has a property that matters. The mirror of a byte is exact.
256 minus the byte equals the byte for the negated angle, because 128 plus r
mirrors to 128 minus r. Negating a sample is one subtraction from zero, exact,
not approximate. This is what makes quarter-wave folding lossless.

## Circle accuracy and the quarter-wave fold

The question started as: is the 8 bit sine table good enough to draw a circle
at 256x256. The answer arrived in three demos, all under the circle demo key.

First finding: the amplitude is pixel-exact at full size. An 8 bit sample
lands on the right pixel. What limits a dotted circle is angular density, the
number of points around the ring, not the bits per sample.

The fold. Store one quadrant of sine, 65 bytes for angles 0 to 64 of 256.
Infer the rest by symmetry:

- Second quadrant: sin(a) equals sin(128 minus a).
- Lower half: sin(a plus 128) equals minus sin(a), done with the exact
  negation above.
- Cosine: cos(a) equals sin(a plus 64), so one table serves both axes.

Eddie's accuracy point, drawn as one circle split down the middle. The right
half plots 256 steps over the full turn. The dots sit about 3 pixels apart and
the ring shows gaps. The left half maps the same 256 entries over a single
quarter turn, so symmetry yields 1024 steps around the circle. The dots sit
under a pixel apart and the ring is solid. Same byte budget per axis, four
times the angular density.

One implementation wrinkle. The quarter table has 65 entries because angle 64
is the quarter point where sine equals 1. Index 256 does not fit a byte, so
the two quarter points are answered as constants rather than a lookup.

A closing fact. Connect the 256 points with lines instead of plotting dots.
The polygon edges are then about 0.01 pixels deep at radius 127, already
invisible. Visible edges from line drawing start below about 24 points. The
fold matters for dot density and table budget, not for line-drawn shapes.

## Recursion on the hardware stack

The hanoi demo solves Towers of Hanoi with the machine's own stack. The
recursive call hanoi(n, from, to, via) keeps its arguments in the zero page.
It saves them across each nested call with four PUSHB, restoring with four
POPB on the way back. The stack strip in the UI deepens and unwinds.

This is the machine's stack solving a stack puzzle, which is why it was chosen
over a fractal tree. It also demonstrates the workarounds below, since the
ISA has no multiply and no indexed stores.

## Working without shifts or indexed stores

The ISA has no shift instructions and no indexed store. Two idioms cover the
gap, both shown in the hanoi and circle demos.

Table lookup by value: put a table in the zero page and index it with the
[D1+A] addressing mode. This replaces both multiply-by-constant, through a
precomputed table, and array indexing for reads.

Pointer walk for stores: to reach element A of an array, load the base pointer
into D1. Step it A times with INC D1, then store through [D1]. This replaces
the missing indexed store.

Both idioms keep the ISA small while leaving real programs writable. Hardware
must pay rent, and these idioms are the rent the missing instructions would
have charged.
