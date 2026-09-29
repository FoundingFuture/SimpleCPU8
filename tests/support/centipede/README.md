# Centipede prototypes

The BASIC programs docs/design/centipede-design.md measures its model
with. Each writes a counter to $E7FF at each checkpoint, and
tests/support/basic_profile.cpp prints the cycles between the changes:

```bash
build-headless/tests/basic/basic_profile --marks tests/support/centipede/chain.bas
build-headless/tests/basic/basic_profile --marks tests/support/centipede/player_shot.bas --pad 24
build-headless/tests/basic/basic_profile --marks tests/support/centipede/statements.bas --per 500
```

| File | Measures |
|---|---|
| statements.bas | one statement a loop of 500 passes, after a loop on REM |
| loops.bas | whole loops of 1000 passes, one POKE a pass among them |
| redraw.bas | a 12 segment chain redrawn cell by cell, four steps |
| chain.bas | a chain step, head and tail, the chain in variables |
| chain_record.bas | the same, the chain loaded from a RAM record and stored back |
| player_shot.bas | the player's half step and the shot's climb, then 40 mushrooms |
| spider.bas | a spider step, half a cell sideways and a row, eating mushrooms |
| tick.bas | the tick's own lines with every part a bare RETURN, 100 ticks |
| status.bas | the score's POKE of six digits on row 0 |
| loop.bas | the loop the step prototypes share, around a bare RETURN |

PAD 24 is right and fire held. statements.bas has one REM loop at line
120 and another at line 1200. A line numbered below 256 costs rt_run 47
cycles more each time it runs. So each statement is taken against the
REM loop in its own range of line numbers.
