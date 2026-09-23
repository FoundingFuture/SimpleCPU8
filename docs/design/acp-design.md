# The ACP, the arithmetic coprocessor

Eddie's design, 2026-09-09, widened 2026-09-10. The third magic box, beside
the GPU and the audio chip. The CPU stays 8 bit and honest. This device does
the arithmetic it cannot.

## Contents

- [Why it exists](#why-it-exists)
- [The name](#the-name)
- [The block](#the-block)
- [The ports](#the-ports)
- [Formats and shapes](#formats-and-shapes)
- [Scalar and element-wise operations](#scalar-and-element-wise-operations)
- [Complex operations](#complex-operations)
- [Vector operations](#vector-operations)
- [Matrix operations](#matrix-operations)
- [Tables](#tables)
- [Bit manipulation](#bit-manipulation)
- [Flags](#flags)
- [What it does not do](#what-it-does-not-do)
- [What it changes on the roadmap](#what-it-changes-on-the-roadmap)
- [Testing](#testing)
- [Still open](#still-open)

## Why it exists

The CPU has no multiply, no divide and no shift. It can still do both, and the
demos prove it. `x + x` is a left shift, with the top bit landing in carry.
`ADC A <- x` after `LD A <- x` is a rotate left through carry. Shift and add
gives multiplication. Shift and subtract gives division.

That is the lesson and it stays. The ACP is for the work where the lesson has
been learned and the cost is now in the way. A 64 bit multiply in software is
64 iterations of a carry chain across eight bytes. A 4 by 4 matrix product is
64 multiplies and 48 adds on top of that. Here each is one command.

Same pattern as the GPU and the audio chip. The offload is a deliberate cheat
and it is stated as one.

## The name

ACP, for arithmetic coprocessor. Eddie's call, 2026-09-10. FPU is wrong,
because this device does integers too.

Two names were weighed and dropped. MPU reads naturally beside GPU and APU.
It has also meant microprocessor unit since the 6502. In a project about the
CPU, a student would ask whether the MPU is the CPU.
NPU is the historically correct term. Intel called the 8087 a numeric
processing unit, and that chip is close to this one. Today NPU reads as neural
processing unit and would teach the wrong thing.

One prefix, `ACP_`, for the ports, the formats, the commands and the flags.
The GPU and the APU each use one prefix, and this device does the same. That
leaves the plain word coprocessor free as the category word. The vector
processor on the roadmap is a coprocessor too.

## The block

One command reads two operands and writes one result, through a block of data
RAM the program nominates. The block is three regions in a row:

```text
offset      size      what
0           A         operand A
A           B         operand B
A + B       R         the result
```

A region is `rows * columns * element` bytes. The element size comes from the
format. The shapes come from the operation. Every table below carries a shapes
column, and that column is the whole story.

**A dash in the B column means B takes no room.** The result starts where B
would have. `ACP_NEG` on a 1 by 1 integer writes its answer at offset 8, not
16. Reserving an unread region would be easier to memorise. It would also
waste RAM on every unary command, and the shapes column already says where
everything sits.

**The program computes the offsets, and the assembler folds them.** With `R`
and `C` defined as constants, `BLOCK + 8*R*C` is one expression. The three
regions are then named once each. That is what constant expressions were built for. It is
also why the regions pack tight rather than padding to a common size.

Big-endian throughout, like every other word on this machine.

The block may sit anywhere in the 64K of data RAM. Nothing stops two blocks
overlapping, and nothing stops a result region overlapping an operand. The
device reads both operands before it writes anything, so an overlap is defined
rather than undefined.

**The ACP writes data RAM.** It is the first device here that does. The GPU
reads data RAM for text mode and writes none of it. So the session wires the
ACP to the machine's RAM the way it wires the GPU. The write path is new. A
block that overlaps a mapped text screen writes to the screen. No data address
is illegal here, and that rule holds for this device.

The whole operation happens in one cycle, like a GPU command. The result is in
RAM by the time the next instruction runs. A 4 by 4 matrix product in one
cycle is a larger cheat than any the GPU makes. It is the same cheat.

## The ports

Twelve ports, one of them a read. The device claims `$40` to `$4F`, so four
are spare.

| port | name | what |
|---|---|---|
| `$40` | `ACP_ADDR_HI` | block address, high byte |
| `$41` | `ACP_ADDR_LO` | block address, low byte |
| `$42` | `ACP_FMT` | the operand element type. Writing it sets the result type to match |
| `$43` | `ACP_RFMT` | the result element type, when it differs |
| `$44` | `ACP_CMD` | the operation. Writing it runs the operation |
| `$45` | `ACP_FLAGS` | read: how the last operation went |
| `$46` | `ACP_ROWS` | rows in A |
| `$47` | `ACP_COLS` | columns in A |
| `$48` | `ACP_COLS_B` | columns in B. Only `ACP_MATMUL` reads it |
| `$49` | `ACP_ARG` | a count, high byte |
| `$4A` | `ACP_ARG2` | a count, low byte |
| `$4B` | `ACP_FUNC` | which function a table samples |

Each device owns a block of sixteen:

| range | device |
|---|---|
| `$00` to `$1F` | the GPU, which needs the room and uses it |
| `$20` to `$2F` | input. Two ports today, fourteen spare |
| `$30` to `$3F` | the audio chip |
| `$40` to `$4F` | this device |

Eddie's call, 2026-09-09. The first draft put this at `$22`. That walled the
controller in at two ports, with no room for a second pad.

**Writing `ACP_FMT` sets both types.** The common case is one type in and the
same type out. Two ports and a default make that one `OUT`, and the conversion
case two. Order matters. Write `ACP_FMT` first, then `ACP_RFMT` if the result
differs.

**The function gets its own port rather than sharing `ACP_ARG2`.** An earlier
draft spent that port twice. It carried a table's function and the low byte of
its count. A count is genuinely 16 bit, since a 256 entry table is the small
case. Capping tables at 255 entries to save a port would economise on the
wrong thing.

The address, both types and all three dimensions latch. They hold until
written again. A program working one block in one shape sets them once, then
writes only `ACP_CMD`.

`ACP_CMD` is written last and triggers the work. That is the GPU's idiom and
it is deliberate. Arguments first, verb last.

## Formats and shapes

`ACP_FMT` and `ACP_RFMT` carry an ELEMENT type and nothing else. The shape is
the dimension ports' job.

| name | value | what | bytes |
|---|---|---|---|
| `ACP_I64` | 0 | signed 64 bit integer | 8 |
| `ACP_U64` | 1 | unsigned 64 bit integer | 8 |
| `ACP_F64` | 2 | 64 bit float | 8 |
| `ACP_C64` | 3 | complex, two 64 bit floats, real first | 16 |

**A scalar is 1 by 1. A vector of N is N by 1.** There is no separate scalar
format and no vector format. A matrix with one row or one column already is
one. Ten format values collapse into four types and two dimensions.
Matrix multiply then works for any shapes that agree, which a fixed enum
cannot express. Eddie's call, 2026-09-10: generic, of any type and of any
size.

Power-on shape is 1 by 1, so a program doing scalar arithmetic never touches
the dimension ports.

A dimension of zero is `ACP_BADDIM`. So is any shape an operation cannot use,
and each row below says which shapes it takes.

**The one widening case.** A 1 by 1 integer multiply writes 16 bytes. Two 64
bit integers multiply to 128 bits exactly, and nothing narrower holds it.
Every other result element is the size its type says. A matrix of 128 bit
products was considered and dropped. It doubles the result region for a case
nobody asked for, and the shapes column would need a second exception.

Row major, for three reasons. C is row major. The printf code already reads
that way. A row is then contiguous, like every other array here.

## Scalar and element-wise operations

Every one works element by element, over whatever shape the dimension ports
give. The same opcode adds two numbers or two 4 by 4 matrices.

In the shapes column, `r` and `c` are `ACP_ROWS` and `ACP_COLS`, and `n` is
`ACP_COLS_B`. A dash means the operand is not read.

| op | A | B | R | what |
|---|---|---|---|---|
| `ACP_ADD` | r*c | r*c | r*c | A plus B |
| `ACP_SUB` | r*c | r*c | r*c | A minus B |
| `ACP_MUL` | r*c | r*c | r*c | element by element, never the matrix product |
| `ACP_DIV` | r*c | r*c | r*c | A divided by B |
| `ACP_REM` | r*c | r*c | r*c | the remainder of A divided by B |
| `ACP_NEG` | r*c | - | r*c | minus A |
| `ACP_ABS` | r*c | - | r*c | the magnitude |
| `ACP_CMP` | r*c | r*c | r*c | minus one, zero or one, per element |
| `ACP_CVT` | r*c | - | r*c | A in the result type |
| `ACP_SCALE` | r*c | 1*1 | r*c | every element times one number |

`ACP_MUL` is element by element even for a matrix. The matrix product is
`ACP_MATMUL`, and giving them separate opcodes means neither reading is ever
in doubt.

**Two of them refuse complex operands.** `ACP_REM` has no meaning for complex
numbers. `ACP_CMP` needs an order that complex numbers do not have. Both set
`ACP_BADFMT` rather than inventing an answer.

Transcendental, float only, and element by element like the rest. An integer
element type is an error rather than a silent conversion.

| op | A | B | R | what |
|---|---|---|---|---|
| `ACP_SQRT` | r*c | - | r*c | the square root |
| `ACP_SIN`, `ACP_COS`, `ACP_TAN` | r*c | - | r*c | in radians |
| `ACP_ASIN`, `ACP_ACOS`, `ACP_ATAN` | r*c | - | r*c | the inverses |
| `ACP_LOG`, `ACP_LOG10` | r*c | - | r*c | logarithms |
| `ACP_EXP` | r*c | - | r*c | e to the power A |
| `ACP_ATAN2` | r*c | r*c | r*c | the angle to the point A, B |
| `ACP_POW` | r*c | r*c | r*c | A to the power B |
| `ACP_HYPOT` | r*c | r*c | r*c | the length of the leg pair |

`ACP_CVT` exists for the case with no arithmetic in it. Setting the two types
differently converts on any operation, so a multiply of two integers can write
floats.

## Complex operations

`ACP_C64` is two 64 bit floats, real part first, so an element is 16 bytes.
The element-wise operations above take it and mean what they mean in
mathematics. Three more are complex only:

| op | A | B | R | result type |
|---|---|---|---|---|
| `ACP_ABS` | r*c | - | r*c | the magnitude, a real |
| `ACP_ARG_OF` | r*c | - | r*c | the angle, a real, in radians |
| `ACP_CONJ` | r*c | - | r*c | the conjugate, complex |

**The angle is `ACP_ARG_OF`, not `ACP_ARG`.** `ACP_ARG` is the port that
carries a table's entry count, and one prefix cannot spell two things. An
earlier draft of this document used the same name for both.

`ACP_ABS` and `ACP_ARG_OF` produce a real out of a complex pair, so a program
sets `ACP_RFMT` to `ACP_F64` to store one. Leaving `ACP_RFMT` at `ACP_C64` is
not an error. The real widens to a complex with a zero imaginary part, which
is what every other conversion on this device does with a narrower value.

**Complex division uses Smith's method.** The textbook formula squares the
divisor, and 1e200 squared is infinity. Smith's method scales first and
survives. A test divides two operands near 1e200 and separates them.

## Vector operations

A vector is a matrix of one column, so these take `r` by 1 and refuse
anything else with `ACP_BADDIM`.

| op | A | B | R | what |
|---|---|---|---|---|
| `ACP_DOT` | r*1 | r*1 | 1*1 | the dot product |
| `ACP_CROSS` | 3*1 | 3*1 | 3*1 | three dimensions only |
| `ACP_NORM` | r*1 | - | 1*1 | the length |
| `ACP_NORMALIZE` | r*1 | - | r*1 | length one |
| `ACP_DIST` | r*1 | r*1 | 1*1 | the distance between the points |

Adding, subtracting, negating and scaling a vector are the element-wise
operations above, because that is what they are.

`ACP_NORM`, `ACP_NORMALIZE` and `ACP_DIST` are float only, because each ends
in a square root. `ACP_DOT` and `ACP_CROSS` take any type, and an integer dot
product is exact.

`ACP_CROSS` on anything but three rows sets `ACP_BADDIM`. A cross product is
defined in three dimensions and pretending otherwise would be the machine
lying.

`ACP_NORMALIZE` of a zero vector sets `ACP_DIVZERO` and writes zero. The
direction is undefined, and a vector of NaN would poison whatever reads it
next.

## Matrix operations

| op | A | B | R | what |
|---|---|---|---|---|
| `ACP_MATMUL` | r*c | c*n | r*n | the matrix product |
| `ACP_MATVEC` | r*c | c*1 | r*1 | the matrix applied to a vector |
| `ACP_TRANSPOSE` | r*c | - | c*r | rows become columns |
| `ACP_IDENTITY` | - | - | r*r | A is ignored too |
| `ACP_DET` | r*r | - | 1*1 | the determinant |
| `ACP_INVERSE` | r*r | - | r*r | or `ACP_SINGULAR`, and the result is left zero |

`ACP_MATMUL` is where the dimension ports earn their keep. A is `r` by `c`,
B is `c` by `n`, and only `n` needs its own port because the inner dimension
is shared. Any shapes that agree work, at any size RAM can hold.

`ACP_DET` and `ACP_INVERSE` need a square A. Anything else is `ACP_BADDIM`.
Both are float only, and an integer or complex type is `ACP_BADFMT`. The other
four take any type, so an integer matrix product is exact.

**The inverse is Gauss-Jordan with partial pivoting.** The determinant uses
cofactors up to 3 by 3, which are exact and short. Above that it uses Gaussian
elimination. Both say so in a comment, beside the code.

## Tables

One command, and the reason it exists is that a table is a result with many
entries rather than one.

`ACP_GEN_TABLE` writes `count` entries of the result type, starting at the
result region and running on. The program reserves the room.

| what | where |
|---|---|
| the function to sample | `ACP_FUNC`, one of the transcendental op values |
| the entry count | `ACP_ARG` high, `ACP_ARG2` low |
| the first input | operand A, which is 1 by 1 here |
| the step between inputs | operand B, also 1 by 1 |

A quarter wave sine table for the circle demo is then one command. The
alternative is a hand written `.data` block. The assembler's `sin(t)` stays,
because it costs the running program nothing. This command is for a table
whose shape the program did not know until it ran.

**A count of zero writes nothing.** It is not an error. A program computing a
count can reach zero legitimately. Refusing it would make every caller guard.

**The room is the program's problem.** A count that runs past the end of RAM
wraps. Every effective address on this machine wraps, and this is no
exception.

## Bit manipulation

The CPU can mask a byte and it can shift one, badly. Addition only ever
carries upward. So a left shift is `ADD` on the same address twice. A right
shift costs a lookup table, or eight unrolled rotates. Above one byte there
is nothing at all.

So the whole family lives here, on 64 bits, integers only. A float or a
complex sets `ACP_BADFMT`. A double has bits, but they are an exponent and a
mantissa. Shifting them is not an operation anybody means to ask for.

| command | what |
|---|---|
| `ACP_AND` | bitwise and |
| `ACP_OR` | bitwise or |
| `ACP_XOR` | bitwise exclusive or |
| `ACP_NOT` | every bit flipped. One operand, so B takes no room |
| `ACP_SHL` | shift left |
| `ACP_SHR` | shift right, zero fill |
| `ACP_ASR` | shift right, sign fill |
| `ACP_ROL` | rotate left |
| `ACP_ROR` | rotate right |

**The count is B, element by element.** It is not a port. So a vector shifts
each of its own elements by its own amount. The shape rules governing every
other command govern these unchanged.

**The count is a magnitude.** It is read unsigned. Every count has an answer
and none of them is an error. At or past 64 a shift empties the register, and
`ACP_ASR` fills it with the sign. A rotate wraps, so 70 places is 6 places.

**`ACP_ASR` earns its place. The plain shifts barely do.** A shift by n is
`ACP_MUL` or `ACP_DIV` by 2 to the n, already, in one cycle. An arithmetic
right shift is the exception. `ACP_DIV` truncates toward zero, so -5 divided
by 2 is -2, where the shift floors it to -3. The rotates are the other
exception, having no arithmetic equivalent at all. The rest of the family
comes along for two reasons. A set with holes in it is harder to remember
than a complete one, and bit work without `ACP_AND` was never usable.

**`ACP_OVERFLOW` is the bit that fell out.** See the flags below. That is what
turns a shift into the bit test the CPU cannot do on a 64 bit value.

## Flags

One read tells a program everything about the last operation. `ACP_FLAGS`
holds what the result is and what went wrong. A program wants the first after
every operation and the second rarely, and one `IN` serves both.

| bit | constant | set when |
|---|---|---|
| 0 | `ACP_ZERO` | the result is zero, or every element of it is |
| 1 | `ACP_NEGATIVE` | the result is negative. Clear for an aggregate |
| 2 | `ACP_DIVZERO` | the divisor was zero, or a zero vector was normalized |
| 3 | `ACP_OVERFLOW` | the result did not fit, or a shift expelled a set bit |
| 4 | `ACP_NAN` | the result is not a number, or any element is not |
| 5 | `ACP_BADFMT` | the operation and the format do not go together |
| 6 | `ACP_SINGULAR` | a matrix had no inverse |
| 7 | `ACP_BADDIM` | the shapes and the operation do not go together |

The names are assembler constants, like the GPU port names and the button
masks. A program spells the bit rather than the number.

**Why the first two bits.** A zero test on a 64 bit result otherwise costs
eight loads and seven ORs. A sign test costs a load and a mask, and the
program must know which byte is the top one. Both are one `IN` and one `AND`
here. The device already knows the answer. Making the CPU work it out again
would be the offload failing at its job.

**Every operation sets every bit.** No operation leaves the flags alone. So
the flags describe the last command and hold until the next one. An operation
rejected for a bad format sets `ACP_BADFMT` and clears the result bits, since
there is no result to describe.

**Reading them.** `IN` sets no CPU flags here, so a program must `AND` before
it branches. There is also no jump-if-not-zero. The idiom is to `JZ` past the
case you do not want, and fall through into the one you do. The controller
does exactly this with its button masks:

```asm
        IN ACP_FLAGS -> A
        AND A <- ACP_NEGATIVE
        JZ notneg              ; clear: skip
        ...                    ; set: falls through to here
notneg:
```

Two bits can be tested together, because the assembler folds constants.
`ACP_DIVZERO | ACP_NAN` masks both faults in one `AND`.

**What the faults write to the result.** An integer divide by zero writes zero
and sets `ACP_DIVZERO`. A float divide by zero writes infinity and sets the
same bit. That is what a float divide by zero means, and hiding it would be
the machine lying.

## What it does not do

Named so nobody looks for them.

- No bulk. One operation, two operands, one result. An array of blocks is the
  vector processor's job and stays on the roadmap. A matrix of any size is
  still one result. This limit is about repeating a command, not about size.
- No 3D pipeline. Model, view and projection matrices, a matrix stack and the
  perspective divide belong to the GPU. It is built for exactly that chain.
  This device does generic mathematics of any type and any size. The GPU
  composes what it needs from the result. Eddie's split, 2026-09-10.
- No state between operations beyond the latched address and formats. Every
  command is complete in itself, so there is no accumulator to leave dirty.
- No 32 bit or 16 bit formats. A narrow value sits in a 64 bit slot with the
  top bytes zeroed. That costs nothing and removes an axis of confusion.
- No rounding mode. Float arithmetic is the host's, which is IEEE 754 double,
  and that is stated rather than hidden.
- No quaternions. A four row vector holds one, and the arithmetic differs, so
  adding them means adding operations rather than a shape. Nothing needs them.
- It does not replace the GPU's sine table. That table drives path rotation in
  one cycle from an 8 bit angle and stays where it is. This device answers a
  different question, in radians, at double precision. Two sine sources, two
  reasons, and the manual says which is which.

## What it changes on the roadmap

`docs/roadmap.md` lists three items this touches, and two of them narrow.

**GPU matrix functions.** The roadmap has the GPU holding a transform loaded
from assembly. With the ACP, the CPU computes the matrix here and hands the
GPU the result. The GPU still needs a way to be handed one, so the item lives
on. Its open question about a fixed point element format is settled instead by
converting, because the ACP has `ACP_CVT`.

**A vector processor.** Still separate, and still the large one. The ACP does
one operation on one pair. That item is about a whole world of vectors in one
frame. Bulk is exactly what this device does not do.

**An FPU.** Absorbed. This is it, and the roadmap entry points here.

## Testing

Every integer operation has an exact reference in `BigInt`. Integer tests
compare exactly over thousands of random operands. The edges matter most:
zero, one, the largest and smallest signed values, and the multiply that fills
all 128 bits.

Float arithmetic compares exactly against the host's own double arithmetic,
because it IS the host's arithmetic. There is no tolerance to choose and none
should be introduced. That holds element by element for the vector and matrix
operations. The device must sum in a stated order. Write that order down, and
test against a reference that uses it.

Transcendentals compare within a stated tolerance against the host's `Math`.
Write the tolerance down in the test. Do not tune it until it passes.

The block is the interesting surface. Fill RAM with a pattern and check what
moved. The device must read exactly its operand bytes and write exactly its
result bytes. Test that a block at the top of RAM wraps or refuses in a stated
way. Test that an overlapping operand and result behaves the way this document
says.

## Still open

- Whether a later revision should read a count and walk an array of blocks, so
  one command does many operations. The block interface allows it without
  changing anything written here. That is the vector processor's shape and it
  should probably wait for that work.
- Whether a fault should raise a CPU-visible condition rather than waiting to
  be read. Every peripheral here is polled. That would be the machine's first
  interrupt, and a much larger question than this device.
