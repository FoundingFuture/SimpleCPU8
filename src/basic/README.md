# BASIC

The BASIC interpreter runs on the CPU. It is written in the machine's own C
dialect and compiled by simplecpu-cc. So this directory holds C sources, not
C++. The seven files port over from the TypeScript project's
packages/ui/src/basic/ once the C compiler is ported.

The interpreter boots from a ROM. simplecpu --basic builds that ROM from
these sources at build time and embeds it in the executable.
