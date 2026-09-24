// The programming guides inside the IDE. The four markdown books in
// docs/guides are compiled into the executable. A small renderer written
// for Dear ImGui draws them. A guide is parsed once into blocks
// and kept. drawGuide lays out a chapter list beside the text.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace sc8 {

enum class Guide { Basic, C, Assembly, Microcode };
constexpr int GUIDE_COUNT = 4;

// The book's title, its first heading.
const char* guideTitle(Guide guide);

// The guide that matches a file: .bas, .c and .h, .asm, microcode.txt.
// Nothing for any other name.
std::optional<Guide> guideFor(std::string_view fileName);

// What the caller keeps between frames. guide and filter are the
// caller's to set. The rest is the view's own scroll state.
struct GuideView {
  Guide guide = Guide::Basic;
  std::string filter;
  int chapter = -1;   // the chapter shown at the top of the text
  int scrollTo = -1;  // a chapter to scroll to on the next draw
  int settle = 0;     // frames left before the chapter tracks the scroll
  float lastScroll = -1.0f;
};

// Draws the guide inside the current window: the chapter list with its
// filter box on the left, the rendered text on the right.
void drawGuide(GuideView& view);

}  // namespace sc8
