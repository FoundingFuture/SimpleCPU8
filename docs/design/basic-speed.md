# BASIC interpreter speed

Where the BASIC interpreter spends its cycles, measured before any change.
Five proposals follow. Proposals 1 and 2 are in their second form, after
Eddie's review of the first. Nothing is implemented. The decisions are at
the end.

## Contents

- [How it was measured](#how-it-was-measured)
- [Statement costs](#statement-costs)
- [Where the cycles go](#where-the-cycles-go)
- [What a token costs](#what-a-token-costs)
- [Proposal 1: a cache of jump targets](#proposal-1-a-cache-of-jump-targets)
- [Proposal 2: keywords stored as tokens](#proposal-2-keywords-stored-as-tokens)
- [Proposal 3: what lx_next costs as compiled](#proposal-3-what-lx_next-costs-as-compiled)
- [Proposal 4: number literals stored with their value](#proposal-4-number-literals-stored-with-their-value)
- [Proposal 5: an operand without an operator skips the descent](#proposal-5-an-operand-without-an-operator-skips-the-descent)
- [The ceiling](#the-ceiling)
- [Decisions](#decisions)

## How it was measured

A scratch C++ program in the session's scratch folder did every measurement.
It is not in the tree.

- It boots basic.rom on the tests' headless machine,
  tests/support/basic_session.h, with the optimal microcode.
- It stores the program with `basic::storeProgram`, types RUN and steps
  the machine with `Machine::instructionStep`.
- It counts from the first instruction of `rt_run` to the RET that leaves it.
- It assembles the build's basic.asm with `sc8::assemble` for the labels.
  The assembled program equals the ROM's, slot for slot.
- Each instruction's cycles go to the C function whose code holds its slot.
  That is the function's own cycles.
- A shadow call stack, pushed on JSR and popped on RET, gives each function's
  cycles with its callees. It also splits a callee's cycles by caller.

Every statement ran on its own line in this loop:

```text
1 B=7:C=9
2 FOR I=1 TO 1000
3 A = A + 1
4 NEXT I
5 END
```

The same program without line 3 is the baseline. A statement's cost is the
difference divided by 1000. So a cost includes the statement's line. That is
the break check, the step to the next record and the lexer's start.
The baseline in docs/design/centipede-design.md held a REM line, and its
figures leave that out.

The first GOTO row ran in the loop above. The other jump rows ran in 300
line programs padded with REM lines. Their FOR sits on line 2 or 3, except
for the GOTO at the bottom, whose FOR is on line 296. Each baseline holds
the same loop without the statement, padded to 300 lines.

"In a frame" is 65,536 divided by the cost. 65,536 is `CYCLES_PER_FRAME`
in src/vm/computer.h.

Small C programs in the scratch folder measured what a different lookup
would cost. simplecpu-cc compiled them and `simplecpu-run --microcode
optimal` ran them. The walk compiled there cost 83,123 cycles to line 298.
The interpreter's `ed_find` cost 83,109. So the scratch figures carry over.

## Statement costs

| Statement | Cycles | In a frame |
|---|---|---|
| `A = A + 1` | 7,138 | 9.2 |
| `A=A+1` | 6,995 | 9.4 |
| `A = B * 3 + C` | 8,760 | 7.5 |
| `POKE 61440, A` | 8,078 | 8.1 |
| `IF A > 5 THEN B = 1`, false | 7,307 | 9.0 |
| `IF A > 5 THEN B = 1`, true | 11,620 | 5.6 |
| `GOTO` the line below, line 3 to line 4 of 5 | 4,693 | 14.0 |
| `GOTO` 200 lines down, line 3 to line 202 of 300 | 60,326 | 1.09 |
| `GOTO` the line below, line 297 to line 298 of 300 | 87,063 | 0.75 |
| `GOSUB` and `RETURN`, routine on line 2 of 300 | 7,935 | 8.3 |
| `GOSUB` and `RETURN`, routine on line 300 of 300 | 91,422 | 0.72 |
| `A = PEEK(61440)` | 10,006 | 6.5 |
| `FOR`/`NEXT`, empty body, on two lines, one pass | 4,405 | 14.9 |
| `FOR I=1 TO 1000:NEXT`, on one line, one pass | 2,911 | 22.5 |

The PEEK row carries an assignment, since PEEK is a function. The two FOR
rows are whole passes with no baseline taken off.

What the table shows:

- A jump costs what the walk to its target costs. The walk starts at the
  program's first line. A GOTO to the next line at the bottom costs more
  than a GOTO 200 lines down near the top.
- The walk costs 280 cycles a line.
- NEXT and RETURN walk nothing. A FOR keeps its line's offset and a GOSUB
  keeps the return offset.
- Spaces cost 143 cycles in `A = A + 1`, 2 percent.

## Where the cycles go

The six most expensive rows. The first three are jumps, and a line search
takes more than 90 percent of each. The other three are the most expensive
rows without a jump. Every figure is cycles a pass.

| Row | Line loop | Dispatch | Lexing | Expression | Variable | Line search | Multiply | Total |
|---|---|---|---|---|---|---|---|---|
| GOSUB to line 300 | 991 | 3,229 | 3,037 | 498 | 0 | 83,667 | 0 | 91,422 |
| GOTO line 297 to 298 | 472 | 1,279 | 1,705 | 498 | 0 | 83,109 | 0 | 87,063 |
| GOTO line 3 to 202 | 519 | 1,279 | 1,705 | 498 | 0 | 56,325 | 0 | 60,326 |
| IF, true | 552 | 4,572 | 3,813 | 2,371 | 312 | 0 | 0 | 11,620 |
| `A = PEEK(61440)` | 552 | 2,975 | 3,192 | 3,130 | 156 | 0 | 0 | 10,006 |
| `A = B * 3 + C` | 552 | 2,975 | 2,595 | 2,088 | 468 | 0 | 81 | 8,760 |
| `A = A + 1`, for the ceiling | 552 | 2,975 | 1,874 | 1,424 | 312 | 0 | 0 | 7,138 |

The columns, by function:

| Column | Functions |
|---|---|
| Line loop | the own cycles of `rt_run`, `run_line`, `key_break` and `lx_seek` |
| Dispatch | `statement`'s own cycles, and every `lx_is` call from `statement` and `run_line` |
| Lexing | `lx_next`, from every caller |
| Expression | the own cycles of `ex_int`, `ex_cmp`, `ex_add`, `ex_mul`, `primary` and `fn_call`, and their `lx_is` calls |
| Variable | `var_slot` |
| Line search | `ed_find`. `line_at` is inlined into `statement` |
| Multiply | `__mul16`, the runtime's 16 bit multiply |

The compiler inlines `ex_or`, `ex_and`, `line_at`, `peek` and `poke`. Their
cycles sit in their callers' columns. The store a POKE makes is 16 cycles
of `statement`. The load a PEEK makes is 14 cycles of `fn_call`. The
multiply in `B * 3` is 81 cycles.

Dispatch is a chain of string compares. `statement` calls `lx_is` for REM,
the bang statement, DATA, PRINT and every other word in turn. An assignment
is the last case. `statement` reaches it after 31 keyword compares and one
more for the `=`. `lx_is` spends 2,116 cycles on them in `A = A + 1`. Each
call site adds 18 cycles of its own: the argument, the JSR and the test of
the answer. A true IF runs the chain twice, for IF and for `B = 1`, and
makes 71 `lx_is` calls in all.

The expression parser does the same on a smaller scale. `primary` tests
`(`, `-`, `+` and NOT before a name. `ex_mul` tests `*`, `/` and MOD after
every operand, and `ex_add` tests `+` and `-`. `fn_call` tests RND, ABS,
LEN, ASC, VAL, KEY, PAD, USR and DATA before PEEK.

## What a token costs

`lx_next` per call, measured inside the loops above:

| Token | Cycles |
|---|---|
| the end of the line | 78 |
| `,` | 203 |
| `+`, `*`, `=` | 240 to 245 |
| `>` | 290 |
| a one digit number | 386 |
| `61440` | 1,170 |
| a one letter variable | 444 to 481 |
| `IF` | 579 |
| `NEXT`, `POKE` | 849 |
| `THEN` | 886 |

Two patterns in the compiled `lx_next` take 45 percent of its cycles.
[Proposal 3](#proposal-3-what-lx_next-costs-as-compiled) has them.

## Proposal 1: a cache of jump targets

A line number after GOTO, GOSUB or THEN is a constant. The program text
cannot change while the program runs. LOAD stops it, store.c line 114, and
no statement adds or removes a line. So the target a jump found once holds
until the program stops. The proposal: keep it.

The cache is 256 slots of 4 bytes, 1,024 bytes of RAM. A slot
holds a site and a target. The site is `lx_tokpos` at the line number.
That is the number's offset in the program text. The target is the offset
`ed_find` returned for it. A site maps to a slot through a hash of its two
bytes, direct mapped. Two sites that share a slot take turns in it. They
stay correct, because the site is compared on every read.

A jump reads the slot for its site first. When the site matches, pc takes
the target, and the digits after the keyword are not parsed again. When it
does not, the jump walks with `ed_find` as it does now, then writes the
slot. `UNDEFINED LINE` is raised where it is raised today, when the jump
runs. Nothing is resolved before the program starts.

GOTO, GOSUB and `THEN n` use the cache. `RESTORE n` and `DATA(n)` keep the
walk. They run once in the usual program. FOR, NEXT and RETURN need
nothing, as before.

`dt_pack` zeroes the table at RUN, in the walk RUN makes anyway. Every
edit.c path that changes the program clears it too: insert, replace,
delete, NEW, RENUM and LOAD. So an immediate mode GOTO after an edit never
reads a stale slot. A POKE into the program text while it runs is not
supported, and docs/guides/basic.md says so.

A hit costs a hash, one word compare and one word load, in compiled C.
Estimated at 100 to 150 cycles. Every jump then costs what the nearest
jump costs today, less its one line walk and its digits. GOTO to the next
line costs 4,693 today and GOSUB to line 2 costs 7,935.

| Row | Now | Estimate | Measured |
|---|---|---|---|
| GOTO line 3 to 202 | 60,326 | about 3,700 | 2,789 |
| GOTO line 297 to 298 | 87,063 | about 3,700 | 2,769 |
| GOSUB to line 300 | 91,422 | about 7,000 | 6,590 |
| GOSUB to line 2 | 7,935 | about 7,000 | 6,553 |

Measured with the cache in, by tests/support/basic_profile.cpp. The
lookup before a THEN costs a true IF 169 cycles when no number follows:
`IF A > 5 THEN B = 1` went from 11,620 to 11,789.

What is left in a jump is dispatch and lexing, which proposal 2 cuts.

This proposal replaces the line index. A binary search over 300 lines
cost 1,368 to 1,388 cycles a jump. The index took 2 bytes a line and a
pass at RUN. The cache costs about 150 cycles a jump after the first, 1,024 bytes
whatever the program's size, and keeps `ed_find` as the resolver. The
index is dropped.

Open: the number of slots. A site takes at least 11 bytes of program, so
6,144 bytes hold at most 558 sites. A program with more sites than slots
shares some. 256 slots is the first guess, and the profile of a real
program corrects it.

## Proposal 2: keywords stored as tokens

Every keyword becomes one byte when a line is stored, the way the Oric-1
and the C64 stored them. The byte is 128 plus the keyword's index in
src/basic/keywords.h. Program text is ASCII, so no other byte reaches 128.

edit.c has one crunch routine. It runs on a line entered at the prompt and
on a line LOAD stores. It skips text inside quotes. It also skips everything
after REM, DATA and the bang statement. data.c reads a DATA line in raw mode
today and is unchanged. A variable is one letter, or a letter and a
digit. No keyword has that form, so a keyword is never part of a variable
name. The C64's `PRINTA` problem does not arise.

The lexer reads a token byte as a keyword in one load and sets `lx_kw` to
the index. A letter starts a variable and never reaches a table.
`statement` tests `lx_kw` first: 0 is an assignment, and otherwise a
switch on `lx_kw` picks the statement. The expression parser tests
`lx_kw` for MOD, AND, OR, NOT and the functions, and `lx_word[0]` for the
operators. simplecpu-cc compiles a switch as CMP and JZ, 5 cycles a case.

LIST, SAVE and the IDE's bridge translate tokens back to text. LIST prints
the keyword from keywords.h. SAVE writes text, so a BAS slot and the ROM's
SRC chunk hold what a person can read. LOAD crunches on the way in.
src/basic/program.cpp expands a stored line when the IDE reads the program
and crunches when it writes one. That is one rule in two places, in the
machine's C and on the host, over the one keywords.h table. keywords_up
and canonicalLine already have that shape, and a test holds them to one
result. The same test covers crunch and expand.

RENUM rewrites line numbers after GOTO, GOSUB and THEN. The digits stay
text, so it works as it does. Its rule that skips text after REM tests
for the REM token.

A keyword then costs one load, about 50 cycles with the call. The saving
against the first form of this proposal is the word read and the lookup,
from the tables above: `IF` 579 and 349, `THEN` 886 and 515, `NEXT` 849
and 576, `POKE` 849 and 1,012.

| Row | Now | Estimate |
|---|---|---|
| `A = A + 1` | 7,138 | about 3,500 |
| `A = B * 3 + C` | 8,760 | about 4,750 |
| `A = PEEK(61440)` | 10,006 | about 4,100 |
| IF, true | 11,620 | about 4,600 |
| `FOR`/`NEXT` on two lines, one pass | 4,405 | about 2,200 |

An assignment has no keyword and gains nothing here beyond the first
form. The program shrinks too: `PRINT` is one byte in place of five.

## Proposal 3: what lx_next costs as compiled

`lx_next` runs for every token of every statement, and neither proposal
above touches its own cycles. Its assembly in the build's basic.asm has two
patterns that cost more than the machine needs. The first is lex.c's to
fix. The second is the compiler's, and the fix belongs in
src/cc/peephole.cpp, where every C program gains from it.

### Every read of lx_text[lx_pos] is a 16 bit sum

`lx_pos` is an `unsigned int`. So each read of `lx_text[lx_pos]` adds two
words through A and loads the result:

```text
LD A <- [lx_text+1]
ADD A <- [lx_pos+1]
LD [__t0+1] <- A
LD A <- [lx_text]
ADC A <- [lx_pos]
LD [__t0] <- A
LD D2 <- [__t0]
LD A <- [D2]
```

That is 23 cycles. The loop that reads a name tests `lx_text[lx_pos]` and
then reads it again for `upper`. A one letter name reads it 7 times. These
sums are 32 percent of `lx_next`'s cycles. `A = A + 1` makes 29 such reads
a pass.

The machine has no D register plus D register add, so no compiler does the
sum cheaper. A line is 250 characters at most, E_LINELONG, and the prompt's
line buffer is 80. So `lx_pos` fits a byte. With `lx_pos` an
`unsigned char`, simplecpu-cc emits this, 10 cycles:

```text
LD A <- [lx_pos]
LD D2 <- [lx_text]
LD A <- [D2+A]
```

A `char *` that walks the line reads in 7 cycles, `LD D2 <- [lx_p]` and
`LD A <- [D2]`. That is a rewrite of lex.c rather than a type change.

Every `lx_seek` caller and every reader of `lx_pos` and `lx_tokpos` must be
checked before the type narrows. They are in lex.c, run.c and data.c.

### Inlined tests are stored and tested again

`isdig`, `isalpha` and `ishex` are inlined. The compiler still builds their
answer as 0 or 1, stores it in `__t0+1`, loads it back and tests it:

```text
CMP A, 48
JC __L70_andskip
CMP A, 58
JC __L68_true
__L70_andskip:
LD A <- 0
JMP __L69_tdone
__L68_true:
LD A <- 1
__L69_tdone:
LD [__t0+1] <- A
__L67_inl_lex_c__isdig:
LD A <- [__t0+1]
JZ __L65_else
```

The compares could jump to `__L65_else` and to the digit loop directly.
The instructions from `LD A <- 0` to the JZ cost 10 to 12 cycles a test. They are 12.5 percent of `lx_next`'s cycles. `lx_next` holds 94
percent of this pattern's cycles in the whole run. The fix is in the
compiler: an inlined function whose answer only decides a branch jumps from
its compare.

### Expected saving

| Row | Byte wide lx_pos | Tests jump directly | Both |
|---|---|---|---|
| `A = A + 1` | 377 | 251 | 628 |
| `A = B * 3 + C` | 520 | 354 | 874 |
| `A = PEEK(61440)` | 559 | 376 | 935 |
| IF, false | 637 | 423 | 1,060 |

The second column is an upper bound. A test whose target does not follow
it still needs one jump.

## Proposal 4: number literals stored with their value

The ZX Spectrum stored a number's binary value behind its digits, so a
line never converted the digits again. `61440` costs 1,170 cycles a read
here, and a one digit number 386. Most of that is the conversion, ten
times a digit through the 16 bit multiply.

The crunch routine of proposal 2 stores a literal in three parts. A
marker byte, the value as a big-endian word, then the digits as typed. The marker is one
more byte above 128, after the keywords. The digits keep their spelling,
so LIST shows `$F000` where `$F000` was typed. The lexer reads the marker,
loads the word in one instruction and skips the digits. A read then
costs about the same as an operator, 240 cycles or less.

The value's two bytes can hold any value, a zero or a colon among them.
So every reader of a stored line skips them by the marker, never by
looking at them. The readers are the lexer, LIST, SAVE, the crunch
routine's own scan for quotes, and program.cpp on the host. RENUM expands
a line, rewrites its targets and crunches the result. So the hidden value
behind a line number follows the digits.

DATA lines are not crunched and read raw, as before. A literal costs 3
bytes more than its digits. The tokens of proposal 2 save about 30
percent of a typical line, so a program still shrinks.

| Row | Now | With proposals 2 and 4 |
|---|---|---|
| `A = B * 3 + C` | 8,760 | about 4,400 |
| `A = PEEK(61440)` | 10,006 | about 3,000 |
| `POKE 61440, A` | 8,078 | about 3,300 |

## Proposal 5: an operand without an operator skips the descent

`POKE 61440, A` stores one byte. The store is 16 cycles of its 8,078.
Each of its two operands goes down the whole expression parser to find
there is no operator after it. The descent is `ex_int`, `ex_or`, `ex_and`,
`ex_cmp`, `ex_add`, `ex_mul` and `primary`. Every level is a C call that
saves and restores its live temps. The two descents cost about 1,400
cycles.

The proposal: `ex_int` looks at the token after a literal or a variable
first. When that token ends the operand, `ex_int` returns the value and never
enters the descent. A comma, a colon, a closing bracket or the end of the
line ends it. Any
other token takes the descent as before, so precedence is unchanged.

Most operands in a game's hot lines are a bare variable or a literal.
A POKE's address and value, PEEK's argument and FOR's limit are the
usual ones. Each saves about 600 cycles. `POKE 61440, A` drops from about
3,300 after proposals 2 and 4 to about 1,500.

## The ceiling

The best hand-written code for `A = A + 1` on this CPU is 9 cycles, with A
as a word at the address `var_a`:

```text
LD D2 <- [var_a]
LD D2 <- D2+1
LD [var_a] <- D2
```

simplecpu-cc emits exactly these three instructions for `a = a + 1` on a C
global. The interpreter spends 7,138 cycles, 793 times as many. So neither
the instruction set nor the compiler sets the ceiling. The interpreter's
design sets it. It reads the source text again every time a line runs, and
it finds a statement by comparing strings.

The compiler does cost the interpreter something. Proposal 3's two patterns
take 628 of the 7,138 cycles. The line walk shows the largest gap. `ed_find`
reads each line number as two bytes, shifts one by 8 through temporaries
and ORs them. The machine is big-endian, and one word load reads the
number. `ed_find` as compiled costs 280 cycles a line. This loop, counted
by hand from docs/design/instruction-set.md, costs 30:

```text
walk:   LD D1 <- [D2]
        JZ done
        LD [t] <- D1
        LD A <- [t+1]
        SUB A <- [n+1]
        LD A <- [t]
        SBC A <- [n]
        JNC done
        LD A <- [D2+2]
        LD D2 <- D2+A
        JMP walk
```

`t` and `n` are zero page words, `n` the target line. Proposal 1 removes
the walk from every jump after its first, so this gap then costs once a
site.

A hand-written interpreter that ran stored keyword numbers would dispatch
through a jump table. That is about 20 cycles a token, by hand count. `A = A + 1`
is five tokens. That puts the floor for an interpreted `A = A + 1` at a few
hundred cycles. The 7,138 measured are 20 or more times that floor.

## Decisions

Eddie decided, after the profile, that every proposal goes in:

- The jump targets are cached. The line index is dropped.
- Keywords are stored as tokens. SAVE, LOAD and LIST translate them back
  to text, and so does the IDE's bridge.
- Number literals are stored with their value, behind a marker.
- Both patterns of proposal 3 are fixed. `lx_pos` becomes a byte in lex.c,
  after the audit of its readers. The stored test is fixed in
  src/cc/peephole.cpp, where every C program gains from it.
- The operand fast path of proposal 5 goes in with the tokens.
- The order of work is the cache, then the tokens, the literals and the
  fast path, then proposal 3.
