// The runtime routines the compiler calls for what the CPU cannot do.
//
// Multiply, divide, remainder and every shift go to the coprocessor. See the
// table in docs/design/c-compiler-design.md: int * int is about 300
// instructions of carry chain on the CPU and about thirty through the ACP.
//
// Every routine takes its operands in the zero page words __ra and __rb and
// leaves its answer in __ra. No frame, no arguments, so a routine is a leaf
// and costs one JSR.
#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace sc8::cc {

constexpr const char* ACP_BLOCK = "__acp";

constexpr const char* RUNTIME_NAMES[] = {
    "__mul16", "__udiv16", "__urem16", "__sdiv16", "__srem16",
    "__shl16", "__ushr16", "__sshr16",
};

using Emit = std::function<void(const std::string&)>;

// The block is 32 bytes when a multiply is in the program and 24 when the
// widest thing is a divide or a shift. Sized by what the program does.
int blockSize(const std::set<std::string>& used);

std::vector<std::string> emitRuntime(const std::set<std::string>& used);

}  // namespace sc8::cc
