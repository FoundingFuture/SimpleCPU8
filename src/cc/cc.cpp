#include "cc/cc.h"

namespace sc8 {

CcResult compile(const std::vector<CcInput>& inputs) {
  CcResult r;
  r.errors.push_back("the C compiler is not ported yet (" + std::to_string(inputs.size()) +
                     " input file(s) ignored)");
  return r;
}

}  // namespace sc8
