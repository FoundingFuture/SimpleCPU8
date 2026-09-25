// simplecpu-run: run a ROM or a source file headless and print what the
// machine ended up as. No display and no devices beyond a logging bus, so
// port writes are listed rather than drawn. The batch tool for tests and
// scripts.
//
//   simplecpu-run game.rom
//   simplecpu-run main.asm --max 1000000 --microcode optimal
//   simplecpu-run main.asm --ram 0 16        dumps 16 bytes of RAM at 0

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "asm/asm.h"
#include "core/cartridge.h"
#include "core/machine.h"
#include "core/mcparse.h"
#include "core/microcode.h"

namespace fs = std::filesystem;
using namespace sc8;

namespace {

int usage() {
  std::fprintf(stderr,
               "usage: simplecpu-run <file.rom | file.asm> [--max N] [--microcode naive|optimal]\n"
               "                     [--ram ADDR COUNT] [--trace] [--stack-size BYTES]\n");
  return 2;
}

std::optional<Cartridge> load(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot read %s\n", p.string().c_str());
    return std::nullopt;
  }
  if (p.extension() == ".rom") {
    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(in), {});
    CartridgeResult r = decodeCartridge(bytes);
    if (!r.cartridge) std::fprintf(stderr, "%s: %s\n", p.string().c_str(), r.error.c_str());
    return r.cartridge;
  }
  const std::string text(std::istreambuf_iterator<char>(in), {});
  Assets assets;
  const fs::path dir = p.parent_path();
  assets.loadFile = [&](std::string_view name) -> std::optional<std::vector<uint8_t>> {
    std::ifstream f(dir / fs::path(name), std::ios::binary);
    if (!f) return std::nullopt;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
  };
  Assembled a = assemble(text, &assets);
  for (const AsmError& e : a.errors) {
    std::fprintf(stderr, "%s:%d: %s\n", p.string().c_str(), e.line, e.message.c_str());
  }
  if (!a.errors.empty()) return std::nullopt;
  return a.cartridge();
}

}  // namespace

int main(int argc, char** argv) {
  fs::path file;
  uint64_t maxInstr = 10'000'000;
  std::string microcodeOverride;
  bool trace = false;
  int ramAt = -1, ramCount = 0;
  int stackSize = STACK_SIZE;

  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--max" && i + 1 < argc) maxInstr = std::stoull(argv[++i]);
    else if (a == "--microcode" && i + 1 < argc) microcodeOverride = argv[++i];
    else if (a == "--trace") trace = true;
    else if (a == "--stack-size" && i + 1 < argc) {
      const auto bytes = parseStackSize(argv[++i]);
      if (!bytes) {
        std::fprintf(stderr, "simplecpu-run: --stack-size takes %d to %d bytes, as 2048, 2K or 0x800\n",
                     MIN_STACK_SIZE, MAX_STACK_SIZE);
        return 2;
      }
      stackSize = *bytes;
    }
    else if (a == "--ram" && i + 2 < argc) {
      ramAt = std::stoi(argv[++i], nullptr, 0);
      ramCount = std::stoi(argv[++i], nullptr, 0);
    } else if (a == "-h" || a == "--help" || (!a.empty() && a[0] == '-')) return usage();
    else if (file.empty()) file = a;
    else return usage();
  }
  if (file.empty()) return usage();

  std::optional<Cartridge> cart = load(file);
  if (!cart) return 1;

  std::string mcName = microcodeOverride.empty() ? cart->microcode : microcodeOverride;
  if (mcName.empty()) mcName = "@naive";
  if (mcName == "naive" || mcName == "optimal") mcName = "@" + mcName;
  Microcode mc;
  if (mcName == "@naive") mc = buildNaive();
  else if (mcName == "@optimal") mc = buildOptimal();
  else {
    McParsed parsed = parseMicrocode(mcName);
    for (const McError& e : parsed.errors) std::fprintf(stderr, "microcode:%d: %s\n", e.line, e.message.c_str());
    if (!parsed.errors.empty()) return 1;
    mc = std::move(parsed.microcode);
  }

  LogIoBus io;
  Machine m(cart->program, std::move(mc), &io);
  m.setTrace(trace);
  m.setStackSize(stackSize);
  std::copy(cart->ram.begin(), cart->ram.end(), m.ram.begin());
  m.run(maxInstr);

  std::printf("status: %s", std::string(statusName(m.status)).c_str());
  if (m.crash) {
    std::printf(" (%s: %s at pc %u)", std::string(crashKindName(m.crash->kind)).c_str(),
                m.crash->message.c_str(), m.crash->lastInstrPc);
  }
  std::printf("\ninstructions: %llu  cycles: %llu\n", static_cast<unsigned long long>(m.instructions),
              static_cast<unsigned long long>(m.cycles));
  std::printf("A=%02x D1=%04x D2=%04x D3=%04x SP=%04x PC=%04x flags=%c%c%c%c\n", m.acc, m.d1, m.d2, m.d3, m.sp, m.pc,
              m.flags.n ? 'N' : '-', m.flags.v ? 'V' : '-', m.flags.z ? 'Z' : '-', m.flags.c ? 'C' : '-');
  if (!io.log.empty()) {
    std::printf("port writes: %zu", io.log.size());
    for (size_t i = 0; i < io.log.size() && i < 32; i++) {
      std::printf("%s $%02x<-%02x", i ? "," : "", io.log[i].port, io.log[i].value);
    }
    std::printf("%s\n", io.log.size() > 32 ? " ..." : "");
  }
  if (ramAt >= 0) {
    for (int i = 0; i < ramCount; i++) {
      const int addr = ramAt + i;
      if (addr >= RAM_SIZE) break;
      if (i % 16 == 0) std::printf("%s%04x:", i ? "\n" : "", addr);
      std::printf(" %02x", m.ram[static_cast<size_t>(addr)]);
    }
    std::printf("\n");
  }
  return m.status == Status::Crashed ? 3 : 0;
}
