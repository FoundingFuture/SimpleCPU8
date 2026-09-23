#include "vm/computer.h"

#include <algorithm>

#include "core/mcparse.h"
#include "vm/display.h"

namespace sc8 {

Computer::Computer() : microcode_(buildNaive()), machine_(std::make_unique<Machine>(std::vector<Instr>{}, microcode_, &bus_)) {
  frame_.assign(static_cast<size_t>(SCREEN_W * SCREEN_H * 4), 0);
}

Computer::~Computer() = default;

void Computer::insert(Cartridge cart) {
  cart_ = std::move(cart);
  if (!cart_.microcode.empty()) selectMicrocode(cart_.microcode);
  powerOn();
}

void Computer::powerOn() {
  machine_ = std::make_unique<Machine>(cart_.program, microcode_, &bus_);
  std::copy(cart_.ram.begin(), cart_.ram.end(), machine_->ram.begin());
  bus_.log.clear();
}

void Computer::reset() { machine_->reset(); }

std::vector<std::string> Computer::selectMicrocode(const std::string& name) {
  std::string n = name;
  if (n == "naive" || n == "optimal") n = "@" + n;
  if (n == "@naive") {
    microcode_ = buildNaive();
  } else if (n == "@optimal") {
    microcode_ = buildOptimal();
  } else {
    McParsed p = parseMicrocode(n);
    if (!p.errors.empty()) {
      std::vector<std::string> out;
      for (const McError& e : p.errors) out.push_back("line " + std::to_string(e.line) + ": " + e.message);
      return out;
    }
    microcode_ = std::move(p.microcode);
  }
  microcodeName_ = n;
  machine_->setMicrocode(microcode_);
  return {};
}

bool Computer::runFrame() {
  const uint64_t target = (machine_->cycles / CYCLES_PER_FRAME + 1) * CYCLES_PER_FRAME;
  while (machine_->status == Status::Running && machine_->cycles < target) {
    machine_->instructionStep();
  }
  return machine_->status == Status::Running;
}

bool Computer::runInstructions(uint64_t n) {
  machine_->run(n);
  return machine_->status == Status::Running;
}

// A 3-3-2 palette test card with a bar that walks with the frame counter,
// so the pipeline from machine to shader can be seen working before the
// GPU arrives.
const std::vector<uint8_t>& Computer::frame() {
  const uint64_t f = frameCounter();
  for (int y = 0; y < SCREEN_H; y++) {
    for (int x = 0; x < SCREEN_W; x++) {
      const size_t i = static_cast<size_t>((y * SCREEN_W + x) * 4);
      const uint8_t index = static_cast<uint8_t>(((y / 16) * 16 + (x / 16)) & 0xff);
      uint8_t r = static_cast<uint8_t>((index >> 5) * 255 / 7);
      uint8_t g = static_cast<uint8_t>(((index >> 2) & 7) * 255 / 7);
      uint8_t b = static_cast<uint8_t>((index & 3) * 255 / 3);
      const bool bar = static_cast<uint64_t>(x) == (f % SCREEN_W);
      const bool halted = machine_->status != Status::Running;
      if (bar) r = g = b = 255;
      if (halted) {
        r = static_cast<uint8_t>(r / 3);
        g = static_cast<uint8_t>(g / 3);
        b = static_cast<uint8_t>(b / 3);
      }
      frame_[i] = r;
      frame_[i + 1] = g;
      frame_[i + 2] = b;
      frame_[i + 3] = 255;
    }
  }
  return frame_;
}

}  // namespace sc8
