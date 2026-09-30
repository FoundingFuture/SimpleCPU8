# Cycles

What every instruction costs, in microcycles, under the ROM's own
microcode set and under the optimal set. The two sit side by side on the
screen. The build counts the rows in src/microcode.txt, the fetch program
plus the instruction's own. It writes the lines into the ROM.

This is the project `simplecpu-make new mine --microcode` lays out. The
set starts as the naive one. Merge two rows of an instruction in
microcode.txt, build, run: its number in the MY column drops. The IDE's
Microcode level shows the rows firing.

    simplecpu-make examples/microcode/cycles
    simplecpu --rom examples/microcode/cycles/build/cycles.rom
