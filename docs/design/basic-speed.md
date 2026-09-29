# BASIC interpreter speed

Where the BASIC interpreter spends its cycles, measured before any change.
Three proposals follow, in Eddie's order of preference. Nothing is
implemented. Eddie decides which proposals go ahead.

## Contents

- [How it was measured](#how-it-was-measured)
- [Statement costs](#statement-costs)
- [Where the cycles go](#where-the-cycles-go)
- [What a token costs](#what-a-token-costs)
- [Proposal 1: a line index built at RUN](#proposal-1-a-line-index-built-at-run)
- [Proposal 2: the lexer names each keyword once](#proposal-2-the-lexer-names-each-keyword-once)
- [Proposal 3: what lx_next costs as compiled](#proposal-3-what-lx_next-costs-as-compiled)
- [The ceiling](#the-ceiling)

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
  cycles with its callees, and splits a callee's cycles by caller.

Every statement ran on its own line in this loop:

```text
1 B=7:C=9
2 FOR I=1 TO 1000
3 A = A + 1
4 NEXT I
5 END
```

The same program without line 3 is the baseline. A statement's cost is the
difference divided by 1000. So a cost includes the statement's line: the
break check, the step to the next record and the lexer's start on the line.
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

## Proposal 1: a line index built at RUN

`dt_pack` already walks every line at RUN. It would also write each line's
offset into an index. GOTO, GOSUB, `THEN n`, `RESTORE n` and `DATA(n)` then
find a line by binary search in place of `ed_find`'s walk.

FOR, NEXT and RETURN need nothing. They already keep offsets, and the
measurement shows no `ed_find` call in them.

A binary search over 300 lines, compiled by simplecpu-cc, cost 1,368 to
1,388 cycles, whatever the target. The walk costs about 250 cycles plus 280
a line. The two meet near the fifth line.

| Row | Now | With the index |
|---|---|---|
| GOTO line 3 to 202 | 60,326 | about 5,400 |
| GOTO line 297 to 298 | 87,063 | about 5,350 |
| GOSUB to line 300 | 91,422 | about 9,150 |
| GOSUB to line 2 | 7,935 | about 8,780 |

The last row is slower by 843 cycles. A search that first compares the
target with the fifth line's number, and walks below it, keeps the walk
where it wins.

The cost:

- RAM: 2 bytes a line. A stored line is at least 5 bytes, so 6,144 bytes
  hold at most 1,228 lines. The index is 2,456 bytes at most.
- RUN: one store a line, in a walk RUN makes anyway.
- Staleness: LOAD stops the running program, store.c line 114. A running
  program adds or removes no line any other way. A POKE into the program
  text, or the IDE writing the program while it runs, would leave the index
  stale. Eddie decides whether either must keep working.

## Proposal 2: the lexer names each keyword once

`lx_next` has read the whole word before `statement` compares it with 31
keywords. The proposal: `lx_next` looks a name up once and sets a keyword
number, `lx_kw`, 0 for a variable. `statement` tests `lx_kw == 0` first and
takes the assignment. Otherwise a switch on `lx_kw` picks the statement.
The expression parser tests `lx_word[0]` for operators and `lx_kw` for MOD,
AND, OR, NOT and the functions.

A name is a variable when it is one letter, or a letter and a digit. No
keyword has that form, so a variable skips the table. A scratch lookup with
keywords grouped by first letter cost:

| Name | Cycles |
|---|---|
| `A` | 23 |
| `A1` | 58 |
| `IF` | 349 |
| `THEN` | 515 |
| `NEXT` | 576 |
| `PEEK` | 637 |
| `POKE` | 1,012 |

A keyword costs more the further it sits in its letter's group. A hash on
the length and two letters would cut that. It was not measured.

simplecpu-cc compiles a switch as CMP and JZ, 5 cycles a case.

Expected saving: every `lx_is` cycle and 18 cycles a call site, less 5
cycles a test put back, less the lookups.

| Row | Now | Estimate |
|---|---|---|
| `A = A + 1` | 7,138 | about 3,530 |
| `A = B * 3 + C` | 8,760 | about 4,750 |
| `A = PEEK(61440)` | 10,006 | about 5,560 |
| IF, true | 11,620 | about 6,810 |
| `FOR`/`NEXT` on two lines, one pass | 4,405 | about 3,600 |

The FOR/NEXT loop gains least, because NEXT's lookup costs 576 cycles every
pass.

The stored program keeps its text. LIST, SAVE and the IDE's bridge in
src/basic/program.cpp stay as they are. Storing keywords as numbers when a
line is stored would also remove most of the lexing. It changes the stored
format, and every one of those readers with it.

## Proposal 3: what lx_next costs as compiled

`lx_next` runs for every token of every statement, and neither proposal
above touches its own cycles. Its assembly in the build's basic.asm has two
patterns that cost more than the machine needs.

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
the walk from every jump, so this gap then matters only for the index's
search. A search probe as compiled costs about 150 cycles.

A hand-written interpreter that ran stored keyword numbers would dispatch a
token through a jump table in about 20 cycles, by hand count. `A = A + 1`
is five tokens. That puts the floor for an interpreted `A = A + 1` at a few
hundred cycles. The 7,138 measured are 20 or more times that floor.
