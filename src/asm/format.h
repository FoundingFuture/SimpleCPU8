// Assembly source laid out the way an assembler listing is.
//
//   label:  LD     A <- [D1]+          ; a comment
//   averylonglabel:
//           JSR    somewhere
//
// Every mnemonic, directive and data keyword starts in one column. The
// operands start two places after the longest instruction mnemonic, so
// they line up whatever the mnemonic. A code label that does not fit
// before the mnemonic column goes on a line of its own. In .ram and .data
// a label binds to the directive on its line, so it stays there. A
// comment after code moves to the comment column the source uses most,
// or further right when the code is longer. A line that is only a
// comment keeps its indentation.
//
// The layout never changes what the assembler reads: the same program,
// RAM image, data and labels come out, on the same line numbers except
// where a label moved onto its own line.
#pragma once

#include <string>
#include <string_view>

namespace sc8 {

struct AsmLayout {
  int mnemonicColumn = 8;
  // 0 lets the formatter pick: the column most trailing comments in the
  // source start at, when that is at least 8 past the operand column, or
  // else 32.
  int commentColumn = 0;
};

// The operand column: the mnemonic column, the longest instruction
// mnemonic, then two spaces.
int operandColumn(const AsmLayout& layout);

std::string formatAssembly(std::string_view source, const AsmLayout& layout = {});

}  // namespace sc8
