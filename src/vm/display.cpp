#include "vm/display.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <vector>

#include "raylib.h"

#include "crt_shader.h"

namespace sc8 {

namespace {

struct EffectInfo {
  const char* key;
  const char* name;
  const char* about;
  // The preset: scanlines, curvature, blur, bloom, vignette, mask.
  float preset[KNOB_COUNT];
  // Which strengths the shader reads for this effect, in the same order.
  bool uses[KNOB_COUNT];
};

constexpr EffectInfo EFFECTS[EFFECT_COUNT] = {
    {"sharp", "Sharp pixels", "every pixel a hard square, no shader", {0, 0, 0, 0, 0, 0}, {false, false, false, false, false, false}},
    {"sharp-smooth", "Sharp smooth", "square pixels with only their edges blended, so any scale looks even",
     {0, 0, 0, 0, 0, 0}, {false, true, false, false, true, false}},
    {"classic", "Classic CRT", "the soft picture: scanlines, a little curve, blur and glow",
     {0.5f, 0.3f, 0.25f, 0.3f, 0.4f, 0}, {true, true, true, true, true, false}},
    {"shadow-mask", "Shadow mask", "a round beam per row over a triad mask, after Timothy Lottes' shader",
     {0.6f, 0.25f, 0.15f, 0.25f, 0.35f, 0.4f}, {true, true, true, true, true, true}},
    {"aperture-grille", "Aperture grille", "vertical phosphor stripes and crisp rows, a Trinitron",
     {0.7f, 0, 0.1f, 0.3f, 0.2f, 0.45f}, {true, true, true, true, true, true}},
    {"slot-mask", "Slot mask", "phosphor stripes broken into staggered slots, a television",
     {0.5f, 0.3f, 0.2f, 0.3f, 0.4f, 0.45f}, {true, true, true, true, true, true}},
    {"lcd", "LCD grid", "a thin grid between the pixels, a handheld screen", {0, 0, 0.1f, 0, 0, 0.5f},
     {false, true, true, false, true, true}},
    {"composite", "Composite", "colour bleeding along the rows, a television on a cable",
     {0.3f, 0.2f, 0.5f, 0.2f, 0.3f, 0}, {true, true, true, true, true, false}},
    {"smooth", "Smooth (Scale2x)", "staircase edges rounded off, the pixels otherwise untouched",
     {0, 0, 0, 0, 0, 0}, {false, true, false, false, true, false}},
};

const EffectInfo& info(Effect e) { return EFFECTS[static_cast<int>(e)]; }

constexpr const char* KNOB_NAMES[KNOB_COUNT] = {"Scanlines", "Curvature", "Blur", "Bloom", "Vignette", "Mask"};

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

const char* effectName(Effect e) { return info(e).name; }
const char* effectKey(Effect e) { return info(e).key; }
const char* effectAbout(Effect e) { return info(e).about; }
const char* knobName(Knob k) { return KNOB_NAMES[static_cast<int>(k)]; }
bool effectUses(Effect e, Knob k) { return info(e).uses[static_cast<int>(k)]; }

bool effectByKey(const std::string& key, Effect* out) {
  const std::string k = lower(key);
  for (int i = 0; i < EFFECT_COUNT; i++) {
    if (k == EFFECTS[i].key || k == lower(EFFECTS[i].name)) {
      *out = static_cast<Effect>(i);
      return true;
    }
  }
  return false;
}

float& DisplaySettings::knob(Knob k) {
  switch (k) {
    case Knob::Scanlines: return scanlines;
    case Knob::Curvature: return curvature;
    case Knob::Blur: return blur;
    case Knob::Bloom: return bloom;
    case Knob::Vignette: return vignette;
    case Knob::Mask: break;
  }
  return mask;
}

void DisplaySettings::usePreset(Effect e) {
  effect = e;
  for (int i = 0; i < KNOB_COUNT; i++) knob(static_cast<Knob>(i)) = info(e).preset[i];
}

void DisplaySettings::setStrength(float s) {
  s = std::clamp(s, 0.0f, 1.0f);
  enabled = s > 0.0f;
  for (int i = 0; i < KNOB_COUNT; i++) {
    knob(static_cast<Knob>(i)) = std::min(1.0f, info(effect).preset[i] * s * 2.0f);
  }
}

std::string DisplaySettings::toText() const {
  std::ostringstream out;
  out << "crt = " << (enabled ? "on" : "off") << "\n";
  out << "crt effect = " << effectKey(effect) << "\n";
  const float values[KNOB_COUNT] = {scanlines, curvature, blur, bloom, vignette, mask};
  for (int i = 0; i < KNOB_COUNT; i++) out << "crt " << lower(KNOB_NAMES[i]) << " = " << values[i] << "\n";
  out << "integer scale = " << (integerScale ? "on" : "off") << "\n";
  return out.str();
}

void DisplaySettings::fromText(const std::string& text) {
  std::istringstream in(text);
  std::string line;
  auto trim = [](std::string s) {
    const size_t a = s.find_first_not_of(" \t\r");
    const size_t b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
  };
  while (std::getline(in, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));
    if (key == "crt") {
      enabled = value == "on";
    } else if (key == "integer scale") {
      integerScale = value == "on";
    } else if (key == "crt effect") {
      effectByKey(value, &effect);
    } else {
      for (int i = 0; i < KNOB_COUNT; i++) {
        if (key != "crt " + lower(KNOB_NAMES[i])) continue;
        char* end = nullptr;
        const float v = std::strtof(value.c_str(), &end);
        if (end != value.c_str()) knob(static_cast<Knob>(i)) = std::clamp(v, 0.0f, 1.0f);
      }
    }
  }
}

struct Display::Impl {
  Texture2D texture{};
  Shader shader{};
  int locScreen = -1, locOutput = -1, locMode = -1, locMask = -1, locScan = -1, locCurve = -1, locBlur = -1, locBloom = -1, locVig = -1;
  std::vector<uint8_t> pixels;
};

Display::Display() : impl_(new Impl) {
  impl_->pixels.assign(static_cast<size_t>(SCREEN_W * SCREEN_H * 4), 0);
  Image img{};
  img.data = impl_->pixels.data();
  img.width = SCREEN_W;
  img.height = SCREEN_H;
  img.mipmaps = 1;
  img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  impl_->texture = LoadTextureFromImage(img);
  SetTextureFilter(impl_->texture, TEXTURE_FILTER_POINT);
  // The blur samples past the edge. Clamped, the edge repeats; wrapped, the
  // top row would show at the bottom.
  SetTextureWrap(impl_->texture, TEXTURE_WRAP_CLAMP);

  impl_->shader = LoadShaderFromMemory(nullptr, CRT_SHADER_FS);
  shaderOk_ = IsShaderValid(impl_->shader);
  if (shaderOk_) {
    impl_->locScreen = GetShaderLocation(impl_->shader, "screenSize");
    impl_->locOutput = GetShaderLocation(impl_->shader, "outputSize");
    impl_->locMode = GetShaderLocation(impl_->shader, "mode");
    impl_->locMask = GetShaderLocation(impl_->shader, "mask");
    impl_->locScan = GetShaderLocation(impl_->shader, "scanlines");
    impl_->locCurve = GetShaderLocation(impl_->shader, "curvature");
    impl_->locBlur = GetShaderLocation(impl_->shader, "blur");
    impl_->locBloom = GetShaderLocation(impl_->shader, "bloom");
    impl_->locVig = GetShaderLocation(impl_->shader, "vignette");
    const float size[2] = {static_cast<float>(SCREEN_W), static_cast<float>(SCREEN_H)};
    SetShaderValue(impl_->shader, impl_->locScreen, size, SHADER_UNIFORM_VEC2);
  }
}

Display::~Display() {
  if (shaderOk_) UnloadShader(impl_->shader);
  UnloadTexture(impl_->texture);
  delete impl_;
}

void Display::upload(std::span<const uint8_t> rgba) {
  if (rgba.size() != impl_->pixels.size()) return;
  std::copy(rgba.begin(), rgba.end(), impl_->pixels.begin());
  UpdateTexture(impl_->texture, impl_->pixels.data());
}

void Display::draw(int x, int y, int w, int h, const DisplaySettings& s) {
  // The square picture, centred in the rectangle it was given.
  int side = std::min(w, h);
  if (s.integerScale && side >= SCREEN_W) side = (side / SCREEN_W) * SCREEN_W;
  const int dx = x + (w - side) / 2;
  const int dy = y + (h - side) / 2;
  const Rectangle src{0, 0, static_cast<float>(SCREEN_W), static_cast<float>(SCREEN_H)};
  const Rectangle dst{static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(side),
                      static_cast<float>(side)};

  const bool useShader = shaderOk_ && s.enabled && s.effect != Effect::Sharp;
  if (useShader) {
    // Linear filtering gives the shader in-between samples. Scale2x and
    // the grid read whole pixels, and the classic look is sharp unblurred.
    const bool linear = s.effect == Effect::Classic ? s.blur > 0.0f
                                                    : s.effect != Effect::Smooth && s.effect != Effect::Lcd;
    SetTextureFilter(impl_->texture, linear ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    // The picture in framebuffer pixels, which a high density display
    // makes larger than its size in points.
    const float density = outputDensity_;
    const float out[2] = {static_cast<float>(side) * density, static_cast<float>(side) * density};
    const int mode = static_cast<int>(s.effect);
    SetShaderValue(impl_->shader, impl_->locOutput, out, SHADER_UNIFORM_VEC2);
    SetShaderValue(impl_->shader, impl_->locMode, &mode, SHADER_UNIFORM_INT);
    SetShaderValue(impl_->shader, impl_->locMask, &s.mask, SHADER_UNIFORM_FLOAT);
    SetShaderValue(impl_->shader, impl_->locScan, &s.scanlines, SHADER_UNIFORM_FLOAT);
    SetShaderValue(impl_->shader, impl_->locCurve, &s.curvature, SHADER_UNIFORM_FLOAT);
    SetShaderValue(impl_->shader, impl_->locBlur, &s.blur, SHADER_UNIFORM_FLOAT);
    SetShaderValue(impl_->shader, impl_->locBloom, &s.bloom, SHADER_UNIFORM_FLOAT);
    SetShaderValue(impl_->shader, impl_->locVig, &s.vignette, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(impl_->shader);
  } else {
    SetTextureFilter(impl_->texture, TEXTURE_FILTER_POINT);
  }
  DrawTexturePro(impl_->texture, src, dst, Vector2{0, 0}, 0.0f, WHITE);
  if (useShader) EndShaderMode();
}

}  // namespace sc8
