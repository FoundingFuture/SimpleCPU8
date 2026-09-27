# The storage device and the bang chain

The fifth device on the bus, on ports $50 to $5F. It moves BASIC program
text between data RAM and the cartridge's BAS chunk, by slot name. BASIC
reaches it through the bang statement. The handler chain behind that
statement is the second half below. docs/standalone.md holds the decisions
this design rests on. src/devices/storage_ports.h holds the numbers.

## Contents

- [What it does](#what-it-does)
- [The slots](#the-slots)
- [The ports](#the-ports)
- [The commands](#the-commands)
- [The status byte](#the-status-byte)
- [The host side](#the-host-side)
- [The C library](#the-c-library)
- [The bang statement](#the-bang-statement)
- [The vector](#the-vector)
- [The handler contract](#the-handler-contract)
- [A driver in assembly](#a-driver-in-assembly)
- [The storage driver](#the-storage-driver)
- [Decisions taken here](#decisions-taken-here)
- [Still open](#still-open)

## What it does

The CPU cannot read the cartridge. docs/design/port-redesign.md sets the
rule: the CPU supplies a pointer and a magic box does the work. This device
keeps that rule. The program names a block of RAM and a slot. The device
copies text between them. Like the ACP it reads and writes data RAM. Unlike
any other device it changes the cartridge.

## The slots

A slot is a name and a text. The cartridge's BAS chunk holds them in order,
as src/core/cartridge.h describes. The name is the key. Two slots never share
a name: SAVE to a name that exists replaces its text in place.

A name is 1 to 16 bytes, each in the range 33 to 126. That rules out the
newline and the form feed the chunk format uses. It rules out the space
too, so a CATALOG line is one name. A cartridge holds at most 64 slots.
A text is at most 65535 bytes.

Names compare byte for byte. `Prog` and `PROG` are two slots.

## The ports

| Port | Name | Direction | Holds |
|---|---|---|---|
| $50 | STO_ADDR_HI | write | the block's address, high byte |
| $51 | STO_ADDR_LO | write | the block's address, low byte |
| $52 | STO_NAME_HI | write | the name's address, high byte |
| $53 | STO_NAME_LO | write | the name's address, low byte |
| $54 | STO_LEN_HI | write, read | write: the room at the block. read: bytes the last command moved |
| $55 | STO_LEN_LO | write, read | the low byte of the same |
| $56 | STO_CMD | write | run the command |
| $57 | STO_STATUS | read | how the last command went |
| $58 | STO_COUNT | read | how many slots the cartridge holds |

The latches are sticky, the way the ACP's are. A program that LOADs three
slots into the same block writes the address once. The length pair is an
argument on the way in and a result on the way out. The GPU's data ports
work the same way.

The block is where text lives in RAM: LOAD and CATALOG write it, SAVE reads
it. The name is a NUL terminated string anywhere in RAM. The room is the
byte count the block has, the NUL included. A command that would write past
it writes nothing and reports STO_FULL.

Every address is 16 bits and wraps, so no address is illegal, the same rule
the ACP follows.

## The commands

| Value | Name | Reads | Writes | Result in STO_LEN |
|---|---|---|---|---|
| $01 | STO_LOAD | the name, the room | the slot's text at the block, then a NUL | the text's length |
| $02 | STO_SAVE | the name, the block up to its NUL | the slot, created or replaced | the text's length |
| $03 | STO_DELETE | the name | nothing | 0 |
| $04 | STO_CATALOG | the room | every name at the block, each with a newline, then a NUL | the listing's length |

SAVE scans the block for its NUL and stops at 65535 bytes. A block with no
NUL inside that cap is refused as STO_FULL. The cap is the whole of RAM
less one byte, so the scan cannot lap. SAVE to a new name when 64 slots
exist is STO_FULL too. Replacing one of the 64 is not a 65th.

CATALOG with no slots writes a single NUL and reports a length of 0.
STO_COUNT reads the slot count at any time, with no command in between.

The command is one cycle, like every device command on this machine.

## The status byte

| Value | Name | Meaning |
|---|---|---|
| 0 | STO_OK | the command did what it says |
| 1 | STO_NOT_FOUND | LOAD or DELETE named a slot the cartridge lacks |
| 2 | STO_FULL | a cap was hit: 64 slots, 65535 bytes, or the room at the block |
| 3 | STO_BAD_NAME | the name is empty, over 16 bytes, unterminated, or holds a byte outside 33 to 126 |
| 4 | STO_BAD_CMD | STO_CMD took a value the device lacks |

A failed command changes nothing. The block, the slots and STO_COUNT are as
they were, and STO_LEN reads 0.

Power on clears the latches and the status. The slots stay, because they
are the cartridge's and a reset is not what changes a ROM.

## The host side

`sc8::Storage` is a `ChainedDevice`. The virtual computer gives it the
cartridge's `basic` list and a callback:

```cpp
storage.attachRam(machine.ram.data());
storage.attach(&cartridge.basic, [&] { writeRomFile(); });
```

The device edits that list in place. SAVE and DELETE call the callback
after the change. The host decides what a change means: simplecpu writes
the ROM file back, a test counts. LOAD and CATALOG never call it. With no
list attached, LOAD and DELETE report STO_NOT_FOUND. SAVE then reports
STO_FULL, as if the cartridge held no slots and had no room.

## The C library

`#include <storage.h>` gives one macro per command, in the style of the
other device headers. Each is exactly the port writes it shows.

```c
char block[256];
char name[8];
sto_load(block, name, sizeof block);
if (sto_status() == STO_OK) n = sto_len();
sto_save(block, name);
sto_delete(name);
sto_catalog(block, sizeof block);
n = sto_count();
```

The status names are C constants, like every name in
src/devices/storage_ports.h.

## The bang statement

A statement that begins with an exclamation mark hands the rest of its line
to the extension chain. The rest of the line means everything after the
mark to the end of the line, colons included. What a driver makes of its
text is the driver's business, and a file name may hold a colon.

```basic
!LOAD "GAME"
10 !CATALOG
```

It works at the prompt and inside a program. Leading spaces after the mark
are dropped before the text is handed on.

The chain answers in one of three ways. A handler acts on the text. The
built-in storage driver acts on it. Or nobody knows the word, and BASIC
reports `? UNKNOWN ! COMMAND ERROR`, with the line number when a program
was running.

## The vector

BASIC keeps a system page in the first 48 bytes of the zero page, which
the compiler leaves alone. docs/basic-system-page.md lists all of it.
Three items there belong to the chain.

| Address | Name | Holds |
|---|---|---|
| $0000 | BANG_VEC | the handler's instruction slot, high byte first |
| $0002 | BANG_TEXT | the address of the statement's text while a handler runs |
| $0004 | SYS_RESULT | the byte the handler left in A, parked by the dispatcher. A CALL from BASIC parks its routine's A here too |

A word here is big-endian, like every word on this machine. DOKE and DEEK
read and write it that way. The vector holds an instruction slot, not a
RAM address. Code lives in program memory, which has its own 64K of three
byte slots. The assembler folds `&label` on a code label to its slot.

At power on BASIC puts its own routine in the vector. That routine claims
nothing, so the built-in storage driver runs next and then the error. The
vector is never zero after boot.

## The handler contract

BASIC calls the routine in the vector with `JSR D2`. On entry:

- D1 holds the address of the NUL terminated text, and so does BANG_TEXT.
- The text is the rest of the line after the mark, leading spaces dropped.

On return, by `RET`:

- A is zero when the routine did not recognise the text. A is nonzero when
  it acted, whether or not the action went well.
- D1 and D2 may hold anything. BASIC reloads its frame pointer from the
  zero page after the call.

A routine may use the hardware stack in balance. It may not touch the zero
page past the system page, which is BASIC's register file. It may not write
BASIC's RAM other than through the address in D1 and the system page. The screen at $FAC0 is for printing.

BASIC's dispatch, in src/basic/bang.c:

```asm
        LD D2 <- [$0000]
        LD D1 <- [$0002]
        JSR D2
        LD D1 <- $8004
        LD [D1] <- A
        LD D1 <- [__sp]
```

## A driver in assembly

A driver installs itself by keeping the old vector as its next and writing
its own entry in its place. It jumps to its next for a word it does not
know, with D1 restored from BANG_TEXT.

```asm
install:
        LD D2 <- [$0000]
        LD [next] <- D2
        LD D2 <- &entry
        LD [$0000] <- D2
        RET

entry:
        LD A <- [D1]
        SUB A <- 66              ; is the text "B..."?
        JZ mine
        LD D1 <- [$0002]
        LD D2 <- [next]
        JMP D2                   ; pass it down the chain
mine:
        ; act on the text at D1
        LD A <- 1
        RET

.ram
next:   dw 0
```

From BASIC the same installation is two statements. The driver's code has
to be in program memory and its `next` word known:

```basic
DOKE 40000, DEEK(0)
DOKE 0, 4660
```

The first keeps the old vector at the driver's next word. The second
points the vector at the driver's entry slot. Both numbers come from the
driver's listing.

`PEEK(a)` reads a byte, `DEEK(a)` a big-endian word, `POKE a, v` writes a
byte and `DOKE a, v` a word. A BASIC integer is signed 16 bits, so the
address $F000 is 61440 on the way in and -4096 out of DEEK.

## The storage driver

The built-in link, in src/basic/store.c. It runs after the vector chain
has passed, and before the error. Four words, each with a quoted name where
one is needed:

| Statement | Does |
|---|---|
| `!SAVE "NAME"` | the listing LIST prints, into the slot NAME, created or replaced |
| `!LOAD "NAME"` | NEW, then every numbered line of the slot's text, stored as if typed |
| `!DELETE "NAME"` | drops the slot |
| `!CATALOG` | prints every slot name, one per line |

The text of a slot is the listing: `10 PRINT "HI"` and a newline per line.
The IDE's editor saves the same form, so what it writes BASIC can load. A
carriage return before the newline is dropped on the way in. A line with
no number is skipped rather than run. A saved listing has none, and running
one while loading would be a surprise.

LOAD stops a running program. Its lines are gone with the old program, so
there is nothing to return to.

The driver's errors, reported the way every BASIC error is:

| Device status | BASIC says |
|---|---|
| STO_NOT_FOUND | `? NOT FOUND ERROR` |
| STO_FULL | `? STORAGE FULL ERROR` |
| STO_BAD_NAME | `? BAD NAME ERROR` |
| a missing or unquoted name | `? SYNTAX ERROR` |

A listing longer than 8192 bytes is `? OUT OF MEMORY ERROR` before the
device sees it. The block is larger than the program store. A listing
spends up to six bytes per line where a record spends three.

## Decisions taken here

The task left these to the port, and each is the reading closest to the
existing design.

- The chain is a vector, not a table. The C dialect cannot call through a
  pointer, but the machine has `JSR D2`. So the dispatch is six lines of
  inline assembly. The chain keeps the Oric shape docs/standalone.md
  describes. The default handler is a two instruction routine inside
  bang_init, jumped over on entry. The compiler drops a function nothing
  calls, so the routine cannot be one.
- The vector sits at $0000, in the system page. The compiler places every
  global by weight and moves them when the program changes. So no C
  variable has a fixed address. The page is reserved from the compiler
  with -zp-reserve 48, and the vector is its first word.
- The statement takes the whole line. A colon inside a name would
  otherwise split a command in two.
- The name is passed by address, through a hi and lo pair, as the ACP
  passes its block. Byte at a time would cost a port and a command.
- The room is a required argument. A short block writes nothing rather
  than a truncated program.
- STO_BAD_CMD exists. The ACP reports an unknown command as ACP_BADFMT, and
  a status of its own is more honest.
- LOAD skips unnumbered lines rather than running them.

## Still open

- The virtual computer does not construct the device yet. It goes between
  the APU and the input device. It attaches to the cartridge's basic list
  with a callback that writes the ROM file.
- The IDE's BASIC editor pane writes the same slot text. Nothing checks
  that yet from the IDE side.
- No test installs a driver written in assembly, because BASIC's ROM is
  built from C alone. The contract is exercised through BASIC's own default
  routine.
