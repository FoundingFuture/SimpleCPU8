// The built-in manual's data. One page per mnemonic, generated from the
// ISA table and the shipped microcode. The device references, generated
// from the port tables in src/devices. Docs derive from the machine, so a
// cycle count or a port number can never drift from it. The port of the
// instruction pages and the reference tables of manual.ts. The section
// prose of the browser's manual is not ported.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/microcode.h"

namespace sc8::manual {

struct InstrShape {
  std::string name;  // canonical syntax, "LD A <- [D1]+"
  uint8_t op;
  int naive;    // cycles including fetch
  int optimal;  // the same under the sealed set
};

// Per-flag facts, so "is the carry affected" is one glance, never a
// paragraph. Every flag gets an explicit line, including "not affected".
struct FlagFacts {
  std::string list;
  std::string z, n, c, v;
};

struct InstrPage {
  std::string mnemonic;
  std::string title;  // category, "LOAD & STORE"
  std::string usage;  // signature, "LD src -> dst"
  std::string params;
  FlagFacts flags;
  std::string registers;
  std::string description;
  std::vector<InstrShape> shapes;
};

// Every page, in opcode order of first appearance. The sugar pages DEC,
// PUSH, POP and IN, which own no opcode, come last.
const std::vector<InstrPage>& instructionPages();

// Cycles for one canonical shape under any set: fetch rows plus the
// instruction's rows. -1 when the set has no rows for it.
int cyclesUnder(const Microcode& mc, const std::string& shape);

// One line of a reference table: a name, its value and its meaning.
struct RefRow {
  std::string name;
  int value;
  std::string meaning;
};

// The GPU command table carries the argument names and what the command
// leaves behind, beside its meaning.
struct GpuCmdRow {
  std::string name;
  int value;
  std::string args;    // the alias names in port order, comma separated
  std::string leaves;  // the result port, or empty
  std::string meaning;
};

struct AcpCmdRow {
  std::string name;
  int value;
  std::string a, b, r;  // shapes, "r*c" or "-"
  std::string types;
  std::string raises;
  std::string what;
};

// The port and command tables, built once from the device headers.
const std::vector<RefRow>& gpuPorts();
const std::vector<RefRow>& gpuAliases();  // sorted by port, sharing visible
const std::vector<RefRow>& gpuMods();
const std::vector<GpuCmdRow>& gpuCommands();
const std::vector<RefRow>& apuPorts();
const std::vector<RefRow>& apuCommands();
const std::vector<RefRow>& acpPorts();
const std::vector<RefRow>& acpFormats();
const std::vector<AcpCmdRow>& acpCommands();
const std::vector<RefRow>& acpFlags();
const std::vector<RefRow>& inputPorts();
const std::vector<RefRow>& inputButtons();
const std::vector<RefRow>& inputMods();

}  // namespace sc8::manual
