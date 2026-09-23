// The C compiler driver. Takes C source files, produces assembly text for
// simplecpu-asm. The port is pending: today compile() reports that.
#pragma once

#include <string>
#include <vector>

namespace sc8 {

struct CcInput {
  std::string path;
  std::string text;
};

struct CcResult {
  std::string assembly;
  std::vector<std::string> errors;
};

CcResult compile(const std::vector<CcInput>& inputs);

}  // namespace sc8
