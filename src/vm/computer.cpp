#include "vm/computer.h"

#include <algorithm>
#include <chrono>
#include <fstream>

#include "core/mcparse.h"
#include "vm/audio.h"
#include "vm/display.h"

namespace sc8 {

Computer::Computer()
    : microcode_(buildNaive()),
      storage_(&input_),
      apu_(&storage_),
      acp_(&apu_),
      gpu_([this] { return machine_ ? machine_->cycles : 0; }, &acp_, [this] { return drawSeed(); }) {
  apu_.setMasterGain(MASTER_GAIN);
  storage_.attach(&cart_.basic, [this] { slotsChanged(); });
  frame_.assign(static_cast<size_t>(SCREEN_W * SCREEN_H * 4), 0);
  newMachine();
}

// A SAVE or DELETE from inside the machine changed the cartridge. With a
// file behind it, the file follows at once, so a crash a second later
// loses nothing. The counter lets the IDE notice without a callback.
void Computer::slotsChanged() {
  slotChanges_++;
  writeRom();
}

bool Computer::writeRom() {
  if (romPath_.empty()) return false;
  std::vector<uint8_t> bytes = encodeCartridge(cart_);
  std::ofstream out(romPath_, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(out);
}

// Typing waits twenty frames, a third of a second at 60 fps, because BASIC
// drains the key queue while it boots and would eat an earlier line.
void Computer::typeText(std::string text) {
  typeBuffer_ = std::move(text);
  typePos_ = 0;
  typeAfterFrame_ = frameCounter() + 20;
}

// BASIC reads a line from the key queue, so typing is a press and a release
// per character, and the queue is kept half empty so nothing is dropped.
void Computer::pumpTyping() {
  if (frameCounter() < typeAfterFrame_) return;
  while (typePos_ < typeBuffer_.size() && input_.queued() < static_cast<size_t>(input::KEY_QUEUE_MAX / 2)) {
    char c = typeBuffer_[typePos_++];
    if (c == '\r') continue;
    if (c == '\n') c = 13;
    else if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    const auto code = static_cast<uint8_t>(static_cast<unsigned char>(c) & 0x7f);
    input_.pushKey(code, false);
    input_.pushKey(code, true);
  }
}

Computer::~Computer() = default;

// The worker's seed: the wall clock mixed with a run counter, so two power
// ons inside one millisecond still differ. A fixed seed wins when set.
uint32_t Computer::drawSeed() {
  if (fixedSeed_) return *fixedSeed_;
  using namespace std::chrono;
  const auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  seedRuns_++;
  return static_cast<uint32_t>(ms) ^ (seedRuns_ * 0x9e3779b1u);
}

void Computer::insert(Cartridge cart) {
  cart_ = std::move(cart);
  if (!cart_.microcode.empty()) selectMicrocode(cart_.microcode);
  gpu_.attachCart(cart_.data);
  apu_.attachCart(cart_.data);
  powerOn();
}

// Every new machine hands its RAM to the GPU and the ACP. The GPU reads
// it for text mode and the ACP writes it. The session did this after each
// new Machine. Forgetting it is the classic bug, so it lives here.
void Computer::newMachine() {
  machine_ = std::make_unique<Machine>(cart_.program, microcode_, &gpu_);
  machine_->setTrace(traceOn_);
  gpu_.attachRam(machine_->ram.data());
  acp_.attachRam(machine_->ram.data());
  storage_.attachRam(machine_->ram.data());
  std::copy(cart_.ram.begin(), cart_.ram.end(), machine_->ram.begin());
  lastDisplayKey_.clear();
}

// The session's restart: the screen and the chip power on fresh along
// with the .ram image. The ACP keeps its state, as it did there. A new
// machine stands in for reset plus image load. A fresh machine is the
// same thing with the stack and the counters cleared too.
void Computer::powerOn() {
  gpu_.powerOn();
  apu_.powerOn();
  storage_.powerOn();
  newMachine();
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
  // Swapping microcode replaces the machine: a restart, per the design.
  gpu_.powerOn();
  newMachine();
  return {};
}

void Computer::setTrace(bool on) {
  traceOn_ = on;
  machine_->setTrace(on);
}

void Computer::setFastFrame(bool on, std::optional<double> gapMs) {
  gpu_.fastFrame = on;
  if (gapMs) gpu_.fastFrameGapMs = *gapMs;
}

void Computer::setBreakpoints(const std::vector<uint16_t>& pcs) { breakpoints_ = std::set<uint16_t>(pcs.begin(), pcs.end()); }

uint64_t Computer::runBudget(uint64_t n) {
  Machine& m = *machine_;
  hitBreakpoint = false;
  const uint64_t start = m.instructions;
  if (breakpoints_.empty()) {
    m.run(n);
    return m.instructions - start;
  }
  for (uint64_t i = 0; i < n; i++) {
    if (!m.instructionStep()) break;
    if (breakpoints_.count(m.pc)) {
      hitBreakpoint = true;
      break;
    }
  }
  return m.instructions - start;
}

uint64_t Computer::runMicroBudget(uint64_t n) {
  Machine& m = *machine_;
  hitBreakpoint = false;
  uint64_t steps = 0;
  uint64_t prevInstr = m.instructions;
  for (uint64_t i = 0; i < n; i++) {
    if (!m.microStep()) break;
    steps++;
    if (m.instructions != prevInstr) {
      prevInstr = m.instructions;
      if (breakpoints_.count(m.pc)) {
        hitBreakpoint = true;
        break;
      }
    }
  }
  return steps;
}

uint64_t Computer::runToNextFrame(uint64_t maxInstr) {
  Machine& m = *machine_;
  hitBreakpoint = false;
  const uint64_t start = m.instructions;
  const int64_t f0 = gpu_.frame();
  for (uint64_t i = 0; i < maxInstr; i++) {
    if (!m.instructionStep()) break;
    if (breakpoints_.count(m.pc)) {
      hitBreakpoint = true;
      break;
    }
    if (gpu_.frame() != f0) break;
  }
  return m.instructions - start;
}

bool Computer::runFrame() {
  runToNextFrame();
  return machine_->status == Status::Running && !hitBreakpoint;
}

bool Computer::runInstructions(uint64_t n) {
  runBudget(n);
  return machine_->status == Status::Running && !hitBreakpoint;
}

void Computer::pumpAudio(Audio& audio) {
  constexpr size_t BLOCK = static_cast<size_t>(apu::AUDIO_RATE * AUDIO_MS / 1000);
  constexpr size_t LOW = static_cast<size_t>(apu::AUDIO_RATE * AUDIO_AHEAD_LOW_MS / 1000);
  constexpr size_t HIGH = static_cast<size_t>(apu::AUDIO_RATE * AUDIO_AHEAD_HIGH_MS / 1000);
  if (!apu_.audioActive()) return;
  if (audio.queued() >= LOW) return;
  audioBlock_.resize(BLOCK);
  // The worker pulled a block every AUDIO_MS on a timer. A host frame is
  // the timer here. It refills to the high mark in one go, then rests
  // until the buffer drains to the low mark.
  while (apu_.audioActive() && audio.queued() < HIGH) {
    apu_.render(audioBlock_);
    audio.push(audioBlock_);
  }
}

const std::vector<uint8_t>& Computer::frame() {
  std::string key = gpu_.displayKey();
  if (key != lastDisplayKey_) {
    gpu_.compose(frame_);
    lastDisplayKey_ = std::move(key);
  }
  return frame_;
}

}  // namespace sc8
