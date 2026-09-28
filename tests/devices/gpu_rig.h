// Helpers the GPU test files share: a Gpu on a settable clock, and the
// argument shapes every command takes. Load the data ports, then run the
// command. A data port means whatever the running command says it means, so
// the tests say it the same way.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"
#include "devices/gpu.h"

namespace gpu_rig {

using Bytes = std::vector<uint8_t>;

// A Gpu whose clock the test moves. The Gpu holds a closure over this
// object, so a rig is built in place and never copied.
struct Clock {
  Clock(const Clock&) = delete;
  Clock& operator=(const Clock&) = delete;
  explicit Clock(uint64_t start = 0) : cycles(start), g([this] { return cycles; }) {}
  uint64_t cycles;
  sc8::Gpu g;
  void setCycles(uint64_t c) { cycles = c; }
};

inline void cmd(sc8::Gpu& g, int c, std::initializer_list<int> args = {}) {
  int i = 0;
  for (int a : args) g.write(static_cast<uint8_t>(sc8::gpu::GPU_DATA0 + i++), static_cast<uint8_t>(a & 0xff));
  g.write(sc8::gpu::GPU_CMD, static_cast<uint8_t>(c));
}

inline void cmdv(sc8::Gpu& g, int c, const std::vector<int>& args) {
  int i = 0;
  for (int a : args) g.write(static_cast<uint8_t>(sc8::gpu::GPU_DATA0 + i++), static_cast<uint8_t>(a & 0xff));
  g.write(sc8::gpu::GPU_CMD, static_cast<uint8_t>(c));
}

// A coordinate pair as every command takes one: signed 16 bit, high byte
// first, because the CPU is big-endian everywhere.
inline std::vector<int> xy(int x, int y) { return {(x >> 8) & 0xff, x & 0xff, (y >> 8) & 0xff, y & 0xff}; }

// A cartridge address: bank, high, low.
inline std::vector<int> at(int a) { return {(a >> 16) & 0xff, (a >> 8) & 0xff, a & 0xff}; }

inline std::vector<int> cat(std::vector<int> a, const std::vector<int>& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

inline std::vector<int> cat(std::vector<int> a, int b) {
  a.push_back(b);
  return a;
}

inline void moveTo(sc8::Gpu& g, int x, int y) { cmdv(g, sc8::gpu::CMD_MOVE_TO, xy(x, y)); }

inline int px(const sc8::Gpu::Frame& f, int x, int y) { return f[static_cast<size_t>(y * sc8::gpu::SCREEN_W + x)]; }

inline int vram(const sc8::Gpu& g, int x, int y) { return g.vram[static_cast<size_t>(y * sc8::gpu::SCREEN_W + x)]; }

inline size_t litPixels(const sc8::Gpu::Frame& f) {
  size_t n = 0;
  for (uint8_t v : f) {
    if (v != 0) n++;
  }
  return n;
}

// A 65536 byte data RAM on the heap, sized like the machine's.
inline std::unique_ptr<Bytes> newRam() { return std::make_unique<Bytes>(static_cast<size_t>(sc8::RAM_SIZE), 0); }

// The overlay's row as text, trailing blanks trimmed.
inline std::string overlayText(const sc8::Gpu& g, int row) {
  std::string s;
  for (int col = 0; col < g.textCols(); col++) {
    const uint8_t code = g.overlayChar[static_cast<size_t>(row * g.textCols() + col)];
    s += code == 0 ? ' ' : static_cast<char>(code);
  }
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s;
}

}  // namespace gpu_rig
