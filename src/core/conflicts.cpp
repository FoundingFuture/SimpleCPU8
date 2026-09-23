#include "core/conflicts.h"

#include <array>

namespace sc8 {

std::optional<Conflict> checkRow(const Row& row) {
  using enum MemAccess;

  // Rule 1: one writer per register atom.
  std::array<std::optional<Signal>, 12> writer{};
  for (Signal s : row) {
    const SignalMeta& meta = signalMeta(s);
    for (int bit = 0; bit < 12; bit++) {
      if (!(meta.writes & (1u << bit))) continue;
      auto& prev = writer[static_cast<size_t>(bit)];
      if (prev) {
        static constexpr std::array<std::string_view, 12> ATOM = {
            "PCH", "PCL", "IR", "ACC", "A", "B", "D1H", "D1L", "D2H", "D2L", "SP", "FLAGS"};
        return Conflict{1, std::string(signalName(*prev)) + " and " + std::string(meta.name) +
                               " both write " + std::string(ATOM[static_cast<size_t>(bit)])};
      }
      prev = s;
    }
  }

  // Rules 2, 3, 4: one access per memory.
  int ram = 0, prog = 0, stack = 0;
  int aluSelects = 0, aluWrites = 0, steps = 0, ios = 0, bases = 0;
  std::optional<Signal> firstAluWrite, firstStep, secondStep;
  for (Signal s : row) {
    const SignalMeta& meta = signalMeta(s);
    if (meta.mem == RamRead || meta.mem == RamWrite) ram++;
    if (meta.mem == ProgRead) prog++;
    if (meta.mem == StackRead || meta.mem == StackWrite) stack++;
    if (meta.aluSelect != AluOp::AluNone) aluSelects++;
    if (meta.aluWrite) {
      aluWrites++;
      if (!firstAluWrite) firstAluWrite = s;
    }
    if (meta.step) {
      steps++;
      if (!firstStep) firstStep = s;
      else if (!secondStep) secondStep = s;
    }
    if (meta.io) ios++;
    if (meta.addrBase) bases++;
  }
  if (ram > 1) return Conflict{2, "more than one data RAM access"};
  if (prog > 1) return Conflict{3, "more than one program memory access"};
  if (stack > 1) return Conflict{4, "more than one stack RAM access"};

  // Rule 5: at most one ALU operation select.
  if (aluSelects > 1) return Conflict{5, "more than one ALU operation select"};

  // Rule 6: ALU writes need exactly one select.
  if (aluWrites > 0 && aluSelects != 1) {
    return Conflict{6, std::string(signalName(*firstAluWrite)) +
                           " needs exactly one ALU operation select"};
  }

  // Rule 7: one step through the shared unit, PC_INC included.
  if (steps > 1) {
    return Conflict{7, std::string(signalName(*firstStep)) + " and " +
                           std::string(signalName(*secondStep)) + " both need the step unit"};
  }

  // Rule 8: one IO signal, and a RAM access needs exactly one address base.
  if (ios > 1) return Conflict{8, "more than one IO signal"};
  if (bases > 1) return Conflict{8, "more than one address base select"};
  if (ram == 1 && bases != 1) {
    return Conflict{8, "a data RAM access needs exactly one address base select"};
  }

  return std::nullopt;
}

}  // namespace sc8
