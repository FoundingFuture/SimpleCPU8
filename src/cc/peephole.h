// A peephole pass over the code generator's assembly, one function body
// at a time, before the text is put together.
//
// The code generator is an accumulator machine that parks every
// intermediate value in a zero page temp. That makes it simple and
// correct, and leaves patterns a person would never write: a byte stored
// to a temp and loaded straight back, a temp written and then written
// again before anything reads it, a jump to the very next line. The pass
// removes those, and nothing else. It never moves a line, and every
// removal is proven safe on straight-line code: at a label, a jump, a
// call or anything it does not recognise, it assumes the worst and keeps
// the line.
#pragma once

#include <string>
#include <vector>

namespace sc8::cc {

// The code generator writes this line where a statement starts. No temp
// is live there, which lets a store be proven dead across jumps. The pass
// removes the markers.
constexpr const char* STMT_MARK = ";@stmt";

// Written around inline assembly. Nothing is assumed across it.
constexpr const char* BARRIER_MARK = ";@barrier";

// The lines with the redundant ones removed. remap, when given, gets one
// entry per input line: the index in the output of that line, or of the
// first line after it that survived. The source map moves with it.
std::vector<std::string> peephole(const std::vector<std::string>& lines, std::vector<int>* remap = nullptr);

}  // namespace sc8::cc
