// The arithmetic coprocessor's ports, element types, commands and flags. A
// peripheral on IO ports $40 to $4F, and the only device that writes data
// RAM. One command reads two operands and writes one result through a block
// of RAM the program nominates. See docs/acp-design.md.
#pragma once

#include <cstdint>

#include "devices/gpu_ports.h"

namespace sc8::acp {

using sc8::gpu::NamedValue;

constexpr uint8_t PORT_LO = 0x40;
constexpr uint8_t PORT_HI = 0x4f;

constexpr NamedValue PORTS[] = {
    {"ACP_ADDR_HI", 0x40},  // block address, high byte
    {"ACP_ADDR_LO", 0x41},  // block address, low byte
    {"ACP_FMT", 0x42},      // operand element type. Writing it sets the result type too
    {"ACP_RFMT", 0x43},     // result element type, when it differs
    {"ACP_CMD", 0x44},      // write to run an operation
    {"ACP_FLAGS", 0x45},    // read: how the last operation went
    {"ACP_ROWS", 0x46},     // rows in A
    {"ACP_COLS", 0x47},     // columns in A
    {"ACP_COLS_B", 0x48},   // columns in B. Only ACP_MATMUL reads it
    {"ACP_ARG", 0x49},      // a count, high byte
    {"ACP_ARG2", 0x4a},     // a count, low byte
    {"ACP_FUNC", 0x4b},     // which function a table samples
};

// The element type. A scalar is 1 by 1 and a vector of N is N by 1.
constexpr NamedValue FMTS[] = {
    {"ACP_I64", 0},
    {"ACP_U64", 1},
    {"ACP_F64", 2},
    {"ACP_C64", 3},
};

constexpr NamedValue CMDS[] = {
    // Element by element, over whatever shape the dimension ports give.
    {"ACP_ADD", 0x01},
    {"ACP_SUB", 0x02},
    {"ACP_MUL", 0x03},
    {"ACP_DIV", 0x04},
    {"ACP_REM", 0x05},
    {"ACP_NEG", 0x06},
    {"ACP_ABS", 0x07},
    {"ACP_CMP", 0x08},
    {"ACP_CVT", 0x09},
    {"ACP_SCALE", 0x0a},
    // Transcendental, float only.
    {"ACP_SQRT", 0x10},
    {"ACP_SIN", 0x11},
    {"ACP_COS", 0x12},
    {"ACP_TAN", 0x13},
    {"ACP_ASIN", 0x14},
    {"ACP_ACOS", 0x15},
    {"ACP_ATAN", 0x16},
    {"ACP_LOG", 0x17},
    {"ACP_LOG10", 0x18},
    {"ACP_EXP", 0x19},
    {"ACP_ATAN2", 0x1a},
    {"ACP_POW", 0x1b},
    {"ACP_HYPOT", 0x1c},
    // Complex only.
    {"ACP_ARG_OF", 0x20},
    {"ACP_CONJ", 0x21},
    // Vectors, which are matrices of one column.
    {"ACP_DOT", 0x30},
    {"ACP_CROSS", 0x31},
    {"ACP_NORM", 0x32},
    {"ACP_NORMALIZE", 0x33},
    {"ACP_DIST", 0x34},
    // Matrices.
    {"ACP_MATMUL", 0x40},
    {"ACP_MATVEC", 0x41},
    {"ACP_TRANSPOSE", 0x42},
    {"ACP_IDENTITY", 0x43},
    {"ACP_DET", 0x44},
    {"ACP_INVERSE", 0x45},
    // A table is a result with many entries.
    {"ACP_GEN_TABLE", 0x50},
    // Bit manipulation, integer only.
    {"ACP_AND", 0x60},
    {"ACP_OR", 0x61},
    {"ACP_XOR", 0x62},
    {"ACP_NOT", 0x63},
    {"ACP_SHL", 0x64},
    {"ACP_SHR", 0x65},
    {"ACP_ASR", 0x66},
    {"ACP_ROL", 0x67},
    {"ACP_ROR", 0x68},
};

constexpr NamedValue FLAG_BITS[] = {
    {"ACP_ZERO", 1 << 0},
    {"ACP_NEGATIVE", 1 << 1},
    {"ACP_DIVZERO", 1 << 2},
    {"ACP_OVERFLOW", 1 << 3},
    {"ACP_NAN", 1 << 4},
    {"ACP_BADFMT", 1 << 5},
    {"ACP_SINGULAR", 1 << 6},
    {"ACP_BADDIM", 1 << 7},
};

}  // namespace sc8::acp
