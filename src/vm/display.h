// The screen: the GPU's 256x256 frame, scaled to the window through the
// CRT shader. Presentation only. The bytes the GPU composed stay what
// they are, and no program can see which effect is on.
#pragma once

#include <cstdint>
#include <span>

namespace sc8 {

constexpr int SCREEN_W = 256;
constexpr int SCREEN_H = 256;

// Each effect has a strength from 0 to 1. `enabled` false is the plain
// nearest-neighbour picture, whatever the strengths say.
struct DisplaySettings {
  bool enabled = true;
  float scanlines = 0.5f;
  float curvature = 0.3f;
  float blur = 0.25f;
  float bloom = 0.3f;
  float vignette = 0.4f;
  bool integerScale = false;

  // One strength for the whole look, applied to every effect at once.
  void setStrength(float s);
};

class Display {
 public:
  Display();
  ~Display();
  Display(const Display&) = delete;
  Display& operator=(const Display&) = delete;

  // Upload one frame: 256 * 256 pixels as RGBA8.
  void upload(std::span<const uint8_t> rgba);

  // Draw the frame into the rectangle, keeping the square aspect. Call
  // between BeginDrawing and EndDrawing.
  void draw(int x, int y, int w, int h, const DisplaySettings& settings);

  bool shaderReady() const { return shaderOk_; }

 private:
  struct Impl;
  Impl* impl_;
  bool shaderOk_ = false;
};

}  // namespace sc8
