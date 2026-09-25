// The screen: the GPU's 256x256 frame, scaled to the window through the
// CRT shader. Presentation only. The bytes the GPU composed stay what
// they are, and no program can see which effect is on.
#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace sc8 {

constexpr int SCREEN_W = 256;
constexpr int SCREEN_H = 256;

// The looks the screen can take. Sharp is the plain nearest-neighbour
// picture; every other look goes through the shader, whose `mode` is the
// enum's value.
enum class Effect { Sharp, SharpSmooth, Classic, ShadowMask, ApertureGrille, SlotMask, Lcd, Composite, Smooth };
inline constexpr int EFFECT_COUNT = 9;

// The name in menus and on the command line, and its short description.
const char* effectName(Effect e);
const char* effectKey(Effect e);  // one word: sharp, sharp-smooth, classic, ...
const char* effectAbout(Effect e);
// The effect a key or a name names, case ignored.
bool effectByKey(const std::string& key, Effect* out);

// The strengths, so a menu can show only the ones an effect reads.
enum class Knob { Scanlines, Curvature, Blur, Bloom, Vignette, Mask };
inline constexpr int KNOB_COUNT = 6;
const char* knobName(Knob k);
bool effectUses(Effect e, Knob k);

// Each strength runs from 0 to 1. `enabled` false is the plain
// nearest-neighbour picture, whatever the effect and strengths say.
struct DisplaySettings {
  bool enabled = true;
  Effect effect = Effect::Classic;
  float scanlines = 0.5f;
  float curvature = 0.3f;
  float blur = 0.25f;
  float bloom = 0.3f;
  float vignette = 0.4f;
  float mask = 0.0f;
  bool integerScale = false;

  float& knob(Knob k);

  // One strength for the whole look: 0 is off, 0.5 the effect's preset,
  // 1 twice the preset, each strength at most 1.
  void setStrength(float s);
  // An effect with the strengths that suit it.
  void usePreset(Effect e);

  // key = value lines, for a settings file.
  std::string toText() const;
  // Reads the lines toText writes. Unknown keys and bad values are skipped.
  void fromText(const std::string& text);

  bool operator==(const DisplaySettings&) const = default;
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

  // Framebuffer pixels per unit of the rectangle draw() is given: 1 for a
  // render texture, the display's scale for a window on a dense screen.
  // The masks and the sharp edges are sized in real pixels.
  void setOutputDensity(float d) { outputDensity_ = d > 0.0f ? d : 1.0f; }

 private:
  struct Impl;
  Impl* impl_;
  bool shaderOk_ = false;
  float outputDensity_ = 1.0f;
};

}  // namespace sc8
