# C and BASIC on SimpleCPU-8

Eddie's direction, 2026-09-13. Two higher levels of programming for the
machine, and a way to see every layer at once:

```text
BASIC line  ->  C statement  ->  assembly  ->  microcode row
```

C is a layer in the tool. BASIC is a program on the CPU. That split is the
whole design, and the rest of the document follows from it.

## Contents

- [The core insight](#the-core-insight)
- [Seeing the layers](#seeing-the-layers)
- [The C subset](#the-c-subset)
- [What the machine does to a compiler](#what-the-machine-does-to-a-compiler)
- [The memory map](#the-memory-map)
- [The zero page is the scarce memory](#the-zero-page-is-the-scarce-memory)
- [The heap stack](#the-heap-stack)
- [The calling convention](#the-calling-convention)
- [ROM](#rom)
- [Types the CPU does not have](#types-the-cpu-does-not-have)
- [Arithmetic and the coprocessor](#arithmetic-and-the-coprocessor)
- [The libraries](#the-libraries)
- [What the machine gains](#what-the-machine-gains)
- [Separate compilation and the archives](#separate-compilation-and-the-archives)
- [What a project carries](#what-a-project-carries)
- [BASIC](#basic)
- [What this rejects](#what-this-rejects)
- [Settled, recorded so they are not reopened](#settled-recorded-so-they-are-not-reopened)
- [Staging](#staging)
- [Testing](#testing)

## The core insight

The assembler does not run on the machine. `asm.ts` is TypeScript in the
browser. It turns text into bytes and hands them to the machine. Nobody
thinks of it as a program the CPU runs.

The C compiler is the same kind of thing. It lives beside `asm.ts`, in
`packages/core/src/cc/`, and it emits SimpleCPU assembly TEXT. The existing
assembler turns that into bytes. C never runs on the machine. C produces what
runs on the machine.

Emitting text rather than bytes decides most of what follows:

- The assembler stays the one source of encoding truth.
- The generated assembly is real, readable, and lands in the listing.
- Every C line maps to assembly lines. The assembler already maps assembly
  lines to instruction slots, in `instrToLine`. Chained, that is C-level
  stepping for free.
- Inline `asm("...")` is trivial. The output already is assembly.

This is honest to the magic box law. A compiler is a tool, not hardware. The
CPU stays 8 bit and the student can read what their `for` loop became.

## Seeing the layers

The editor gets a language mode: asm, C, and later BASIC. Whatever the mode,
the panel beside the editor shows the layer BELOW, live, following execution.

In C mode: the C source on the left. A read-only generated-assembly pane that
scrolls with the program, the current C line highlighted and its expansion
visible. The microcode flow behind it, as today.

Three step buttons, which are the three layers:

| button | runs until |
|---|---|
| Step | the C line changes, by the source map |
| Step asm | the next instruction, as today |
| uStep | the next microcode row, as today |

Press each in turn and one C statement becomes six instructions becomes forty
signals.

BASIC adds a fourth. The interpreter keeps its current line number in a known
RAM variable. The UI reads it and highlights the BASIC line. Four layers
moving together on one screen is the thesis of the project made visible.

### A tab for every file, all of them open

A project is many files now, so the editor is a strip of tabs and every file
in the project has one. Nothing is closed, because a project of five files is
not a filesystem to browse. It is five things you are working on.

The tabs carry what a person needs to see without opening anything:

- The file holding the build line is marked, because there is exactly one and
  it decides what the others are for.
- A file with an error is marked, so a failed build does not send anybody
  hunting through five tabs for the message.
- A file holding a breakpoint is marked. Breakpoints span files now, and one
  left in a file nobody has open is otherwise a mystery when it fires.

**The view follows execution.** Step into a function that lives in another
file and that tab comes forward with the line highlighted. Without it,
C-level stepping across a multi-file program would step into nowhere.

Derived files are tabs too, on the pane BESIDE the editor rather than in it,
because they are not yours to edit. That is the generated assembly,
`ROM.h`, the resolved build, and the zero page map. Your files on one side and what the
tool made of them on the other, which is the layers idea again in the
furniture.

### A breakpoint on a line of C

Breakpoints are instruction slots today, and a C breakpoint is the same thing
with the source map in front of it. Click a C line, and the machine stops
BEFORE that line runs, which is what a breakpoint on an instruction already
does.

Three things decide whether it feels right, and all three are the compiler's.

**One slot per line is the statement boundary.** A C line can generate
instructions in more than one place. A `for` header generates three: the
setup, the test, and the step. Breaking on the first in program order would
be the setup, which runs once. Breaking on all of them would stop three times an
iteration. So the compiler marks ONE slot per line as the
statement start, as a debugger's line table does. The breakpoint goes there. For a `for`, that is the test.

**A line with no code snaps forward.** A declaration, a comment, a blank
line. The breakpoint lands on the next line that generated something, and the
editor shows where it landed rather than pretending it took the click.

**The peephole pass carries the attribution.** A pass that drops the line an
instruction came from breaks every breakpoint. Every C-level step goes with
them. That is a requirement on the optimiser
from the first commit, not something to repair later.

An inlined library call has no call to step into, and a breakpoint on
`gpu_clear(0)` lands on the first `OUT` it became. That is correct rather
than a limitation: the expansion IS the line.

## The C subset

A defined subset, not C. The target is too small for anything else. A subset
with a written boundary is teachable. A large language with holes is not.

Types: `char` and `unsigned char`, 8 bits. `int` and `unsigned int`, 16 bits.
Pointers, 16 bits. Arrays. Structs later. `double`, `float` and `long` too,
and every operation on them is a coprocessor command. The section on the
types the CPU does not have says how.

Statements: `if`, `else`, `while`, `for`, `do`, `switch`, `return`, `break`,
`continue`. Functions with byte and word arguments and one return value.
Globals and locals. Inline `asm`.

Port I/O is two intrinsics, `out(port, value)` and `in(port)`, and every
built-in constant the assembler knows is a C constant too: `GPU_CMD`,
`CMD_CLEAR`, `BTN_FIRE`, `ACP_F64`, all of them.

Everything that is refused is refused with a message that names the reason
and the alternative. Reading ROM names `rom_copy`. Complex arithmetic names
`acp_point`. A call deeper than the hardware stack is a crash rather than a
compile error, and the crash says which stack ran out.

## What the machine does to a compiler

The target is harsher than a 6502. Each row here shapes the code generator.

| constraint | consequence |
|---|---|
| ALU operands are `imm8`, zero page, or a byte at `[D1+n]` or `[D2+n]` | every expression temp lives in `$00` to `$FF`. The zero page is the register file. A local is an operand in place |
| one byte register, two pointer registers | accumulator codegen, in the Small-C tradition |
| no multiply or divide. Shifts go one bit at a time | a constant multiply, divide or shift is shifts and adds. The rest is a runtime library on the ACP. mul8, div8, mul16 and div16 are the pure CPU version, written as demos |
| CMP and TST keep A. A jump on each flag, set and clear | a condition is CMP and a jump on its flags, straight from the condition. Signed compare is N xor V. A word load sets Z, so a NULL test is the load |
| no indexed store, loads only through `[D1+A]` | an array store steps D2 with `LD D2 <- D2+A` first |
| a 16 bit add only in the address stage, into D1 or D2 | frames, pointer steps and word increments go through `LD Dx <- Dy+n` |
| the hardware stack is not addressable | C locals cannot live on it |
| Harvard, program memory read only, 3 byte slots | no self-modifying code. `const` data the CPU reads must be in RAM |
| JSR takes an immediate, and there is no indirect jump | function pointers and large `switch` need a dispatch table |

Codegen evaluates into A and bounces through zero page temps, the way a
careful student would write it. A leaf operand, a constant or a variable,
goes to the ALU directly without a temp. Every emitted block carries the C
it came from as a comment, so the assembly is itself a teaching document.

A peephole pass in src/cc/peephole.cpp then tidies what the generator
wrote. It drops a jump to the next line. It drops a load of a value A or
D2 already holds, and a load nothing reads. It drops a store to a temp nobody
reads. It follows where the flags stop mattering, so a load goes only when
no jump reads the flags it set.
Codegen marks every statement boundary with `;@stmt` and fences inline
assembly with `;@barrier`. Nothing moves across a barrier, and no temp
is live across a statement mark. A static function of up to 40 nodes is
expanded at each call, with its parameters in the caller's frame.
docs/design/instruction-set.md lists the sequences it emits for each
construct, with their costs.

## The memory map

A compiled program cannot place its tables by hand the way Pac-Man puts its
palette at `$2000`. The runtime owns a map, and the compiler obeys it.

```text
$0000-$00FF   zero page: __ret, the compiler's temps, then __zp and what fits
$0100-        the runtime: the ACP scratch block, sized by the program,
              and the six byte shadow of the ACP configuration
              globals, then bss
              ... heap grows up ...
              ... stack grows down ...
$FAC0-$FFFF   the text screen, when mapped
```

The stack top is a default, `$FAC0`, below a 42 by 32 text screen, whether
or not the program maps one. `simplecpu-cc --heap-stack-top ADDR` moves it.
BASIC's build and BASIC and C projects pass `$F000`, below BASIC's 4 KB
screen. A stack that started above the screen would overwrite it on the
first call.

## The zero page is the scarce memory

The zero page is not an optimisation on this machine. It is the only place a
byte can be reached at all without spending a pointer register.

There is no `LD A <- [addr16]`. The 16 bit address form exists for the D
registers and nothing else. A byte global above `$00FF` is read by putting
its address in a D register and going through it. And every ALU operand is an
immediate or a zero page address, so arithmetic on anything else bounces
through a zero page temp first.

| where a byte lives | read it | arithmetic on it |
|---|---|---|
| the zero page | 1 instruction | 1 instruction, direct |
| a frame local, `[D1+n]` | 1 instruction | 3, through a temp |
| a global above `$00FF` | 2, and it spends D2 | 4, through a temp |

D1 is the frame pointer, so the bottom row spends D2, which is the only
pointer left and the one an array wanted. That is what makes the zero page scarce rather than
convenient.

### The allocator counts, and weights a loop

Most programs should never say anything about the zero page. The compiler
allocates it, and the rule is a count.

Every mention of a variable scores. A mention inside a loop scores ten times
more for each loop it is inside. So a variable touched once in a loop scores
ten, and beats one touched five times in setup code. Being in a loop at all
outranks not being in one, which is the answer that matters.

When a loop's bound is a literal the compiler reads it and uses it, capped so
that one long loop cannot swamp the whole program. `for (i = 0; i < 100; i++)`
weights by a hundred rather than by ten. When the bound is a variable, the
flat weight stands.

The zero page then goes to the highest scores until it is full.

### What counting cannot see

A count is mentions, not executions, and the two are not the same thing.

Two variables each mentioned once, each inside one loop, score the same. One
of those loops may turn three times and the other a hundred thousand, and
nothing in the source says which. A bound that is a variable is a bound the
compiler does not know.

That is not a flaw to be engineered away. It is the ceiling of what any
static rule can do, which is why every real compiler that cares about this
takes a profile instead. The weighting above buys the difference between in a
loop and not in a loop, and that is most of the value. It cannot buy the
difference between two loops.

So the count is a good default and never an authority, and the next section
is what a program says when it knows better.

**So the machine gains a counter, and the count can be measured instead.**
The heat maps hold cycle stamps, which say what was touched recently. A pair
of access counters beside them says how often.

Run the program, read the real accesses per address, and allocate the zero
page from measurement. The compiler knows which address each variable got,
so the counts map straight back onto names. A second build then places them
by what the program did rather than by what its source looked like.

The static count stays as the first build's answer, because there is nothing
to measure until a program has run once. See the machine section for the
counter itself.

### `__zp`, for when the count is wrong

A heuristic is a heuristic, so there is an override:

```c
__zp unsigned char frame;      /* I know this one is hot */
```

A qualifier rather than a pragma, because `__ROM` is already a storage class
naming a memory and the zero page is a memory. That is also what SDCC and
cc65 do, `__data` against `__xdata` and `__zeropage__`, for this same reason.
And a qualifier travels with its declaration, where a pragma listing names
can go on naming a variable somebody deleted.

Allocation is then ordered:

1. The compiler's own reservations. The stack pointer, and the expression
   temps, whose number comes from the deepest expression in the program.
2. Everything marked `__zp`, in declaration order. **If this does not fit,
   that is the error**, and it names the bytes asked for, the bytes free, and
   what the reservations took.
3. Whatever room is left, by weighted count.

Putting the error at step 2 keeps it honest. It can only ever say that the
`__zp` set does not fit, never that an expression got deep and a variable
moved.

`__zp` on an automatic local is an error naming `static`. A zero page local is
a static by definition, and a static local in a function that recurses is a
bug rather than a speed-up.

A program with no `__zp` in it anywhere is the normal case. The qualifier is
for the day the count is wrong and somebody can see that it is, which the
section above says will happen.

### None of it is hidden

The build tab carries the zero page map beside `ROM.h`: every byte, what has
it, the score that won it, and how many are left.

An allocator that quietly decided which of your variables are fast would be
the magic this machine exists to remove. One that shows its working is a
lesson in why the zero page mattered so much on the machines this copies.

## The heap stack

The hardware stack is its own memory, 2048 bytes by default, reached only
through SP, and nothing can address into it. So C frames live in data RAM,
on a stack the C runtime keeps itself: the heap stack.

The machine gives it a register. D3 is the heap stack pointer, with the
forms a frame needs. Bytes, words and ALU operands sit at `[D3+n]`, and
address adds move it and copy it. A frame is one address add each way:

```asm
LD D3 <- D3-12        ; the frame, through the address adder
; the body: every local is [D3+n]
LD D3 <- D3+14        ; the frame and the two argument bytes
RET
```

It was six instructions when the pointer was a word in the zero page.
There was a SUB and an SBC on its two bytes. Then three once the address adder could load
D1, which was the frame pointer. Now D3 is both the pointer and the frame
base, so nothing is loaded or stored at all.

The costs:

| operation | instructions | why |
|---|---|---|
| read or write a local | 1 | `[D3+n]` is a real addressing mode |
| arithmetic on a local | 1 | the ALU takes `[D3+n]` as its operand |
| a prologue | 1 | an address add |
| an epilogue | 2 | an address add and RET |
| recursion | 2 more at entry | the heap stack check below |

Return addresses stay on the hardware stack. JSR and RET do not change, and
they stay one instruction each. The heap stack holds data only, so the two
never confuse each other. SP is 16 bits, and `simplecpu --stack-size` makes
the hardware stack up to 64K. A program that recurses past it crashes on
the hardware stack, and the error says which stack ran out.

D1 and D2 are both free for pointers. An array load is `D2 = base, A =
index, LD A <- [D2+A]`, one instruction. An array STORE has no `[D2+A]`
form. `LD D2 <- D2+A` steps the pointer to the element, then the store
goes through `[D2]`.

### How big the heap stack is

It runs from under the text screen down to a floor. The top is `$FAC0`
unless `--heap-stack-top ADDR` moves it. By default the floor
is the end of the program's data, so the heap stack gets every free byte.
`#pragma heap_stack_size N` in any file, or `simplecpu-cc
--heap-stack-size N`, sets the floor N bytes below the top instead. A size
that would reach into the program's data is refused.

The compiler knows every frame's size and every direct call. So it adds
up the deepest point of a chain of calls with no recursion. It refuses a
main whose chain cannot fit. That check costs nothing at run
time. What it cannot add up it checks at run time, at the entry of:

- A function that can recurse, since its depth depends on its data.
- A function named in inline assembly or taken as an address. The
  compiler cannot see from how deep it is called.
- A function main does not reach through calls, such as one BASIC calls
  with USR.

The check is two instructions against a constant the compiler works out.
The constant is the floor plus the deepest unchecked chain under the
function:

```asm
LD D2 <- D3-__hs_f    ; C is the adder's carry: clear when D3 is below
JNC __stack_overflow
```

`__stack_overflow` prints `HEAP STACK OVERFLOW` on the screen and halts. In
the BASIC interpreter the checks cost 0.35% of the benchmark's cycles.

## The calling convention

The caller writes the arguments below its frame through D2, at fixed
offsets, moves D3 down to them and JSRs. The callee moves D3 down past
its own locals, and everything is `[D3+n]`. Its locals are at the
bottom, the saved temps above them, then the arguments. On return it adds its frame
and the arguments back, so the caller's D3 is its frame base again.

A byte comes back in the low byte of `__ret`. A word comes back in
`__ret`. Every argument, local and saved temp is in RAM, in the order the
compiler laid it out. That is what a student stepping through a call
should get to see. An assembly routine called from C gives D3 back as it
found it.

## ROM

The cartridge is the device. ROM is what is on it. The GPU and the audio chip
read it and the CPU cannot. C has no storage class for memory the CPU cannot
see. So the compiler adds one.

```c
__ROM const unsigned char ship[] = { 1, 8, 8, /* pixels */ };
__ROM const char fmt[] = "SCORE %u";
```

The compiler emits exactly what a hand-written program does today:

```asm
.data
ship:   db 1, 8, 8, ...
fmt:    db "SCORE %u", 0
```

Four rules make it airtight.

- `__ROM` implies `const`. The cartridge is read only, so writing `const` too
  is allowed and redundant, and a ROM object is never assignable.
- A ROM object needs a constant initializer. ROM is built at assembly time and
  there is nothing else it could be built from.
- A ROM object cannot be read from C. `ship[0]` is an error, and the error
  names `rom_copy`. Its address and size are the whole of what the CPU gets.
- `const` without `__ROM` is RAM. `const unsigned char ramps[12]` goes to
  `.ram` as `db` lines, readable by the CPU. That is the fly demo's colour
  table, and the reason is one a student can already see there.

A string literal passed straight to `gpu_printf` goes to ROM automatically.
The template is ROM by hardware definition.

### rom_t

A cartridge address is bank, high, low: 24 bits, on a machine whose `int` is
16. The type is `rom_t`. On a named object it never exists at run time and
folds into three immediates. A program that picks a sprite from a table of
addresses holds `rom_t` values in RAM, 3 bytes each. The library then emits
three loads. `rom_t + int` is a 24 bit add the compiler can emit, and it is
allowed from the start.

Three macros give the parts. They are part of the type, so they live in the
library's `rom.h`, written once and tested once:

```c
#define ROM_BANK(r)   /* bits 23 to 16 */
#define ROM_HI(r)     /* bits 15 to 8 */
#define ROM_LO(r)     /* bits 7 to 0 */
```

The macros give the bytes. Each inline library function knows which PORTS the
bytes go to. A cartridge address is `GPU_CART_*` on a command with no index
and `GPU_SRC_*` on a command that took an index first, the two frames rule.
That is the exact hazard `port-batches.test.ts` exists to catch, and a
student cannot get it wrong through `gpu_sprite_def`.

### ROM.h

`ROM.h` is the cartridge map, as built. Every ROM object from both sources,
uploaded assets and `__ROM` declarations, in address order, regenerated on
every compile. It is output, like the listing, and shown as a read-only tab.

```c
/* ROM.h. The cartridge as built. Regenerated every compile. Do not edit. */

/* $000000  ship, declared in main.c line 12 */
#define ROM_ship_SIZE     195
#define ROM_ship_BANK     0x00
#define ROM_ship_HI       0x00
#define ROM_ship_LO       0x00

/* $0000CC  theme.mid, from the Assets pane */
extern __ROM const unsigned char ROM_theme[];
#define ROM_theme_SIZE    1842
#define ROM_theme_BANK    0x00
#define ROM_theme_HI      0x00
#define ROM_theme_LO      0xCC
```

Two naming rules keep it clash free. The constants are always
`ROM_<label>_*`, whichever way the object arrived. The object's own name
follows whoever named it: an uploaded asset is `ROM_theme` because the tool
named it, and a C-declared object stays `ship` because the programmer did.
So `ROM.h` does not redeclare a C object. It adds the constants and says
where the object came from. Declaring `ship` in C and uploading `ship.png`
is a compile error, not a silent shadow.

Images also get `_W`, `_H` and `_FRAMES`, read from the blob header at build
time.

What makes this possible is that ROM layout does not depend on code
generation. It depends on sizes and order, and both are known from the
declarations and the asset bytes. So one build is two phases. Collect every
ROM object and lay it out. Emit `ROM.h`. Then compile the code with it in
scope. A program can `#include "ROM.h"` and use `ROM_ship_HI` for an object
declared three lines above the include. The number exists by the time
codegen reads that line.

The file reads as a memory map of the cartridge. Its tab is the C layer's
equivalent of the zero page pane. The assembler's dedup still applies: two
ROM objects with identical bytes get one address, and `ROM.h` shows both
labels at it.

This also fixes a hazard that exists today. Invaders writes its sample lengths
out by hand, with a comment admitting `get_sizelo` cannot see a `db` block.
In C it is `apu_def_sample(0, ROM_hit, ROM_hit_SIZE, 60)`, and the size
cannot be wrong because nobody typed it.

### rom_copy

The one way to read ROM is to copy it. Pac-Man does this today. The maze
lives in ROM, `CMD_COPY` moves it into RAM in one cycle, and the CPU walks
the copy.

```c
rom_copy(maze, ROM_maze1, ROM_maze1_SIZE);
```

It lives in the `sys` library, it is `CMD_COPY` underneath, and it is the
answer every ROM read error points at.

## Types the CPU does not have

The ACP works in `i64`, `u64`, `f64` and `c64`. The CPU can move those bytes
and do nothing else with them. The compiler gives the first three real
operators anyway, and every one of them compiles to a coprocessor command.

`double` is a C type. `a * b + c` on doubles is legal, and it becomes three
block writes, three commands and a read. `float` is accepted and means the
same thing, because the ACP has one float format and a program written for
another compiler should not fail on the spelling. `long` and `long long` are
`i64` the same way. Comparisons go through `ACP_CMP`, and the CPU tests the
minus one, zero or one it hands back. Conversions between `int` and `double`
go through `ACP_CVT`. A literal like `3.14` is eight bytes of IEEE 754,
big-endian, written by the compiler into `.ram` as `db` lines.

A double is eight bytes and never fits in A. So every double lives in RAM,
and the runtime copies it into the scratch block to operate on it. That is the
cost: about fifty instructions an operation, most of them the copies. Chained expressions can keep their temporaries contiguous and skip the
copies. That is the first optimisation to make once there is something to
measure.

`c64` stays a library type in v1, reached through `acp_point` and `acp_run`.
Complex arithmetic in the language itself is a later chapter.

There is no software float library. The ACP is the float unit. A student who writes `x = y * 0.5` watches the layers pane turn it into a
block write and `ACP_MUL`. That is what the machine does with a double, and
the only thing it can do with one.

The block interface is still there for anything the operators do not cover.
It reads naturally, because the block IS a struct:

```c
struct { double a, b, r; } blk;     /* A, B, R, contiguous: the block layout */
acp_point(&blk, ACP_F64, 1, 1);
acp_run(ACP_ADD);
if (acp_flags() & ACP_DIVZERO) { ... }
```

## Arithmetic and the coprocessor

The CPU has no multiply or divide, and it shifts one bit at a time. The
runtime does not have to build the rest out of adds. The ACP is on the bus. The machine's own rule is that heavy
work goes to a peripheral, stated as a cheat. The compiler's runtime uses it
wherever it wins, and it wins almost everywhere.

The line is cost, not width. A byte multiply through the ACP is fifteen
instructions. mul8 on the CPU is about a hundred.

| operation | on the CPU | through the ACP |
|---|---|---|
| `int * int` | about 300, mul16's carry chain | about 15 |
| `int / int` and `%` | about 300, div16 | about 15 |
| `char * char` | about 100, mul8 | about 15 |
| `int >> 1` | 6, SHR and ROR | about 15 |
| `int << 1` | 6, SHL and ROL | about 15 |
| `char << 1` | 3 | about 15 |
| `&`, `\|`, `^`, `+`, `-` | 1 or 2 | about 15 |

So the CPU keeps what it does natively in a few instructions. That is the
logic ops, add and subtract, and a shift by a constant. A multiply by a
constant with few set bits is shifts and adds. So `x * 10` is three
shifts and an add. A divide or remainder by a power of two is a shift or an AND.
A multiply by a variable goes to the coprocessor, at every width. So do
any other divide and a shift by a variable.

Why the ACP is a flat fifteen. The runtime owns a scratch block, pre-zeroed
at startup. A 16 bit operand is two stores into an 8 byte slot. The command
is a handful of OUTs. The low two bytes of the result are two loads. And the
low 16 bits of a product do not depend on whether the operand was sign or
zero extended. So a multiply never pays for a sign fill. Only division and
the arithmetic right shift do.

The block the runtime uses is sized by the program rather than by the worst
case. The next section says how.

### The device has state, and the runtime must not be seen

`ADDR`, `FMT`, `RFMT`, `ROWS` and `COLS` are latched in the ACP, and none of
them reads back. A runtime that borrowed the device for `a * b` would leave
it pointed at its own scratch. A student who wrote `acp_point`, then `n = i *
2`, then `acp_run(ACP_ADD)` would run the add on the runtime's scratch.

So the library shadows the configuration in RAM, in six bytes: the two
address bytes, the two formats, and the two dimensions. `acp_point` stores
them. `acp_run` sends them again before every command. Six extra OUTs per
user-level ACP command. The runtime can then use the device whenever it
likes without anyone noticing. This is the one place the library is not a
thin inline, and the reason is written on it.

`ACP_COLS_B` is not shadowed, because the runtime never writes it. A program
that set it for a matrix product still has it.

### The scratch block is sized by the program

The runtime needs a block of its own for the work it does on the program's
behalf. It is scratch and only one operation is live at a time, so one block
serves all of them.

Its size is not a constant. The compiler emits every one of these commands,
so it knows at compile time which ones a program performs. It reserves the
largest block any of them needs, and no more.

| what the runtime does | A | B | R | block |
|---|---|---|---|---|
| integer multiply | 8 | 8 | 16 | 32 |
| integer divide, remainder, right shift | 8 | 8 | 8 | 24 |
| double add, subtract, multiply, divide, compare | 8 | 8 | 8 | 24 |
| double negate, absolute value, square root | 8 | none | 8 | 16 |
| int to double, double to int | 8 | none | 8 | 16 |
| a program that never uses the coprocessor | | | | 0 |

A program doing only double arithmetic reserves 24. One that only converts
reserves 16. One that never reaches the ACP reserves nothing.

The 16 in the first row is the device's, not the language's. A 1 by 1 integer
multiply writes 128 bits, because two 64 bit integers multiply to exactly
that, and C wants the low two bytes of it. The block still has to hold what
the device writes.

A program's own blocks are its own. They are C structs, so they are already
the size their fields say, and the runtime never touches them.

### Two runtimes, one switch

mul8 and div8 exist to teach the carry chain. A compiler that hid it would
waste them. So there is a switch, and it turns INTEGER multiply, divide and
shift back into the software the demos are. A student compiles the same
`a * b` both ways. In the same layers pane, three hundred instructions stand
against one coprocessor command.

The switch reaches nothing else. There is no software float to fall back to,
so `double` uses the ACP whichever way the switch is set. That is not a gap.
A CPU with no multiply has no business pretending to have a double. The
switch is a lesson about the carry chain, not a second machine.

The default is the ACP, which is the owner's decision. The ACP path is not an
optimisation to be turned on later either. It is how `double` and `float`
work at all, so it is there from the first commit.

## The libraries

Mostly headers, mostly inline. `gpu_clear(c)` is two port writes. As a call
it would be a JSR, a frame, two OUTs and a RET, four times the cost. Stepping
into it would show stack housekeeping instead of hardware. As an inline it
compiles to the two OUTs a student wrote by hand last week.

The rule: a library function is inline unless it has real logic. Real
functions only where there is work, such as `printf` argument marshalling,
`memcpy`, and the multiply and divide the compiler needs anyway. The layers
stay visible BECAUSE the library is thin. Name the hardware, do not wrap it.

| library | contents | mostly |
|---|---|---|
| `sys` | `halt`, `wait_frame`, `frame`, `rand`, `memcpy`, `memset`, `peek`, `poke`, `rom_copy`, and the mul, div and shift runtime in both of its forms | real functions |
| `gpu` | one function per `CMD_*`, the same name in lower case. `gpu_printf` | inline |
| `apu` | `apu_def_sample`, `apu_trigger`, `apu_note_on`, `apu_load_midi`, `apu_status`, `apu_voices` | inline |
| `io` | `io_pad()`, `io_key()`, and `io_pressed(BTN_FIRE)` | inline |
| `acp` | `acp_point`, `acp_run`, `acp_flags`, the opaque types, typed helpers | inline and a few functions |

One function per command, named after it, is deliberate. The manual's command
table maps one to one onto the header. The coverage test that refuses an
undocumented command can refuse an unwrapped one.

A quiet win. The IN sets no flags trap disappears. `if (gpu_hit_test(1, 2))`
compiles to the read AND the mask. The sharpest gotcha on the machine becomes
something the layer above does right.

`gpu_printf` is variadic. The compiler marshals the arguments into a RAM
block, big-endian and sized by the format, and passes the pointer. That is
exactly what `CMD_PRINTF` wants, so the library function is tiny and the
compiler does the work.

Headers are text bundled with the compiler, on the host, like the demo
sources: `#include <gpu.h>`. They are documented in the manual as their own
section and pinned by coverage like the ports and commands. Each header is
also a teaching document. Open `gpu.h` and every inline is two or three OUTs
with port names the reader already knows.

## What the machine gains

Five changes, and none of them is the compiler. Each stands on its own and
would improve hand-written assembly too.

### A wider stack pointer

SP goes to 16 bits and the stack to 4096 bytes, so nesting stops near 2048
deep rather than 128. It touches one constant, the value SP starts at, four
masks, the two bounds that crash, the stack strip and two lines of
documentation. No new signal and no new wire: the step unit is already 16 bit
for PC and the D registers.

Sixteen rather than twelve because every register here is 8 bits or 16, and a
12 bit SP would be the only odd width in the machine. Putting the bound on
the MEMORY instead sets it beside `RAM_SIZE` and `CART_SIZE`, which are
already numbers somebody chose.

### An indirect jump and call

Every jump target on this machine is an immediate in the operand. So a jump
to one of many places, chosen at run time, is a chain of compares. A BASIC interpreter dispatching forty tokens pays that chain on every
statement it runs, forever. A C function pointer cannot be expressed at all.

So the machine gains two: a jump and a call, each to the address held in a D
register. Both, rather than the jump alone with a trampoline for calls. An asymmetry
between `JMP` and `JSR` is a thing a reader would have to learn and
remember.

**It is spelled `JMP D1`, not `JMP [D1]`.** Square brackets are contents-of on this machine, so `[D1]` is the BYTE in
RAM at the address in D1. This instruction wants the register itself, with
no dereference, so it takes no brackets. `JMP D1` and `JMP D2`, matching the way every other operand names
the register it uses.

The cost is four opcodes of the 180 free, and two signals that load PC from a
D register. Naming the register costs both twice, because microcode keys on
the opcode. They sit at 0x04 to 0x07, which extends the branch family down
into the gap below `JMP`. That signal needs a wire in the datapath, and its entry in the
signal reference and the schema. `JSR D1` reuses the pushes `JSR` already
has, with that signal in place of `PC_LOAD`. Rule 1 covers the conflicts already, because the new signal
writes PC and so does every other jump. `JMP D1` is one microcode row and one cycle, because PC and the D registers
are both 16 bits and it is a straight transfer.

A jump table then falls out of what already exists. The table is words in
RAM, the index goes in A, `LD D2 <- [D1+A]` reads the entry, and `JMP D2`
takes it.

**A function pointer is not a ROM address.** The destination is not in the
cartridge. Code lives in PROGRAM memory, which is its own 64K of three byte slots. The
cartridge is a fourth memory, and only the GPU and the audio chip read it. A function pointer never touches it.

A function pointer is an instruction SLOT number, 16 bits, and the assembler
already hands them out. `&myfunc` on a code label gives the slot, and a table
of them sits in ordinary data RAM where the CPU can read it:

```asm
        LD D1 <- tab
        LD A <- [n]
        LD D2 <- [D1+A]        ; the entry, a slot number
        JMP D2

.ram
tab:    dw &f0, &f1, &f2
```

Checked on the assembler rather than assumed. A code label reports
`kind: code`, `&` on one folds to its slot number, and `dw &label` in `.ram`
stores it.

The real difficulty is elsewhere, and it is Harvard. A code pointer indexes
program memory and a data pointer indexes data RAM. Both are 16 bits and they mean different memories, so casting one to the
other is meaningless. C here cannot let `void *` hold a function. The
compiler keeps the two apart, and says so when a program mixes them.

PC counts instructions rather than bytes, so pointer arithmetic on a function
pointer counts functions, not bytes. It is refused for the same reason.

### A per address access counter

The machine keeps two heat maps already, `ramReadAt` and `ramWriteAt`, in
cycle stamps. They answer what was touched recently. Two counters beside
them, `ramReads` and `ramWrites`, answer how often, which is the question
the zero page allocator actually has.

They saturate rather than wrap. A tight loop at MAX would reach the top of a
32 bit count in minutes, and a wrapped total is worse than a stuck one.

They reset where the heat maps reset, on Restart and on load. The UI can
reset them alone, so a program can be profiled over one phase of itself
rather than over its startup.

No program can observe them, which is the same rule the heat maps already
live under. They are the host watching, not the machine telling.

Two uses, and the second is the one that pays for it. The compiler places
the zero page from a measured profile. And the zero page pane shows counts
beside recency, so a person watching a program run can see which addresses
it actually works on.

### A RAM to RAM block move

There is no way to move a block of RAM on this machine. `CMD_COPY` brings
the cartridge in, and the ACP writes only its own block, so a copy is a byte
at a time in a CPU loop. That is `memcpy`, every struct assignment, every
array copy, and BASIC's collector compacting its string heap.

It belongs to the GPU, in the memory block beside `CMD_COPY` and
`CMD_MEMMAP`, which has fourteen free. The GPU is already this machine's DMA
engine: it moves the cartridge into RAM, and it writes tables into RAM for
`CMD_COLLIDE_ALL` and the palette. RAM to RAM is the missing third. It is not
the IO subsystem, which is input devices, and a block move is not input.

The ports fall out of `CMD_COPY` with the bank slot empty, so four of the six
are the names that command already uses:

| port | `CMD_COPY` | `CMD_RAM_MOVE` |
|---|---|---|
| `$02` | `GPU_CART_BANK` | not read |
| `$03` | `GPU_CART_HI` | `GPU_FROM_HI` |
| `$04` | `GPU_CART_LO` | `GPU_FROM_LO` |
| `$05` | `GPU_DEST_HI` | `GPU_DEST_HI` |
| `$06` | `GPU_DEST_LO` | `GPU_DEST_LO` |
| `$07` | `GPU_LEN_HI` | `GPU_LEN_HI` |
| `$08` | `GPU_LEN_LO` | `GPU_LEN_LO` |

**MOVE and not COPY, because the regions may overlap.** A collector compacts
a heap downward over itself, and that is the case that motivated this. So it
has to behave the way `memmove` does rather than `memcpy`. Written down
because a reader would otherwise have to find out.

Two new names, four reused, one opcode of the fourteen free. A programmer who
knows `CMD_COPY` knows this one on sight.

### Compares, address adds and shifts

Later, with the compiler's output there to measure, the CPU gained 35
opcodes. CMP and TST set the flags and keep A. The ALU and CMP take an
operand at `[D1+n]` or `[D2+n]`. `LD Dx <- Dy+n` and `LD Dx <- Dy+A` put an
address sum into a D register. SHL, SHR, ROL, ROR and ASR shift A by a bit.
docs/design/decisions.md records why each one pays its rent. Between them
and the code generator's work, the BASIC interpreter compiles 12% smaller
and its loop benchmark runs in a third of the cycles.

## Separate compilation and the archives

A program is many source files, and the libraries are archives: `gpu.a`,
`io.a`, `acp.a`, `audio.a`, `sys.a`.

The link step works on assembly TEXT, which is what the compiler already
emits. Each translation unit becomes one block of it. The linker resolves the names between them, drops what nothing reaches, and
hands one text to the assembler. So the assembler stays the one source of
encoding truth. An archive member is a block of assembly with a symbol list,
not a binary object.

Three things follow, and the first two are work.

**Labels get scoped.** Every label is global in one assembly file today, so
two units with a `loop:` collide. The compiler mangles each unit's private
labels and leaves the exported ones alone. A `static` function is private by
that rule rather than by a second mechanism.

**The zero page is allocated whole-program.** Temps and globals want one
view of it. That happens at link time, once the units are known. The heap stack is what makes that easy. Locals are
frame offsets, so no unit needs to know another's frame.

**Dead code costs nothing.** The linker takes only the members something
reaches. A program that never divides does not carry the divide, which
matters on a machine where program memory is 64K slots and the whole budget.

### Assembly is many files too

The linker works on assembly text, so a hand-written program gets the same
treatment as a compiled one. `; build:` in a comment, `as` in place of `cc`,
and the rest is identical.

Three things are the assembler's rather than the compiler's.

**A label is local to its file unless it is exported.** In C the compiler
knows what is `static` and mangles the rest. Assembly has no such word, and
every shipped program here is full of `loop:`, `done:` and `next:`. Global by
default would mean that splitting one file into two renames most of it, so
the default is the other way round and `.export` shares a name. It is the
opposite of C's default, for the opposite reason.

**A `.zp` section, beside `.ram` and `.data`.** Assembly picks its own
addresses today by declaring early, and across linked files that becomes an
accident of link order. A third section says it instead, and it is the same
256 bytes with the same error when it does not fit.

**The entry point is the first file on the build line.** Its first instruction
is slot 0, which is how a single file already works. The build line is where
that order is visible.

### C and assembly in one program

This falls out and is worth having on purpose. The compiler emits assembly
text and a hand-written file IS assembly text, so the linker cannot tell them
apart:

```c
// build: cc -o game main.c menu.c fast.asm -lgpu
```

A program in C with its inner loop written by hand, calling in both
directions. C reaches a `.export`ed label as an ordinary function. Assembly reaches a C
function the same way, as long as it builds the frame the calling convention
describes.

A C function that only BASIC or assembly calls is one `main` never reaches,
so the dead code rule would drop it. The project build therefore reads the
callers first. It collects the names after `CALL`, `JSR`, `JMP` and `USR` in
every `.bas` file, and every name in every `.asm` file outside comments and
strings. Those names go to the compiler as extra roots beside `main`, in
`CcOptions::externalCalls`. The compiler keeps each C function they name,
and all it calls, and then the assembler joins the output with the `.asm`
files. Inline assembly is read the same way inside the compiler. A root that
names a `static` function is refused, because its label is private to its
file.

### The build line, in place of a Makefile

A program of many files needs somebody to say which ones, and in what order,
against which archives. Make is a language of its own, with its own tabs and its own rules. Meeting
it before you have met C is a bad first day.

So the build is a comment, in the file that holds `main`:

```c
// build: cc -o game main.c menu.c output.c -lgpu -lio
```

That is what a person would type on any Unix box, which is the point. The
flags are the real ones. `-o` names the output, `-l` links an archive, and a
`-m` flag is a machine option, so `-msoft-mul` is the switch that puts
integer multiply and divide back into software.

The runtime is linked whether or not anything asks, the way nobody writes
`-lc`.

There may be more than one build line, and they run in order. So the two
step shape is there for anyone who wants to see objects exist:

```c
// build: cc -c main.c menu.c output.c
// build: ld -o game main.o menu.o output.o -lgpu -lio
```

Exactly one FILE carries them. None is an error saying what to write. Two is an error naming both files,
because a program with build lines in two places cannot say which one built
it.

The resolved build gets a tab of its own beside `ROM.h`: which files, which
archives, in which order. A build that is a comment must not also be a
mystery.

And the build line is an input to the compile, so it joins the hash in the
next section. Changing `-msoft-mul` changes every multiply in the output.

### Where inline meets an archive

The port wrappers must stay inline. `gpu_clear` is two OUTs. A JSR around them would cost more than they do, and
would show stack work where the layers pane should show hardware.

So they are inline in the header AND an out-of-line member in the archive,
which is what C's `inline` has always meant. The compiler expands them at the
call site. The member exists for the one case that needs an address, which is
now a real case: `&gpu_clear` in a table of function pointers.

The members with genuine logic live only in the archive. That is the runtime,
`printf` marshalling, `memcpy`, and anything with a loop in it.

## What a project carries

A project zip holds `program.asm` today, with the assets and optionally
`microcode.txt`. A C project holds its source FILES, plural, and the assets
the same way.

The generated assembly goes in too, and this is the owner's rule: only when
it is provably the output of the C beside it. Edited and not recompiled, and
the assembly is not written at all.

Knowing that is a hash, recorded at the last compile and checked at both
ends. What it covers matters more than it looks:

- Every C source file, and the build line, which decides what is compiled
  and how.
- The asset BYTES. ROM layout decides the cartridge addresses the generated
  assembly bakes in as immediates. Change an image and `OUT GPU_SRC_HI, $04`
  is pointing at the wrong blob.
- The compiler's version.
- The build options, meaning the optimisation and which runtime.

Not the microcode. Microcode decides how an instruction executes, never which
instructions were emitted.

On save, the assembly is written only if the current inputs hash to what the
last compile recorded. On load, the inputs are hashed again and compared. A
match uses the stored assembly. A mismatch recompiles, without asking.

The check happens at both ends because a zip is a file somebody can edit. A
hand-edited one must not be able to smuggle in assembly that never came from
that C.

The source map travels with the assembly, under the same hash. Without it a
loaded project would have no C-level stepping until it recompiled, which is
the thing the stored assembly exists to avoid.

This asks one thing of the compiler. It must be deterministic: the same
inputs give the same text, byte for byte. That is also what makes golden
fixtures possible, so it is a requirement the compiler has anyway.

## BASIC

A Tiny BASIC class interpreter written in the C subset, compiled with this
compiler, and shipped as a ROM. It runs ON the machine. The machine has what
a BASIC computer needs: text mode for the screen and `IO_KEY` for the
keyboard.

So the machine boots to `READY.` and you type `10 PRINT "HELLO"`. That is not
a demo of BASIC. It is a BASIC computer, on a machine whose every layer opens.

Line numbers. `LET PRINT INPUT IF GOTO GOSUB RETURN FOR NEXT END`.
Tokenized storage. Integer variables and arrays. Plus the machine's own
words: `POKE PEEK OUT INP PLOT COLOR SOUND`.

And strings, which is the owner's decision and most of the work.

### Strings mean a heap and a collector

`A$`, string arrays, and `LEFT$ RIGHT$ MID$ LEN CHR$ ASC STR$ VAL`, with `+`
for concatenation. That is a proper BASIC and not a Tiny one, and it costs a
heap.

The design is the one Microsoft BASIC used, because it is the one that fits.
A variable holds a DESCRIPTOR of three bytes: a length and a pointer. The
characters live in a heap that grows down from the top of the interpreter's
own region, toward the variable table growing up. When they meet, the
collector walks every descriptor and compacts.

Two things make it cheaper than it sounds.

A literal in the program text is not copied. Its descriptor points into the
tokenized program, which never moves, so `A$ = "HELLO"` allocates nothing.
Only a computed string reaches the heap: a concatenation, a `MID$`, an
`INPUT`.

And the collector is a teaching artifact rather than an embarrassment. It is
written in C, it is steppable, and a student can watch a machine reclaim its
own memory. The pause is real and was real on the machines this copies.

Compaction leans on `CMD_RAM_MOVE`, which the machine gains for this and
for `memcpy` besides. Without it a collector would move a heap one byte at
a time through the CPU.

Two or three thousand lines of C, and the strings are most of the second
thousand.

Two ways in, one interpreter. Interactive, over text mode and the keyboard.
And from the host: BASIC mode in the editor, where Run places the text in
RAM for the interpreter. The tests drive that second route.

It will be slow. A tokenized interpreter on an 8 bit machine with no multiply
is the honest 8 bit experience. MAX speed makes it usable, and a later BASIC
with coprocessor floats is a natural chapter.

## What this rejects

A bytecode VM. Compile C to p-code and run an interpreter on the machine.
Smaller code and a simpler compiler, and it hides the machine completely. You
would be watching a VM's fetch loop, not your C. It defeats the layers. BASIC
being an interpreter is different. That is the point of that layer, and it is
written in C so you can see straight through it.

Retargeting an existing compiler, SDCC, cc65 or an LLVM backend. The target
is too odd: zero page only ALU operands, no indexed store, Harvard with 3 byte
slots. And it has to be TypeScript, in the browser, with source maps into this
UI. A purpose-built Small-C class compiler is a few thousand lines, and every
byte it emits is ours.

A software float library. The ACP is the float unit, and a double is eight
bytes the CPU could only ever copy.

## Settled, recorded so they are not reopened

The heap stack under D3, which is the frame base too. The stack top as a
default that `--heap-stack-top` moves. `__ROM` as a storage class the
CPU cannot read, with `ROM.h` generated from both sources on every compile.
`rom_copy` as the only way to read ROM, and `__ROM` as its spelling. A build
line in a comment rather than a Makefile. No mouse for now, so the `io` library covers the pad and the keyboard, and a
mouse stays purely additive whenever a program wants one. BASIC with string
variables, so a heap and a collector, which has its own section. What a project
carries, which has its own section. SP widened to 16 bits with the stack
bounded at 4096 bytes, which is a machine change rather than a compiler one.
The ACP runtime from day one, because it is how `double` works rather than a
way of making it faster. An indirect jump AND an indirect call through a register, spelled `JMP D1`
and `JSR D1`. Many source files with a linker, and the libraries as
archives. The ACP as the runtime for multiply,
divide and every right shift, at every width. `double`, `float` and `long` as
real types whose operators are coprocessor commands. No software float
library. No bytecode VM.

## Staging

What is built, and what is not. This section is the record.

1. **The machine's four additions. Built.** SP to 16 bits, `JMP D1` and
   `JSR D1`, `CMD_RAM_MOVE`, and the access counters. The counters are not
   gated behind tracing, which costs 2 to 3 percent on a loop that touches
   RAM every instruction.
2. **The compiler core. Built.** Lexer, preprocessor, parser and accumulator
   codegen in `packages/core/src/cc/`. char, int, pointers, arrays, the whole
   statement set, functions with arguments and recursion, `__ROM` and
   `ROM.h`, the port intrinsics, inline asm, and `gpu_printf`. The source map
   is there and the breakpoints use it.
3. **The linker. Built, one stage earlier than the design put it.** The units
   are merged as trees rather than as assembly text: a `static` name is made
   private by mangling it with its file, a public name defined twice is an
   error naming the other file, and nothing the roots reach is emitted. The
   roots are `main`, the names BASIC and the project's `.asm` files call,
   and the names in inline assembly. A
   program sees the three properties the design wanted. The build line
   chooses the files and their order.
   **Not built:** `.export` and `.zp` for hand written assembly, so a program
   of many ASSEMBLY files is still one file. Archives are headers rather than
   linkable members, so `-lgpu` is read and has nothing to resolve.
4. **The runtime, both forms. Built.** The coprocessor runtime, and
   `-msoft-mul` putting integer multiply, divide, remainder and shift back
   into adds on the CPU. Measured, per operation and including the call: a
   multiply is 50 instructions through the coprocessor and 361 in software, a
   divide 50 against 648, a right shift 56 against 306.
5. **The device libraries. Built.** `gpu`, `apu`, `acp`, `io`, `sys` and
   `rom`, generated from the machine's own tables so a command cannot exist
   without a wrapper. Documented in the manual and pinned by coverage.
6. **The UI. Built.** The language picker, a tab per file, the build pane
   with its five views, and breakpoints on a line of C.
   **Not built:** stepping by C line. A step is still one instruction.
7. **The measured zero page. Built.** Press Measure and the counters are read
   back, mapped onto names, and the zero page is placed from executions
   rather than from mentions.
8. **BASIC. Built.** Seven files of C in `packages/ui/src/basic/`. Line
   numbered programs and immediate mode, integer and string variables, the
   control flow, a string heap with a compacting collector, and the drawing
   statements. Interactive over text mode and the keyboard.
9. **The UI again. Not built.** BASIC has no mode of its own: it is a C
   project like any other, loaded from the picker or the button. The fourth
   layer is visible because all four panes are on screen at once, not because
   anything highlights it.
10. **Later. Not built.** Structs, `double` and `long` as real types,
    complex in the language itself, and static frames for functions that do
    not recurse.

Two things the design assumed that turned out otherwise, recorded because
they are the sort of thing that gets rediscovered.

The indirect jump costs four opcodes and two signals, not two and one. The
register has to be named, and microcode keys on the opcode, so `JMP` and
`JSR` each pay twice.

Temps are shared by every function, so a call has to save the live ones. The
design's frame is locals and arguments. The real one is locals, a temp save
area, and arguments. `dbl(3) + dbl(1)` was 4 rather than 8 until it was.

## Testing

The test rig already exists. A test compiles a C snippet, assembles the
result, runs it on a Session and reads RAM back. That is how every demo is
tested today, and it is how every compiler feature will be.

Three kinds of test, from the first commit:

- Behaviour. A snippet runs and leaves the right bytes.
- Shape. A snippet compiles to the assembly the design says it should. A
  codegen regression whose output still runs is still red.
- Refusal. Every rule above that says compile error has a test. It provokes
  the error and checks that the message names the alternative.

And the coverage tests extend to the new surface. Every command has a library
function. Every library function is in the manual. Every `ROM.h` constant
matches the assembled cartridge.
