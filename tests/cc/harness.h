// The rig the design asks for: compile a C snippet, assemble the result, run
// it on the machine and read RAM back. Behaviour first, shape second.
//
// The browser harness puts the real GPU in front of the ACP. The GPU is not
// in this tree yet, so a stub stands in its place: it answers the two memory
// commands the compiler's own output leans on, CMD_RAM_MOVE for every double
// and CMD_COPY for rom_copy, and it forwards every other port to the ACP.
// Nothing draws. The tests that need drawing or text are skipped and say so.
#pragma once

#include <doctest.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "asm/asm.h"
#include "cc/cc.h"
#include "cc/lex.h"
#include "core/machine.h"
#include "devices/acp.h"
#include "devices/device.h"

namespace sc8::cctest {

using cc::CcError;
using cc::CcOptions;
using cc::SourceFile;

// The stand-in for the GPU. See the file comment.
class StubGpu : public ChainedDevice {
 public:
  explicit StubGpu(IoBus* fallback) : ChainedDevice(fallback) {}
  void attachRam(uint8_t* ram) { ram_ = ram; }
  void setCart(std::vector<uint8_t> cart) { cart_ = std::move(cart); }
  void write(uint8_t port, uint8_t value) override;
  uint8_t read(uint8_t port) override;
  // Every command byte written, in order, for a test that wants the trace.
  std::vector<uint8_t> commands;

 private:
  uint8_t* ram_ = nullptr;
  std::vector<uint8_t> cart_;
  uint8_t data_[7]{};
  uint32_t rand_ = 0x2545F491;
};

struct Ran {
  std::string asmText;
  Assembled assembled;
  std::unique_ptr<Acp> acp;
  std::unique_ptr<StubGpu> gpu;
  std::unique_ptr<Machine> m;

  int addr(const std::string& name) const;
  int u8(const std::string& name) const;
  int u16(const std::string& name) const;
  int i16(const std::string& name) const;
  double f64(const std::string& name) const;
  bool halted() const { return m->status == Status::Halted; }
};

std::string compile(const std::string& src, const CcOptions& opts = {});

// Several files, which is what the design wants people to learn.
std::string compileFiles(const std::vector<SourceFile>& files, const CcOptions& opts = {});

Ran runFiles(const std::vector<SourceFile>& files, uint64_t budget = 400000);

Ran run(const std::string& src, uint64_t budget = 200000, bool optimal = false);

Ran runAsm(const std::string& asmText, uint64_t budget, bool optimal);

// The cartridge has to be loaded into the GPU, the way the session does it
// after an assembly.
Ran runWithCart(const std::string& src, uint64_t budget = 400000);

// Runs and insists the program reached HLT rather than running out of budget.
Ran ran(const std::string& src, uint64_t budget = 200000);

// The same program under both microcode sets must agree. Cheap to check and
// it catches a codegen that leaned on a cycle count.
template <class Read>
auto agree(const std::string& src, Read read) {
  const Ran a = run(src, 400000, false);
  const Ran b = run(src, 400000, true);
  CHECK_MESSAGE(read(b) == read(a), "the two microcode sets disagree");
  return read(a);
}

std::string refuses(const std::string& src);

std::string numbered(const std::string& asmText);

bool has(const std::string& text, const std::string& needle);
int countOf(const std::string& text, const std::string& needle);

}  // namespace sc8::cctest
