# BASIC DATA, READ and RESTORE implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** BASIC gains DATA, READ, RESTORE, the DATA(n) function and POKE with a list, as docs/basic-data-design.md specifies.

**Architecture:** A new C file, src/basic/data.c, holds one value parser that placement at RUN, DATA(n) and READ all use. The program stays text. The lexer gains the start of the token it read and a raw mode that leaves strings in the text instead of the heap, so data.c reads DATA lines with the same lexer as every statement.

**Tech Stack:** The interpreter is C compiled by simplecpu-cc at build time. The tests are doctest, booting the ROM in a headless machine.

**Spec:** docs/basic-data-design.md

## Global constraints

- Messages on the screen, exactly: `READ FOUND NO MORE DATA` (code 28, E_NODATA), `LINE n HOLDS NO DATA` (code 29, E_NOTDATA), `A DATA VALUE IS ONE BYTE: -128 TO 255` (code 30, E_DATABYTE).
- Existing messages are reused unchanged: `THERE IS NO LINE n`, `THE PROGRAM MEMORY IS FULL: 6144 BYTES AT MOST`, `A STRING CANNOT BE USED AS A NUMBER`, `A NUMBER CANNOT BE USED AS A STRING`, `SYNTAX ERROR`.
- A first value written as `$` and three or four hex digits is an address. Nothing else is.
- A placed value is one byte, -128 to 255. READ returns a value as written.
- Every library links sc8_warnings. The build is warning free.
- Both presets pass: `cmake --build --preset default && ctest --preset default`, and the same with `headless`.
- Comments and docs follow the docs-style rules. Run its checker on every markdown file touched.
- No commit mentions an AI. The repo's email is eddie@foundingfuture.com and is already set.
- The interpreter is C for simplecpu-cc: declarations at the top of a block, no `//` comments in the .c files, `/* */` only.

## Review focus

1. A DATA line typed in small letters, `100 data $9c40,1`, places its byte. Test in Task 1.
2. `DATA(n)` inside a longer expression, `A=DATA(100)+1`, leaves the rest of the line readable. Test in Task 2.
3. READ followed by more statements on its line, `READ A,B: RESTORE: READ C`. Test in Task 3.
4. A string holding a comma, `DATA "A,B"`, is one value of three bytes. Tests in Task 1 and Task 3.
5. RUN twice: the second RUN reads from the first value again. Test in Task 3.

## Files

- Create `src/basic/data.c`: the value parser, placement, DATA(n), the READ cursor and its reset.
- Modify `src/basic/lex.c`, `src/basic/basic.h`: `lx_tokpos`, `lx_raw`, the new error codes and the data.c declarations.
- Modify `src/basic/keywords.h`: DATA, READ, RESTORE.
- Modify `src/basic/run.c`: the DATA, READ and RESTORE statements, POKE with a list, placement in `rt_run`, the three messages.
- Modify `src/basic/expr.c`: the DATA(n) function.
- Modify `src/basic/edit.c`: forget the READ cursor on a change, RENUM of `RESTORE n` and `DATA(n)`.
- Modify `src/basic/main.c`: the change check around each command.
- Modify `src/basic/CMakeLists.txt`: data.c in the source list.
- Modify `src/basic/lines.cpp`: the IDE's renumbering of `RESTORE n` and `DATA(n)`.
- Modify `tests/basic/basic_test.cpp`, `tests/basic/lines_test.cpp`: the tests.
- Modify `docs/guides/basic.md`, `docs/basic-system-page.md`, `docs/standalone.md`, `src/basic/README.md`: the docs.

## How to build and run the tests

```bash
cmake --preset headless
cmake --build --preset headless
./build-headless/tests/basic/sc8_basic_tests -ts="DATA, READ and RESTORE"
```

If the binary is elsewhere, `find build-headless -name sc8_basic_tests -type f` finds it. The build compiles the interpreter with simplecpu-cc, so a C error in src/basic shows up as a failed `Compiling BASIC` step.

### Task 1: DATA lines placed at RUN

**Files:**
- Create: `src/basic/data.c`
- Modify: `src/basic/lex.c`, `src/basic/basic.h`, `src/basic/keywords.h`, `src/basic/run.c`, `src/basic/CMakeLists.txt`, `src/basic/main.c:1`, `src/project/project.cpp:541` (a comment)
- Test: `tests/basic/basic_test.cpp`

**Interfaces:**
- Produces: `extern unsigned int lx_tokpos;` (where the token last read starts in `lx_text`), `extern unsigned char lx_raw;` (1: a string token leaves its characters in the text, `lx_str` is their index in `lx_text` and `lx_len` their count). `void dt_pack(void);` places every DATA line. `#define E_DATABYTE 30`.
- In data.c, static and used by Tasks 2 and 3: `is_data(p)`, `open_line(p)`, `value()`, `sep()`, `walk(stop, place)`, the globals `dv_kind`, `dv_num`, `dv_byte`, `dv_str`, `dv_len`, `dt_at`, `line_fixed`, `line_addr`, `sv_line`.

- [ ] **Step 1: Write the failing tests**

Add these helpers to the anonymous namespace in `tests/basic/basic_test.cpp`, after `countOf`:

```cpp
// The program, written into memory the way the IDE's writeProgram does.
// It is faster than typing a long program and it is the path the IDE takes.
void setProgram(Session& s, const std::string& program) {
  auto& ram = s.m->ram;
  const std::vector<uint8_t> bytes = basic::encodeProgram(program);
  const size_t at = static_cast<size_t>((ram[basic::SYS_PROG] << 8) | ram[basic::SYS_PROG + 1]);
  std::copy(bytes.begin(), bytes.end(), ram.begin() + static_cast<long>(at));
  ram[basic::SYS_PROG_LEN] = static_cast<uint8_t>(bytes.size() >> 8);
  ram[basic::SYS_PROG_LEN + 1] = static_cast<uint8_t>(bytes.size() & 255);
}

// Where the program ends in memory, which is where its data area starts.
int programEnd(const Session& s) {
  const auto& ram = s.m->ram;
  return ((ram[basic::SYS_PROG] << 8) | ram[basic::SYS_PROG + 1]) +
         ((ram[basic::SYS_PROG_LEN] << 8) | ram[basic::SYS_PROG_LEN + 1]);
}

// An integer variable, A to Z, read out of memory. A letter owns eleven
// word slots, high byte first.
int16_t intVar(const Session& s, char letter) {
  const auto& ram = s.m->ram;
  const size_t vars = static_cast<size_t>((ram[12] << 8) | ram[13]);
  const size_t at = vars + static_cast<size_t>(letter - 'A') * 22;
  return static_cast<int16_t>((ram[at] << 8) | ram[at + 1]);
}
```

Add a new suite at the end of the file:

```cpp
TEST_SUITE("DATA, READ and RESTORE") {
  TEST_CASE("RUN places a line's bytes at its address, and the next line carries on") {
    auto s = boot();
    settle(*s);
    type(*s, "10 END");
    type(*s, "100 DATA $9C40,10,20,30");
    type(*s, "110 DATA 40,50,60");
    type(*s, "RUN");
    for (int i = 0; i < 6; i++) CHECK_EQ(s->m->ram[static_cast<size_t>(0x9c40 + i)], 10 * (i + 1));
  }

  TEST_CASE("a second address moves the cursor again") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA $9C40,1,2");
    type(*s, "110 DATA $9D00,3");
    type(*s, "120 DATA 4");
    type(*s, "RUN");
    CHECK_EQ(s->m->ram[0x9c40], 1);
    CHECK_EQ(s->m->ram[0x9c41], 2);
    CHECK_EQ(s->m->ram[0x9d00], 3);
    CHECK_EQ(s->m->ram[0x9d01], 4);
  }

  TEST_CASE("a negative value and a string place as bytes") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA $9C40,-1,-128,\"A,B\",\"\",$0FF,0255");
    type(*s, "RUN");
    CHECK_EQ(s->m->ram[0x9c40], 255);
    CHECK_EQ(s->m->ram[0x9c41], 128);
    CHECK_EQ(s->m->ram[0x9c42], 'A');
    CHECK_EQ(s->m->ram[0x9c43], ',');
    CHECK_EQ(s->m->ram[0x9c44], 'B');
    CHECK_EQ(s->m->ram[0x9c45], 255);
    CHECK_EQ(s->m->ram[0x9c46], 255);
    CHECK_EQ(s->m->ram[0x9c47], 0);
  }

  TEST_CASE("lines before any address go to the free memory after the program") {
    auto s = boot();
    settle(*s);
    type(*s, "10 END");
    type(*s, "100 DATA 7,8");
    type(*s, "RUN");
    const int end = programEnd(*s);
    CHECK_EQ(s->m->ram[static_cast<size_t>(end)], 7);
    CHECK_EQ(s->m->ram[static_cast<size_t>(end + 1)], 8);
  }

  TEST_CASE("a DATA line typed in small letters places its bytes") {
    auto s = boot();
    settle(*s);
    type(*s, "100 data $9c40,1");
    type(*s, "RUN");
    CHECK_EQ(s->m->ram[0x9c40], 1);
  }

  TEST_CASE("a value that is not a byte stops RUN at its line") {
    auto s = boot();
    settle(*s);
    type(*s, "10 PRINT \"RAN\"");
    type(*s, "100 DATA 1,256");
    type(*s, "RUN");
    CHECK(has(flat(*s), "A DATA VALUE IS ONE BYTE: -128 TO 255 IN LINE 100"));
    CHECK_FALSE(has(after(text(*s), "RUN"), "RAN"));
    CHECK_EQ(s->m->ram[0x12], 30);
    type(*s, "100 DATA -129");
    type(*s, "RUN");
    CHECK_EQ(s->m->ram[0x12], 30);
    type(*s, "100 DATA 1,$100");
    type(*s, "RUN");
    CHECK_EQ(s->m->ram[0x12], 30);
    type(*s, "100 DATA 65546");
    type(*s, "RUN");
    CHECK_EQ(s->m->ram[0x12], 30);
  }

  TEST_CASE("an expression or a name in DATA is a syntax error at its line") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA 1+2");
    type(*s, "RUN");
    CHECK(has(flat(*s), "SYNTAX ERROR IN LINE 100: EXPECTED , OR THE END OF THE LINE BUT FOUND +"));
    type(*s, "CLS");
    type(*s, "100 DATA X");
    type(*s, "RUN");
    CHECK(has(flat(*s), "SYNTAX ERROR IN LINE 100: EXPECTED A NUMBER OR A STRING BUT FOUND X"));
  }

  TEST_CASE("DATA after a colon is a syntax error") {
    auto s = boot();
    settle(*s);
    type(*s, "10 PRINT 1: DATA 5");
    type(*s, "RUN");
    CHECK(has(flat(*s), "EXPECTED A STATEMENT BUT FOUND DATA"));
  }

  TEST_CASE("DATA reached by a program, or typed at the prompt, does nothing") {
    auto s = boot();
    settle(*s);
    type(*s, "10 DATA 1,2");
    type(*s, "20 PRINT 7");
    type(*s, "RUN");
    CHECK(has(after(text(*s), "RUN"), "7"));
    CHECK_FALSE(has(text(*s), "?"));
    type(*s, "DATA 1,2");
    CHECK_FALSE(has(text(*s), "?"));
  }

  TEST_CASE("DATA that does not fit after the program stops RUN") {
    auto s = boot();
    settle(*s);
    // 23 remarks of 248 bytes, one of 145 and a DATA line of 248 leave 44
    // bytes free, and the line holds 120 values.
    std::string program;
    for (int n = 1; n <= 23; n++) program += std::to_string(n) + " REM " + std::string(240, 'X') + "\n";
    program += "24 REM " + std::string(137, 'X') + "\n";
    program += "1000 DATA 1";
    for (int i = 1; i < 120; i++) program += ",1";
    program += "\n";
    setProgram(*s, program);
    type(*s, "RUN");
    CHECK(has(flat(*s), "THE PROGRAM MEMORY IS FULL: 6144 BYTES AT MOST IN LINE 1000"));
  }
}
```

- [ ] **Step 2: Run the tests to see them fail**

Run: `cmake --build --preset headless && ./build-headless/tests/basic/sc8_basic_tests -ts="DATA, READ and RESTORE"`
Expected: the build succeeds and the tests fail. DATA is an unknown word, so RUN stops with `UNKNOWN WORD DATA` and nothing is placed.

- [ ] **Step 3: Give the lexer the token's start and a raw string mode**

In `src/basic/basic.h`, after `extern unsigned int lx_str;`:

```c
extern unsigned int lx_tokpos;  /* where the token last read starts in lx_text */
/* 1 while data.c reads a DATA line. A string then stays in the text:
 * lx_str is the index of its first character and lx_len its length.
 * Nothing goes on the heap, so walking DATA lines leaves no garbage.
 */
extern unsigned char lx_raw;
```

In `src/basic/lex.c`, after `unsigned char lx_len;`:

```c
unsigned int lx_tokpos;
unsigned char lx_raw;
```

In `lx_next`, set the start after the spaces are skipped:

```c
    while (lx_text[lx_pos] == 32) lx_pos = lx_pos + 1;
    lx_tokpos = lx_pos;
    c = lx_text[lx_pos];
```

In the string branch, replace the allocation and the lines after it up to `return;`:

```c
        if (lx_raw) {
            lx_str = start;
            lx_len = i;
        } else {
            lx_str = str_new(i);
            if (lx_str) {
                unsigned char j;
                for (j = 0; j < i; j++) heap[lx_str + 1 + j] = lx_text[start + j];
            }
            lx_len = 0;
        }
        if (lx_text[lx_pos] == 34) lx_pos = lx_pos + 1;
        lx_tok = T_STR;
        return;
```

- [ ] **Step 4: Declare the error code and data.c**

In `src/basic/basic.h`, after `#define E_RENUM   27 ...`:

```c
#define E_DATABYTE 30 /* a DATA value that does not fit in a byte */
```

After the `store.c` declarations, before `#endif`:

```c
/* data.c: DATA lines. RUN places their bytes, DATA(n) finds where a line's
 * bytes go and READ takes the values. docs/basic-data-design.md.
 */
void dt_pack(void);
```

- [ ] **Step 5: Add DATA to the reserved words**

In `src/basic/keywords.h`, the first line of the string becomes:

```c
    " ABS AND ASC CALL CATALOG CHR$ CIRCLE CLS DATA DEEK DELETE DOKE DRAW END" \
```

- [ ] **Step 6: Write data.c**

Create `src/basic/data.c`:

```c
#include "basic.h"

/* DATA lines. The program stays text, and one parser reads it for RUN,
 * DATA(n) and READ, so the three cannot disagree about what a line holds.
 * docs/basic-data-design.md is the design.
 */

#define LINE_AT(p) ((prog[(p)] << 8) | prog[(p) + 1])

#define V_NUM 1
#define V_STR 2

/* The value read last: its kind, a number as written, whether that number
 * fits in a byte, and a string's place and length in lx_text.
 */
static unsigned char dv_kind;
static int dv_num;
static unsigned char dv_byte;
static unsigned int dv_str;
static unsigned char dv_len;

/* The address a line opened with, when line_fixed says it has one. */
static unsigned int line_addr;
static unsigned char line_fixed;

/* The drop cursor. Until an address moves it, it is in the program's data
 * area, which ends at the top of program memory.
 */
static unsigned int dt_at;
static unsigned char dt_fixed;

/* The line the caller was on, for the message after an error that is not
 * the DATA line's own.
 */
static int sv_line;

/* Is the line at p a DATA line. Leaves the lexer on its first word. */
static unsigned char is_data(unsigned int p)
{
    lx_raw = 1;
    lx_start((char *)&prog[p + 3]);
    return lx_is("DATA");
}

/* Past a value: a comma before the next one, or the end of the line. */
static unsigned char sep(void)
{
    if (lx_tok == T_END) return 1;
    if (lx_is(",")) {
        lx_next();
        if (lx_tok == T_END) { rt_expect("A NUMBER OR A STRING"); return 0; }
        return 1;
    }
    rt_expect(", OR THE END OF THE LINE");
    return 0;
}

/* Past the DATA word of the line at p, which the lexer stands on, and past
 * its address when it opens with one. Returns 0 after an error.
 */
static unsigned char open_line(unsigned int p)
{
    unsigned int digits;
    err_line = LINE_AT(p);
    line_fixed = 0;
    lx_next();
    if (lx_tok != T_NUM || lx_text[lx_tokpos] != 36) return 1;
    /* The way the number is written decides: $E000 and $0010 are
     * addresses, $10 is a value.
     */
    digits = lx_pos - lx_tokpos - 1;
    if (digits != 3 && digits != 4) return 1;
    line_addr = lx_num;
    line_fixed = 1;
    lx_next();
    return sep();
}

/* One value at the lexer into dv_*, and step past it. Returns 0 after a
 * syntax error.
 */
static unsigned char value(void)
{
    unsigned int at;
    unsigned int end;
    unsigned char hex;
    unsigned char neg;
    neg = 0;
    if (lx_is("-")) { neg = 1; lx_next(); }
    if (lx_tok == T_STR && !neg) {
        dv_kind = V_STR;
        dv_str = lx_str;
        dv_len = lx_len;
        lx_next();
        return 1;
    }
    if (lx_tok != T_NUM) { rt_expect("A NUMBER OR A STRING"); return 0; }
    dv_kind = V_NUM;
    dv_num = neg ? -lx_num : lx_num;
    /* Count the digits that matter. The lexer wraps at 16 bits, so 65546
     * reads as 10, and only the text says it is not a byte.
     */
    at = lx_tokpos;
    end = lx_pos;
    hex = lx_text[at] == 36;
    if (hex) at = at + 1;
    while (at + 1 < end && lx_text[at] == 48) at = at + 1;
    if (end - at > (hex ? 2 : 3)) dv_byte = 0;
    else if (neg) dv_byte = lx_num <= 128;
    else dv_byte = lx_num <= 255;
    lx_next();
    return 1;
}

/* A byte at the drop cursor, written when place is 1. */
static unsigned char drop(unsigned char b, unsigned char place)
{
    if (!dt_fixed && dt_at >= (unsigned int)prog + PROGMAX) { rt_error(E_MEMORY); return 0; }
    if (place) poke(dt_at, b);
    dt_at = dt_at + 1;
    return 1;
}

/* Walk the DATA lines in order, placing each byte when place is 1. Stop at
 * the line at offset stop once its address is taken, or at the end.
 */
static void walk(unsigned int stop, unsigned char place)
{
    unsigned int p;
    unsigned char i;
    dt_at = (unsigned int)prog + prog_len;
    dt_fixed = 0;
    p = 0;
    while (LINE_AT(p)) {
        if (is_data(p)) {
            if (!open_line(p)) return;
            if (line_fixed) { dt_at = line_addr; dt_fixed = 1; }
            if (p == stop) return;
            while (lx_tok != T_END) {
                if (!value()) return;
                if (dv_kind == V_STR) {
                    for (i = 0; i < dv_len; i++) {
                        if (!drop(lx_text[dv_str + i], place)) return;
                    }
                } else {
                    if (!dv_byte) { rt_error(E_DATABYTE); return; }
                    if (!drop(dv_num, place)) return;
                }
                if (!sep()) return;
            }
        }
        p = p + prog[p + 2];
    }
}

void dt_pack(void)
{
    sv_line = 0;
    walk(PROGMAX, 1);
    lx_raw = 0;
}
```

- [ ] **Step 7: Compile data.c into the ROM**

In `src/basic/CMakeLists.txt`, the source list becomes:

```cmake
set(SC8_BASIC_SOURCES main.c term.c lex.c expr.c strings.c edit.c run.c bang.c store.c data.c)
```

In the comment at the top, `the nine C files` becomes `the ten C files`. In the comment above `add_custom_command`, the sentence about new files becomes `bang.c, store.c and data.c are new here: the bang statement, the storage driver and DATA.`

In `src/project/project.cpp`, in the comment above `ccOpts.zpReserve = 32;`, `names its own nine files` becomes `names its own files`.

In `src/basic/main.c`, the first line becomes:

```c
// build: cc -o basic main.c term.c lex.c expr.c strings.c edit.c run.c bang.c store.c data.c
```

- [ ] **Step 8: The DATA statement, placement in rt_run and the message**

In `src/basic/run.c`, in `statement()`, after the bang line (`if (lx_is("!")) ...`):

```c
    /* RUN, DATA(n) and READ read a DATA line. Running it does nothing.
     * DATA must open its line, because that is where they look for it.
     */
    if (lx_is("DATA")) {
        if (lx_tokpos) { rt_expect("A STATEMENT"); return 1; }
        lx_tok = T_END;
        return 1;
    }
```

In `say_error`, after the `E_RENUM` line:

```c
    else if (err == E_DATABYTE) term_puts("A DATA VALUE IS ONE BYTE: -128 TO 255");
```

In `rt_run`, after `loop_back = 0;` and before `while (running) {`:

```c
    /* The DATA lines' bytes go where they belong before line 1 runs. */
    dt_pack();
    if (err) { say_error(); running = 0; return; }
    err_line = 0;
```

- [ ] **Step 9: Run the tests to see them pass**

Run: `cmake --build --preset headless && ./build-headless/tests/basic/sc8_basic_tests -ts="DATA, READ and RESTORE"`
Expected: all ten pass.

Then the whole suite: `ctest --preset headless`
Expected: all pass.

- [ ] **Step 10: Commit**

```bash
git add src/basic/data.c src/basic/lex.c src/basic/basic.h src/basic/keywords.h src/basic/run.c src/basic/CMakeLists.txt src/basic/main.c src/project/project.cpp tests/basic/basic_test.cpp
git commit -m "BASIC: DATA lines, placed in memory at RUN

A line that opens with a three or four digit hex number places its
bytes there, and the lines after it carry on. Lines before any address
go to the free program memory after the program."
```

### Task 2: DATA(n)

**Files:**
- Modify: `src/basic/data.c`, `src/basic/basic.h`, `src/basic/expr.c`, `src/basic/run.c`
- Test: `tests/basic/basic_test.cpp`

**Interfaces:**
- Consumes: from Task 1, `walk`, `is_data`, `dt_at`, `sv_line`, `lx_tokpos`, `lx_raw`.
- Produces: `int dt_addr(int line);` the address line's bytes go to. `static void lx_save(void)` and `static void lx_restore(void)` in data.c, used by Task 3. `#define E_NOTDATA 29`.

- [ ] **Step 1: Write the failing tests**

Add to the suite `"DATA, READ and RESTORE"`:

```cpp
  TEST_CASE("DATA(n) gives the address, after an address too, and writes nothing") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA $9C40,10,20,30");
    type(*s, "110 DATA 40");
    type(*s, "120 DATA $9D00");
    type(*s, "130 DATA 5");
    type(*s, "A=DATA(100)");
    type(*s, "B=DATA(110)");
    type(*s, "C=DATA(120)");
    type(*s, "D=DATA(130)");
    CHECK_EQ(intVar(*s, 'A'), static_cast<int16_t>(0x9c40));
    CHECK_EQ(intVar(*s, 'B'), static_cast<int16_t>(0x9c43));
    CHECK_EQ(intVar(*s, 'C'), static_cast<int16_t>(0x9d00));
    CHECK_EQ(intVar(*s, 'D'), static_cast<int16_t>(0x9d00));
    CHECK_EQ(s->m->ram[0x9c40], 0);
  }

  TEST_CASE("DATA(n) of a line in the data area matches where RUN puts it") {
    auto s = boot();
    settle(*s);
    type(*s, "10 A=DATA(110)");
    type(*s, "100 DATA 1,2");
    type(*s, "110 DATA 3");
    type(*s, "RUN");
    CHECK_EQ(intVar(*s, 'A'), static_cast<int16_t>(programEnd(*s) + 2));
    CHECK_EQ(s->m->ram[static_cast<size_t>(programEnd(*s) + 2)], 3);
  }

  TEST_CASE("DATA(n) inside a longer expression leaves the rest of the line") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA $9C40,1");
    type(*s, "A=DATA(100)+1: B=7");
    CHECK_EQ(intVar(*s, 'A'), static_cast<int16_t>(0x9c41));
    CHECK_EQ(intVar(*s, 'B'), 7);
  }

  TEST_CASE("DATA(n) of a line without DATA, or of no line, says so") {
    auto s = boot();
    settle(*s);
    type(*s, "20 END");
    type(*s, "PRINT DATA(20)");
    CHECK(has(flat(*s), "LINE 20 HOLDS NO DATA"));
    CHECK_EQ(s->m->ram[0x12], 29);
    type(*s, "PRINT DATA(99)");
    CHECK(has(flat(*s), "THERE IS NO LINE 99"));
    CHECK_EQ(s->m->ram[0x12], 2);
  }
```

- [ ] **Step 2: Run the tests to see them fail**

Run: `cmake --build --preset headless && ./build-headless/tests/basic/sc8_basic_tests -ts="DATA, READ and RESTORE"`
Expected: the four new tests fail. `DATA(100)` is not a function yet, so the prompt says `DATA IS NOT A VARIABLE`.

- [ ] **Step 3: Declare**

In `src/basic/basic.h`, on the line before `#define E_DATABYTE 30`:

```c
#define E_NOTDATA 29  /* RESTORE n or DATA(n) of a line that holds no DATA */
```

After `void dt_pack(void);`:

```c
/* The address line's bytes go to, the same one RUN places them at. Writes
 * nothing, so it answers before a RUN and after an edit.
 */
int dt_addr(int line);
```

- [ ] **Step 4: Write dt_addr**

In `src/basic/data.c`, after the `sv_line` declaration:

```c
/* The caller's place in its own line. A walk moves the lexer into DATA
 * lines, and lx_restore reads the caller's token again from its start.
 * The callers stand on a name or a bracket, which reads back the same.
 */
static char *sv_text;
static unsigned int sv_pos;

static void lx_save(void)
{
    sv_text = lx_text;
    sv_pos = lx_tokpos;
    sv_line = err_line;
}

/* After an error the lexer stays where it failed and err_line names the
 * line the error is in.
 */
static void lx_restore(void)
{
    lx_raw = 0;
    if (err) return;
    err_line = sv_line;
    lx_seek(sv_text, sv_pos);
}
```

At the end of the file:

```c
int dt_addr(int line)
{
    unsigned int p;
    p = ed_find(line);
    if (LINE_AT(p) != line) { err_arg = line; rt_error(E_NOLINE); return 0; }
    lx_save();
    if (is_data(p)) walk(p, 0);
    else { err_arg = line; rt_error(E_NOTDATA); }
    lx_restore();
    return dt_at;
}
```

- [ ] **Step 5: The function in expressions**

In `src/basic/expr.c`, in `fn_call`, before the `PEEK` branch:

```c
    /* DATA(n): where line n's bytes go. The walk runs while the lexer
     * stands on the closing bracket, which data.c reads back after it.
     */
    if (lx_is("DATA")) {
        lx_next();
        if (!lx_is("(")) { rt_expect("( AFTER DATA"); return 0; }
        lx_next();
        a = ex_int();
        if (err) return 0;
        if (!lx_is(")")) { rt_expect(")"); return 0; }
        a = dt_addr(a);
        if (err) return 0;
        lx_next();
        return a;
    }
```

- [ ] **Step 6: The message**

In `src/basic/run.c`, in `say_error`, before the `E_DATABYTE` line:

```c
    else if (err == E_NOTDATA) { term_puts("LINE "); term_putn(err_arg); term_puts(" HOLDS NO DATA"); }
```

- [ ] **Step 7: Run the tests to see them pass**

Run: `cmake --build --preset headless && ./build-headless/tests/basic/sc8_basic_tests -ts="DATA, READ and RESTORE"`
Expected: all pass. Then `ctest --preset headless`, all pass.

- [ ] **Step 8: Commit**

```bash
git add src/basic/data.c src/basic/basic.h src/basic/expr.c src/basic/run.c tests/basic/basic_test.cpp
git commit -m "BASIC: DATA(n), the address a DATA line's bytes go to"
```

### Task 3: READ and RESTORE

**Files:**
- Modify: `src/basic/data.c`, `src/basic/basic.h`, `src/basic/keywords.h`, `src/basic/run.c`, `src/basic/edit.c`, `src/basic/main.c`
- Test: `tests/basic/basic_test.cpp`

**Interfaces:**
- Consumes: from Tasks 1 and 2, `is_data`, `open_line`, `value`, `sep`, `lx_save`, `lx_restore`, `dv_*`, `sv_line`.
- Produces: `int dt_read_int(void);`, `unsigned int dt_read_str(void);` (0 is the empty string, as INKEY$ returns it), `void dt_restore(void);`, `void dt_restore_line(int line);`, `void dt_check(void);`, `void dt_mark(void);`, `#define E_NODATA 28`.

- [ ] **Step 1: Write the failing tests**

Add to the suite `"DATA, READ and RESTORE"`:

```cpp
  TEST_CASE("READ runs across lines and never returns the address") {
    auto s = boot();
    settle(*s);
    type(*s, "10 FOR I=1 TO 6: READ A: PRINT A;: NEXT I");
    type(*s, "100 DATA $9C40,10,20,30");
    type(*s, "110 DATA 40,50,60");
    type(*s, "RUN");
    CHECK(has(after(text(*s), "RUN"), "102030405060"));
  }

  TEST_CASE("READ takes strings, and a value as written") {
    auto s = boot();
    settle(*s);
    type(*s, "10 READ A$,B,C$,D$: PRINT A$;B;C$;D$;\"!\"");
    type(*s, "100 DATA \"HI\",-1,\"A,B\",\"\"");
    type(*s, "RUN");
    CHECK(has(after(text(*s), "RUN"), "HI-1A,B!"));
  }

  TEST_CASE("RESTORE starts again, and RESTORE n starts at line n") {
    auto s = boot();
    settle(*s);
    type(*s, "10 READ A,B: RESTORE: READ C: RESTORE 110: READ D: PRINT A;B;C;D");
    type(*s, "100 DATA 1,2");
    type(*s, "110 DATA $9C40,3");
    type(*s, "RUN");
    CHECK(has(after(text(*s), "RUN"), "1213"));
  }

  TEST_CASE("each RUN reads from the first value again") {
    auto s = boot();
    settle(*s);
    type(*s, "10 READ A: PRINT A*11");
    type(*s, "100 DATA 5,6");
    type(*s, "RUN");
    type(*s, "RUN");
    CHECK_EQ(countOf(text(*s), "55"), 2);
    CHECK_FALSE(has(text(*s), "66"));
  }

  TEST_CASE("READ past the last value says so") {
    auto s = boot();
    settle(*s);
    type(*s, "10 READ A,B");
    type(*s, "100 DATA 1");
    type(*s, "RUN");
    CHECK(has(flat(*s), "READ FOUND NO MORE DATA IN LINE 10"));
    CHECK_EQ(s->m->ram[0x12], 28);
  }

  TEST_CASE("RESTORE n of a line without DATA, or of no line, says so") {
    auto s = boot();
    settle(*s);
    type(*s, "10 RESTORE 20");
    type(*s, "20 END");
    type(*s, "RUN");
    CHECK(has(flat(*s), "LINE 20 HOLDS NO DATA IN LINE 10"));
    CHECK_EQ(s->m->ram[0x12], 29);
    type(*s, "10 RESTORE 99");
    type(*s, "RUN");
    CHECK(has(flat(*s), "THERE IS NO LINE 99 IN LINE 10"));
  }

  TEST_CASE("READ of the wrong kind of value names the READ line") {
    auto s = boot();
    settle(*s);
    type(*s, "10 READ A");
    type(*s, "100 DATA \"X\"");
    type(*s, "RUN");
    CHECK(has(flat(*s), "A STRING CANNOT BE USED AS A NUMBER IN LINE 10"));
    type(*s, "10 READ A$");
    type(*s, "100 DATA 5");
    type(*s, "RUN");
    CHECK(has(flat(*s), "A NUMBER CANNOT BE USED AS A STRING IN LINE 10"));
  }

  TEST_CASE("a program changed at the prompt reads from the first value again") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA 1,2");
    type(*s, "READ A");
    type(*s, "READ B");
    CHECK_EQ(intVar(*s, 'A'), 1);
    CHECK_EQ(intVar(*s, 'B'), 2);
    type(*s, "110 DATA 3");
    type(*s, "READ C");
    CHECK_EQ(intVar(*s, 'C'), 1);
  }

  TEST_CASE("a program the IDE rewrote reads from the first value again") {
    auto s = boot();
    settle(*s);
    type(*s, "100 DATA 1,2");
    type(*s, "READ A");
    // Same length, so only the bytes tell the programs apart.
    setProgram(*s, "100 DATA 7,2\n");
    type(*s, "READ B");
    CHECK_EQ(intVar(*s, 'B'), 7);
  }
```

- [ ] **Step 2: Run the tests to see them fail**

Run: `cmake --build --preset headless && ./build-headless/tests/basic/sc8_basic_tests -ts="DATA, READ and RESTORE"`
Expected: the nine new tests fail with `UNKNOWN WORD READ` or `UNKNOWN WORD RESTORE`.

- [ ] **Step 3: Declare, and add the words**

In `src/basic/basic.h`, before `#define E_NOTDATA 29 ...`:

```c
#define E_NODATA  28  /* READ past the last DATA value */
```

After `int dt_addr(int line);`:

```c
/* The next DATA value for READ, into a number or a string. */
int dt_read_int(void);
unsigned int dt_read_str(void);
/* READ starts again at the first value, or at the first of line n. */
void dt_restore(void);
void dt_restore_line(int line);
/* Around each command at the prompt. A READ part way through a program
 * that changed between two commands starts again at the first value.
 */
void dt_check(void);
void dt_mark(void);
```

In `src/basic/keywords.h`, the definition becomes:

```c
#define BASIC_KEYWORDS \
    " ABS AND ASC CALL CATALOG CHR$ CIRCLE CLS DATA DEEK DELETE DOKE DRAW END" \
    " FOR GOSUB GOTO IF INK INKEY$ INPUT JMP JSR KEY LEN LET LIST LOAD" \
    " MID$ MOD MOVE NEW NEXT NOT OR PAD PAPER PEEK PIXEL PLOT POKE PRINT" \
    " READ REM RENUM RESTORE RETURN RND RUN SAVE STEP STOP STR$ THEN TO USR VAL WAIT "
```

- [ ] **Step 4: The READ cursor in data.c**

In `src/basic/data.c`, after the `dt_fixed` declaration:

```c
/* Where READ is: rd_on is 0 until the first value is found, then rd_line
 * is the offset of the DATA line and rd_pos where its next value starts.
 * rd_hash is the program's hash at the end of the last command.
 */
static unsigned char rd_on;
static unsigned int rd_line;
static unsigned int rd_pos;
static unsigned int rd_hash;
```

In `dt_pack`, first line of the body:

```c
    rd_on = 0;
```

At the end of the file:

```c
/* Stand the lexer on the next value READ takes. Returns 0 after an error. */
static unsigned char rd_seek(void)
{
    unsigned int p;
    p = 0;
    if (rd_on) {
        err_line = LINE_AT(rd_line);
        lx_raw = 1;
        lx_seek((char *)&prog[rd_line + 3], rd_pos);
        if (lx_tok != T_END) return 1;
        p = rd_line + prog[rd_line + 2];
    }
    while (LINE_AT(p)) {
        if (is_data(p)) {
            if (!open_line(p)) return 0;
            rd_on = 1;
            rd_line = p;
            rd_pos = lx_tokpos;
            if (lx_tok != T_END) return 1;
        }
        p = p + prog[p + 2];
    }
    err_line = sv_line;
    rt_error(E_NODATA);
    return 0;
}

/* The next value into dv_*, with the cursor moved past it. */
static unsigned char rd_next(void)
{
    if (!rd_seek()) return 0;
    if (!value()) return 0;
    if (!sep()) return 0;
    rd_pos = lx_tokpos;
    return 1;
}

int dt_read_int(void)
{
    int v;
    v = 0;
    lx_save();
    if (rd_next()) {
        if (dv_kind == V_STR) { err_line = sv_line; rt_error(E_TYPE); }
        else v = dv_num;
    }
    lx_restore();
    return v;
}

unsigned int dt_read_str(void)
{
    unsigned int s;
    unsigned char i;
    s = 0;
    lx_save();
    if (rd_next()) {
        if (dv_kind != V_STR) { err_line = sv_line; rt_error(E_NEEDSTR); }
        else if (dv_len) {
            s = str_new(dv_len);
            if (s) {
                for (i = 0; i < dv_len; i++) heap[s + 1 + i] = lx_text[dv_str + i];
            }
        }
    }
    lx_restore();
    return s;
}

void dt_restore(void)
{
    rd_on = 0;
}

void dt_restore_line(int line)
{
    unsigned int p;
    p = ed_find(line);
    if (LINE_AT(p) != line) { err_arg = line; rt_error(E_NOLINE); return; }
    lx_save();
    if (!is_data(p)) { err_arg = line; rt_error(E_NOTDATA); }
    else if (open_line(p)) {
        rd_on = 1;
        rd_line = p;
        rd_pos = lx_tokpos;
    }
    lx_restore();
}

/* DESIGN: the IDE writes the program into memory between two commands, and
 * nothing tells the interpreter. A hash taken after each command finds the
 * change. It runs only at the prompt and only while READ is part way.
 */
static unsigned int prog_hash(void)
{
    unsigned int h;
    unsigned int i;
    h = prog_len;
    for (i = 0; i < prog_len; i++) h = ((h << 1) | (h >> 15)) ^ prog[i];
    return h;
}

void dt_check(void)
{
    if (rd_on && prog_hash() != rd_hash) rd_on = 0;
}

void dt_mark(void)
{
    if (rd_on) rd_hash = prog_hash();
}
```

- [ ] **Step 5: The statements and the message**

In `src/basic/run.c`, in `statement()`, after the INPUT line (`if (lx_is("INPUT")) ...`):

```c
    /* READ A, B$: the next DATA values, across lines. */
    if (lx_is("READ")) {
        lx_next();
        for (;;) {
            if (lx_tok != T_NAME) { rt_expect("A VARIABLE AFTER READ"); return 1; }
            if (IS_STRVAR) {
                unsigned char letter;
                unsigned int s;
                letter = lx_word[0] - 65;
                s = dt_read_str();
                if (err) return 1;
                svar[letter] = s;
            } else if (IS_INTVAR) {
                int slot;
                int v;
                slot = var_slot();
                v = dt_read_int();
                if (err) return 1;
                vars[slot] = v;
            } else {
                rt_error(E_NOTVAR);
                return 1;
            }
            lx_next();
            if (!lx_is(",")) return 1;
            lx_next();
        }
    }

    if (lx_is("RESTORE")) {
        lx_next();
        if (lx_tok == T_END || lx_is(":")) { dt_restore(); return 1; }
        {
            int n;
            n = ex_int();
            if (err) return 1;
            dt_restore_line(n);
            return 1;
        }
    }
```

In `say_error`, before the `E_NOTDATA` line:

```c
    else if (err == E_NODATA) term_puts("READ FOUND NO MORE DATA");
```

- [ ] **Step 6: Forget the cursor when the program changes**

In `src/basic/edit.c`, add `dt_restore();` as the last statement of `ed_new`, as the first statement of `ed_store` (before `p = ed_find(line);`), and as the first statement of `ed_renum` (before `/* The last number must fit ...`).

In `src/basic/main.c`, the loop body becomes:

```c
        term_putc(62);
        term_readline(input);
        dt_check();
        rt_line(input);
        dt_mark();
        if (running == 0) { term_puts("READY"); term_nl(); }
```

- [ ] **Step 7: Run the tests to see them pass**

Run: `cmake --build --preset headless && ./build-headless/tests/basic/sc8_basic_tests -ts="DATA, READ and RESTORE"`
Expected: all pass. Then `ctest --preset headless`, all pass.

- [ ] **Step 8: Commit**

```bash
git add src/basic/data.c src/basic/basic.h src/basic/keywords.h src/basic/run.c src/basic/edit.c src/basic/main.c tests/basic/basic_test.cpp
git commit -m "BASIC: READ and RESTORE

READ skips a line's address. A program changed at the prompt or by the
IDE reads from the first value again."
```

### Task 4: POKE with a list

**Files:**
- Modify: `src/basic/run.c`
- Test: `tests/basic/basic_test.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: nothing later tasks use.

- [ ] **Step 1: Write the failing test**

Add to the suite `"DATA, READ and RESTORE"`:

```cpp
  TEST_CASE("POKE with a list writes each value to the next box") {
    auto s = boot();
    settle(*s);
    type(*s, "POKE $9C40,1,2,3");
    CHECK_EQ(s->m->ram[0x9c40], 1);
    CHECK_EQ(s->m->ram[0x9c41], 2);
    CHECK_EQ(s->m->ram[0x9c42], 3);
    type(*s, "POKE $9C50,9: PRINT PEEK($9C50)");
    CHECK(has(after(text(*s), "PEEK($9C50)"), "9"));
  }
```

- [ ] **Step 2: Run the test to see it fail**

Run: `cmake --build --preset headless && ./build-headless/tests/basic/sc8_basic_tests -tc="POKE with a list*"`
Expected: FAIL, `ram[0x9c41]` is 0, and the prompt reports a syntax error at the second comma.

- [ ] **Step 3: POKE takes a list**

In `src/basic/run.c`, the POKE branch becomes:

```c
    if (lx_is("POKE")) {
        lx_next();
        {
            int a;
            a = ex_int();
            if (lx_is(",")) lx_next();
            poke(a, ex_int());
            /* POKE a, 1, 2, 3 fills the boxes after a in turn. */
            while (lx_is(",")) {
                lx_next();
                a = a + 1;
                poke(a, ex_int());
            }
            return 1;
        }
    }
```

- [ ] **Step 4: Run the tests to see them pass**

Run: `cmake --build --preset headless && ./build-headless/tests/basic/sc8_basic_tests -ts="DATA, READ and RESTORE"`
Expected: all pass. Then `ctest --preset headless`, all pass.

- [ ] **Step 5: Commit**

```bash
git add src/basic/run.c tests/basic/basic_test.cpp
git commit -m "BASIC: POKE takes a list of values for the boxes that follow"
```

### Task 5: RENUM follows RESTORE and DATA(n)

**Files:**
- Modify: `src/basic/edit.c` (`renum_text`), `src/basic/lines.cpp` (`rewriteReferences`)
- Test: `tests/basic/basic_test.cpp`, `tests/basic/lines_test.cpp`

**Interfaces:**
- Consumes: the DATA and RESTORE keywords from Tasks 1 and 3.
- Produces: nothing later tasks use.

- [ ] **Step 1: Write the failing tests**

In `tests/basic/basic_test.cpp`, add to the suite `"DATA, READ and RESTORE"`:

```cpp
  TEST_CASE("RENUM changes RESTORE n and DATA(n) and leaves a DATA line's values") {
    auto s = boot();
    settle(*s);
    type(*s, "5 RESTORE 7: A=DATA (7)");
    type(*s, "7 DATA 5, 7");
    type(*s, "RENUM");
    const auto& ram = s->m->ram;
    const size_t at = static_cast<size_t>((ram[basic::SYS_PROG] << 8) | ram[basic::SYS_PROG + 1]);
    const size_t len = static_cast<size_t>((ram[basic::SYS_PROG_LEN] << 8) | ram[basic::SYS_PROG_LEN + 1]);
    CHECK_EQ(basic::decodeProgram(std::span<const uint8_t>(ram.data() + at, len)),
             "10 RESTORE 20: A=DATA (20)\n20 DATA 5, 7\n");
  }
```

In `tests/basic/lines_test.cpp`, add to the suite `"the editor keeps BASIC lines in number order"`:

```cpp
  TEST_CASE("renumbering moves RESTORE and DATA( along and leaves a DATA line's values") {
    CHECK_EQ(renumbered("5 RESTORE 7: A=DATA (7)\n7 DATA 5, 7\n"), "10 RESTORE 20: A=DATA (20)\n20 DATA 5, 7\n");
  }
```

- [ ] **Step 2: Run the tests to see them fail**

Run: `cmake --build --preset headless && ./build-headless/tests/basic/sc8_basic_tests -tc="*RESTORE*DATA*"`
Expected: both fail. The references keep 7.

- [ ] **Step 3: The interpreter's RENUM**

In `src/basic/edit.c`, in `renum_text`, add two declarations to the top of the function:

```c
    unsigned char dp;
    unsigned int k;
```

Replace the block from `d = word_is(&src[i], j - i, "GOTO") ...` up to `if (d) {` with:

```c
                d = word_is(&src[i], j - i, "GOTO") || word_is(&src[i], j - i, "GOSUB") ||
                    word_is(&src[i], j - i, "THEN") || word_is(&src[i], j - i, "RESTORE");
                /* DATA( names a line. DATA and a value is a DATA line. */
                dp = word_is(&src[i], j - i, "DATA");
                while (i < j && o <= 250) { renum_buf[o] = src[i]; o = o + 1; i = i + 1; }
                if (dp) {
                    k = i;
                    while (src[k] == 32) k = k + 1;
                    if (src[k] == 40) {
                        while (i <= k && o <= 250) { renum_buf[o] = src[i]; o = o + 1; i = i + 1; }
                        d = 1;
                    }
                }
                if (d) {
```

The rest of the `if (d)` block stays as it is. It copies spaces, then renumbers the digits.

Also update the comment above `renum_text`: `The text of the line at p with its references renumbered` stays, and add after its first sentence: `A reference is the number after GOTO, GOSUB, THEN, RESTORE or DATA(.`

- [ ] **Step 4: The IDE's renumbering**

In `src/basic/lines.cpp`, in `rewriteReferences`, replace the line

```cpp
    if (word != "GOTO" && word != "GOSUB" && word != "THEN") continue;
```

with:

```cpp
    if (word == "DATA") {
      // DATA( names a line. DATA and a value is a DATA line.
      size_t k = i;
      while (k < body.size() && body[k] == ' ') k++;
      if (k == body.size() || body[k] != '(') continue;
      out.append(body, i, k + 1 - i);
      i = k + 1;
    } else if (word != "GOTO" && word != "GOSUB" && word != "THEN" && word != "RESTORE") {
      continue;
    }
```

The comment above `rewriteReferences` becomes:

```cpp
// The body's references rewritten through the map: a number after GOTO,
// GOSUB, THEN, RESTORE or DATA(. Strings are skipped, and REM and ! end the
// scan, since the rest of their line is not BASIC.
```

- [ ] **Step 5: Run the tests to see them pass**

Run: `cmake --build --preset headless && ./build-headless/tests/basic/sc8_basic_tests`
Expected: all pass. Then `ctest --preset headless`, all pass.

- [ ] **Step 6: Commit**

```bash
git add src/basic/edit.c src/basic/lines.cpp tests/basic/basic_test.cpp tests/basic/lines_test.cpp
git commit -m "BASIC: RENUM follows RESTORE n and DATA(n), at the prompt and in the IDE"
```

### Task 6: The docs, and the full check

**Files:**
- Modify: `docs/guides/basic.md`, `docs/basic-system-page.md`, `docs/standalone.md`, `src/basic/README.md`

**Interfaces:**
- Consumes: the finished feature.
- Produces: nothing.

- [ ] **Step 1: A guide section**

In `docs/guides/basic.md`, add `- [Tables of numbers](#tables-of-numbers)` to the contents after `Memory as numbered boxes`, and this section after the end of `## Memory as numbered boxes`, before `## Making decisions`:

````markdown
## Tables of numbers

A program often needs a list of numbers that never changes: the notes of
a tune, the shape of a character, the walls of a level. `DATA` holds such
a list, and `READ` takes the values one at a time.

```basic
10 FOR I=1 TO 4
20 READ N
30 PRINT N*N;" ";
40 NEXT I
100 DATA 3,5,7,9
```

```text
9 25 49 81
```

Each `READ` takes the next value. When one `DATA` line runs out, `READ`
carries on with the next one. `RESTORE` sends it back to the first value,
and `RESTORE 100` to the first value of line 100. `READ A$` takes a
string, written in quotes in the `DATA` line.

`DATA` must be the first word of its line. A running program passes over
a `DATA` line and does nothing.

When the program starts, `RUN` also puts every value in memory, one byte
each. So a value must fit in a box: 0 to 255, or -128 to -1. A `DATA`
line that starts with a number of three or four hex digits says which box
the bytes go to. The next lines carry on from there.

```basic
10 PRINT PEEK(40000);" ";PEEK(40003)
100 DATA $9C40,10,20,30
110 DATA 40,50,60
```

```text
10 40
```

`$9C40` is 40000, so 10, 20 and 30 go to boxes 40000 to 40002, and line
110 carries on at 40003. `READ` skips the address and gives 10 first.
`DATA(110)` gives the box line 110 starts at, 40003. Numbers stop at
32767, so `PRINT DATA(110)` shows -25533. `PEEK` and `POKE` still find
the right box.

`POKE` takes a list too. `POKE 40000,1,2,3` puts 1 in box 40000, 2 in
40001 and 3 in 40002.
````

- [ ] **Step 2: The word tables**

In `docs/guides/basic.md`, `## Every word`, statements table: replace the `POKE a,v` row and add rows after it:

```markdown
| `POKE a,v,w` | puts byte v in box a, w in box a+1, one box for each value |
| `DATA v,w` | holds values for `READ`, placed in memory by `RUN` |
| `READ V` | takes the next `DATA` value |
| `RESTORE` or `RESTORE n` | makes `READ` start again, at the first value or at line n |
```

Functions table, after the `DEEK(a)` row:

```markdown
| `DATA(n)` | the box where line n's `DATA` bytes go |
```

- [ ] **Step 3: The error table**

In `docs/basic-system-page.md`, after the row for code 27:

```markdown
| 28 | E_NODATA | `READ FOUND NO MORE DATA` |
| 29 | E_NOTDATA | `LINE n HOLDS NO DATA` |
| 30 | E_DATABYTE | `A DATA VALUE IS ONE BYTE: -128 TO 255` |
```

- [ ] **Step 4: The status and the file table**

In `docs/standalone.md`, the paragraph that begins `BASIC gets DATA, READ and RESTORE, designed and not built yet.` begins instead `BASIC has DATA, READ and RESTORE.`

In `src/basic/README.md`, add a row to the file table after `store.c`:

```markdown
| data.c | DATA lines: placement at RUN, DATA(n), READ and RESTORE. See docs/basic-data-design.md. |
```

and extend the paragraph on differences from the browser, after the sentence about RENUM:

```markdown
DATA, READ, RESTORE and DATA(n) are new in data.c. POKE takes a list of
values in run.c. lex.c records where each token starts, `lx_tokpos`, and
has a raw mode for strings, `lx_raw`, that data.c reads DATA lines with.
```

Also change `Seven of the files are the browser project's` only if the count of extracted files is no longer seven. It still is: data.c is new, like bang.c and store.c.

- [ ] **Step 5: Check the docs**

Run the docs-style checker on every file touched:

```bash
python3 ~/.claude/skills/synced/*/docs-style/scripts/check_docs.py docs/guides/basic.md docs/basic-system-page.md docs/standalone.md src/basic/README.md
```

Expected: `0 error(s)`. Lines the change did not touch may already report. Compare against `git stash; <same command>; git stash pop` and fix only what this change added.

- [ ] **Step 6: Both presets**

```bash
cmake --preset default && cmake --build --preset default && ctest --preset default
cmake --preset headless && cmake --build --preset headless && ctest --preset headless
```

Expected: both build without a warning and every test passes.

- [ ] **Step 7: Commit and push**

```bash
git add docs/guides/basic.md docs/basic-system-page.md docs/standalone.md src/basic/README.md
git commit -m "BASIC guide: tables of numbers with DATA, READ and RESTORE"
git push
```
