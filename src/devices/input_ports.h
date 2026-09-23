// The input device: a controller byte, a key event queue and the modifier
// levels, on IO ports $20 to $22. Buttons and the key buffer are hardware
// state, so they survive load and restart.
#pragma once

#include <cstdint>

#include "devices/gpu_ports.h"

namespace sc8::input {

using sc8::gpu::NamedValue;

constexpr uint8_t PORT_LO = 0x20;
constexpr uint8_t PORT_HI = 0x22;

// How many key events the device buffers before the oldest is dropped.
constexpr int KEY_QUEUE_MAX = 64;

constexpr NamedValue PORTS[] = {
    // Read: the button byte, a level, so a held button stays set.
    {"IO_CONTROLLER", 0x20},
    // Read: pop one key event. Low seven bits the key code, the top bit the
    // release flag. Zero means the queue is empty.
    {"IO_KEY", 0x21},
    // Read: the modifier keys held right now. A level, like the pad, so
    // reading it costs no key event.
    {"IO_MODS", 0x22},
};

// Button bit masks. A program tests one with AND A <- BTN_FIRE.
constexpr NamedValue BUTTONS[] = {
    {"BTN_UP", 0x01},
    {"BTN_DOWN", 0x02},
    {"BTN_LEFT", 0x04},
    {"BTN_RIGHT", 0x08},
    {"BTN_FIRE", 0x10},
    {"BTN_SPACE", 0x20},
    {"BTN_ENTER", 0x40},
};

constexpr NamedValue MODS[] = {
    {"MOD_SHIFT", 0x01},
    {"MOD_CTRL", 0x02},
    {"MOD_ALT", 0x04},
    {"MOD_META", 0x08},
};

}  // namespace sc8::input
