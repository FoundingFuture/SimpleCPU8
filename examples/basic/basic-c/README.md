# BASIC calls C

A project that holds BASIC, C and assembly together. The ROM is one
program: the interpreter's own sources compiled with src/double.c, and
src/answer.asm appended after them. BASIC boots, runs src/autorun.bas
and calls the C and the assembly by name.

```basic
20 A = 21
30 CALL DOUBLE
40 PRINT A
50 CALL ANSWER
60 PRINT PEEK(4)
65 PRINT USR(2, TRIPLE, 14)
```

`CALL DOUBLE` calls the C function of that name, which doubles BASIC's
variable A through `<basicvars.h>`. `CALL ANSWER` calls the assembly
routine, and PEEK(4) reads the A register it came back with. `USR(2, TRIPLE, 14)` calls a C function that takes a number and gives
one back: 14 goes in, 42 comes out, a word wide. The build turns each
name into the label's instruction slot before the program goes into the
ROM. LIST shows the numbers.

    simplecpu-make .
    simplecpu --rom build/basic-c.rom
