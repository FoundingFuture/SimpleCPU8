// The chain idiom every peripheral uses: a device claims its port range and
// forwards everything else to the next device down. The last link is a
// LogIoBus, so an unclaimed port is logged rather than lost.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include "core/machine.h"

namespace sc8 {

class ChainedDevice : public IoBus {
 public:
  explicit ChainedDevice(IoBus* fallback = nullptr) : fallback_(fallback ? fallback : &end_) {}

  void setFallback(IoBus* fallback) {
    fallback_ = fallback ? fallback : &end_;
    if (sink_) fallback_->attachFaultSink(sink_);
  }
  IoBus& fallback() { return *fallback_; }

  void attachFaultSink(FaultSink* sink) override {
    sink_ = sink;
    fallback_->attachFaultSink(sink);
  }
  void detachFaultSink(FaultSink* sink) override {
    if (sink_ == sink) sink_ = nullptr;
    fallback_->detachFaultSink(sink);
  }

 protected:
  IoBus* fallback_;
  // Report a refused command to the machine. With no machine attached the
  // fault goes nowhere, and the device carries on as if it were ignored.
  void raiseFault(CrashKind kind, std::string message) {
    if (sink_) sink_->deviceFault(DeviceFault{kind, std::move(message)});
  }

 private:
  LogIoBus end_;
  FaultSink* sink_ = nullptr;
};

// The cartridge's data section, read by the GPU and the APU. A device keeps
// the span it was given, so the owner keeps the bytes alive.
using CartView = std::span<const uint8_t>;

}  // namespace sc8
