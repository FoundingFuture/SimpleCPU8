// The font lives in its header as constexpr data. This file exists so the
// module has a translation unit beside the others and a place for anything
// that later outgrows the header.
#include "devices/font.h"

namespace sc8 {

static_assert(FONT.size() == FONT_GLYPHS * FONT_H);
static_assert(glyphRow('L', 0) == 0b00001);
static_assert(glyphRow('L', 6) == 0b11111);

}  // namespace sc8
