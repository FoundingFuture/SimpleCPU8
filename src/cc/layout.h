// The memory map the compiler obeys, and the names the runtime reserves.
// See docs/design/c-compiler-design.md, "The memory map".
#pragma once

#include <string>

#include "core/machine.h"

namespace sc8::cc {

// The text grid, 42 columns by 32 rows of the 6 by 8 font. In the browser
// project these derive from the GPU's font. The GPU is not ported into
// this tree yet, so the three numbers live here until it lands, and
// startup_test.cpp pins the stack top to them.
constexpr int TEXT_COLS = 42;
constexpr int TEXT_ROWS = 32;
constexpr int TEXT_CELLS = TEXT_COLS * TEXT_ROWS;

// DESIGN: the C stack always starts below the text screen, even in a program
// that never maps one. The design left this to the linker, at $FFFF without
// text mode and $FBFF with it. One address instead, always the lower: a
// student who adds one call to gpu_set_textmode must not have their stack
// start overwriting the screen. A kilobyte and a third of 64 is the price of
// never having to explain that particular corruption.
constexpr int C_STACK_TOP = RAM_SIZE - TEXT_CELLS;

// The zero page's own residents, in the order they are emitted. Everything
// here is a compiler reservation and comes before any of the program's own
// variables, which is step 1 of the allocation order in the design.
constexpr const char* ZP_RET = "__ret";  // 2: every return value comes back here
constexpr const char* ZP_CMP = "__cmp";  // 1: one byte of scratch a word compare needs
constexpr const char* ZP_RA = "__ra";    // 2: runtime operand A, and its result
constexpr const char* ZP_RB = "__rb";    // 2: runtime operand B

constexpr int RESERVED_BYTES = 2 + 1 + 2 + 2;

constexpr int ZERO_PAGE_SIZE = ZERO_PAGE;

// A temp slot is two bytes: high then low, like every word on this machine.
inline std::string tempLabel(int n) { return "__t" + std::to_string(n); }

// A frame reaches its locals and arguments through [D3+n], and n is a byte.
constexpr int MAX_FRAME = 255;

}  // namespace sc8::cc
