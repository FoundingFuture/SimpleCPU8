// The software runtime: multiply, divide, remainder and shift on the CPU
// alone, with no coprocessor.
//
// DESIGN: this exists to be READ. mul8 and div8 are demos that teach the
// carry chain, and a compiler that always hid the work behind one ACP
// command would waste them. -msoft-mul compiles the same `a * b` the other
// way, and the layers pane then shows a few hundred instructions where it
// showed one port write.
//
// The same entry points as the coprocessor runtime, so nothing above knows
// which one it got: operands in __ra and __rb, answer in __ra.
#pragma once

#include <set>
#include <string>
#include <vector>

namespace sc8::cc {

// The extra zero page words this runtime needs. Reserved only when it is in
// the program, because the zero page is 256 bytes and it is the register
// file.
constexpr const char* ZP_RP = "__rp";  // the running product, or the remainder
constexpr const char* ZP_RQ = "__rq";  // the quotient being built
constexpr const char* ZP_RC = "__rc";  // a bit counter
constexpr int SOFT_ZP_BYTES = 2 + 2 + 1;

std::vector<std::string> emitSoftRuntime(const std::set<std::string>& used);

}  // namespace sc8::cc
