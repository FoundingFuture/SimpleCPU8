#include "harness.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "core/microcode.h"
#include "devices/gpu_ports.h"

namespace sc8::cctest {

namespace {

const Microcode& naive() {
  static const Microcode mc = buildNaive();
  return mc;
}

const Microcode& optimal() {
  static const Microcode mc = buildOptimal();
  return mc;
}

int cmdNamed(std::string_view name) {
  for (const auto& nv : gpu::CMDS) if (nv.name == name) return nv.value;
  throw std::runtime_error("no GPU command " + std::string(name));
}

}  // namespace

void StubGpu::write(uint8_t port, uint8_t value) {
  if (port > gpu::PORT_HI) { fallback_->write(port, value); return; }
  if (port >= 0x02 && port <= 0x08) { data_[port - 2] = value; return; }
  if (port == 0x1e) { rand_ = value ? value : 1; return; }
  if (port != 0x00) return;
  commands.push_back(value);
  static const int RAM_MOVE = cmdNamed("CMD_RAM_MOVE");
  static const int COPY = cmdNamed("CMD_COPY");
  if (value == RAM_MOVE && ram_) {
    const uint32_t from = static_cast<uint32_t>((data_[1] << 8) | data_[2]);
    const uint32_t dest = static_cast<uint32_t>((data_[3] << 8) | data_[4]);
    const uint32_t len = static_cast<uint32_t>((data_[5] << 8) | data_[6]);
    // The direction is chosen the way memmove chooses it, and an address
    // wraps at 16 bits like every RAM access on this machine.
    std::vector<uint8_t> tmp(len);
    for (uint32_t i = 0; i < len; i++) tmp[i] = ram_[(from + i) & 0xffff];
    for (uint32_t i = 0; i < len; i++) ram_[(dest + i) & 0xffff] = tmp[i];
  } else if (value == COPY && ram_) {
    const uint32_t src = static_cast<uint32_t>((data_[0] << 16) | (data_[1] << 8) | data_[2]);
    const uint32_t dest = static_cast<uint32_t>((data_[3] << 8) | data_[4]);
    const uint32_t len = static_cast<uint32_t>((data_[5] << 8) | data_[6]);
    for (uint32_t i = 0; i < len; i++) {
      const uint32_t at = src + i;
      ram_[(dest + i) & 0xffff] = at < cart_.size() ? cart_[at] : 0;
    }
  }
  // The data ports clear after every command, whatever it was.
  std::memset(data_, 0, sizeof data_);
}

uint8_t StubGpu::read(uint8_t port) {
  if (port > gpu::PORT_HI) return fallback_->read(port);
  if (port >= 0x02 && port <= 0x08) return data_[port - 2];
  if (port == 0x1e) {
    rand_ ^= rand_ << 13;
    rand_ ^= rand_ >> 17;
    rand_ ^= rand_ << 5;
    return static_cast<uint8_t>(rand_);
  }
  return 0;
}

int Ran::addr(const std::string& name) const {
  auto hit = assembled.labels.find(name);
  if (hit == assembled.labels.end()) {
    std::string all;
    for (const auto& [k, v] : assembled.labels) all += (all.empty() ? "" : ", ") + k;
    throw std::runtime_error("no label " + name + " in " + all);
  }
  return hit->second.value;
}

int Ran::u8(const std::string& name) const { return m->ram[static_cast<size_t>(addr(name))]; }

int Ran::u16(const std::string& name) const {
  const size_t at = static_cast<size_t>(addr(name));
  return (m->ram[at] << 8) | m->ram[at + 1];
}

int Ran::i16(const std::string& name) const {
  const int v = u16(name);
  return v >= 0x8000 ? v - 0x10000 : v;
}

double Ran::f64(const std::string& name) const {
  const size_t at = static_cast<size_t>(addr(name));
  uint64_t bits = 0;
  for (size_t i = 0; i < 8; i++) bits = (bits << 8) | m->ram[at + i];
  double v;
  std::memcpy(&v, &bits, 8);
  return v;
}

std::string compile(const std::string& src, const CcOptions& opts) { return cc::compileSource(src, opts).text; }

std::string compileFiles(const std::vector<SourceFile>& files, const CcOptions& opts) {
  return cc::compileProgram(files, opts).text;
}

Ran runFiles(const std::vector<SourceFile>& files, uint64_t budget) {
  return runAsm(cc::compileProgram(files).text, budget, false);
}

Ran run(const std::string& src, uint64_t budget, bool optimal) { return runAsm(compile(src), budget, optimal); }

Ran runWithCart(const std::string& src, uint64_t budget) { return runAsm(compile(src), budget, false); }

Ran runAsm(const std::string& asmText, uint64_t budget, bool useOptimal) {
  Ran r;
  r.asmText = asmText;
  r.assembled = assemble(asmText);
  if (!r.assembled.errors.empty()) {
    std::string msg = "the compiler emitted assembly the assembler refused:\n";
    for (const AsmError& e : r.assembled.errors) {
      msg += "  line " + std::to_string(e.line) + ": " + e.message + "\n";
    }
    throw std::runtime_error(msg + "--- assembly ---\n" + numbered(asmText));
  }
  // The compiler's runtime is the coprocessor, so a machine with no ACP on
  // the bus compiles fine and computes zero. The GPU stub sits in front of
  // it the way the session wires them.
  r.acp = std::make_unique<Acp>();
  r.gpu = std::make_unique<StubGpu>(r.acp.get());
  r.m = std::make_unique<Machine>(r.assembled.program, useOptimal ? optimal() : naive(), r.gpu.get());
  r.gpu->setCart(r.assembled.cart);
  std::copy(r.assembled.ram.begin(), r.assembled.ram.begin() + static_cast<long>(r.assembled.ramLength),
            r.m->ram.begin());
  r.acp->attachRam(r.m->ram.data());
  r.gpu->attachRam(r.m->ram.data());
  r.m->run(budget);
  return r;
}

Ran ran(const std::string& src, uint64_t budget) {
  Ran r = run(src, budget);
  if (r.m->status != Status::Halted) {
    std::string msg = "expected the program to halt, it is " + std::string(statusName(r.m->status));
    if (r.m->crash) {
      msg += " (" + std::string(crashKindName(r.m->crash->kind)) + ": " + r.m->crash->message + ")";
    }
    throw std::runtime_error(msg + "\n--- assembly ---\n" + numbered(r.asmText));
  }
  return r;
}

std::string refuses(const std::string& src) {
  try {
    compile(src);
  } catch (const CcError& e) {
    return e.message();
  } catch (const std::exception& e) {
    return std::string("threw ") + e.what();
  }
  return "no error";
}

std::string numbered(const std::string& asmText) {
  std::string out;
  size_t start = 0;
  int n = 1;
  for (;;) {
    size_t nl = asmText.find('\n', start);
    const bool last = nl == std::string::npos;
    if (last) nl = asmText.size();
    char buf[16];
    std::snprintf(buf, sizeof buf, "%4d ", n++);
    out += buf + asmText.substr(start, nl - start) + "\n";
    if (last) break;
    start = nl + 1;
  }
  return out;
}

bool has(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

int countOf(const std::string& text, const std::string& needle) {
  int n = 0;
  size_t at = 0;
  while ((at = text.find(needle, at)) != std::string::npos) {
    n++;
    at += needle.size();
  }
  return n;
}

}  // namespace sc8::cctest
