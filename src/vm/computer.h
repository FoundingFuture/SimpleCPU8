// The virtual computer: a machine, the devices on its bus, and a cartridge
// in the slot. Owns no window. simplecpu and the IDE both drive one of
// these and draw what it composes.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/cartridge.h"
#include "core/machine.h"
#include "core/microcode.h"

namespace sc8 {

// One GPU frame is exactly 65536 cycles at every speed setting: the frame
// counter is the cycle counter shifted right 16.
constexpr uint64_t CYCLES_PER_FRAME = 65536;

class Computer {
 public:
  Computer();
  ~Computer();
  Computer(const Computer&) = delete;
  Computer& operator=(const Computer&) = delete;

  // Put a cartridge in the slot and power on. The microcode named by the
  // ROM is selected unless an override was set first.
  void insert(Cartridge cart);
  const Cartridge& cartridge() const { return cart_; }

  // Power on fresh: program, RAM image and devices from the cartridge.
  void powerOn();
  // Reset the CPU. RAM, stack and the devices keep their state.
  void reset();

  // Pick the microcode: "@naive", "@optimal", or custom text. Returns the
  // parse errors for a custom set, or an empty list.
  std::vector<std::string> selectMicrocode(const std::string& name);
  const std::string& microcodeName() const { return microcodeName_; }
  // The sealed set can run, never be read. The IDE asks this before it
  // shows a row.
  bool microcodeInspectable() const { return microcodeName_ != "@optimal"; }

  Machine& machine() { return *machine_; }
  const Machine& machine() const { return *machine_; }

  // Run to the end of the current GPU frame. Returns false once the
  // machine is no longer running.
  bool runFrame();
  bool runInstructions(uint64_t n);

  // The composed screen, 256 x 256 RGBA8. Until the GPU is ported this is
  // a test card that moves with the cycle counter.
  const std::vector<uint8_t>& frame();

  uint64_t frameCounter() const { return machine_->cycles >> 16; }

 private:
  Cartridge cart_;
  std::string microcodeName_ = "@naive";
  Microcode microcode_;
  std::unique_ptr<Machine> machine_;
  LogIoBus bus_;
  std::vector<uint8_t> frame_;
};

}  // namespace sc8
