# 8x8 multiply

Multiplies two bytes into a 16 bit product, one printf line per pair.
Shift and add with no shift instruction: x + x is x << 1, and the bit leaving
the top lands in C. Halts.
