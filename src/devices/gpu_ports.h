// The GPU's ports, aliases, commands and modifier bits. A peripheral on IO
// ports $00 to $1F: 256x256 pixels, one byte each, through a 256 entry RGB
// palette. See docs/gpu-ports.md for the reference these tables generate.
//
// Eleven ports, where there were thirty. Everything is a command, and a
// data port means whatever the running command says it means. The two
// direct reads sit at the ceiling and the command block grows from the
// floor, so the map grows from both ends into the middle.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace sc8::gpu {

constexpr uint8_t PORT_LO = 0x00;
constexpr uint8_t PORT_HI = 0x1f;

struct NamedValue {
  std::string_view name;
  int value;
};

constexpr NamedValue PORTS[] = {
    {"GPU_CMD", 0x00},      // write a command byte to run it
    {"GPU_CMD_MOD", 0x01},  // modifier bits for the next command
    {"GPU_DATA0", 0x02},    // arguments in, results out
    {"GPU_DATA1", 0x03},
    {"GPU_DATA2", 0x04},
    {"GPU_DATA3", 0x05},
    {"GPU_DATA4", 0x06},
    {"GPU_DATA5", 0x07},
    {"GPU_DATA6", 0x08},
    {"GPU_RAND", 0x1e},   // read: a random byte. write: reseed
    {"GPU_FRAME", 0x1f},  // read: the frame counter
};

constexpr int DATA_COUNT = 7;

// Names for the data ports, by the job a command gives them. Every one
// resolves to a port between $02 and $08, and several names share a port.
// A command with no index puts its first argument in DATA0. A command WITH
// an index puts the index there and everything shifts up by one.
constexpr NamedValue ALIASES[] = {
    // Primitives: x and y, signed 16 bit, high byte first.
    {"GPU_X_HI", 0x02},
    {"GPU_X", 0x03},
    {"GPU_Y_HI", 0x04},
    {"GPU_Y", 0x05},
    {"GPU_PIXEL", 0x06},
    {"GPU_COLOR", 0x02},
    {"GPU_RADIUS", 0x02},
    // A cartridge address, where the command takes no index.
    {"GPU_CART_BANK", 0x02},
    {"GPU_CART_HI", 0x03},
    {"GPU_CART_LO", 0x04},
    // A cartridge address, where the command takes an index first.
    {"GPU_SRC_BANK", 0x03},
    {"GPU_SRC_HI", 0x04},
    {"GPU_SRC_LO", 0x05},
    // A RAM address, and a count beside it.
    {"GPU_ADDR_HI", 0x02},
    {"GPU_ADDR_LO", 0x03},
    {"GPU_COUNT_HI", 0x04},
    {"GPU_COUNT_LO", 0x05},
    // CMD_RAM_MOVE's source.
    {"GPU_FROM_HI", 0x03},
    {"GPU_FROM_LO", 0x04},
    // CMD_COPY, the widest command: cartridge, then RAM, then a length.
    {"GPU_DEST_HI", 0x05},
    {"GPU_DEST_LO", 0x06},
    {"GPU_LEN_HI", 0x07},
    {"GPU_LEN_LO", 0x08},
    // CMD_MEMMAP.
    {"GPU_MAP", 0x02},
    {"GPU_MAP_HI", 0x03},
    {"GPU_MAP_LO", 0x04},
    // Sprites. The index is DATA0 on every command that takes one.
    {"GPU_SPRITE", 0x02},
    {"GPU_SPRITE_B", 0x03},
    {"GPU_SPRITE_FRAME", 0x03},
    {"GPU_SPRITE_FLIP", 0x03},
    {"GPU_SPRITE_X_HI", 0x03},
    {"GPU_SPRITE_X", 0x04},
    {"GPU_SPRITE_Y_HI", 0x05},
    {"GPU_SPRITE_Y", 0x06},
    // Collision answers come back where the index went in.
    {"GPU_HIT", 0x02},
    // Sprite groups.
    {"GPU_GROUP", 0x02},
    {"GPU_GROUP_B", 0x03},
    {"GPU_HIT_FROM", 0x04},
    {"GPU_SPRITE_GROUP", 0x06},
    // Paths and the transform chain.
    {"GPU_ANGLE", 0x02},
    {"GPU_SCALE", 0x02},
    // Palette rotation.
    {"GPU_SPEED", 0x02},
    {"GPU_DIR", 0x02},
    {"GPU_FIRST", 0x02},
    {"GPU_LAST", 0x03},
    // Text.
    {"GPU_TEXT_COLOR", 0x02},
    {"GPU_TEXT_BG", 0x03},
    {"GPU_TEXT_FLAGS", 0x04},
    {"GPU_TEXT_COL", 0x02},
    {"GPU_TEXT_ROW", 0x03},
    {"GPU_TEXT_CHAR", 0x02},
    {"GPU_TEXT_ARG_HI", 0x05},
    {"GPU_TEXT_ARG_LO", 0x06},
    // The 3D world.
    {"GPU_MESH", 0x02},
    {"GPU_MESH_BYTE", 0x02},
    {"GPU_RAMP", 0x02},
    {"GPU_RED", 0x03},
    {"GPU_GREEN", 0x04},
    {"GPU_BLUE", 0x05},
};

// Modifier bits, held until written again. A mode, not a one-shot.
constexpr NamedValue MODS[] = {
    {"MOD_STICKY", 1},  // data ports keep their values after the command
    {"MOD_INC0", 2},    // DATA0 steps by one after the command
};

// Command bytes for GPU_CMD. One block of sixteen per family, and a command
// never moves out of its family's block:
//
//   $00-$0F  drawing        $40-$4F  transforms and paths
//   $10-$1F  palette        $50-$5F  text
//   $20-$2F  sprites        $60-$6F  the 3D world
//   $30-$3F  collision      $70-$7F  memory
//
// $00 is not a command, so a stray write lands on nothing.
constexpr NamedValue CMDS[] = {
    {"CMD_CLEAR", 0x01},
    {"CMD_SET_COLOR", 0x02},
    {"CMD_MOVE_TO", 0x03},
    {"CMD_LINE_TO", 0x04},
    {"CMD_RECT", 0x05},
    {"CMD_CIRCLE", 0x06},
    {"CMD_RING", 0x07},
    {"CMD_PLOT", 0x08},
    {"CMD_READ_PIXEL", 0x09},
    {"CMD_SAVE_SCREEN", 0x0a},
    {"CMD_RESTORE_SCREEN", 0x0b},
    {"CMD_RESET_PALETTE", 0x10},
    {"CMD_LOAD_PALETTE", 0x11},
    {"CMD_STORE_PALETTE", 0x12},
    {"CMD_FETCH_PALETTE", 0x13},
    {"CMD_ROTATE_LEFT", 0x14},
    {"CMD_ROTATE_RIGHT", 0x15},
    {"CMD_ROTATE_SPEED", 0x16},
    {"CMD_ROTATE_DIR", 0x17},
    {"CMD_ROTATE_RANGE", 0x18},
    {"CMD_SPRITE_DEF", 0x20},
    {"CMD_SPRITE_MOVE", 0x21},
    {"CMD_SPRITE_SHOW", 0x22},
    {"CMD_SPRITE_HIDE", 0x23},
    {"CMD_SPRITE_FRAME", 0x24},
    {"CMD_SPRITE_FLIP", 0x25},
    {"CMD_STAMP", 0x26},
    {"CMD_BLIT", 0x27},
    {"CMD_HIT_TEST", 0x30},
    {"CMD_HIT_SCAN", 0x31},
    {"CMD_COLLIDE_ALL", 0x32},
    {"CMD_SPRITE_HITS", 0x33},
    {"CMD_GROUP_HITS", 0x34},
    {"CMD_HIT_IN_GROUP", 0x35},
    {"CMD_COLLIDE_GROUP_ALL", 0x36},
    {"CMD_ROT_X", 0x40},
    {"CMD_ROT_Y", 0x41},
    {"CMD_ROT_Z", 0x42},
    {"CMD_SET_SCALE", 0x43},
    {"CMD_DRAW_PATH", 0x44},
    {"CMD_DRAW_PATH3D", 0x45},
    {"CMD_SET_TEXTMODE", 0x50},
    {"CMD_SET_GRAPHICSMODE", 0x51},
    {"CMD_TEXT_STYLE", 0x52},
    {"CMD_TEXT_AT", 0x53},
    {"CMD_TEXT_CHAR", 0x54},
    {"CMD_TEXT_CLEAR", 0x55},
    {"CMD_PRINTF", 0x56},
    {"CMD_LOAD_FONT", 0x57},
    {"CMD_SET_WORLDMODE", 0x60},
    {"CMD_MESH_LOAD", 0x61},
    {"CMD_MESH_WRITE", 0x62},
    {"CMD_WORLD_RAMP", 0x63},
    {"CMD_MATRIX_MAP", 0x64},
    {"CMD_COPY", 0x70},
    {"CMD_MEMMAP", 0x71},
    {"CMD_RAM_MOVE", 0x72},
};

// What CMD_MEMMAP points at.
constexpr NamedValue MAPS[] = {
    {"MAP_PALETTE_IN", 0},
    {"MAP_PALETTE_OUT", 1},
    {"MAP_PALETTE", 2},
    {"MAP_COLLIDE", 3},
    {"MAP_GROUPS", 4},
};

// Video modes. Graphics is the power-on mode.
constexpr NamedValue MODES[] = {
    {"MODE_GRAPHICS", 0},
    {"MODE_TEXT", 1},
    {"MODE_WORLD", 2},
};

// MODE_WORLD's record flag bits, so a scene is written with names.
constexpr NamedValue WORLD_FLAGS[] = {
    {"OBJ_FLAG_ACTIVE", 1},
    {"OBJ_FLAG_MATRIX", 2},
    {"CAM_FLAG_VIEW_MATRIX", 1},
    {"CAM_FLAG_PROJ_MATRIX", 2},
};

constexpr int SCREEN_W = 256;
constexpr int SCREEN_H = 256;
constexpr int CART_SIZE = 1 << 20;  // 1MB: 16 banks of 64KB, flat addressing

}  // namespace sc8::gpu
