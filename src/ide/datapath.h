// The drawn CPU schema and its wiring knowledge. Seventeen boxes and the
// wires between them. Which components and wires a control signal touches.
// The short card for each box. The port of dpschema.ts, datapath.ts and
// the titles of componentdocs.ts. Pure data, so a test could drive it
// headless.
#pragma once

#include <array>
#include <span>
#include <string_view>
#include <vector>

#include "core/signals.h"

namespace sc8::dp {

// One box of the schema, in a 496 x 252 drawing space. The browser's SVG
// was 410 x 240: D3 widened it, and its wires deepened the gap under the D
// row. `value` marks a box with a live value slot.
struct Box {
  std::string_view id;
  float x, y, w, h;
  std::string_view label;
  bool value;
};

// A wire is an orthogonal polyline routed through the gaps between boxes.
struct Wire {
  std::string_view id;
  std::vector<std::array<float, 2>> pts;
};

constexpr float VIEW_W = 496;
constexpr float VIEW_H = 252;

std::span<const Box> boxes();
std::span<const Wire> wires();

// The component ids, in the order the browser's COMPONENTS listed them.
std::span<const std::string_view> components();

// Which components and which wires one signal lights. Approximate on
// purpose: a block diagram shows flow, the row text carries the truth.
std::vector<std::string_view> componentsFor(Signal s);
std::vector<std::string_view> wiresFor(Signal s);

// The short card, what hovering a box shows.
struct Card {
  std::string_view id;
  std::string_view title;  // "EA · effective address adder"
  std::string_view kind;   // "register · remembers across cycles"
  std::string_view what;   // the first paragraph of the full card
};

const Card* cardFor(std::string_view id);

// Eight signal colors by row position, SIG_COLORS in the browser, as
// 0xRRGGBB. Signal i in a row gets color i: its chip, its boxes and its
// wires match.
constexpr unsigned SIG_COLORS[8] = {0x58a6ff, 0xf778ba, 0xe3b341, 0x56d4dd,
                                    0xffa657, 0xd2a8ff, 0x58d364, 0xf85149};

}  // namespace sc8::dp
