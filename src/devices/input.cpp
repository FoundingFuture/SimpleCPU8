#include "devices/input.h"

namespace sc8 {

void InputBus::pushKey(uint8_t code, bool release) {
  keys_.push_back(static_cast<uint8_t>((code & 0x7f) | (release ? 0x80 : 0)));
  if (keys_.size() > static_cast<size_t>(input::KEY_QUEUE_MAX)) keys_.pop_front();
}

uint8_t InputBus::read(uint8_t port) {
  if (port == 0x20) return buttons;
  if (port == 0x21) {
    if (keys_.empty()) return 0;
    const uint8_t ev = keys_.front();
    keys_.pop_front();
    return ev;
  }
  if (port == 0x22) return mods;
  return fallback_->read(port);
}

void InputBus::write(uint8_t port, uint8_t value) { fallback_->write(port, value); }

}  // namespace sc8
