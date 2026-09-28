// The chain idiom every peripheral uses: a device claims its port range and
// forwards everything else to the next device down. The last link is a
// LogIoBus, so an unclaimed port is logged rather than lost.
#pragma once

#include <cstdint>
#include <span>

#include "core/machine.h"

namespace sc8 {

class ChainedDevice : public IoBus {
 public:
  explicit ChainedDevice(IoBus* fallback = nullptr) : fallback_(fallback ? fallback : &end_) {}

  void setFallback(IoBus* fallback) { fallback_ = fallback ? fallback : &end_; }
  IoBus& fallback() { return *fallback_; }
  // A fault raised further down the chain comes up through every link.
  std::optional<DeviceFault> takeFault() override { return fallback_->takeFault(); }

 protected:
  IoBus* fallback_;

 private:
  LogIoBus end_;
};

// The cartridge's data section, read by the GPU and the APU. A device keeps
// the span it was given, so the owner keeps the bytes alive.
using CartView = std::span<const uint8_t>;

}  // namespace sc8
