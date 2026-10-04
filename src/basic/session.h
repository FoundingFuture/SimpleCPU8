// A BASIC machine with no window. It boots a ROM with the IDE's device
// chain, the GPU in text mode and keys typed through the input device.
// The project build checks .bas files on it (check.cpp), and tests/basic
// and tests/project run BASIC on it.
#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "basic/basic_rom.h"
#include "core/cartridge.h"
#include "core/machine.h"
#include "core/microcode.h"
#include "devices/acp.h"
#include "devices/apu.h"
#include "devices/gpu.h"
#include "devices/input.h"
#include "devices/storage.h"

namespace sc8::basic {

inline const Microcode& optimal() {
  static const Microcode mc = buildOptimal();
  return mc;
}

// The Session's machine and device chain, on the BASIC cartridge. The Gpu
// holds a closure over the machine's cycle counter, so a Session is built in
// place and never moved.
struct Session {
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  Cartridge cart;
  InputBus input;
  // The slots the storage device works on, standing in for the cartridge's
  // basic list, and how many times it said they changed.
  BasicSlots slots;
  int changes = 0;
  Storage storage{&input};
  Apu apu{&storage};
  Acp acp{&apu};
  Gpu gpu;
  std::unique_ptr<Machine> m;

  Session() : gpu([this] { return m ? m->cycles : 0; }, &acp) {
    const CartridgeResult r = decodeCartridge(std::vector<uint8_t>(basicRom().begin(), basicRom().end()));
    if (!r.cartridge) throw std::runtime_error("basic.rom does not decode: " + r.error);
    cart = *r.cartridge;
  }

  // On a cartridge of the caller's, one a project build made. Its BAS
  // slots are what the storage device serves, so AUTORUN runs at boot.
  explicit Session(Cartridge c) : cart(std::move(c)), slots(cart.basic), gpu([this] { return m ? m->cycles : 0; }, &acp) {}

  // Session.load: power the devices on, hand them the cartridge, build the
  // machine and apply the .ram image.
  void load() {
    gpu.powerOn();
    gpu.attachCart(cart.data);
    apu.powerOn();
    apu.attachCart(cart.data);
    m = std::make_unique<Machine>(cart.program, optimal(), &gpu);
    // Nothing here reads a recording, and a run of several million
    // instructions per test is what the budgets ask for.
    m->setTrace(false);
    m->countAccesses = false;
    gpu.attachRam(m->ram.data());
    acp.attachRam(m->ram.data());
    storage.powerOn();
    storage.attachRam(m->ram.data());
    storage.attach(&slots, [this] { changes++; });
    storage.attachAssets(&cart.assets);
    m->ram.fill(0);
    std::copy(cart.ram.begin(), cart.ram.end(), m->ram.begin());
  }

  void runBudget(uint64_t n) { m->run(n); }
  void pushKey(int code, bool release) { input.pushKey(static_cast<uint8_t>(code), release); }
};

}  // namespace sc8::basic
