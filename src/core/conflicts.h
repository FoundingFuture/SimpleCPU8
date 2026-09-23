// The eight row rules from spec v2, section "Conflict rules". A row failing
// any rule is an illegal micro-instruction: flagged statically, crash when
// executed.
#pragma once

#include <optional>
#include <string>

#include "core/signals.h"

namespace sc8 {

struct Conflict {
  int rule;  // 0 means an unknown signal
  std::string message;
};

std::optional<Conflict> checkRow(const Row& row);

}  // namespace sc8
