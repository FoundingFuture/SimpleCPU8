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

There are 141 opcodes and 37 mnemonics. The high nibble groups them.

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
| $90 to $94 | TST |
| $98 to $9C | SHL, SHR, ROL, ROR, ASR |
| $A0 to $AC | D3: bytes and words at [D3+n], address adds into and out of D3, D3 at an address |
| $B0 to $B7 | the ALU and CMP on a byte at [D3+n], low nibble as in $40 to $47 |

The free codes are $02, $03, $0D, $56, $57, $5A to $5F, $65 to $78 and
$7D to $7F. Then $95 to $97, $9D to $9F, $AD to $AF and $B8 to $FE. $FF
marks a program slot nothing was loaded into.

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
| $38 | `LD D1 <- D1+n` | 3 | 2 | Z C |  |
| $39 | `LD D2 <- D2+n` | 3 | 2 | Z C |  |
| $3A | `LD D1 <- D2+n` | 3 | 2 | Z C |  |
| $3B | `LD D2 <- D1+n` | 3 | 2 | Z C |  |
| $3C | `LD D1 <- D1+A` | 3 | 2 | Z C |  |
| $3D | `LD D2 <- D2+A` | 3 | 2 | Z C |  |
| $3E | `LD D1 <- D2+A` | 3 | 2 | Z C |  |
| $3F | `LD D2 <- D1+A` | 3 | 2 | Z C |  |

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

### D3, the data stack pointer

| Op | Instruction | Naive | Optimal | Sets | Reads |
|---|---|---|---|---|---|
| $94 | `TST A, [D3+n]` | 5 | 3 | Z N |  |
| $A0 | `LD A <- [D3+n]` | 5 | 3 | Z N |  |
| $A1 | `LD [D3+n] <- A` | 3 | 2 | none |  |
| $A2 | `LD D1 <- [D3+n]` | 5 | 4 | Z |  |
| $A3 | `LD D2 <- [D3+n]` | 5 | 4 | Z |  |
| $A4 | `LD [D3+n] <- D1` | 4 | 3 | none |  |
| $A5 | `LD [D3+n] <- D2` | 4 | 3 | none |  |
| $A6 | `LD D3 <- D3+n` | 3 | 2 | Z C |  |
| $A7 | `LD D1 <- D3+n` | 3 | 2 | Z C |  |
| $A8 | `LD D2 <- D3+n` | 3 | 2 | Z C |  |
| $A9 | `LD D3 <- D1+n` | 3 | 2 | Z C |  |
| $AA | `LD D3 <- D2+n` | 3 | 2 | Z C |  |
| $AB | `LD D3 <- [addr16]` | 5 | 4 | Z |  |
| $AC | `LD [addr16] <- D3` | 4 | 3 | none |  |
| $B0 | `ADD A <- [D3+n]` | 6 | 3 | Z N C V |  |
| $B1 | `SUB A <- [D3+n]` | 6 | 3 | Z N C V |  |
| $B2 | `AND A <- [D3+n]` | 6 | 3 | Z N |  |
| $B3 | `OR A <- [D3+n]` | 6 | 3 | Z N |  |
| $B4 | `XOR A <- [D3+n]` | 6 | 3 | Z N |  |
| $B5 | `ADC A <- [D3+n]` | 6 | 3 | Z N C V | C |
| $B6 | `SBC A <- [D3+n]` | 6 | 3 | Z N C V | C |
| $B7 | `CMP A, [D3+n]` | 5 | 3 | Z N C V |  |

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
| Z | every ALU result, CMP and TST, a byte load into A, a word load into a D register, an address add, POPB, POPW, INW | the value is zero. For a word it is all 16 bits, so a load is a NULL test |
| N | every ALU result, CMP and TST, a byte load into A, POPB | bit 7 of the byte |
| C | ADD, ADC, SUB, SBC, CMP, the shifts, an address add | the carry out, the borrow for a subtract, the bit a shift pushed out. For an address add, the adder's carry out of bit 15 |
| V | ADD, ADC, SUB, SBC, CMP | signed overflow of the byte result |

Stores, pushes, OUT, INB, INC D1, INC D2 and the jumps set no flag. A load
leaves C and V alone, so a chain of ADD and ADC can load between its
steps. An address add sets C, so it cannot sit inside such a chain.
After LD D2 <- D3-FLOOR, C clear means D3 was below FLOOR. That is the
inverse of CMP's borrow.

Each flag has a jump for set and for clear. After CMP A, x or SUB A <- x:

| Test | Unsigned | Signed |
|---|---|---|
| A == x | JZ | JZ |
| A != x | JNZ | JNZ |
| A < x | JC | N xor V: JV then JN or JP |
| A >= x | JNC | the inverse of the line above |

## What the C compiler uses them for

The compiler keeps expression values in zero page temps and works through
A. D3 is the heap stack pointer and the frame base. D2 is the scratch
register for addresses, and D1 carries word arguments.
A peephole pass removes a store and reload of the same byte and a store
nobody reads. An inlined test's answer, built as 0 or 1 only to be tested,
goes, and its compares jump where the test would have. A jump to a JMP
takes the JMP's target, a conditional jump over a JMP turns round, and
code no jump reaches goes. The sequences below are what it emits, with
optimal cycles.

A local variable lives in the frame at [D3+n]. A byte local is read with
LD A <- [D3+n], 3 cycles, and written with LD [D3+n] <- A, 2 cycles. The
ALU reads it in place: ADD A <- [D3+n] is 3 cycles, with no copy to a
temp first. A word local moves whole through D2: LD D2 <- [D3+n] and LD
[D3+n] <- D2. A global in the zero page is read the same way through
[label].

`i = i + 1` on a byte is LD A, ADD A <- 1 and LD back: 8 cycles. On an
int or a pointer, the address adder does the 16 bit add in one step:

```asm
        LD D2 <- [D3+2]     ; 4
        LD D2 <- D2+1       ; 2, and D2-1 for i - 1
        LD [D3+2] <- D2     ; 3
```

A pointer plus a constant is the same shape, with the constant scaled by
the element size.

An array element with a byte index is the base in D2 and the index in A:

```asm
        LD A <- [D3+0]      ; i
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
        LD A <- [D3+0]
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
AND. A signed char widens without a branch. SHL puts its sign in C, and
LD A <- 0 then SBC A <- 0 turns C into the high byte.

A frame is made and dropped by moving D3 through the address adder:

```asm
        LD D3 <- D3-6       ; 2, this frame's locals and saved temps
        ; the body
        LD D3 <- D3+7       ; 2, the frame and the one byte argument
        RET
```

A call writes its arguments below the frame through D2, then moves D3
down to them and does JSR. The callee's epilogue takes D3 back past
them, so the caller's frame base is D3 again with nothing reloaded. Temps
are saved in the frame only around a call. A function that calls
nothing has a frame of its locals alone. A return writes the epilogue in
place instead of a jump to it. An argument that is a constant, a
variable or a string is read straight into the new frame. A word argument
goes through D1. A small static function is not called at all. Its body
is expanded in the caller, and its parameters live in the caller's frame.

The heap stack runs from under the text screen down to a floor. The floor
is the end of the program's data, or what #pragma heap_stack_size sets. A
function that can recurse, or that assembly can call, checks at entry:

```asm
        LD D2 <- D3-__hs_f  ; 2, the floor plus what the calls below need
        JNC __stack_overflow ; 2, C clear: D3 went below it
```

Every other function is on a fixed chain whose depth the compiler adds
up. When main's chain cannot fit, the compiler refuses the program.

## What stays with the coprocessor

Some integer operations go to the maths coprocessor. A multiply by a
variable goes there, and so does one by a constant with many set bits. So
does a divide or remainder by anything but a power of two. A shift by a variable goes
there too. The runtime routines __mul16, __udiv16, __sdiv16, __urem16,
__srem16, __shl16, __ushr16 and __sshr16 do the talking. Doubles are the coprocessor's entirely. The -msoft-mul build does the
integer ones on the CPU instead, as loops.
