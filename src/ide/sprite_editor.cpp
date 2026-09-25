#include "ide/sprite_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "imgui.h"

#include "assets/assets.h"
#include "ide/panes.h"

namespace sc8 {

namespace {

constexpr size_t UNDO_LIMIT = 200;
constexpr ImU32 CHECK_A = IM_COL32(58, 58, 64, 255);
constexpr ImU32 CHECK_B = IM_COL32(82, 82, 90, 255);
constexpr ImU32 GRID = IM_COL32(0, 0, 0, 90);
constexpr ImU32 HOVER = IM_COL32(255, 255, 255, 200);

ImU32 colourOf(const std::vector<uint8_t>& pal, uint8_t i, int alpha = 255) {
  const size_t o = static_cast<size_t>(i) * 3;
  return IM_COL32(pal[o], pal[o + 1], pal[o + 2], alpha);
}

// A square of the palette, with index 0 drawn as the transparent checker.
void swatch(ImDrawList* dl, ImVec2 a, ImVec2 b, const std::vector<uint8_t>& pal, uint8_t i) {
  if (i == 0) {
    const ImVec2 m((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    dl->AddRectFilled(a, b, CHECK_A);
    dl->AddRectFilled(a, m, CHECK_B);
    dl->AddRectFilled(m, b, CHECK_B);
  } else {
    dl->AddRectFilled(a, b, colourOf(pal, i));
  }
}

const char* toolName(int t) {
  static const char* names[] = {"Pencil", "Line", "Rect", "Ellipse", "Fill", "Gradient", "Roll"};
  return names[t];
}

const char* toolHelp(int t) {
  static const char* help[] = {
      "P: click sets a pixel, a drag draws; Shift clears to transparent",
      "L: drag from one end to the other; Shift draws transparent",
      "R: drag from corner to corner; Filled fills it; Shift draws transparent",
      "E: the ellipse in the box from corner to corner; Filled fills it; Shift draws transparent",
      "F: fills the area of one colour under the click; Shift fills with transparent",
      "G: click in an area and drag: it shades from the first colour at the click to the second where you "
      "let go. Shift shades into transparent",
      "O: drag sideways to roll the row, up or down to roll the column. Shift rolls the whole frame. "
      "Pixels that leave one side come back on the other",
  };
  return help[t];
}

}  // namespace

SpriteEditor::SpriteEditor(Host host) : host_(std::move(host)), palette_(machinePalette()) {}

bool SpriteEditor::open(const std::string& name, const std::string& saveAs) {
  const std::optional<std::vector<uint8_t>> bytes = host_.read(name);
  if (!bytes) {
    host_.note(name + ": cannot read it");
    return false;
  }
  std::string error;
  std::optional<sprite::Loaded> loaded = sprite::load(*bytes, palette_, &error);
  if (!loaded) {
    host_.note(name + ": " + error);
    return false;
  }
  if (!loaded->note.empty()) host_.note(name + ": " + loaded->note + "; saving writes them that way");
  strip_ = std::move(loaded->strip);
  name_ = saveAs.empty() ? name : saveAs;
  frame_ = 0;
  open_ = true;
  dirty_ = !loaded->note.empty() || !saveAs.empty();
  undo_.clear();
  redo_.clear();
  newWidth_ = strip_.width;
  newHeight_ = strip_.height;
  fit_ = 3;
  return true;
}

void SpriteEditor::create(const std::string& name, int width, int height, int frames) {
  strip_ = sprite::blank(width, height, frames);
  name_ = name;
  frame_ = 0;
  open_ = true;
  dirty_ = true;
  undo_.clear();
  redo_.clear();
  newWidth_ = width;
  newHeight_ = height;
  fit_ = 3;
}

void SpriteEditor::close() {
  open_ = false;
  dirty_ = false;
  onScreen_ = false;
  undo_.clear();
  redo_.clear();
}

bool SpriteEditor::save() {
  if (!open_) return true;
  if (!host_.write(name_, sprite::encodePng(strip_, palette_))) {
    host_.note(name_ + ": cannot write it");
    return false;
  }
  dirty_ = false;
  return true;
}

void SpriteEditor::pushUndo() {
  undo_.push_back({strip_, frame_});
  if (undo_.size() > UNDO_LIMIT) undo_.erase(undo_.begin());
  redo_.clear();
}

void SpriteEditor::undo() {
  if (undo_.empty()) return;
  redo_.push_back({strip_, frame_});
  strip_ = std::move(undo_.back().strip);
  frame_ = undo_.back().frame;
  undo_.pop_back();
  newWidth_ = strip_.width;
  newHeight_ = strip_.height;
  changed();
}

void SpriteEditor::redo() {
  if (redo_.empty()) return;
  undo_.push_back({strip_, frame_});
  strip_ = std::move(redo_.back().strip);
  frame_ = redo_.back().frame;
  redo_.pop_back();
  newWidth_ = strip_.width;
  newHeight_ = strip_.height;
  changed();
}

void SpriteEditor::body() {
  if (!open_) {
    ImGui::TextWrapped("No sprite is open. Double-click a picture under Assets in the Files pane, or use "
                       "New sprite... there.");
    return;
  }
  frame_ = std::clamp(frame_, 0, static_cast<int>(strip_.frames.size()) - 1);
  shortcuts();
  toolbar();

  // The canvas on the left, the colours and the frame tools in a column
  // on the right, the frames along the bottom.
  const float side = std::min(280.0f, ImGui::GetContentRegionAvail().x * 0.45f);
  const float stripHeight = 72.0f + ImGui::GetFrameHeightWithSpacing() * 3.0f + ImGui::GetStyle().ScrollbarSize;
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float leftWidth = std::max(120.0f, avail.x - side - ImGui::GetStyle().ItemSpacing.x);
  const float topHeight = std::max(120.0f, avail.y - stripHeight);
  ImGui::BeginChild("##canvas", ImVec2(leftWidth, topHeight), ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_HorizontalScrollbar);
  canvas();
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##side", ImVec2(0, topHeight), ImGuiChildFlags_None);
  paletteBox();
  ImGui::Spacing();
  sizeBox();
  ImGui::EndChild();
  ImGui::BeginChild("##frames", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
  frameStrip();
  ImGui::EndChild();
}

void SpriteEditor::shortcuts() {
  if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput) return;
  const ImGuiIO& io = ImGui::GetIO();
  const bool cmd = io.KeyCtrl || io.KeySuper;
  if (cmd && ImGui::IsKeyPressed(ImGuiKey_Z)) {
    if (io.KeyShift) redo();
    else undo();
    return;
  }
  if (cmd && ImGui::IsKeyPressed(ImGuiKey_Y)) {
    redo();
    return;
  }
  if (cmd) return;
  if (ImGui::IsKeyPressed(ImGuiKey_P)) tool_ = Tool::Pencil;
  if (ImGui::IsKeyPressed(ImGuiKey_L)) tool_ = Tool::Line;
  if (ImGui::IsKeyPressed(ImGuiKey_R)) tool_ = Tool::Rect;
  if (ImGui::IsKeyPressed(ImGuiKey_E)) tool_ = Tool::Ellipse;
  if (ImGui::IsKeyPressed(ImGuiKey_F)) tool_ = Tool::Fill;
  if (ImGui::IsKeyPressed(ImGuiKey_G)) tool_ = Tool::Gradient;
  if (ImGui::IsKeyPressed(ImGuiKey_O)) tool_ = Tool::Roll;
  if (ImGui::IsKeyPressed(ImGuiKey_X)) std::swap(primary_, secondary_);
  if (ImGui::IsKeyPressed(ImGuiKey_Space)) playing_ = !playing_;
  const int n = static_cast<int>(strip_.frames.size());
  if (ImGui::IsKeyPressed(ImGuiKey_Comma)) frame_ = (frame_ + n - 1) % n;
  if (ImGui::IsKeyPressed(ImGuiKey_Period)) frame_ = (frame_ + 1) % n;
}

void SpriteEditor::toolbar() {
  ImGui::Text("%s%s", name_.c_str(), dirty_ ? " *" : "");
  ImGui::SameLine();
  ImGui::TextDisabled("%d x %d, %zu frame(s)", strip_.width, strip_.height, strip_.frames.size());
  ImGui::SameLine();
  if (ImGui::SmallButton("Save")) {
    if (save()) host_.note("saved " + name_);
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(undo_.empty());
  if (ImGui::SmallButton("Undo")) undo();
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Ctrl+Z");
  ImGui::SameLine();
  ImGui::BeginDisabled(redo_.empty());
  if (ImGui::SmallButton("Redo")) redo();
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Ctrl+Shift+Z or Ctrl+Y");

  for (int t = 0; t <= static_cast<int>(Tool::Roll); t++) {
    if (t) ImGui::SameLine();
    if (ImGui::RadioButton(toolName(t), static_cast<int>(tool_) == t)) tool_ = static_cast<Tool>(t);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", toolHelp(t));
  }
  ImGui::Checkbox("Filled", &filled_);
  ImGui::SameLine();
  ImGui::Checkbox("Dither", &dither_);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("the gradient mixes neighbouring colours in a fine pattern");
  ImGui::SameLine();
  ImGui::Checkbox("Grid", &grid_);
  ImGui::SameLine();
  ImGui::Checkbox("Onion skin", &onion_);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("the frame before this one shows faintly where this one is transparent");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120.0f);
  ImGui::SliderFloat("Zoom", &zoom_, 2.0f, 48.0f, "%.0f x");
  ImGui::SameLine();
  if (ImGui::SmallButton("Fit")) fit_ = 1;
  ImGui::TextDisabled("%s. A right click picks up a colour.", toolHelp(static_cast<int>(tool_)));
}

void SpriteEditor::canvas() {
  const sprite::Frame& f = frame();
  const ImVec2 room = ImGui::GetContentRegionAvail();
  if (fit_ > 0) {
    // The largest whole zoom at which the frame fits the canvas.
    fit_--;
    zoom_ = std::clamp(std::floor(std::min(room.x / static_cast<float>(f.width), room.y / static_cast<float>(f.height))),
                       2.0f, 48.0f);
  }
  const float cell = std::round(zoom_);
  const ImVec2 size(static_cast<float>(f.width) * cell, static_cast<float>(f.height) * cell);
  // Centred in the child while it fits.
  ImVec2 cur = ImGui::GetCursorPos();
  cur.x += std::max(0.0f, (room.x - size.x) * 0.5f);
  cur.y += std::max(0.0f, (room.y - size.y) * 0.5f);
  ImGui::SetCursorPos(cur);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##pixels", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  const bool hovered = ImGui::IsItemHovered();
  const ImGuiIO& io = ImGui::GetIO();
  const float mx = io.MousePos.x - origin.x, my = io.MousePos.y - origin.y;
  const int px = static_cast<int>(std::floor(mx / cell)), py = static_cast<int>(std::floor(my / cell));

  // The mouse: left draws with the tool, right picks up a colour.
  if (ImGui::IsItemActivated() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    pushUndo();
    stroking_ = true;
    erasing_ = io.KeyShift;
    base_ = frame();
    startX_ = lastX_ = px;
    startY_ = lastY_ = py;
    startMouseX_ = mx;
    startMouseY_ = my;
    rollAxis_ = erasing_ && tool_ == Tool::Roll ? 3 : 0;
    strokeTo(px, py, mx, my);
  } else if (stroking_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    strokeTo(px, py, mx, my);
  }
  if (stroking_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    stroking_ = false;
    // A stroke that changed nothing leaves no undo step behind.
    if (frame() == base_ && !undo_.empty()) undo_.pop_back();
    else changed();
  }
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && f.inside(px, py)) primary_ = f.at(px, py);

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const sprite::Frame* before =
      onion_ && strip_.frames.size() > 1
          ? &strip_.frames[static_cast<size_t>((frame_ + static_cast<int>(strip_.frames.size()) - 1) %
                                               static_cast<int>(strip_.frames.size()))]
          : nullptr;
  for (int y = 0; y < f.height; y++) {
    for (int x = 0; x < f.width; x++) {
      const ImVec2 a(origin.x + static_cast<float>(x) * cell, origin.y + static_cast<float>(y) * cell);
      const ImVec2 b(a.x + cell, a.y + cell);
      const uint8_t c = f.at(x, y);
      if (c != 0) {
        dl->AddRectFilled(a, b, colourOf(palette_, c));
        continue;
      }
      dl->AddRectFilled(a, b, ((x + y) & 1) ? CHECK_A : CHECK_B);
      if (before && before->at(x, y) != 0) dl->AddRectFilled(a, b, colourOf(palette_, before->at(x, y), 80));
    }
  }
  if (grid_ && cell >= 6.0f) {
    for (int x = 0; x <= f.width; x++) {
      const float gx = origin.x + static_cast<float>(x) * cell;
      dl->AddLine(ImVec2(gx, origin.y), ImVec2(gx, origin.y + size.y), GRID);
    }
    for (int y = 0; y <= f.height; y++) {
      const float gy = origin.y + static_cast<float>(y) * cell;
      dl->AddLine(ImVec2(origin.x, gy), ImVec2(origin.x + size.x, gy), GRID);
    }
  }
  dl->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(140, 140, 150, 255));
  if (hovered && f.inside(px, py)) {
    const ImVec2 a(origin.x + static_cast<float>(px) * cell, origin.y + static_cast<float>(py) * cell);
    dl->AddRect(a, ImVec2(a.x + cell, a.y + cell), HOVER, 0.0f, 0, 2.0f);
    ImGui::SetTooltip("%d, %d  colour %d", px, py, f.at(px, py));
  }
}

void SpriteEditor::strokeTo(int x, int y, float mouseX, float mouseY) {
  sprite::Frame& f = frame();
  const uint8_t c = erasing_ ? 0 : primary_;
  switch (tool_) {
    case Tool::Pencil:
      sprite::line(f, lastX_, lastY_, x, y, c);
      break;
    case Tool::Line:
      f = base_;
      sprite::line(f, startX_, startY_, x, y, c);
      break;
    case Tool::Rect:
      f = base_;
      sprite::rect(f, startX_, startY_, x, y, c, filled_);
      break;
    case Tool::Ellipse:
      f = base_;
      sprite::ellipse(f, startX_, startY_, x, y, c, filled_);
      break;
    case Tool::Fill:
      // Once, where the button went down.
      if (x == startX_ && y == startY_ && f == base_) sprite::fill(f, x, y, c);
      break;
    case Tool::Gradient:
      f = base_;
      sprite::gradient(f, startX_, startY_, startX_, startY_, x, y, primary_, erasing_ ? 0 : secondary_, palette_,
                       dither_);
      break;
    case Tool::Roll: {
      const float cell = std::round(zoom_);
      const int dx = static_cast<int>(std::round((mouseX - startMouseX_) / cell));
      const int dy = static_cast<int>(std::round((mouseY - startMouseY_) / cell));
      if (rollAxis_ == 0 && (dx != 0 || dy != 0)) rollAxis_ = std::abs(dx) >= std::abs(dy) ? 1 : 2;
      f = base_;
      if (rollAxis_ == 1) sprite::rollRow(f, startY_, dx);
      else if (rollAxis_ == 2) sprite::rollColumn(f, startX_, dy);
      else if (rollAxis_ == 3) sprite::roll(f, dx, dy);
      break;
    }
  }
  lastX_ = x;
  lastY_ = y;
}

void SpriteEditor::paletteBox() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float width = ImGui::GetContentRegionAvail().x;
  const float big = ImGui::GetFrameHeight() * 1.4f;

  // The two colours: the first draws, the second ends a gradient.
  ImVec2 p = ImGui::GetCursorScreenPos();
  swatch(dl, ImVec2(p.x + big * 0.6f, p.y + big * 0.6f), ImVec2(p.x + big * 1.6f, p.y + big * 1.6f), palette_,
         secondary_);
  swatch(dl, p, ImVec2(p.x + big, p.y + big), palette_, primary_);
  dl->AddRect(p, ImVec2(p.x + big, p.y + big), IM_COL32(255, 255, 255, 255));
  ImGui::Dummy(ImVec2(big * 1.6f, big * 1.6f));
  ImGui::SameLine();
  ImGui::BeginGroup();
  ImGui::Text("draw %d, second %d", primary_, secondary_);
  if (ImGui::SmallButton("Swap")) std::swap(primary_, secondary_);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("X");
  ImGui::SameLine();
  ImGui::TextDisabled("0 is transparent");
  ImGui::EndGroup();

  // The 256 colours, 16 a row. A left click picks the drawing colour, a
  // right click the second.
  const float cell = std::floor(std::min(width, 320.0f) / 16.0f);
  p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##palette", ImVec2(cell * 16, cell * 16),
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  const ImVec2 m = ImGui::GetIO().MousePos;
  const int hx = static_cast<int>((m.x - p.x) / cell), hy = static_cast<int>((m.y - p.y) / cell);
  const bool over = ImGui::IsItemHovered() && hx >= 0 && hx < 16 && hy >= 0 && hy < 16;
  if (over) {
    const auto i = static_cast<uint8_t>(hy * 16 + hx);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) primary_ = i;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) secondary_ = i;
    const size_t o = static_cast<size_t>(i) * 3;
    ImGui::SetTooltip("%d  #%02X%02X%02X%s", i, palette_[o], palette_[o + 1], palette_[o + 2],
                      i == 0 ? "  transparent" : "");
  }
  for (int i = 0; i < 256; i++) {
    const ImVec2 a(p.x + static_cast<float>(i % 16) * cell, p.y + static_cast<float>(i / 16) * cell);
    swatch(dl, a, ImVec2(a.x + cell, a.y + cell), palette_, static_cast<uint8_t>(i));
  }
  for (const auto& [i, col] : {std::pair{primary_, IM_COL32(255, 255, 255, 255)}, std::pair{secondary_, IM_COL32(255, 200, 0, 255)}}) {
    const ImVec2 a(p.x + static_cast<float>(i % 16) * cell, p.y + static_cast<float>(i / 16) * cell);
    dl->AddRect(a, ImVec2(a.x + cell, a.y + cell), col, 0.0f, 0, 2.0f);
  }
  ImGui::TextDisabled("left click: draw colour, right click: second");
}

void SpriteEditor::previewBody(DisplaySettings& display) {
  previewShown_ = false;
  if (!open_) {
    ImGui::TextDisabled("no sprite is open");
    return;
  }
  // The controls under the picture take about five lines.
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float side = std::max(64.0f, std::min(avail.x, avail.y - ImGui::GetFrameHeightWithSpacing() * 4.5f));
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - side) * 0.5f));
  panes::previewTarget().show(side);
  previewShown_ = true;
  if (ImGui::Button(playing_ ? "Pause" : "Play")) playing_ = !playing_;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Space. Paused, the preview shows the frame being edited");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90.0f);
  if (ImGui::SliderInt("fps", &strip_.fps, 1, 60)) changed();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("frames per second; saved in the PNG");
  ImGui::SameLine();
  ImGui::Checkbox("Ping-pong", &pingPong_);
  ImGui::SetNextItemWidth(90.0f);
  int size = previewScale_ - 1;
  if (ImGui::Combo("Size", &size, "1 x, as the machine draws it\0" "2 x\0" "3 x\0" "4 x\0")) previewScale_ = size + 1;
  ImGui::SameLine();
  ImGui::SetNextItemWidth(60.0f);
  int back = backdrop_;
  if (ImGui::InputInt("Backdrop", &back, 1, 16)) backdrop_ = static_cast<uint8_t>(std::clamp(back, 0, 255));
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("the colour behind the sprite, a palette index");

  // The screen's own effect, so the sprite is judged the way the game
  // will show it. Changing it here changes the Screen pane too.
  ImGui::Checkbox("Effect", &display.enabled);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::BeginCombo("##effect", effectName(display.effect))) {
    for (int i = 0; i < EFFECT_COUNT; i++) {
      const auto e = static_cast<Effect>(i);
      if (ImGui::Selectable(effectName(e), display.effect == e)) {
        display.usePreset(e);
        display.enabled = true;
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", effectAbout(e));
    }
    ImGui::EndCombo();
  }
  ImGui::Checkbox("Play on the Screen pane", &onScreen_);
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("the Screen pane shows the preview instead of the machine, at the pane's full size");
  }
}

void SpriteEditor::sizeBox() {
  ImGui::SeparatorText("Frame size");
  ImGui::SetNextItemWidth(80.0f);
  ImGui::InputInt("W", &newWidth_, 1, 8);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80.0f);
  ImGui::InputInt("H", &newHeight_, 1, 8);
  newWidth_ = std::clamp(newWidth_, 1, sprite::MAX_SIDE);
  newHeight_ = std::clamp(newHeight_, 1, sprite::MAX_SIDE);
  ImGui::SameLine();
  ImGui::BeginDisabled(newWidth_ == strip_.width && newHeight_ == strip_.height);
  if (ImGui::Button("Resize")) {
    pushUndo();
    sprite::resize(strip_, newWidth_, newHeight_);
    changed();
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("every frame, kept at the top left; at most %d a side", sprite::MAX_SIDE);
  }
  ImGui::SeparatorText("This frame");
  auto act = [&](const char* label, const char* tip, auto fn) {
    if (ImGui::SmallButton(label)) {
      pushUndo();
      fn(frame());
      changed();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
  };
  act("Roll <", "every row one pixel left, wrapping round", [](sprite::Frame& f) { sprite::roll(f, -1, 0); });
  ImGui::SameLine();
  act("Roll >", "every row one pixel right, wrapping round", [](sprite::Frame& f) { sprite::roll(f, 1, 0); });
  ImGui::SameLine();
  act("Roll ^", "every column one pixel up, wrapping round", [](sprite::Frame& f) { sprite::roll(f, 0, -1); });
  ImGui::SameLine();
  act("Roll v", "every column one pixel down, wrapping round", [](sprite::Frame& f) { sprite::roll(f, 0, 1); });
  act("Flip H", "mirror left to right", [](sprite::Frame& f) { sprite::flipH(f); });
  ImGui::SameLine();
  act("Flip V", "mirror top to bottom", [](sprite::Frame& f) { sprite::flipV(f); });
  ImGui::SameLine();
  act("Clear", "every pixel transparent", [](sprite::Frame& f) { std::fill(f.px.begin(), f.px.end(), uint8_t{0}); });
}

void SpriteEditor::frameStrip() {
  const int n = static_cast<int>(strip_.frames.size());
  const bool full = n >= sprite::MAX_FRAMES;
  // Each button goes on the line when it fits, else it starts the next.
  auto flow = [](const char* label) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float w = ImGui::CalcTextSize(label).x + st.FramePadding.x * 2.0f;
    const float right = ImGui::GetItemRectMax().x + st.ItemSpacing.x + w;
    if (right <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
  };
  auto frameOp = [&](const char* label, bool enabled, const char* tip, auto fn) {
    flow(label);
    ImGui::BeginDisabled(!enabled);
    if (ImGui::SmallButton(label)) {
      pushUndo();
      fn();
      changed();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tip);
  };
  const auto at = [&](int i) { return strip_.frames.begin() + i; };
  const sprite::Frame empty(strip_.width, strip_.height);
  ImGui::Text("Frame %d of %d", frame_ + 1, n);
  frameOp("Add at end", !full, "a transparent frame after the last", [&] {
    strip_.frames.push_back(empty);
    frame_ = n;
  });
  frameOp("Insert before", !full, "a transparent frame before this one", [&] { strip_.frames.insert(at(frame_), empty); });
  frameOp("Insert after", !full, "a transparent frame after this one", [&] {
    strip_.frames.insert(at(frame_ + 1), empty);
    frame_++;
  });
  frameOp("Duplicate", !full, "a copy of this frame after it", [&] {
    const sprite::Frame copy = frame();
    strip_.frames.insert(at(frame_ + 1), copy);
    frame_++;
  });
  frameOp("Move left", frame_ > 0, "swap with the frame before", [&] {
    std::swap(strip_.frames[static_cast<size_t>(frame_)], strip_.frames[static_cast<size_t>(frame_ - 1)]);
    frame_--;
  });
  frameOp("Move right", frame_ < n - 1, "swap with the frame after", [&] {
    std::swap(strip_.frames[static_cast<size_t>(frame_)], strip_.frames[static_cast<size_t>(frame_ + 1)]);
    frame_++;
  });
  frameOp("Delete", n > 1, "remove this frame", [&] {
    strip_.frames.erase(at(frame_));
    frame_ = std::min(frame_, static_cast<int>(strip_.frames.size()) - 1);
  });
  flow("at most 16 frames; , and . step");
  ImGui::TextDisabled("at most %d frames; , and . step", sprite::MAX_FRAMES);

  // The thumbnails, each fitted to a 72 unit square. A click selects.
  const float box = 72.0f;
  const float scale = std::max(1.0f, std::floor(box / static_cast<float>(std::max(strip_.width, strip_.height))));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  for (int i = 0; i < static_cast<int>(strip_.frames.size()); i++) {
    if (i) ImGui::SameLine();
    ImGui::PushID(i);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##thumb", ImVec2(box, box))) frame_ = i;
    const sprite::Frame& f = strip_.frames[static_cast<size_t>(i)];
    const float w = static_cast<float>(f.width) * scale, h = static_cast<float>(f.height) * scale;
    const ImVec2 o(p.x + (box - w) * 0.5f, p.y + (box - h) * 0.5f);
    dl->AddRectFilled(o, ImVec2(o.x + w, o.y + h), CHECK_A);
    for (int y = 0; y < f.height; y++) {
      for (int x = 0; x < f.width; x++) {
        const uint8_t c = f.at(x, y);
        if (c == 0) continue;
        const ImVec2 a(o.x + static_cast<float>(x) * scale, o.y + static_cast<float>(y) * scale);
        dl->AddRectFilled(a, ImVec2(a.x + scale, a.y + scale), colourOf(palette_, c));
      }
    }
    dl->AddRect(p, ImVec2(p.x + box, p.y + box),
                i == frame_ ? IM_COL32(255, 200, 0, 255) : IM_COL32(110, 110, 120, 255), 0.0f, 0,
                i == frame_ ? 2.0f : 1.0f);
    char label[16];
    std::snprintf(label, sizeof label, "%d", i + 1);
    dl->AddText(ImVec2(p.x + 3, p.y + 1), IM_COL32(220, 220, 220, 255), label);
    ImGui::PopID();
  }
}

void SpriteEditor::previewPixels(double now, std::vector<uint8_t>& rgba) const {
  rgba.assign(static_cast<size_t>(SCREEN_W * SCREEN_H * 4), 255);
  const size_t bo = static_cast<size_t>(backdrop_) * 3;
  for (size_t i = 0; i < rgba.size(); i += 4) {
    rgba[i] = palette_[bo];
    rgba[i + 1] = palette_[bo + 1];
    rgba[i + 2] = palette_[bo + 2];
  }
  if (!open_ || strip_.frames.empty()) return;
  const int n = static_cast<int>(strip_.frames.size());
  int shown = std::clamp(frame_, 0, n - 1);
  if (playing_ && n > 1) {
    const auto step = static_cast<long>(now * strip_.fps);
    if (pingPong_) {
      const long period = 2L * (n - 1);
      const long k = step % period;
      shown = static_cast<int>(k < n ? k : period - k);
    } else {
      shown = static_cast<int>(step % n);
    }
  }
  const sprite::Frame& f = strip_.frames[static_cast<size_t>(shown)];
  const int s = std::clamp(SCREEN_W / std::max(f.width, f.height), 1, std::max(previewScale_, 1));
  const int ox = (SCREEN_W - f.width * s) / 2, oy = (SCREEN_H - f.height * s) / 2;
  for (int y = 0; y < f.height * s; y++) {
    for (int x = 0; x < f.width * s; x++) {
      const uint8_t c = f.at(x / s, y / s);
      const int sx = ox + x, sy = oy + y;
      if (c == 0 || sx < 0 || sy < 0 || sx >= SCREEN_W || sy >= SCREEN_H) continue;
      const size_t o = static_cast<size_t>((sy * SCREEN_W + sx) * 4);
      const size_t po = static_cast<size_t>(c) * 3;
      rgba[o] = palette_[po];
      rgba[o + 1] = palette_[po + 1];
      rgba[o + 2] = palette_[po + 2];
    }
  }
}

}  // namespace sc8
