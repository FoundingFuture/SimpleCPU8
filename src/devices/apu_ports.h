// The audio chip's ports and commands. A peripheral on IO ports $30 to $3F
// that reads the same cartridge the GPU reads. Eight tracks, eight players
// each, 64 voices. Output is 8 bit unsigned mono at AUDIO_RATE.
#pragma once

#include <cstdint>

#include "devices/gpu_ports.h"

namespace sc8::apu {

using sc8::gpu::NamedValue;

constexpr uint8_t PORT_LO = 0x30;
constexpr uint8_t PORT_HI = 0x3f;

constexpr int AUDIO_RATE = 8000;
constexpr int TRACK_COUNT = 8;
constexpr int PLAYERS_PER_TRACK = 8;
constexpr int VOICE_COUNT = TRACK_COUNT * PLAYERS_PER_TRACK;
constexpr int SLOT_COUNT = 64;

constexpr NamedValue PORTS[] = {
    {"APU_CART_LO", 0x30},    // ROM address low byte, for a define or a load
    {"APU_CART_HI", 0x31},    // ROM address high byte
    {"APU_CART_BANK", 0x32},  // ROM bank, 0 to 15, flat 1MB addressing
    {"APU_TRACK", 0x33},      // select the track, 0 to 7
    {"APU_NOTE", 0x34},       // note number, 0 to 127, also the root for a define
    {"APU_ARG", 0x35},        // scalar argument: velocity, length low, octave, slot
    {"APU_ARG2", 0x36},       // scalar argument: length high
    {"APU_SLOT", 0x37},       // select the sample slot
    {"APU_CMD", 0x38},        // run a command, the value picks it
    {"APU_STATUS", 0x39},     // read: bit 0 set while a tune plays
    {"APU_VOICES", 0x3a},     // read: count of active voices
};

constexpr NamedValue CMDS[] = {
    {"CMD_DEF_SAMPLE", 0x01},
    {"CMD_SET_INSTRUMENT", 0x02},
    {"CMD_KEYMAP_ENTRY", 0x03},
    {"CMD_NOTE_ON", 0x04},
    {"CMD_NOTE_OFF", 0x05},
    {"CMD_TRIGGER", 0x06},
    {"CMD_LOAD_MIDI", 0x07},
    {"CMD_PLAY", 0x08},
    {"CMD_STOP", 0x09},
    {"CMD_STOP_ALL", 0x0a},
    {"CMD_LOOP_SLOT", 0x0b},
};

}  // namespace sc8::apu
