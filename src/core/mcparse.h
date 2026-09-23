// Microcode text format: one section per microprogram, named by the
// canonical instruction syntax, rows of comma separated signals, # comments.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/microcode.h"

namespace sc8 {

constexpr int ROW_CAP = 16;

struct McError {
  int line;  // 0 for an error about the whole file
  std::string message;
};

struct McParsed {
  Microcode microcode;
  std::vector<McError> errors;
};

McParsed parseMicrocode(std::string_view text);
std::string serializeMicrocode(const Microcode& m);

}  // namespace sc8
