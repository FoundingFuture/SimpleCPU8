// Helpers the pane files share: the render target the screen pane shows,
// std::string backed text inputs, and hex formatting.
#pragma once

#include <string>

#include "imgui.h"
#include "raylib.h"

namespace sc8::panes {

// A square render target a pane shows through Dear ImGui. Drawing
// through the CRT shader has to happen in raylib's own pass, so the
// picture is rendered first and the pane shows the result. The texture
// follows the size the pane last showed it at, in framebuffer pixels, so
// a phosphor mask lands on real pixels rather than being scaled again.
class PaneTarget {
 public:
  // The texture, at the size the pane asked for last frame.
  RenderTexture2D& texture();
  int side() const { return side_; }
  // Show it as a square `side` units wide at the cursor. Also asks for
  // that size on the next frame.
  void show(float side);

 private:
  RenderTexture2D rt_{};
  int side_ = 0;
  int wanted_ = 768;
};

// The screen pane's picture and the sprite editor's preview.
PaneTarget& screenTarget();
PaneTarget& previewTarget();

// A multiline or single line text input over a std::string. The string
// keeps spare capacity, so a resize callback grows it as the text grows.
// `callback` gets every event but the resize, which the string needs, with
// `user` as its UserData. Pass the flags that ask for those events.
bool inputMultiline(const char* id, std::string& text, ImVec2 size, ImGuiInputTextFlags flags = 0,
                    ImGuiInputTextCallback callback = nullptr, void* user = nullptr);
bool inputLine(const char* label, std::string& text, const char* hint = "", ImGuiInputTextFlags flags = 0);

std::string hex(unsigned v, int digits);

// A color as 0xRRGGBB to ImGui's packed form.
ImU32 rgb(unsigned c, int alpha = 255);

}  // namespace sc8::panes
