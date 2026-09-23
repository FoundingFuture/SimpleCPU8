# BASIC driver

The BASIC interpreter and an assembly driver in one ROM, laid out by
instruction slot. The interpreter starts at slot 0. The driver sits at slot
$F000, in its own PROG segment, and answers the HELLO bang command. demo.bas
installs it with DOKE and calls it. Boot the ROM, then type:

```basic
!LOAD "DEMO"
RUN
```

The ROM is built from two sources: basic.asm, which simplecpu-cc writes
from src/basic, and driver.asm. simplecpu-asm takes both on one command
line and each starts in the code section. demo.bas goes into the ROM's
BAS chunk with --bas DEMO=demo.bas.
