// The input device: a controller byte, a key event queue and the modifier
// levels, on ports $20 to $22. Buttons and the key buffer are hardware
// state, so they survive load and restart. See input_ports.h for the map.
#pragma once

#include <cstdint>
#include <deque>

#include "devices/device.h"
#include "devices/input_ports.h"

namespace sc8 {

class InputBus : public ChainedDevice {
 public:
  using ChainedDevice::ChainedDevice;

  // Live button state, one bit per button, set by the host from the keyboard.
  uint8_t buttons = 0;
  // Live modifier state, one bit per modifier, set the same way.
  uint8_t mods = 0;

  // Queue one key event. code is 0 to 127. A press and a release are two
  // separate events. The oldest event is dropped once the queue is full.
  void pushKey(uint8_t code, bool release);
  void clearKeys() { keys_.clear(); }
  size_t queued() const { return keys_.size(); }

  uint8_t read(uint8_t port) override;
  void write(uint8_t port, uint8_t value) override;

 private:
  std::deque<uint8_t> keys_;
};

}  // namespace sc8
