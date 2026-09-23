// The storage device's ports, commands and status codes. A peripheral on IO
// ports $50 to $5F that moves BASIC program text between the cartridge's
// BAS chunk and data RAM. Slots are named, and a name is a NUL terminated
// string in RAM the program points the device at. See docs/storage-design.md.
#pragma once

#include <cstdint>

#include "devices/gpu_ports.h"

namespace sc8::storage {

using sc8::gpu::NamedValue;

constexpr uint8_t PORT_LO = 0x50;
constexpr uint8_t PORT_HI = 0x5f;

// The caps STO_FULL reports against.
constexpr int SLOT_MAX = 64;
constexpr int TEXT_MAX = 65535;
constexpr int NAME_LIMIT = 16;

constexpr NamedValue PORTS[] = {
    // The text block. LOAD and CATALOG write it, SAVE reads it.
    {"STO_ADDR_HI", 0x50},
    {"STO_ADDR_LO", 0x51},
    // The NUL terminated slot name, anywhere in RAM.
    {"STO_NAME_HI", 0x52},
    {"STO_NAME_LO", 0x53},
    // Write: the room at the block. Read: bytes the last command moved.
    {"STO_LEN_HI", 0x54},
    {"STO_LEN_LO", 0x55},
    {"STO_CMD", 0x56},     // write to run a command.
    {"STO_STATUS", 0x57},  // read: how the last command went.
    {"STO_COUNT", 0x58},   // read: slots the cartridge holds.
};

constexpr NamedValue CMDS[] = {
    {"STO_LOAD", 0x01},     // the slot's text to the block, then a NUL.
    {"STO_SAVE", 0x02},     // the block up to its NUL, into the slot.
    {"STO_DELETE", 0x03},   // drop the slot.
    {"STO_CATALOG", 0x04},  // a line per name to the block, then a NUL.
};

constexpr NamedValue STATUS[] = {
    {"STO_OK", 0},
    {"STO_NOT_FOUND", 1},  // no slot of that name.
    {"STO_FULL", 2},       // 64 slots, 65535 bytes, or the room.
    {"STO_BAD_NAME", 3},   // empty, long, or a byte outside 33 to 126.
    {"STO_BAD_CMD", 4},    // a command byte the device lacks.
};

}  // namespace sc8::storage
