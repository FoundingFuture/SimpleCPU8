# SimpleCPU-8 instruction set

Every opcode the machine has, with its cost and the flags it sets and
reads. Then the C constructs the compiler uses each one for. The tables are
generated from src/core/isa.cpp and the two shipped microcode sets. A cost
is microcycles with the fetch included. Naive is the teaching set, optimal
is the sealed set a ROM runs by default.

## Contents

- [The opcode map](#the-opcode-map)
- [The instructions](#the-instructions)
- [The flags](#the-flags)
- [What the C compiler uses them for](#what-the-c-compiler-uses-them-for)
- [What stays with the coprocessor](#what-stays-with-the-coprocessor)

## The opcode map

There are 119 opcodes and 30 mnemonics. The high nibble groups them.

| Range | Family |
|---|---|
| $00 to $0F | control: NOP, HLT, the jumps through a D register, JMP, JZ, JC, JN, JV, JSR, RET |
| $10 to $21 | byte loads and stores through A |
| $22 to $37 | word loads and stores through D1 and D2 |
| $38 to $3F | address arithmetic: a D register loaded with D1 or D2 plus an offset or A |
| $40 to $4F | the ALU on a zero page byte or an immediate, and CMP |
| $50 to $59 | the stack, and INC D1 and INC D2 |
| $60 to $64 | the ports |
| $79 to $7C | JNZ, JNC, JP, JNV: the jumps on a clear flag, low nibble as in $09 to $0C |
| $80 to $8F | the ALU and CMP on a byte through [D1+n] or [D2+n], low nibble as in $40 to $47 |
| $90 to $93 | TST |
| $98 to $9C | SHL, SHR, ROL, ROR, ASR |

The free codes are $02, $03, $0D, $56, $57, $5A to $5F, $65 to $78, $7D
to $7F, $94 to $97 and $9D to $FE. $FF marks a program slot nothing was
loaded into.

## The instructions

### Control and jumps

| Op | Instruction | Naive | Optimal | Sets | Reads |
|---|---|---|---|---|---|
| $00 | `NOP` | 2 | 1 | none |  |
| $01 | `HLT` | 2 | 2 | none |  |
| $04 | `JMP D1` | 2 | 2 | none |  |
| $05 | `JMP D2` | 2 | 2 | none |  |
| $06 | `JSR D1` | 7 | 3 | none |  |
| $07 | `JSR D2` | 7 | 3 | none |  |
| $08 | `JMP` | 2 | 2 | none |  |
| $09 | `JZ` | 3 | 2 | none | Z |
| $0A | `JC` | 3 | 2 | none | C |
| $0B | `JN` | 3 | 2 | none | N |
| $0C | `JV` | 3 | 2 | none | V |
| $0E | `JSR` | 7 | 3 | none |  |
| $0F | `RET` | 5 | 3 | none |  |
| $79 | `JNZ` | 3 | 2 | none | Z |
| $7A | `JNC` | 3 | 2 | none | C |
| $7B | `JP` | 3 | 2 | none | N |
| $7C | `JNV` | 3 | 2 | none | V |

### Byte loads and stores

| Op | Instruction | Naive | Optimal | Sets | Reads |
|---|---|---|---|---|---|
| $10 | `LD A <- imm8` | 5 | 3 | Z N |  |
| $11 | `LD A <- [addr8]` | 5 | 3 | Z N |  |
| $12 | `LD [addr8] <- A` | 3 | 2 | none |  |
| $13 | `LD A <- [A]` | 5 | 3 | Z N |  |
| $14 | `LD A <- [D1]` | 5 | 3 | Z N |  |
| $15 | `LD A <- [D2]` | 5 | 3 | Z N |  |
| $16 | `LD A <- [D1]+` | 6 | 3 | Z N |  |
| $17 | `LD A <- [D2]+` | 6 | 3 | Z N |  |
| $18 | `LD [D1] <- A` | 3 | 2 | none |  |
| $19 | `LD [D2] <- A` | 3 | 2 | none |  |
| $1A | `LD [D1]+ <- A` | 4 | 2 | none |  |
| $1B | `LD [D2]+ <- A` | 4 | 2 | none |  |
| $1C | `LD A <- [D1+n]` | 5 | 3 | Z N |  |
| $1D | `LD A <- [D2+n]` | 5 | 3 | Z N |  |
| $1E | `LD A <- [D1+A]` | 5 | 3 | Z N |  |
| $1F | `LD A <- [D2+A]` | 5 | 3 | Z N |  |
| $20 | `LD [D1+n] <- A` | 3 | 2 | none |  |
| $21 | `LD [D2+n] <- A` | 3 | 2 | none |  |

### Word loads and stores

| Op | Instruction | Naive | Optimal | Sets | Reads |
|---|---|---|---|---|---|
| $22 | `LD D1 <- imm16` | 4 | 3 | Z |  |
| $23 | `LD D2 <- imm16` | 4 | 3 | Z |  |
| $24 | `LD D1 <- [addr16]` | 5 | 4 | Z |  |
| $25 | `LD D2 <- [addr16]` | 5 | 4 | Z |  |
| $26 | `LD [addr16] <- D1` | 4 | 3 | none |  |
| $27 | `LD [addr16] <- D2` | 4 | 3 | none |  |
| $28 | `LD D2 <- [D1]` | 5 | 4 | Z |  |
| $29 | `LD D1 <- [D2]` | 5 | 4 | Z |  |
| $2A | `LD D2 <- [D1]+` | 7 | 4 | Z |  |
| $2B | `LD D1 <- [D2]+` | 7 | 4 | Z |  |
| $2C | `LD D1 <- [A]` | 5 | 4 | Z |  |
| $2D | `LD D2 <- [A]` | 5 | 4 | Z |  |
| $2E | `LD D1 <- [A]+` | 7 | 4 | Z |  |
| $2F | `LD D2 <- [A]+` | 7 | 4 | Z |  |
| $30 | `LD D2 <- [D1+n]` | 5 | 4 | Z |  |
| $31 | `LD D1 <- [D2+n]` | 5 | 4 | Z |  |
| $32 | `LD D2 <- [D1+A]` | 5 | 4 | Z |  |
| $33 | `LD D1 <- [D2+A]` | 5 | 4 | Z |  |
| $34 | `LD [D1] <- D2` | 4 | 3 | none |  |
| $35 | `LD [D2] <- D1` | 4 | 3 | none |  |
| $36 | `LD [D1+n] <- D2` | 4 | 3 | none |  |
| $37 | `LD [D2+n] <- D1` | 4 | 3 | none |  |

### Address arithmetic

| Op | Instruction | Naive | Optimal | Sets | Reads |
|---|---|---|---|---|---|
| $38 | `LD D1 <- D1+n` | 4 | 3 | Z |  |
| $39 | `LD D2 <- D2+n` | 4 | 3 | Z |  |
| $3A | `LD D1 <- D2+n` | 4 | 3 | Z |  |
| $3B | `LD D2 <- D1+n` | 4 | 3 | Z |  |
| $3C | `LD D1 <- D1+A` | 4 | 3 | Z |  |
| $3D | `LD D2 <- D2+A` | 4 | 3 | Z |  |
| $3E | `LD D1 <- D2+A` | 4 | 3 | Z |  |
| $3F | `LD D2 <- D1+A` | 4 | 3 | Z |  |

### ALU on the zero page and immediates

| Op | Instruction | Naive | Optimal | Sets | Reads |
|---|---|---|---|---|---|
| $40 | `ADD A <- [addr8]` | 6 | 3 | Z N C V |  |
| $41 | `SUB A <- [addr8]` | 6 | 3 | Z N C V |  |
| $42 | `AND A <- [addr8]` | 6 | 3 | Z N |  |
| $43 | `OR A <- [addr8]` | 6 | 3 | Z N |  |
| $44 | `XOR A <- [addr8]` | 6 | 3 | Z N |  |
| $45 | `ADC A <- [addr8]` | 6 | 3 | Z N C V | C |
| $46 | `SBC A <- [addr8]` | 6 | 3 | Z N C V | C |
| $47 | `CMP A, [addr8]` | 5 | 3 | Z N C V |  |
| $48 | `ADD A <- imm8` | 6 | 3 | Z N C V |  |
| $49 | `SUB A <- imm8` | 6 | 3 | Z N C V |  |
| $4A | `AND A <- imm8` | 6 | 3 | Z N |  |
| $4B | `OR A <- imm8` | 6 | 3 | Z N |  |
| $4C | `XOR A <- imm8` | 6 | 3 | Z N |  |
| $4D | `ADC A <- imm8` | 6 | 3 | Z N C V | C |
| $4E | `SBC A <- imm8` | 6 | 3 | Z N C V | C |
| $4F | `CMP A, imm8` | 5 | 3 | Z N C V |  |

### ALU through a D register

| Op | Instruction | Naive | Optimal | Sets | Reads |
|---|---|---|---|---|---|
| $80 | `ADD A <- [D1+n]` | 6 | 3 | Z N C V |  |
| $81 | `SUB A <- [D1+n]` | 6 | 3 | Z N C V |  |
| $82 | `AND A <- [D1+n]` | 6 | 3 | Z N |  |
| $83 | `OR A <- [D1+n]` | 6 | 3 | Z N |  |
| $84 | `XOR A <- [D1+n]` | 6 | 3 | Z N |  |
| $85 | `ADC A <- [D1+n]` | 6 | 3 | Z N C V | C |
| $86 | `SBC A <- [D1+n]` | 6 | 3 | Z N C V | C |
| $87 | `CMP A, [D1+n]` | 5 | 3 | Z N C V |  |
| $88 | `ADD A <- [D2+n]` | 6 | 3 | Z N C V |  |
| $89 | `SUB A <- [D2+n]` | 6 | 3 | Z N C V |  |
| $8A | `AND A <- [D2+n]` | 6 | 3 | Z N |  |
| $8B | `OR A <- [D2+n]` | 6 | 3 | Z N |  |
| $8C | `XOR A <- [D2+n]` | 6 | 3 | Z N |  |
| $8D | `ADC A <- [D2+n]` | 6 | 3 | Z N C V | C |
| $8E | `SBC A <- [D2+n]` | 6 | 3 | Z N C V | C |
| $8F | `CMP A, [D2+n]` | 5 | 3 | Z N C V |  |
| $90 | `TST A, [addr8]` | 5 | 3 | Z N |  |
| $91 | `TST A, imm8` | 5 | 3 | Z N |  |
| $92 | `TST A, [D1+n]` | 5 | 3 | Z N |  |
| $93 | `TST A, [D2+n]` | 5 | 3 | Z N |  |

### Shifts

| Op | Instruction | Naive | Optimal | Sets | Reads |
|---|---|---|---|---|---|
| $98 | `SHL A` | 5 | 3 | Z N C |  |
| $99 | `SHR A` | 5 | 3 | Z N C |  |
| $9A | `ROL A` | 5 | 3 | Z N C | C |
| $9B | `ROR A` | 5 | 3 | Z N C | C |
| $9C | `ASR A` | 5 | 3 | Z N C |  |

### Stack

| Op | Instruction | Naive | Optimal | Sets | Reads |
|---|---|---|---|---|---|
| $50 | `PUSHB A` | 4 | 2 | none |  |
| $51 | `POPB A` | 6 | 3 | Z N |  |
| $52 | `PUSHW D1` | 6 | 3 | none |  |
| $53 | `PUSHW D2` | 6 | 3 | none |  |
| $54 | `POPW D1` | 7 | 4 | Z |  |
| $55 | `POPW D2` | 7 | 4 | Z |  |

### Pointer steps

| Op | Instruction | Naive | Optimal | Sets | Reads |
|---|---|---|---|---|---|
| $58 | `INC D1` | 3 | 2 | none |  |
| $59 | `INC D2` | 3 | 2 | none |  |

### Ports

| Op | Instruction | Naive | Optimal | Sets | Reads |
|---|---|---|---|---|---|
| $60 | `OUT` | 3 | 2 | none |  |
| $61 | `OUTA` | 3 | 2 | none |  |
| $62 | `INB` | 3 | 2 | none |  |
| $63 | `INW D1` | 5 | 4 | Z |  |
| $64 | `INW D2` | 5 | 4 | Z |  |

## The flags

| Flag | Set by | Means |
|---|---|---|
| Z | every ALU result, CMP and TST, a byte load into A, a word load into D1 or D2, POPB, POPW, INW | the value is zero. For a word it is all 16 bits, so a load is a NULL test |
| N | every ALU result, CMP and TST, a byte load into A, POPB | bit 7 of the byte |
| C | ADD, ADC, SUB, SBC, CMP, the shifts | the carry out, the borrow for a subtract, the bit a shift pushed out |
| V | ADD, ADC, SUB, SBC, CMP | signed overflow of the byte result |

Stores, pushes, OUT, INB, INC D1, INC D2 and the jumps set no flag. A load
leaves C and V alone, so a chain of ADD and ADC can load between its
steps.

Each flag has a jump for set and for clear. After CMP A, x or SUB A <- x:

| Test | Unsigned | Signed |
|---|---|---|
| A == x | JZ | JZ |
| A != x | JNZ | JNZ |
| A < x | JC | N xor V: JV then JN or JP |
| A >= x | JNC | the inverse of the line above |

## What the C compiler uses them for

The compiler keeps expression values in zero page temps and works through
A. D1 is the frame pointer, and D2 is its scratch register for addresses.
A peephole pass removes a store and reload of the same byte and a store
nobody reads. The sequences below are what it emits, with optimal cycles.

A local variable lives in the frame at [D1+n]. A byte local is read with
LD A <- [D1+n], 3 cycles, and written with LD [D1+n] <- A, 2 cycles. The
ALU reads it in place: ADD A <- [D1+n] is 3 cycles, with no copy to a
temp first. A word local moves whole through D2: LD D2 <- [D1+n] and LD
[D1+n] <- D2. A global in the zero page is read the same way through
[label].

`i = i + 1` on a byte is LD A, ADD A <- 1 and LD back: 8 cycles. On an
int or a pointer, the address adder does the 16 bit add in one step:

```asm
        LD D2 <- [D1+2]     ; 4
        LD D2 <- D2+1       ; 3, and D2-1 for i - 1
        LD [D1+2] <- D2     ; 3
```

A pointer plus a constant is the same shape, with the constant scaled by
the element size.

An array element with a byte index is the base in D2 and the index in A:

```asm
        LD A <- [D1+0]      ; i
        LD D2 <- buf        ; the array's address
        LD A <- [D2+A]      ; buf[i]
```

A store steps D2 with LD D2 <- D2+A and then writes LD [D2] <- A. A is
needed there for the value. A constant index is a displacement, LD A <- [D2+5].
A word element steps D2 by A twice. An int index is a 16 bit add through
A into a temp, then LD D2 <- [temp].

A condition compiles to a compare and one jump, never to a 0 or 1 first.
`if (c >= 48 && c <= 57)` becomes:

```asm
        LD A <- [D1+0]
        CMP A, 48
        JC not              ; below 48
        CMP A, 58
        JNC not             ; 58 or more
```

CMP keeps A, so the second compare needs no reload. <= k is written as <
k+1, which keeps the variable in A. A compare of two bytes, or of a byte
and a constant that fits one, runs at byte width. A word compare is SUB
and SBC, since only those chain the borrow. An int or a pointer tested on
its own is one load: LD D2 <- [p] sets Z from all 16 bits. A signed
compare against zero is the sign bit: a load and JN or JP.

A shift by a constant is SHL and ROL, or SHR and ROR, one pair per bit.
For a signed value, ASR on the high byte keeps the sign. Eight bits or more
is a byte move first. A multiply by a constant with few set bits is
shifts and adds. So x * 10 is three shifts and one add. A divide by a
power of two is a shift. C rounds towards zero, so a negative signed
value gets k - 1 added first. A remainder by a power of two is an
AND.

A frame is made and dropped through the address adder:

```asm
        LD D1 <- [__sp]     ; the caller's stack pointer
        LD D1 <- D1-6       ; this frame's locals and saved temps
        LD [__sp] <- D1
        ; the body
        LD D1 <- D1+7       ; the frame and the one byte argument
        LD [__sp] <- D1
        RET
```

Temps are saved in the frame only around a call. So a function that calls
nothing has a frame of its locals alone. A return writes the epilogue in
place instead of a jump to it. An argument that is a constant, a variable or a string is
read straight into the new frame. A word argument goes through D1, since
D1 is loaded again after the call. A small static function is not called
at all. Its body is expanded in the caller, and its parameters live in
the caller's frame.

## What stays with the coprocessor

Some integer operations go to the maths coprocessor. A multiply by a
variable goes there, and so does one by a constant with many set bits. So
does a divide or remainder by anything but a power of two. A shift by a variable goes
there too. The runtime routines __mul16, __udiv16, __sdiv16, __urem16,
__srem16, __shl16, __ushr16 and __sshr16 do the talking. Doubles are the coprocessor's entirely. The -msoft-mul build does the
integer ones on the CPU instead, as loops.
