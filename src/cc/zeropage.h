// Who gets the zero page.
//
// The zero page is not an optimisation on this machine. There is no
// LD A <- [addr16], and every ALU operand is an immediate or a zero page
// address, so a byte anywhere else costs a pointer register to reach and a
// temp to compute with. It is the register file, and it holds 256 bytes.
//
// The order is the design's:
//   1. the compiler's own reservations, which the caller has already placed
//   2. everything marked __zp, in declaration order. If THIS does not fit,
//      that is the error, and it names what took the room
//   3. whatever is left, by weighted count
#pragma once

#include <map>
#include <string>
#include <vector>

#include "cc/ast.h"

namespace sc8::cc {

struct Placed {
  VarDecl decl;
  double score;
  std::string why;
};

// Real accesses per variable, read back off the machine after a run.
using Profile = std::map<std::string, double>;

// Count every mention of every name, weighted by the loops around it.
std::map<std::string, double> scoreNames(const Unit& unit);

// The order globals are laid down in, which is what decides the zero page.
// `reserved` is how many bytes the compiler already took. When there is a
// measurement it wins, and the map says which answer each variable got.
std::vector<Placed> allocate(const Unit& unit, int reserved, const Profile* measured = nullptr);

}  // namespace sc8::cc
