// The sprite editor's place in the IDE: its pane, its dialogs, the asset
// it reads and writes, and the preview rendered through the display. The
// editor itself is sprite_editor.cpp.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "imgui.h"
#include "raylib.h"

#include "assets/font.h"
#include "ide/ide.h"
#include "ide/panes.h"

namespace fs = std::filesystem;

namespace sc8 {

namespace {

std::string lowerExt(const std::string& name) {
  std::string e = fs::path(name).extension().string();
  for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return e;
}

// A name an asset can have: letters, digits, dot, dash and underline.
bool assetNameOk(const std::string& name) {
  if (name.empty() || name[0] == '.') return false;
  return std::all_of(name.begin(), name.end(), [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_';
  });
}

}  // namespace

bool Ide::isPicture(const std::string& name) {
  const std::string e = lowerExt(name);
  return e == ".png" || e == ".bmp" || e == ".gif" || e == ".jpg" || e == ".jpeg" || e == ".tga";
}

bool Ide::isDrawable(const std::string& name) { return isPicture(name) || SpriteEditor::isFont(name); }

bool Ide::setFontView(const std::string& name) {
  const std::optional<SpriteEditor::FontView> v = SpriteEditor::fontViewNamed(name);
  if (!v) return false;
  sprite_.setFontView(*v);
  return true;
}

std::optional<std::vector<uint8_t>> Ide::readAsset(const std::string& name) const {
  if (!projectDir_.empty()) {
    std::ifstream in(layout().assets / name, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
  }
  for (const auto& [n, bytes] : romFiles_) {
    if (n == "assets/" + name) return bytes;
  }
  return std::nullopt;
}

bool Ide::writeAsset(const std::string& name, const std::vector<uint8_t>& bytes) {
  assetsStale_ = true;
  if (refuseExample(name + " saved")) return false;
  if (!projectDir_.empty()) {
    const fs::path dir = layout().assets;
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::ofstream o(dir / name, std::ios::binary);
    o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(o);
  }
  // A ROM or scratch project holds its assets in memory until it is saved.
  filesChanged_ = true;
  for (auto& [n, b] : romFiles_) {
    if (n == "assets/" + name) {
      b = bytes;
      return true;
    }
  }
  romFiles_.push_back({"assets/" + name, bytes});
  return true;
}

void Ide::openSprite(const std::string& name) {
  if (sprite_.dirty() && sprite_.name() != name) {
    pendingSprite_ = name;
    askSpriteSwitch_ = true;
    return;
  }
  // A picture that is not a PNG opens as a new PNG beside it, since the
  // strip's frame count lives in a PNG text chunk.
  std::string saveAs;
  if (isPicture(name) && lowerExt(name) != ".png") saveAs = fs::path(name).stem().string() + ".png";
  if (sprite_.open(name, saveAs)) {
    spriteVisible_ = true;
    focusSprite_ = 6;
    if (!saveAs.empty()) {
      bool exists = false;
      for (const AssetEntry& a : assetList()) exists = exists || a.name == saveAs;
      note(name + " opened in the sprite editor; Save writes it as " + saveAs +
           (exists ? ", which replaces the " + saveAs + " there now" : ""));
    }
  }
}

void Ide::spritePane() {
  // A dock node shows the tab of the window that has the focus, and only
  // one window has it. So the preview takes it first, for a few frames,
  // and the editor after it, keeping the keyboard.
  const bool focusPreview = focusSprite_ > 3;
  const bool focus = focusSprite_ > 0 && !focusPreview;
  if (focusSprite_ > 0) focusSprite_--;
  bool open = true;
  if (focusPreview) ImGui::SetNextWindowFocus();
  if (ImGui::Begin("Sprite preview", &open)) {
    const DisplaySettings before = display;
    sprite_.previewBody(display);
    if (!(display == before)) saveDisplay();
  }
  ImGui::End();
  if (focus) ImGui::SetNextWindowFocus();
  ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);
  // The ### keeps one window whatever the title says.
  if (ImGui::Begin((sprite_.title() + "###Sprite").c_str(), &open)) sprite_.body();
  ImGui::End();
  if (!open) spriteVisible_ = false;
}

void Ide::newSpriteDialog() {
  if (!askNewSprite_) return;
  ImGui::OpenPopup("New sprite");
  if (ImGui::BeginPopupModal("New sprite", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    panes::inputLine("File name", newSpriteName_);
    ImGui::InputInt("Width", &newSpriteW_);
    ImGui::InputInt("Height", &newSpriteH_);
    ImGui::InputInt("Frames", &newSpriteFrames_);
    newSpriteW_ = std::clamp(newSpriteW_, 1, sprite::MAX_SIDE);
    newSpriteH_ = std::clamp(newSpriteH_, 1, sprite::MAX_SIDE);
    newSpriteFrames_ = std::clamp(newSpriteFrames_, 1, sprite::MAX_FRAMES);
    ImGui::TextDisabled("a frame is at most %d x %d, a strip at most %d frames", sprite::MAX_SIDE, sprite::MAX_SIDE,
                        sprite::MAX_FRAMES);
    std::string name = newSpriteName_;
    if (lowerExt(name) != ".png") name += ".png";
    bool taken = false;
    for (const AssetEntry& a : assetList()) taken = taken || a.name == name;
    const bool ok = assetNameOk(name) && !taken;
    if (!assetNameOk(name)) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "use letters, digits, '.', '-' and '_'");
    if (taken) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s is already an asset", name.c_str());
    ImGui::BeginDisabled(!ok);
    if (ImGui::Button("Create")) {
      askNewSprite_ = false;
      ImGui::CloseCurrentPopup();
      sprite_.create(name, newSpriteW_, newSpriteH_, newSpriteFrames_);
      spriteVisible_ = true;
      focusSprite_ = 6;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      askNewSprite_ = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void Ide::newFontDialog() {
  if (!askNewFont_) return;
  ImGui::OpenPopup("New font");
  if (ImGui::BeginPopupModal("New font", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    panes::inputLine("File name", newFontName_);
    ImGui::TextDisabled("a copy of the built-in font, cell 6 x 8, with the block at $7F eight columns wide");
    const std::string name = font::fileName(newFontName_);
    bool taken = false;
    for (const AssetEntry& a : assetList()) taken = taken || a.name == name;
    const bool ok = assetNameOk(name) && !taken;
    if (!assetNameOk(name)) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "use letters, digits, '.', '-' and '_'");
    if (taken) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s is already an asset", name.c_str());
    ImGui::BeginDisabled(!ok);
    if (ImGui::Button("Create")) {
      askNewFont_ = false;
      ImGui::CloseCurrentPopup();
      // The file is written first, so the font is an asset like any other
      // and the editor opens it the way a double click would.
      if (writeAsset(name, font::newFile())) {
        note("created " + name);
        openSprite(name);
      } else {
        note(name + ": cannot write it");
      }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      askNewFont_ = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void Ide::spriteSwitchDialog() {
  if (!askSpriteSwitch_) return;
  ImGui::OpenPopup("Unsaved sprite");
  if (ImGui::BeginPopupModal("Unsaved sprite", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("%s has unsaved changes.", sprite_.name().c_str());
    auto next = [&] {
      askSpriteSwitch_ = false;
      ImGui::CloseCurrentPopup();
      const std::string name = *pendingSprite_;
      pendingSprite_.reset();
      if (name.empty()) askNewSprite_ = true;
      else openSprite(name);
    };
    if (ImGui::Button("Save")) {
      if (sprite_.save()) next();
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard")) {
      sprite_.close();
      next();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      askSpriteSwitch_ = false;
      pendingSprite_.reset();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void Ide::askNewSprite() {
  if (sprite_.dirty()) {
    pendingSprite_ = "";
    askSpriteSwitch_ = true;
    return;
  }
  askNewSprite_ = true;
}

void Ide::spritePreview() {
  if (!spriteVisible_ || !sprite_.isOpen() || !sprite_.previewShown()) return;
  sprite_.previewPixels(GetTime(), previewPixels_);
  // A font is judged on the machine's own pixels, without the screen's look.
  DisplaySettings look = display;
  if (!sprite_.previewEffects()) look.enabled = false;
  panes::PaneTarget& t = panes::previewTarget();
  BeginTextureMode(t.texture());
  ClearBackground(BLACK);
  previewScreen_.setOutputDensity(1.0f);
  previewScreen_.upload(previewPixels_);
  previewScreen_.draw(0, 0, t.side(), t.side(), look);
  EndTextureMode();
}

void Ide::loadDisplay() {
  const fs::path dir = Settings::dir();
  if (dir.empty()) return;
  std::ifstream in(dir / "display.txt");
  if (!in) return;
  std::stringstream text;
  text << in.rdbuf();
  display.fromText(text.str());
}

void Ide::saveDisplay() {
  const fs::path dir = Settings::dir();
  if (dir.empty()) return;
  std::ofstream o(dir / "display.txt");
  o << "# SimpleCPU-8 IDE display\n" << display.toText();
}

}  // namespace sc8
