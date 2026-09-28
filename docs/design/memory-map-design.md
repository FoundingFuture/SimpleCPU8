# Memory map design

Data RAM grows from 2048 bytes to the full 64KB the CPU can already address.
The text screen maps at the top, so it is free RAM until a program switches
text mode on. This records the decisions and what they cost.

## Contents

- [Why now](#why-now)
- [What already works](#what-already-works)
- [The map](#the-map)
- [Address overflow](#address-overflow)
- [The text screen](#the-text-screen)
- [The snapshot problem](#the-snapshot-problem)
- [The heat view](#the-heat-view)
- [What does not change](#what-does-not-change)
- [Decisions taken](#decisions-taken)

## Why now

Two reasons, and the second is the larger one.

The GPU port redesign kept running into 2048 bytes. A palette is 768 bytes, a
text plane is 1024, a collision table is 256. Together they are 2048, which is
all of data RAM. Any design therefore had to make programs share one region
and sequence their use of it. That constraint disappears at 64KB.

The bigger reason is the teaching ladder Eddie wants: BASIC, then C, then
assembly, then microcode. Interpreter, compiler, assembler, and the machine
under all three. A BASIC interpreter needs the program text, a variable table
and a workspace, all in data RAM. In 2048 bytes there is no room for a program
worth writing.

## What already works

The CPU can already form any 16-bit address. `D1` and `D2` are 16-bit, and
`addr16` is a full 16-bit operand. Only the dereference is capped:
`machine.ts:258` crashes when an effective address reaches 2048.

So a program can legally compute `$8000` into `D1` today, and then die using
it. The cap is arbitrary, not architectural. Lifting it makes the machine
honest rather than capable.

## The map

```text
$0000  zero page, 256 bytes, reached by addr8
$0100  general data RAM
  .
  .
$F000  text screen, 4 KB, a grid the text cell sets
$FFFF  top
```

Free RAM when text mode is off. A screen when it is on. Writing to it while
text mode runs shows up on the next frame. That is already how the mapped
region behaves.

The GPU's text cell sets the grid, 42 by 32 at power on. That grid reads
1344 bytes. A 4 by 4 cell gives 64 by 64, which fills the 4 KB.

The C stack grows down from $FAC0 by default, below a 42 by 32 screen.
`--heap-stack-top ADDR` moves it. BASIC passes $F000, below its 4 KB screen.

## Address overflow

The effective address adder takes a 16-bit base, an optional 8-bit offset and
an optional carry, so it can produce 65791. Above 64KB there is no memory.

The address wraps, masked to 16 bits. It does not crash.

Wrapping is what an 8-bit machine does, and it makes every address the adder
can produce a legal one. That removes the `illegal-data-address` crash
entirely, because no address can be illegal any more.

This is a real loss and worth stating. A program that walks a pointer off the
end used to stop with a clear message. It will now quietly read address 0.
The trade is that the machine stops pretending an address it can build is one
it refuses to use.

## The text screen

`CMD_TEXT_MAP` stays. A program can still point the screen anywhere, which is
what the mapped-RAM demos teach.

What changes is the convention: the top of RAM is where it belongs, and the
demos and the manual say so. A program that maps it elsewhere is doing
something deliberate rather than following the only example it saw. The
address is `$F000`, room for the 64 by 64 grid of a 4 by 4 cell. It was
`$FAC0` for the 42 by 32 grid alone, and `$FC00` under the 8 by 8 font.
examples/matrix and examples/textmode keep 6 by 8 and map theirs at
`$FAC0`.

Room to move it is what makes double buffering possible. Show `$FAC0`, build
the next frame at `$F580`, wait for `GPU_FRAME` to tick, then map `$F580` and
build the next one back at `$FAC0`. The screen never shows a half-drawn frame.
That is worth a demo, because it teaches page flipping on a machine small
enough to see all of it.

## The snapshot problem

The worker sends the UI a snapshot every 33ms, holding all of RAM plus two
`Uint32Array` heat maps sized to RAM. At 2048 bytes that is 18KB a snapshot.
At 64KB it is 576KB, or 16.9MB a second, which would make the UI crawl.

The RAM itself is only 64KB of that. The two heat arrays are 89%: four bytes
of timestamp for every byte of memory.

The fix is to send what the UI shows rather than everything it might show.

- The zero page view reads 256 bytes. Send those always.
- Watch chips read a handful of addresses. Send those.
- The heat view reads every address, but only while its tab is open. It
  reduces each one to a colour, so the worker reduces it first and sends one
  byte per address.

That is about 1.5KB a snapshot, whatever the size of RAM, and 65KB while the
heat tab is open. Today it is 18KB whether anything is looking or not.

The heat byte packs what the renderer actually uses. Bit 7 says a write was
more recent than a read, and bits 0 to 6 carry the log-scaled heat. The colour
ramp stays on the page, where it belongs.

Two new messages carry what the worker cannot guess: which addresses are
watched, and whether the heat tab is open.

Sharing one 64KB buffer between the worker and the page would be the obvious
answer, and `SharedArrayBuffer` is the mechanism. It needs the page to be
cross-origin isolated, which needs `COOP` and `COEP` headers from the server.
This project sends none, and a plain static webserver cannot be told to. The
deployment promise is that the build serves from any webserver, in any
directory. Shared memory costs more than it saves here, and the numbers above
say nothing needs sharing.

This work is needed whatever the size. Shipping four bytes per address, so
the UI can throw away three, was always waste. 64KB only makes it visible.

## The heat view

At 2048 bytes the view drew each byte as an 8 by 4 block to fill its 256 by
256 canvas. At 64KB it draws one pixel per byte, because 256 times 256 is
65536 exactly.

The memory map stops being a diagram of memory and becomes memory.

The two `Uint32Array` timestamp maps stay in the worker. What crosses is one
byte per address, and only while the tab is open.

## What does not change

- The zero page is still the first 256 bytes, so `addr8` addressing and every
  existing program work unchanged.
- The stack is still its own memory, addressed only by `SP`. It is not part
  of this space. It later grew to 4096 bytes, on a 16 bit `SP`.
- Program memory is still separate. It holds 64K slots of an opcode and a
  16-bit operand, and the CPU cannot read it as data.
- The cartridge is still 1MB, still GPU and APU only.

## Decisions taken

- Data RAM becomes 65536 bytes.
- An effective address wraps at 16 bits rather than crashing, which retires
  the `illegal-data-address` crash. Every address is legal, and a program
  that goes somewhere surprising gets to find out what is there.
- The text screen's home is the top of RAM, and `CMD_TEXT_MAP` still moves it.
- The snapshot always sends the zero page and the watched addresses. It sends
  heat only while its tab is open, as one byte per address.
- `Session.snapshot()` keeps its shape, because the tests read it. The worker
  sends a separate wire snapshot.
- The heat view draws one pixel per byte.
