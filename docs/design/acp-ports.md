# SimpleCPU-8 ACP reference

The ACP is the arithmetic coprocessor, a peripheral on the IO bus. It is not
part of the CPU. The CPU talks to it with OUT, OUTA and IN. The ACP claims
ports 0x40 to 0x4F. Writes to other ports go to the rest of the bus.

One command reads two operands and writes one result, through a block of data
RAM the program nominates. The whole operation takes one cycle. That is a
deliberate cheat, and `docs/acp-design.md` says why.

The ACP is the only device that WRITES data RAM. The GPU reads it for text
mode and writes none of it.

## Contents

- [The block](#the-block)
- [Ports](#ports)
- [Element types](#element-types)
- [Shapes](#shapes)
- [Commands](#commands)
- [Flags](#flags)
- [A worked example](#a-worked-example)

## The block

Three regions in a row, at the address in `ACP_ADDR_HI` and `ACP_ADDR_LO`:

```text
offset      size      what
0           A         operand A
A           B         operand B
A + B       R         the result
```

A region is `rows * columns * element` bytes. The element size comes from
`ACP_FMT`. The shapes come from the command, and the commands table gives them.

A dash in the B column means B takes no room. The result then starts where B
would have, so `ACP_NEG` on one 8 byte number writes at offset 8.

Big-endian throughout, like every other word on this machine. Every address
wraps at 16 bits, so a block that runs off the top of RAM comes back at zero.

## Ports

| port | name | what |
|---|---|---|
| 0x40 | ACP_ADDR_HI | block address, high byte |
| 0x41 | ACP_ADDR_LO | block address, low byte |
| 0x42 | ACP_FMT | operand element type. Writing it sets the result type too |
| 0x43 | ACP_RFMT | result element type, when it differs |
| 0x44 | ACP_CMD | write to run a command |
| 0x45 | ACP_FLAGS | read: how the last command went |
| 0x46 | ACP_ROWS | rows in A |
| 0x47 | ACP_COLS | columns in A |
| 0x48 | ACP_COLS_B | columns in B. Only ACP_MATMUL reads it |
| 0x49 | ACP_ARG | a table's entry count, high byte |
| 0x4a | ACP_ARG2 | a table's entry count, low byte |
| 0x4b | ACP_FUNC | which function a table samples |

Everything latches. A program working one block in one shape sets the address,
the type and the dimensions once. After that it writes only ACP_CMD.

## Element types

| value | name | what | bytes |
|---|---|---|---|
| 0x00 | ACP_I64 | signed 64 bit integer | 8 |
| 0x01 | ACP_U64 | unsigned 64 bit integer | 8 |
| 0x02 | ACP_F64 | 64 bit float | 8 |
| 0x03 | ACP_C64 | complex, two 64 bit floats, real first | 16 |

Float arithmetic is the host's IEEE 754 double arithmetic, exactly. There is
no rounding mode to choose.

## Shapes

`ACP_ROWS` and `ACP_COLS` give A's shape, and both power on at 1. A scalar is
1 by 1 and a vector of N is N by 1, so there is no separate vector type.

A dimension of zero sets ACP_BADDIM, and so does a shape a command cannot use.

The one widening case is a 1 by 1 integer multiply, which writes 16 bytes
because two 64 bit integers multiply to 128 bits exactly.

## Commands

In the shapes columns, `r` and `c` are ACP_ROWS and ACP_COLS, and `n` is
ACP_COLS_B. A dash means the operand is not read and takes no room.

Element by element, over any shape. Every one takes every type unless the
row says otherwise.

| value | name | A | B | R | what |
|---|---|---|---|---|---|
| 0x01 | ACP_ADD | r*c | r*c | r*c | A plus B |
| 0x02 | ACP_SUB | r*c | r*c | r*c | A minus B |
| 0x03 | ACP_MUL | r*c | r*c | r*c | element by element, never the matrix product |
| 0x04 | ACP_DIV | r*c | r*c | r*c | A divided by B |
| 0x05 | ACP_REM | r*c | r*c | r*c | remainder. Not complex |
| 0x06 | ACP_NEG | r*c | - | r*c | minus A |
| 0x07 | ACP_ABS | r*c | - | r*c | magnitude. Complex gives a real |
| 0x08 | ACP_CMP | r*c | r*c | r*c | minus one, zero or one. Not complex |
| 0x09 | ACP_CVT | r*c | - | r*c | A in the result type |
| 0x0a | ACP_SCALE | r*c | 1*1 | r*c | every element times one number |

Transcendental, float only, element by element.

| value | name | A | B | R | what |
|---|---|---|---|---|---|
| 0x10 | ACP_SQRT | r*c | - | r*c | square root |
| 0x11 | ACP_SIN | r*c | - | r*c | sine, in radians |
| 0x12 | ACP_COS | r*c | - | r*c | cosine |
| 0x13 | ACP_TAN | r*c | - | r*c | tangent |
| 0x14 | ACP_ASIN | r*c | - | r*c | arc sine |
| 0x15 | ACP_ACOS | r*c | - | r*c | arc cosine |
| 0x16 | ACP_ATAN | r*c | - | r*c | arc tangent |
| 0x17 | ACP_LOG | r*c | - | r*c | natural logarithm |
| 0x18 | ACP_LOG10 | r*c | - | r*c | base ten logarithm |
| 0x19 | ACP_EXP | r*c | - | r*c | e to the power A |
| 0x1a | ACP_ATAN2 | r*c | r*c | r*c | the angle to the point A, B |
| 0x1b | ACP_POW | r*c | r*c | r*c | A to the power B |
| 0x1c | ACP_HYPOT | r*c | r*c | r*c | the length of the leg pair |

Complex only.

| value | name | A | B | R | what |
|---|---|---|---|---|---|
| 0x20 | ACP_ARG_OF | r*c | - | r*c | the angle, a real, in radians |
| 0x21 | ACP_CONJ | r*c | - | r*c | the conjugate |

Vectors, which are matrices of one column. ACP_NORM, ACP_NORMALIZE and
ACP_DIST are float only.

| value | name | A | B | R | what |
|---|---|---|---|---|---|
| 0x30 | ACP_DOT | r*1 | r*1 | 1*1 | the dot product |
| 0x31 | ACP_CROSS | 3*1 | 3*1 | 3*1 | three dimensions only |
| 0x32 | ACP_NORM | r*1 | - | 1*1 | the length |
| 0x33 | ACP_NORMALIZE | r*1 | - | r*1 | length one |
| 0x34 | ACP_DIST | r*1 | r*1 | 1*1 | the distance between the points |

Matrices, row major. ACP_DET and ACP_INVERSE are float only and need a
square A.

| value | name | A | B | R | what |
|---|---|---|---|---|---|
| 0x40 | ACP_MATMUL | r*c | c*n | r*n | the matrix product |
| 0x41 | ACP_MATVEC | r*c | c*1 | r*1 | the matrix applied to a vector |
| 0x42 | ACP_TRANSPOSE | r*c | - | c*r | rows become columns |
| 0x43 | ACP_IDENTITY | r*c | - | r*r | A is ignored |
| 0x44 | ACP_DET | r*r | - | 1*1 | the determinant |
| 0x45 | ACP_INVERSE | r*r | - | r*r | or ACP_SINGULAR, and the result is zeroed |

A table is a result with many entries. Float only.

| value | name | A | B | R | what |
|---|---|---|---|---|---|
| 0x50 | ACP_GEN_TABLE | 1*1 | 1*1 | count*1 | ACP_FUNC sampled from A, stepping by B |

Every sum runs from element zero upward. Doubles do not add associatively, so
that order is part of the contract rather than an accident.

Bit manipulation, integers only. A float or a complex sets ACP_BADFMT. On a
shift or a rotate, B is a count per element. So a vector shifts each of its
own elements by its own amount.

| value | name | A | B | R | what |
|---|---|---|---|---|---|
| 0x60 | ACP_AND | r*c | r*c | r*c | bitwise and |
| 0x61 | ACP_OR | r*c | r*c | r*c | bitwise or |
| 0x62 | ACP_XOR | r*c | r*c | r*c | bitwise exclusive or |
| 0x63 | ACP_NOT | r*c | - | r*c | every bit of A flipped |
| 0x64 | ACP_SHL | r*c | r*c | r*c | shift left by B |
| 0x65 | ACP_SHR | r*c | r*c | r*c | shift right by B, zero fill |
| 0x66 | ACP_ASR | r*c | r*c | r*c | shift right by B, sign fill |
| 0x67 | ACP_ROL | r*c | r*c | r*c | rotate left by B |
| 0x68 | ACP_ROR | r*c | r*c | r*c | rotate right by B |

The count is read as an unsigned magnitude. Every count has an answer and
none of them is an error. At or past 64 a shift empties the register, and
ACP_ASR fills it with the sign. A rotate wraps, so 70 places is 6.

ACP_ASR is the one of these no other command can stand in for. ACP_DIV by a
power of two truncates toward zero. So -5 divided by 2 is -2, where the
arithmetic shift floors it to -3.

## Flags

Read ACP_FLAGS. Every command replaces every bit, so the flags describe the
last command and nothing older.

| bit | name | set when |
|---|---|---|
| 0x01 | ACP_ZERO | every element of the result is zero |
| 0x02 | ACP_NEGATIVE | a 1 by 1 result is negative |
| 0x04 | ACP_DIVZERO | the divisor was zero, or a zero vector was normalized |
| 0x08 | ACP_OVERFLOW | the result did not fit, or a shift expelled a set bit |
| 0x10 | ACP_NAN | some element is not a number |
| 0x20 | ACP_BADFMT | the command and the element type do not go together |
| 0x40 | ACP_SINGULAR | a matrix had no inverse |
| 0x80 | ACP_BADDIM | the shapes and the command do not go together |

IN sets no CPU flags, so a program must AND before it branches. There is no
jump-if-not-zero. JZ past the case you do not want, and fall through into the
one you do.

## A worked example

Multiply two 64 bit integers and read the flags back.

```asm
        OUT ACP_ADDR_HI, blk >> 8
        OUT ACP_ADDR_LO, blk & 255
        OUT ACP_FMT, ACP_I64
        OUT ACP_CMD, ACP_MUL

        IN ACP_FLAGS -> A
        AND A <- ACP_ZERO
        JZ notzero
notzero: HLT

.ram
blk:    db 0,0,0,0,0,0,0,7      ; operand A
        db 0,0,0,0,0,0,0,6      ; operand B
        db 0,0,0,0,0,0,0,0      ; the result, 16 bytes for a 1 by 1 product
        db 0,0,0,0,0,0,0,0
```

The address split is one expression each because the assembler folds
constants. Two flag bits combine the same way: `ACP_DIVZERO | ACP_NAN` masks
both faults in one AND.
