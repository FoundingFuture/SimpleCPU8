#include "vm/display.h"

#include <algorithm>
#include <vector>

#include "raylib.h"

#include "crt_shader.h"

namespace sc8 {

void DisplaySettings::setStrength(float s) {
  s = std::clamp(s, 0.0f, 1.0f);
  enabled = s > 0.0f;
  scanlines = s;
  curvature = s * 0.6f;
  blur = s * 0.5f;
  bloom = s * 0.6f;
  vignette = s * 0.8f;
}

struct Display::Impl {
  Texture2D texture{};
  Shader shader{};
  int locScreen = -1, locScan = -1, locCurve = -1, locBlur = -1, locBloom = -1, locVig = -1;
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

  impl_->shader = LoadShaderFromMemory(nullptr, CRT_SHADER_FS);
  shaderOk_ = IsShaderValid(impl_->shader);
  if (shaderOk_) {
    impl_->locScreen = GetShaderLocation(impl_->shader, "screenSize");
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

  const bool useShader = shaderOk_ && s.enabled;
  if (useShader) {
    // Linear filtering gives the shader in-between samples for the blur.
    SetTextureFilter(impl_->texture, s.blur > 0.0f ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
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
