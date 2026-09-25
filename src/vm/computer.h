// The virtual computer: a machine, the devices on its bus, and a cartridge
// in the slot. Owns no window. simplecpu and the IDE both drive one of
// these and draw what it composes. The port of the headless Session in the
// browser project, minus the wire shapes a worker needed.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/cartridge.h"
#include "core/machine.h"
#include "core/microcode.h"
#include "devices/acp.h"
#include "devices/apu.h"
#include "devices/gpu.h"
#include "devices/input.h"
#include "devices/storage.h"

namespace sc8 {

class Audio;

// One GPU frame is exactly 65536 cycles at every speed setting. The frame
// counter is the cycle counter shifted right 16.
constexpr uint64_t CYCLES_PER_FRAME = 65536;

// Headroom so a dense mix of voices does not clip the 8 bit sum. The
// browser session set the same number, so a ROM sounds the same here.
constexpr double MASTER_GAIN = 0.4;

// The audio pump renders blocks this long, as the worker did. It keeps the
// output buffer between the low and the high mark.
constexpr int AUDIO_MS = 50;
constexpr int AUDIO_AHEAD_LOW_MS = 100;
constexpr int AUDIO_AHEAD_HIGH_MS = 200;

// A frame past this many instructions is stuck: the frame runner gives up.
constexpr uint64_t FRAME_CEIL = 2000000;

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

  // Power on fresh: a new machine with the program and the RAM image, the
  // GPU and the APU powered on. The session called this restart.
  void powerOn();
  // Reset the CPU. RAM, stack and the devices keep their state.
  void reset();

  // Pick the microcode: "@naive", "@optimal", or custom text. Returns the
  // parse errors for a custom set, or an empty list. Swapping microcode
  // replaces the machine, a restart, as the session did.
  std::vector<std::string> selectMicrocode(const std::string& name);
  const std::string& microcodeName() const { return microcodeName_; }
  // The sealed set can run, never be read. The IDE asks this before it
  // shows a row.
  bool microcodeInspectable() const { return microcodeName_ != "@optimal"; }

  // The hardware stack's size in bytes, for the next power on. simplecpu
  // --stack-size sets it. The default is STACK_SIZE.
  void setStackSize(int bytes) { stackSize_ = bytes; }
  int stackSize() const { return stackSize_; }

  Machine& machine() { return *machine_; }
  const Machine& machine() const { return *machine_; }

  // The devices, for the IDE's inspectors and the host's keyboard.
  Gpu& gpu() { return gpu_; }
  const Gpu& gpu() const { return gpu_; }
  InputBus& input() { return input_; }
  Apu& apu() { return apu_; }
  const Apu& apu() const { return apu_; }
  Acp& acp() { return acp_; }
  Storage& storage() { return storage_; }

  // Where the cartridge came from. With a path set, a SAVE or DELETE from
  // the running machine writes the ROM file back, so the change survives.
  // Empty means the cartridge lives only in memory.
  void setRomPath(std::string path) { romPath_ = std::move(path); }
  const std::string& romPath() const { return romPath_; }
  // How many times the running machine changed the cartridge's slots.
  uint64_t slotChanges() const { return slotChanges_; }
  // Write the cartridge to romPath(). False when there is no path or the
  // write failed.
  bool writeRom();

  // Type text into the running machine through the key queue, paced so the
  // queue never overflows. A newline sends Enter. Called each host frame.
  void typeText(std::string text);
  void pumpTyping();
  bool typing() const { return typePos_ < typeBuffer_.size(); }

  // The GPU's random seed. Unset, every power on draws a wall clock seed.
  // A program reading GPU_RAND then plays a different game each run. A fixed
  // seed gives the same stream every time, for tests and the differential
  // runs. Takes effect at the next power on.
  void setSeed(std::optional<uint32_t> seed) { fixedSeed_ = seed; }

  // Whether the machine records what it did. Held here as well as on the
  // machine, because a power on builds a new machine. The switch belongs
  // to the person watching.
  void setTrace(bool on);
  bool tracing() const { return traceOn_; }

  // The fast-frame debug switch, see Gpu::fastFrame. It lives on the GPU,
  // which survives power on: it belongs to the person at the controls.
  void setFastFrame(bool on, std::optional<double> gapMs = std::nullopt);

  // Breakpoints by PC. Every run call stops at the instruction boundary
  // where PC lands on one and sets hitBreakpoint.
  void setBreakpoints(const std::vector<uint16_t>& pcs);
  bool hitBreakpoint = false;

  // Run up to n instructions. Returns the number executed, so the caller
  // can pace batches per host frame.
  uint64_t runBudget(uint64_t n);
  // Run up to n microcode rows, for watching the flow. Returns the number
  // executed. A breakpoint stops it at the instruction boundary.
  uint64_t runMicroBudget(uint64_t n);
  // Run flat out until the GPU's frame counter ticks, then stop. Returns
  // the instructions executed. Bounded so a stuck program cannot spin.
  uint64_t runToNextFrame(uint64_t maxInstr = FRAME_CEIL);

  // Run to the next GPU frame. Returns false once the machine is no
  // longer running. What the frame-locked loop and the IDE call.
  bool runFrame();
  bool runInstructions(uint64_t n);

  // Render APU blocks into the output while the chip has sound, keeping
  // the buffer between AUDIO_AHEAD_LOW_MS and AUDIO_AHEAD_HIGH_MS ahead.
  // Called once per host frame. Silence costs one check.
  void pumpAudio(Audio& audio);

  // The composed screen, 256 x 256 RGBA8, through the effective palette.
  // Composed again only when the GPU's display key moved, so a still
  // screen costs nothing per host frame.
  const std::vector<uint8_t>& frame();

  uint64_t frameCounter() const { return machine_->cycles >> 16; }

 private:
  void newMachine();
  uint32_t drawSeed();
  void slotsChanged();

  Cartridge cart_;
  std::string microcodeName_ = "@naive";
  int stackSize_ = STACK_SIZE;
  Microcode microcode_;
  std::unique_ptr<Machine> machine_;
  // The chain: GPU, then the ACP, then the APU, then the input device.
  // Each forwards the ports it does not claim. Declared in construction
  // order, since each one holds the pointer of the next.
  InputBus input_;
  Storage storage_;
  Apu apu_;
  Acp acp_;
  Gpu gpu_;
  std::string romPath_;
  uint64_t slotChanges_ = 0;
  std::string typeBuffer_;
  size_t typePos_ = 0;
  uint64_t typeAfterFrame_ = 0;
  std::optional<uint32_t> fixedSeed_;
  uint32_t seedRuns_ = 0;
  bool traceOn_ = true;
  std::set<uint16_t> breakpoints_;
  std::vector<uint8_t> frame_;
  std::string lastDisplayKey_;
  std::vector<uint8_t> audioBlock_;
};

}  // namespace sc8
