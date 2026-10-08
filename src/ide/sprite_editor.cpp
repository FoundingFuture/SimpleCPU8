#include "ide/sprite_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "imgui.h"

#include "assets/assets.h"
#include "assets/font.h"
#include "ide/ini_line.h"
#include "ide/panes.h"

namespace sc8 {

namespace {

constexpr size_t UNDO_LIMIT = 200;
constexpr ImU32 CHECK_A = IM_COL32(58, 58, 64, 255);
constexpr ImU32 CHECK_B = IM_COL32(82, 82, 90, 255);
constexpr ImU32 GRID = IM_COL32(0, 0, 0, 90);
constexpr ImU32 HOVER = IM_COL32(255, 255, 255, 200);
// Font mode's two pixels, the shade over what lies outside the cell, and
// the line at the cell's edge.
constexpr ImU32 INK = IM_COL32(236, 236, 228, 255);
constexpr ImU32 INK_OUTSIDE = IM_COL32(120, 120, 120, 255);
constexpr ImU32 PAPER = IM_COL32(34, 36, 44, 255);
constexpr ImU32 FONT_GRID = IM_COL32(255, 255, 255, 28);
constexpr ImU32 OUTSIDE = IM_COL32(150, 30, 30, 110);
constexpr ImU32 CELL_EDGE = IM_COL32(255, 140, 60, 255);
constexpr ImU32 PICKED = IM_COL32(255, 200, 0, 255);
constexpr ImU32 GROUPED = IM_COL32(90, 200, 255, 255);
// The font preview's text: palette white on palette black.
constexpr uint8_t PREVIEW_INK = 0xFF;
constexpr uint8_t PREVIEW_PAPER = 0x00;
constexpr int GLYPH_SIDE = 8;
constexpr int SHEET_SIDE = 16;  // codes a row of the glyph sheet
constexpr int LAST_CODE = gpu::GLYPHS - 1;
// The last first code of a strip of four, and of a 2 by 2 block.
constexpr int LAST_STRIP = gpu::GLYPHS - 4;
constexpr int LAST_BLOCK = gpu::GLYPHS - SHEET_SIDE - 2;
constexpr float MIN_ZOOM = 2.0f;
constexpr float MAX_ZOOM = 48.0f;

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
  static const char* names[] = {"Pencil", "Line", "Rect", "Ellipse", "Fill", "Gradient", "Roll", "Half circle"};
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
      "",
  };
  return help[t];
}

// Font mode draws set or clear pixels, and Shift draws the other one.
const char* fontToolHelp(int t) {
  static const char* help[] = {
      "P: click draws a pixel, a drag a stroke; Shift draws the other of set and clear",
      "L: drag from one end to the other; Shift draws the other of set and clear",
      "R: drag from corner to corner; Filled fills it; Shift draws the other of set and clear",
      "E: the ellipse in the box from corner to corner; Filled fills it; Shift draws the other",
      "F: fills the area of set or clear pixels under the click; Shift fills with the other",
      "",
      "O: drag sideways to roll the row, up or down to roll the column. Shift rolls the whole picture. "
      "Pixels that leave one side come back on the other",
      "H: half an ellipse in the box from corner to corner, its flat side on one edge. H again or Turn "
      "turns it. Filled fills it; Shift draws the other",
  };
  return help[t];
}

const char* facingName(sprite::Facing f) {
  static const char* names[] = {"up", "right", "down", "left"};
  return names[static_cast<int>(f)];
}

const char* fontViewKey(SpriteEditor::FontView v) {
  static const char* names[] = {"single", "strip", "block"};
  return names[static_cast<int>(v)];
}

// "$41 A" for a printable code, "$80" for any other.
std::string codeLabel(int code) {
  char b[16];
  if (code > ' ' && code < 0x7f) std::snprintf(b, sizeof b, "$%02X %c", code, code);
  else std::snprintf(b, sizeof b, "$%02X", code);
  return b;
}

// A glyph at `scale` screen units a pixel, paper behind it. Set pixels
// outside the cell show dim. A row's run of set pixels is one rectangle,
// which keeps the sheet's 256 glyphs well inside one draw list.
void drawGlyph(ImDrawList* dl, const sprite::Frame& f, ImVec2 o, float scale, int cellW, int cellH) {
  dl->AddRectFilled(o, ImVec2(o.x + static_cast<float>(f.width) * scale, o.y + static_cast<float>(f.height) * scale),
                    PAPER);
  for (int y = 0; y < f.height; y++) {
    const bool rowIn = y < cellH;
    for (int x = 0; x < f.width;) {
      if (f.at(x, y) == 0) {
        x++;
        continue;
      }
      const bool in = rowIn && x < cellW;
      int end = x + 1;
      while (end < f.width && f.at(end, y) != 0 && (rowIn && end < cellW) == in) end++;
      const ImVec2 a(o.x + static_cast<float>(x) * scale, o.y + static_cast<float>(y) * scale);
      dl->AddRectFilled(a, ImVec2(o.x + static_cast<float>(end) * scale, a.y + scale), in ? INK : INK_OUTSIDE);
      x = end;
    }
  }
}

// Put the next item, `width` wide, beside the last one when it fits in the
// window. Otherwise it starts the next line, so a narrow pane wraps its
// controls where it would clip them.
void flow(float width) {
  const ImGuiStyle& st = ImGui::GetStyle();
  const float right = ImGui::GetItemRectMax().x + st.ItemSpacing.x + width;
  if (right <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
}

float textWidth(const char* s) { return ImGui::CalcTextSize(s).x; }
float buttonWidth(const char* label) { return textWidth(label) + ImGui::GetStyle().FramePadding.x * 2.0f; }
// A checkbox or a radio button with its label.
float toggleWidth(const char* label) {
  return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + textWidth(label);
}
// An InputInt that shows `digits` digits beside its two step buttons.
float intFieldWidth(int digits) {
  const ImGuiStyle& st = ImGui::GetStyle();
  return textWidth("0") * static_cast<float>(digits) + st.FramePadding.x * 2.0f +
         (ImGui::GetFrameHeight() + st.ItemInnerSpacing.x) * 2.0f;
}
// A labelled field `width` wide: the field, then its label.
float labelledWidth(float width, const char* label) {
  return width + ImGui::GetStyle().ItemInnerSpacing.x + textWidth(label);
}

// A dim note after the last item, wrapped at the window's edge.
void hint(const char* text) {
  flow(textWidth(text));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled("%s", text);
  ImGui::PopTextWrapPos();
}

// The preview picture's pixel at (x, y) takes palette colour c.
void plot(std::vector<uint8_t>& rgba, const std::vector<uint8_t>& pal, int x, int y, uint8_t c) {
  if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
  const size_t o = static_cast<size_t>((y * SCREEN_W + x) * 4);
  const size_t po = static_cast<size_t>(c) * 3;
  rgba[o] = pal[po];
  rgba[o + 1] = pal[po + 1];
  rgba[o + 2] = pal[po + 2];
}

}  // namespace

std::optional<SpriteEditor::FontView> SpriteEditor::fontViewNamed(std::string_view name) {
  for (int i = 0; i < 3; i++) {
    if (name == fontViewKey(static_cast<FontView>(i))) return static_cast<FontView>(i);
  }
  return std::nullopt;
}

bool SpriteEditor::isFont(const std::string& name) { return name.ends_with(font::EXTENSION); }

SpriteEditor::SpriteEditor(Host host) : host_(std::move(host)), palette_(machinePalette()) {
  // A glyph is small, so font mode starts at a larger zoom.
  views_[static_cast<size_t>(Mode::Font)].zoom = 32.0f;
}

const std::array<SpriteEditor::Tool, SpriteEditor::TOOLS>& SpriteEditor::tools() const {
  // The shared tools keep their places. The half circle takes the
  // gradient's, so Roll stays last in both.
  static const std::array<Tool, TOOLS> spriteTools = {Tool::Pencil, Tool::Line,     Tool::Rect, Tool::Ellipse,
                                                      Tool::Fill,   Tool::Gradient, Tool::Roll};
  static const std::array<Tool, TOOLS> fontTools = {Tool::Pencil, Tool::Line,       Tool::Rect, Tool::Ellipse,
                                                    Tool::Fill,   Tool::HalfCircle, Tool::Roll};
  return mode_ == Mode::Font ? fontTools : spriteTools;
}

bool SpriteEditor::hasTool(Tool t) const {
  const auto& ts = tools();
  return std::find(ts.begin(), ts.end(), t) != ts.end();
}

bool SpriteEditor::open(const std::string& name, const std::string& saveAs) {
  const std::optional<std::vector<uint8_t>> bytes = host_.read(name);
  if (!bytes) {
    host_.note(name + ": cannot read it");
    return false;
  }
  std::string error;
  if (isFont(name)) {
    std::optional<font::Opened> opened = font::open(*bytes, &error);
    if (!opened) {
      host_.note(name + ": " + error);
      return false;
    }
    mode_ = Mode::Font;
    strip_ = std::move(opened->strip);
    cellW_ = opened->width;
    cellH_ = opened->height;
    savedText_ = font::save(strip_, cellW_, cellH_);
    name_ = name;
    dirty_ = false;
    pickCode('A');
  } else {
    std::optional<sprite::Loaded> loaded = sprite::load(*bytes, palette_, &error);
    if (!loaded) {
      host_.note(name + ": " + error);
      return false;
    }
    if (!loaded->note.empty()) host_.note(name + ": " + loaded->note + "; saving writes them that way");
    mode_ = Mode::Sprite;
    strip_ = std::move(loaded->strip);
    name_ = saveAs.empty() ? name : saveAs;
    frame_ = 0;
    dirty_ = !loaded->note.empty() || !saveAs.empty();
    newWidth_ = strip_.width;
    newHeight_ = strip_.height;
  }
  open_ = true;
  stroking_ = false;
  undo_.clear();
  redo_.clear();
  fit_ = view().keepZoom ? 0 : 3;
  view().keepZoom = false;
  return true;
}

void SpriteEditor::create(const std::string& name, int width, int height, int frames) {
  mode_ = Mode::Sprite;
  strip_ = sprite::blank(width, height, frames);
  name_ = name;
  frame_ = 0;
  open_ = true;
  dirty_ = true;
  stroking_ = false;
  undo_.clear();
  redo_.clear();
  newWidth_ = width;
  newHeight_ = height;
  fit_ = view().keepZoom ? 0 : 3;
  view().keepZoom = false;
}

void SpriteEditor::close() {
  open_ = false;
  dirty_ = false;
  onScreen_ = false;
  stroking_ = false;
  undo_.clear();
  redo_.clear();
}

std::string SpriteEditor::title() const {
  if (!open_) return "Sprite";
  return std::string(mode_ == Mode::Font ? "Font: " : "Sprite: ") + name_ + (dirty_ ? " *" : "");
}

void SpriteEditor::setFontView(FontView v) {
  if (fontView_ == v) return;
  fontView_ = v;
  pickCode(frame_);
  if (mode_ == Mode::Font) fit_ = 2;
}

bool SpriteEditor::save() {
  if (!open_) return true;
  if (mode_ == Mode::Font) {
    std::vector<uint8_t> text = font::save(strip_, cellW_, cellH_);
    if (text != savedText_) {
      if (!host_.write(name_, text)) {
        host_.note(name_ + ": cannot write it");
        return false;
      }
      savedText_ = std::move(text);
    }
    dirty_ = false;
    return true;
  }
  if (!host_.write(name_, sprite::encodePng(strip_, palette_))) {
    host_.note(name_ + ": cannot write it");
    return false;
  }
  dirty_ = false;
  return true;
}

std::string SpriteEditor::viewText() const {
  const View& sv = views_[static_cast<size_t>(Mode::Sprite)];
  const View& fv = views_[static_cast<size_t>(Mode::Font)];
  char buf[512];
  std::snprintf(buf, sizeof buf,
                "Sprite.Tool=%d\nSprite.Zoom=%d\nSprite.Grid=%d\nSprite.Onion=%d\nSprite.Filled=%d\n"
                "Sprite.Dither=%d\nSprite.Colours=%d,%d\nSprite.PreviewSize=%d\nSprite.Backdrop=%d\n"
                "Sprite.PingPong=%d\nSprite.OnScreen=%d\n"
                "Font.Tool=%d\nFont.Zoom=%d\nFont.Grid=%d\nFont.Onion=%d\nFont.Filled=%d\nFont.Ink=%d\n"
                "Font.Facing=%d\nFont.View=%s\nFont.Fps=%d\n",
                static_cast<int>(sv.tool), static_cast<int>(sv.zoom), sv.grid ? 1 : 0, sv.onion ? 1 : 0,
                sv.filled ? 1 : 0, dither_ ? 1 : 0, primary_, secondary_, previewScale_, backdrop_, pingPong_ ? 1 : 0,
                onScreen_ ? 1 : 0, static_cast<int>(fv.tool), static_cast<int>(fv.zoom), fv.grid ? 1 : 0,
                fv.onion ? 1 : 0, fv.filled ? 1 : 0, ink_ ? 1 : 0, static_cast<int>(facing_), fontViewKey(fontView_),
                fontFps_);
  return std::string(buf) + "Font.Sample=" + sample_ + "\n";
}

void SpriteEditor::setView(const std::string& line) {
  int a = 0, b = 0;
  const bool fontKey = line.starts_with("Font.");
  if (!fontKey && !line.starts_with("Sprite.")) return;
  const std::string key = line.substr(fontKey ? 5 : 7);
  View& v = views_[static_cast<size_t>(fontKey ? Mode::Font : Mode::Sprite)];
  if (readInts(key, "Tool", a)) {
    // A tool the mode lacks falls back to the pencil.
    const Mode was = mode_;
    mode_ = fontKey ? Mode::Font : Mode::Sprite;
    const auto tool = static_cast<Tool>(std::clamp(a, 0, static_cast<int>(Tool::HalfCircle)));
    v.tool = hasTool(tool) ? tool : Tool::Pencil;
    mode_ = was;
  } else if (readInts(key, "Zoom", a)) {
    // A saved zoom stands; the fit to the canvas is for a zoom not chosen.
    v.zoom = static_cast<float>(std::clamp(a, static_cast<int>(MIN_ZOOM), static_cast<int>(MAX_ZOOM)));
    v.keepZoom = true;
  } else if (readInts(key, "Grid", a)) {
    v.grid = a != 0;
  } else if (readInts(key, "Onion", a)) {
    v.onion = a != 0;
  } else if (readInts(key, "Filled", a)) {
    v.filled = a != 0;
  } else if (fontKey) {
    if (readInts(key, "Ink", a)) {
      ink_ = a != 0;
    } else if (readInts(key, "Facing", a)) {
      facing_ = static_cast<sprite::Facing>(std::clamp(a, 0, 3));
    } else if (key.starts_with("View=")) {
      if (const std::optional<FontView> fv = fontViewNamed(key.substr(5))) fontView_ = *fv;
    } else if (readInts(key, "Fps", a)) {
      fontFps_ = std::clamp(a, 1, 60);
    } else if (key.starts_with("Sample=")) {
      sample_ = key.substr(7);
    }
  } else if (readInts(key, "Dither", a)) {
    dither_ = a != 0;
  } else if (readInts(key, "Colours", a, b)) {
    primary_ = static_cast<uint8_t>(std::clamp(a, 0, 255));
    secondary_ = static_cast<uint8_t>(std::clamp(b, 0, 255));
  } else if (readInts(key, "PreviewSize", a)) {
    previewScale_ = std::clamp(a, 1, 4);
  } else if (readInts(key, "Backdrop", a)) {
    backdrop_ = static_cast<uint8_t>(std::clamp(a, 0, 255));
  } else if (readInts(key, "PingPong", a)) {
    pingPong_ = a != 0;
  } else if (readInts(key, "OnScreen", a)) {
    onScreen_ = a != 0;
  }
}

void SpriteEditor::pushUndo() {
  undo_.push_back({strip_, frame_, group_, cellW_, cellH_});
  if (undo_.size() > UNDO_LIMIT) undo_.erase(undo_.begin());
  redo_.clear();
}

void SpriteEditor::undo() {
  if (undo_.empty()) return;
  redo_.push_back({strip_, frame_, group_, cellW_, cellH_});
  Snapshot& s = undo_.back();
  strip_ = std::move(s.strip);
  frame_ = s.frame;
  group_ = s.group;
  cellW_ = s.cellW;
  cellH_ = s.cellH;
  undo_.pop_back();
  newWidth_ = strip_.width;
  newHeight_ = strip_.height;
  changed();
}

void SpriteEditor::redo() {
  if (redo_.empty()) return;
  undo_.push_back({strip_, frame_, group_, cellW_, cellH_});
  Snapshot& s = redo_.back();
  strip_ = std::move(s.strip);
  frame_ = s.frame;
  group_ = s.group;
  cellW_ = s.cellW;
  cellH_ = s.cellH;
  redo_.pop_back();
  newWidth_ = strip_.width;
  newHeight_ = strip_.height;
  changed();
}

std::array<int, 4> SpriteEditor::groupCodes() const {
  if (fontView_ == FontView::Block) return {group_, group_ + 1, group_ + SHEET_SIDE, group_ + SHEET_SIDE + 1};
  return {group_, group_ + 1, group_ + 2, group_ + 3};
}

void SpriteEditor::pickCode(int code) {
  frame_ = std::clamp(code, 0, LAST_CODE);
  switch (fontView_) {
    case FontView::Single:
      group_ = frame_;
      break;
    case FontView::Strip:
      // A code already in the strip keeps the strip where it is.
      if (frame_ < group_ || frame_ > group_ + 3) group_ = std::min(frame_, LAST_STRIP);
      break;
    case FontView::Block:
      // The block's top left, moved in so that all four codes exist.
      group_ = frame_;
      if (group_ % SHEET_SIDE == SHEET_SIDE - 1) group_--;
      if (group_ > LAST_BLOCK) group_ -= SHEET_SIDE;
      break;
  }
}

void SpriteEditor::stepCode(int d) {
  switch (fontView_) {
    case FontView::Single:
      pickCode((frame_ + d + gpu::GLYPHS) % gpu::GLYPHS);
      break;
    case FontView::Strip:
      frame_ = group_ + (frame_ - group_ + d + 4) % 4;
      break;
    case FontView::Block: {
      // The next block to the right or left, over the codes a block can
      // start at.
      int g = group_;
      do g = (g + d + gpu::GLYPHS) % gpu::GLYPHS;
      while (g % SHEET_SIDE == SHEET_SIDE - 1 || g > LAST_BLOCK);
      group_ = frame_ = g;
      break;
    }
  }
}

bool SpriteEditor::outsideCell(int x, int y) const { return x % GLYPH_SIDE >= cellW_ || y % GLYPH_SIDE >= cellH_; }

void SpriteEditor::loadBlock() {
  const std::array<int, 4> c = groupCodes();
  const auto& fs = strip_.frames;
  block_ = sprite::joinBlock(fs[static_cast<size_t>(c[0])], fs[static_cast<size_t>(c[1])],
                             fs[static_cast<size_t>(c[2])], fs[static_cast<size_t>(c[3])]);
}

void SpriteEditor::storeBlock() {
  if (!blockView()) return;
  const std::array<int, 4> c = groupCodes();
  auto& fs = strip_.frames;
  sprite::splitBlock(block_, fs[static_cast<size_t>(c[0])], fs[static_cast<size_t>(c[1])],
                     fs[static_cast<size_t>(c[2])], fs[static_cast<size_t>(c[3])]);
}

void SpriteEditor::body() {
  if (!open_) {
    ImGui::TextWrapped("Nothing is open. Double-click a picture or a .font under Assets in the Files pane, or "
                       "use New sprite... or New font... there.");
    return;
  }
  frame_ = std::clamp(frame_, 0, static_cast<int>(strip_.frames.size()) - 1);
  if (!hasTool(view().tool)) view().tool = Tool::Pencil;
  shortcuts();
  toolbar();
  if (mode_ == Mode::Font) {
    fontBody();
    return;
  }

  // The canvas on the left, the colours and the frame tools in a column
  // on the right, the frames along the bottom.
  const float side = std::min(ImGui::GetFontSize() * 22.0f, ImGui::GetContentRegionAvail().x * 0.45f);
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
  frameOps("This frame");
  // What follows the palette, so the next frame sizes the palette to leave
  // room for it.
  paletteTail_ = ImGui::GetCursorPosY() - paletteEnd_;
  ImGui::EndChild();
  ImGui::BeginChild("##frames", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
  frameStrip();
  ImGui::EndChild();
}

void SpriteEditor::fontBody() {
  // The canvas on the left, with the four cells and the glyph tools under
  // it. The set and clear buttons, the cell and the glyph sheet go on the
  // right.
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float em = ImGui::GetFontSize();
  const float side = std::clamp(avail.x * 0.5f, em * 15.0f, em * 36.0f);
  const float leftWidth = std::max(120.0f, avail.x - side - ImGui::GetStyle().ItemSpacing.x);
  // The cells pane is as tall as its content was last frame, so wrapped
  // buttons never need a scrollbar.
  const float bottom = std::max(cellsHeight_, ImGui::GetFrameHeightWithSpacing() * 3.0f);
  ImGui::BeginChild("##left", ImVec2(leftWidth, 0), ImGuiChildFlags_None);
  ImGui::BeginChild("##canvas", ImVec2(0, std::max(120.0f, ImGui::GetContentRegionAvail().y - bottom)),
                    ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
  canvas();
  ImGui::EndChild();
  ImGui::BeginChild("##cells", ImVec2(0, 0), ImGuiChildFlags_Borders);
  fourCells();
  frameOps(blockView() ? "The block" : "This glyph");
  cellsHeight_ = ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y;
  ImGui::EndChild();
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##side", ImVec2(0, 0), ImGuiChildFlags_None);
  inkBox();
  cellBox();
  glyphSheet();
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
  Tool& tool = view().tool;
  if (ImGui::IsKeyPressed(ImGuiKey_P)) tool = Tool::Pencil;
  if (ImGui::IsKeyPressed(ImGuiKey_L)) tool = Tool::Line;
  if (ImGui::IsKeyPressed(ImGuiKey_R)) tool = Tool::Rect;
  if (ImGui::IsKeyPressed(ImGuiKey_E)) tool = Tool::Ellipse;
  if (ImGui::IsKeyPressed(ImGuiKey_F)) tool = Tool::Fill;
  if (ImGui::IsKeyPressed(ImGuiKey_O)) tool = Tool::Roll;
  if (ImGui::IsKeyPressed(ImGuiKey_Space)) playing_ = !playing_;
  if (mode_ == Mode::Font) {
    if (ImGui::IsKeyPressed(ImGuiKey_H)) {
      // H picks the half circle, and H again turns it.
      if (tool == Tool::HalfCircle) facing_ = static_cast<sprite::Facing>((static_cast<int>(facing_) + 1) % 4);
      tool = Tool::HalfCircle;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_X)) ink_ = !ink_;
    if (ImGui::IsKeyPressed(ImGuiKey_Comma)) stepCode(-1);
    if (ImGui::IsKeyPressed(ImGuiKey_Period)) stepCode(1);
    return;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_G)) tool = Tool::Gradient;
  if (ImGui::IsKeyPressed(ImGuiKey_X)) std::swap(primary_, secondary_);
  const int n = static_cast<int>(strip_.frames.size());
  if (ImGui::IsKeyPressed(ImGuiKey_Comma)) frame_ = (frame_ + n - 1) % n;
  if (ImGui::IsKeyPressed(ImGuiKey_Period)) frame_ = (frame_ + 1) % n;
}

void SpriteEditor::toolbar() {
  const bool fontMode = mode_ == Mode::Font;
  View& v = view();
  ImGui::Text("%s%s", name_.c_str(), dirty_ ? " *" : "");
  char info[64];
  if (fontMode) {
    std::snprintf(info, sizeof info, "cell %d x %d, 256 glyphs", cellW_, cellH_);
  } else {
    std::snprintf(info, sizeof info, "%d x %d, %zu frame(s)", strip_.width, strip_.height, strip_.frames.size());
  }
  hint(info);
  flow(buttonWidth("Save"));
  if (ImGui::SmallButton("Save")) {
    if (save()) host_.note("saved " + name_);
  }
  flow(buttonWidth("Undo"));
  ImGui::BeginDisabled(undo_.empty());
  if (ImGui::SmallButton("Undo")) undo();
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Ctrl+Z");
  flow(buttonWidth("Redo"));
  ImGui::BeginDisabled(redo_.empty());
  if (ImGui::SmallButton("Redo")) redo();
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Ctrl+Shift+Z or Ctrl+Y");

  bool first = true;
  for (Tool t : tools()) {
    const int i = static_cast<int>(t);
    if (!first) flow(toggleWidth(toolName(i)));
    first = false;
    if (ImGui::RadioButton(toolName(i), v.tool == t)) v.tool = t;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fontMode ? fontToolHelp(i) : toolHelp(i));
  }
  ImGui::Checkbox("Filled", &v.filled);
  if (fontMode) {
    char turn[32];
    std::snprintf(turn, sizeof turn, "Turn: round %s", facingName(facing_));
    flow(buttonWidth(turn));
    if (ImGui::SmallButton(turn)) {
      facing_ = static_cast<sprite::Facing>((static_cast<int>(facing_) + 1) % 4);
      v.tool = Tool::HalfCircle;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("the half circle's round side; H with the tool picked turns it too");
  } else {
    flow(toggleWidth("Dither"));
    ImGui::Checkbox("Dither", &dither_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("the gradient mixes neighbouring colours in a fine pattern");
  }
  flow(toggleWidth("Grid"));
  ImGui::Checkbox("Grid", &v.grid);
  flow(toggleWidth("Onion skin"));
  ImGui::Checkbox("Onion skin", &v.onion);
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(fontMode ? "the glyph before this one shows faintly where this one is clear"
                               : "the frame before this one shows faintly where this one is transparent");
  }
  const float zoomWidth = textWidth("00 x") * 2.5f;
  flow(labelledWidth(zoomWidth, "Zoom"));
  ImGui::SetNextItemWidth(zoomWidth);
  ImGui::SliderFloat("Zoom", &v.zoom, MIN_ZOOM, MAX_ZOOM, "%.0f x");
  flow(buttonWidth("Fit"));
  if (ImGui::SmallButton("Fit")) fit_ = 1;
  const int t = static_cast<int>(v.tool);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled("%s. A right click picks up %s.", fontMode ? fontToolHelp(t) : toolHelp(t),
                      fontMode ? "set or clear" : "a colour");
  ImGui::PopTextWrapPos();
}

void SpriteEditor::canvas() {
  const bool fontMode = mode_ == Mode::Font;
  if (blockView() && !stroking_) loadBlock();
  const sprite::Frame& f = target();
  View& v = view();
  const ImVec2 room = ImGui::GetContentRegionAvail();
  if (fit_ > 0) {
    // The largest whole zoom at which the frame fits the canvas.
    fit_--;
    v.zoom = std::clamp(std::floor(std::min(room.x / static_cast<float>(f.width), room.y / static_cast<float>(f.height))),
                        MIN_ZOOM, MAX_ZOOM);
  }
  const float cell = std::round(v.zoom);
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
    base_ = target();
    startX_ = lastX_ = px;
    startY_ = lastY_ = py;
    startMouseX_ = mx;
    startMouseY_ = my;
    rollAxis_ = erasing_ && v.tool == Tool::Roll ? 3 : 0;
    strokeTo(px, py, mx, my);
  } else if (stroking_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    strokeTo(px, py, mx, my);
  }
  if (stroking_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    stroking_ = false;
    // A stroke that changed nothing leaves no undo step behind.
    if (target() == base_ && !undo_.empty()) undo_.pop_back();
    else changed();
  }
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && f.inside(px, py)) {
    if (fontMode) ink_ = f.at(px, py) != 0;
    else primary_ = f.at(px, py);
  }

  ImDrawList* dl = ImGui::GetWindowDrawList();
  // The onion skin is the frame before, or the code before. The block has
  // four glyphs and no one glyph before it.
  const int n = static_cast<int>(strip_.frames.size());
  const sprite::Frame* before =
      v.onion && n > 1 && !blockView() ? &strip_.frames[static_cast<size_t>((frame_ + n - 1) % n)] : nullptr;
  for (int y = 0; y < f.height; y++) {
    for (int x = 0; x < f.width; x++) {
      const ImVec2 a(origin.x + static_cast<float>(x) * cell, origin.y + static_cast<float>(y) * cell);
      const ImVec2 b(a.x + cell, a.y + cell);
      const uint8_t c = f.at(x, y);
      if (fontMode) {
        dl->AddRectFilled(a, b, c != 0 ? INK : PAPER);
        if (c == 0 && before && before->at(x, y) != 0) dl->AddRectFilled(a, b, IM_COL32(236, 236, 228, 60));
        if (outsideCell(x, y)) dl->AddRectFilled(a, b, OUTSIDE);
        continue;
      }
      if (c != 0) {
        dl->AddRectFilled(a, b, colourOf(palette_, c));
        continue;
      }
      dl->AddRectFilled(a, b, ((x + y) & 1) ? CHECK_A : CHECK_B);
      if (before && before->at(x, y) != 0) dl->AddRectFilled(a, b, colourOf(palette_, before->at(x, y), 80));
    }
  }
  if (v.grid && cell >= 6.0f) {
    const ImU32 grid = fontMode ? FONT_GRID : GRID;
    for (int x = 0; x <= f.width; x++) {
      const float gx = origin.x + static_cast<float>(x) * cell;
      dl->AddLine(ImVec2(gx, origin.y), ImVec2(gx, origin.y + size.y), grid);
    }
    for (int y = 0; y <= f.height; y++) {
      const float gy = origin.y + static_cast<float>(y) * cell;
      dl->AddLine(ImVec2(origin.x, gy), ImVec2(origin.x + size.x, gy), grid);
    }
  }
  if (fontMode) {
    // Each glyph's cell outlined, so the part the GPU draws stands out.
    for (int gy = 0; gy < f.height; gy += GLYPH_SIDE) {
      for (int gx = 0; gx < f.width; gx += GLYPH_SIDE) {
        const ImVec2 a(origin.x + static_cast<float>(gx) * cell, origin.y + static_cast<float>(gy) * cell);
        dl->AddRect(a, ImVec2(a.x + static_cast<float>(cellW_) * cell, a.y + static_cast<float>(cellH_) * cell),
                    CELL_EDGE, 0.0f, 0, 2.0f);
      }
    }
    if (blockView()) {
      const float mid = static_cast<float>(GLYPH_SIDE) * cell;
      dl->AddLine(ImVec2(origin.x + mid, origin.y), ImVec2(origin.x + mid, origin.y + size.y), GROUPED, 1.0f);
      dl->AddLine(ImVec2(origin.x, origin.y + mid), ImVec2(origin.x + size.x, origin.y + mid), GROUPED, 1.0f);
    }
  }
  dl->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(140, 140, 150, 255));
  if (hovered && f.inside(px, py)) {
    const ImVec2 a(origin.x + static_cast<float>(px) * cell, origin.y + static_cast<float>(py) * cell);
    dl->AddRect(a, ImVec2(a.x + cell, a.y + cell), HOVER, 0.0f, 0, 2.0f);
    if (fontMode) {
      const int code = blockView() ? groupCodes()[static_cast<size_t>((py / GLYPH_SIDE) * 2 + px / GLYPH_SIDE)] : frame_;
      ImGui::SetTooltip("%d, %d  %s  %s%s", px % GLYPH_SIDE, py % GLYPH_SIDE, f.at(px, py) ? "set" : "clear",
                        codeLabel(code).c_str(), outsideCell(px, py) ? "  outside the cell" : "");
    } else {
      ImGui::SetTooltip("%d, %d  colour %d", px, py, f.at(px, py));
    }
  }
}

void SpriteEditor::strokeTo(int x, int y, float mouseX, float mouseY) {
  sprite::Frame& f = target();
  const View& v = view();
  uint8_t c = erasing_ ? 0 : primary_;
  if (mode_ == Mode::Font) c = ink_ != erasing_ ? font::INK : 0;
  switch (v.tool) {
    case Tool::Pencil:
      sprite::line(f, lastX_, lastY_, x, y, c);
      break;
    case Tool::Line:
      f = base_;
      sprite::line(f, startX_, startY_, x, y, c);
      break;
    case Tool::Rect:
      f = base_;
      sprite::rect(f, startX_, startY_, x, y, c, v.filled);
      break;
    case Tool::Ellipse:
      f = base_;
      sprite::ellipse(f, startX_, startY_, x, y, c, v.filled);
      break;
    case Tool::HalfCircle:
      f = base_;
      sprite::halfCircle(f, startX_, startY_, x, y, facing_, c, v.filled);
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
      const float cell = std::round(v.zoom);
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
  storeBlock();
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
  hint("0 is transparent");
  ImGui::EndGroup();

  // The 256 colours, 16 a row. A left click picks the drawing colour, a
  // right click the second. The palette gives up height so that the
  // controls under it fit the column.
  const float room = ImGui::GetContentRegionAvail().y - paletteTail_;
  const float cell = std::max(4.0f, std::floor(std::min({width, 320.0f, room}) / 16.0f));
  p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##palette", ImVec2(cell * 16, cell * 16),
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  paletteEnd_ = ImGui::GetCursorPosY();
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
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled("left click: draw colour, right click: second");
  ImGui::PopTextWrapPos();
}

void SpriteEditor::inkBox() {
  // Set and clear in place of the palette: what the tools draw.
  auto choice = [&](const char* label, bool set, const char* tip) {
    const bool on = ink_ == set;
    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button(label, ImVec2(std::max(80.0f, buttonWidth(label)), 0))) ink_ = set;
    if (on) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
  };
  choice("Set", true, "the tools set pixels; X swaps");
  flow(std::max(80.0f, buttonWidth("Clear")));
  choice("Clear", false, "the tools clear pixels; X swaps");
  hint("Shift draws the other");
}

void SpriteEditor::cellBox() {
  ImGui::SeparatorText("Cell");
  int w = cellW_, h = cellH_;
  const float field = intFieldWidth(1);
  ImGui::SetNextItemWidth(field);
  const bool wChanged = ImGui::InputInt("W", &w, 1, 1);
  flow(labelledWidth(field, "H"));
  ImGui::SetNextItemWidth(field);
  const bool hChanged = ImGui::InputInt("H", &h, 1, 1);
  w = std::clamp(w, gpu::TEXT_CELL_MIN, gpu::TEXT_CELL_MAX);
  h = std::clamp(h, gpu::TEXT_CELL_MIN, gpu::TEXT_CELL_MAX);
  if ((wChanged || hChanged) && (w != cellW_ || h != cellH_)) {
    pushUndo();
    cellW_ = w;
    cellH_ = h;
    changed();
  }
  char grid[32];
  std::snprintf(grid, sizeof grid, "%d x %d characters", SCREEN_W / cellW_, SCREEN_H / cellH_);
  hint(grid);
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("the part of each glyph the GPU draws, from the top left, %d to %d each way. The rest is "
                      "shaded and kept",
                      gpu::TEXT_CELL_MIN, gpu::TEXT_CELL_MAX);
  }
}

void SpriteEditor::glyphSheet() {
  ImGui::SeparatorText("Glyphs");
  // The view picker sits in the sheet's toolbar.
  static const char* labels[] = {"Single", "Strip of 4", "2 x 2 block"};
  static const char* tips[] = {"edit one glyph",
                               "four codes in a row as the frames of a strip; the preview plays them",
                               "codes n, n+1, n+16 and n+17 as one 16 by 16 picture"};
  for (int i = 0; i < 3; i++) {
    if (i) flow(toggleWidth(labels[i]));
    if (ImGui::RadioButton(labels[i], fontView_ == static_cast<FontView>(i))) setFontView(static_cast<FontView>(i));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tips[i]);
  }

  // 16 codes a row, each glyph with its code in hex under it.
  ImGui::BeginChild("##sheet", ImVec2(0, 0), ImGuiChildFlags_Borders);
  const float width = ImGui::GetContentRegionAvail().x;
  const float label = ImGui::GetTextLineHeight();
  const float box = std::max(18.0f, std::floor(width / static_cast<float>(SHEET_SIDE)));
  const float scale = std::max(1.0f, std::floor((box - 4.0f) / static_cast<float>(GLYPH_SIDE)));
  const float glyph = scale * static_cast<float>(GLYPH_SIDE);
  const float rowH = glyph + label + 4.0f;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##codes", ImVec2(box * SHEET_SIDE, rowH * SHEET_SIDE));
  const ImVec2 m = ImGui::GetIO().MousePos;
  const int hx = static_cast<int>(std::floor((m.x - p.x) / box)), hy = static_cast<int>(std::floor((m.y - p.y) / rowH));
  if (ImGui::IsItemHovered() && hx >= 0 && hx < SHEET_SIDE && hy >= 0 && hy < SHEET_SIDE) {
    const int code = hy * SHEET_SIDE + hx;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) pickCode(code);
    ImGui::SetTooltip("%s, %d", codeLabel(code).c_str(), code);
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const std::array<int, 4> group = groupCodes();
  for (int code = 0; code <= LAST_CODE; code++) {
    const ImVec2 cellAt(p.x + static_cast<float>(code % SHEET_SIDE) * box,
                        p.y + static_cast<float>(code / SHEET_SIDE) * rowH);
    const ImVec2 o(cellAt.x + (box - glyph) * 0.5f, cellAt.y + 1.0f);
    drawGlyph(dl, strip_.frames[static_cast<size_t>(code)], o, scale, cellW_, cellH_);
    const bool inGroup = fontView_ != FontView::Single && std::find(group.begin(), group.end(), code) != group.end();
    if (code == frame_ || inGroup) {
      dl->AddRect(ImVec2(o.x - 1, o.y - 1), ImVec2(o.x + glyph + 1, o.y + glyph + 1),
                  code == frame_ ? PICKED : GROUPED, 0.0f, 0, 2.0f);
    }
    char hex[4];
    std::snprintf(hex, sizeof hex, "%02X", code);
    const float tw = ImGui::CalcTextSize(hex).x;
    dl->AddText(ImVec2(cellAt.x + (box - tw) * 0.5f, o.y + glyph + 1.0f),
                code == frame_ ? PICKED : IM_COL32(150, 150, 160, 255), hex);
  }
  ImGui::EndChild();
}

void SpriteEditor::fourCells() {
  // Under the canvas: the code being edited, and the strip's four frames
  // or the block's four codes.
  switch (fontView_) {
    case FontView::Single:
      ImGui::Text("%s, %d", codeLabel(frame_).c_str(), frame_);
      hint(", and . step the code; a click in the sheet picks one");
      break;
    case FontView::Strip:
      ImGui::Text("Codes $%02X to $%02X, editing %s", group_, group_ + 3, codeLabel(frame_).c_str());
      hint(", and . step; the preview plays the four");
      break;
    case FontView::Block:
      ImGui::Text("Block $%02X $%02X / $%02X $%02X", group_, group_ + 1, group_ + SHEET_SIDE, group_ + SHEET_SIDE + 1);
      hint(", and . move the block");
      break;
  }
  if (fontView_ != FontView::Strip) return;
  // The four frames, each a click to edit.
  const float scale = 6.0f;
  const float side = scale * static_cast<float>(GLYPH_SIDE);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  for (int i = 0; i < 4; i++) {
    const int code = group_ + i;
    if (i) ImGui::SameLine();
    ImGui::PushID(i);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##frame", ImVec2(side + 8.0f, side + 8.0f))) frame_ = code;
    drawGlyph(dl, strip_.frames[static_cast<size_t>(code)], ImVec2(p.x + 4.0f, p.y + 4.0f), scale, cellW_, cellH_);
    dl->AddRect(p, ImVec2(p.x + side + 8.0f, p.y + side + 8.0f), code == frame_ ? PICKED : IM_COL32(110, 110, 120, 255),
                0.0f, 0, code == frame_ ? 2.0f : 1.0f);
    ImGui::PopID();
  }
}

void SpriteEditor::previewBody(DisplaySettings& display) {
  previewShown_ = false;
  if (!open_) {
    ImGui::TextDisabled("nothing is open");
    return;
  }
  if (mode_ == Mode::Font) {
    fontPreviewBody();
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

void SpriteEditor::fontPreviewBody() {
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float side = std::max(64.0f, std::min(avail.x, avail.y - ImGui::GetFrameHeightWithSpacing() * 3.0f));
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - side) * 0.5f));
  panes::previewTarget().show(side);
  previewShown_ = true;
  ImGui::SetNextItemWidth(-1.0f);
  panes::inputLine("##sample", sample_, "a line of text to set in the font");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("the sample line, set at the cell's size as the machine draws it");
  if (fontView_ == FontView::Strip) {
    if (ImGui::Button(playing_ ? "Pause" : "Play")) playing_ = !playing_;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Space. Paused, the preview shows the glyph being edited");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::SliderInt("fps", &fontFps_, 1, 60);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("frames per second of the strip of four; kept in the layout");
  }
  ImGui::TextDisabled("the machine's pixels, no screen effect");
}

void SpriteEditor::sizeBox() {
  ImGui::SeparatorText("Frame size");
  const float field = intFieldWidth(2);
  ImGui::SetNextItemWidth(field);
  ImGui::InputInt("W", &newWidth_, 1, 8);
  flow(labelledWidth(field, "H"));
  ImGui::SetNextItemWidth(field);
  ImGui::InputInt("H", &newHeight_, 1, 8);
  newWidth_ = std::clamp(newWidth_, 1, sprite::MAX_SIDE);
  newHeight_ = std::clamp(newHeight_, 1, sprite::MAX_SIDE);
  flow(buttonWidth("Resize"));
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
}

void SpriteEditor::frameOps(const char* title) {
  ImGui::SeparatorText(title);
  bool first = true;
  auto act = [&](const char* label, const char* tip, auto fn) {
    if (!first) flow(buttonWidth(label));
    first = false;
    if (ImGui::SmallButton(label)) {
      pushUndo();
      if (blockView()) loadBlock();
      fn(target());
      storeBlock();
      changed();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
  };
  act("Roll <", "every row one pixel left, wrapping round", [](sprite::Frame& f) { sprite::roll(f, -1, 0); });
  act("Roll >", "every row one pixel right, wrapping round", [](sprite::Frame& f) { sprite::roll(f, 1, 0); });
  act("Roll ^", "every column one pixel up, wrapping round", [](sprite::Frame& f) { sprite::roll(f, 0, -1); });
  act("Roll v", "every column one pixel down, wrapping round", [](sprite::Frame& f) { sprite::roll(f, 0, 1); });
  act("Flip H", "mirror left to right", [](sprite::Frame& f) { sprite::flipH(f); });
  act("Flip V", "mirror top to bottom", [](sprite::Frame& f) { sprite::flipV(f); });
  act("Clear", mode_ == Mode::Font ? "every pixel clear" : "every pixel transparent",
      [](sprite::Frame& f) { std::fill(f.px.begin(), f.px.end(), uint8_t{0}); });
}

void SpriteEditor::frameStrip() {
  const int n = static_cast<int>(strip_.frames.size());
  const bool full = n >= sprite::MAX_FRAMES;
  auto frameOp = [&](const char* label, bool enabled, const char* tip, auto fn) {
    flow(buttonWidth(label));
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
  char limit[48];
  std::snprintf(limit, sizeof limit, "at most %d frames; , and . step", sprite::MAX_FRAMES);
  hint(limit);

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
  const uint8_t back = mode_ == Mode::Font ? PREVIEW_PAPER : backdrop_;
  rgba.assign(static_cast<size_t>(SCREEN_W * SCREEN_H * 4), 255);
  const size_t bo = static_cast<size_t>(back) * 3;
  for (size_t i = 0; i < rgba.size(); i += 4) {
    rgba[i] = palette_[bo];
    rgba[i + 1] = palette_[bo + 1];
    rgba[i + 2] = palette_[bo + 2];
  }
  if (!open_ || strip_.frames.empty()) return;

  if (mode_ == Mode::Font) {
    // Text set as the GPU sets it. Each glyph is cut to the cell, and the
    // cells sit side by side on the grid, one machine pixel a font pixel.
    const int cols = SCREEN_W / cellW_, rows = SCREEN_H / cellH_;
    auto put = [&](int col, int row, int code) {
      if (col >= cols || row >= rows) return;
      const sprite::Frame& g = strip_.frames[static_cast<size_t>(code & LAST_CODE)];
      for (int y = 0; y < cellH_; y++) {
        for (int x = 0; x < cellW_; x++) {
          if (g.at(x, y) != 0) plot(rgba, palette_, col * cellW_ + x, row * cellH_ + y, PREVIEW_INK);
        }
      }
    };
    // The sample line, wrapped at the grid's width.
    int row = 1;
    int col = 0;
    for (char ch : sample_) {
      put(col, row, static_cast<unsigned char>(ch));
      if (++col == cols) {
        col = 0;
        row++;
      }
    }
    row += 2;
    // The glyphs being edited: the one, the strip playing in the first cell
    // and still beside it, or the block.
    const std::array<int, 4> g = groupCodes();
    switch (fontView_) {
      case FontView::Single:
        put(0, row, frame_);
        break;
      case FontView::Strip: {
        const int shown = playing_ ? group_ + static_cast<int>(static_cast<long>(now * fontFps_) % 4) : frame_;
        put(0, row, shown);
        for (int i = 0; i < 4; i++) put(2 + i, row, g[static_cast<size_t>(i)]);
        break;
      }
      case FontView::Block:
        put(0, row, g[0]);
        put(1, row, g[1]);
        put(0, row + 1, g[2]);
        put(1, row + 1, g[3]);
        row++;
        break;
    }
    row += 2;
    // Every code, 32 to a line where the grid is that wide.
    const int perLine = std::min(cols, 32);
    for (int code = 0; code <= LAST_CODE; code++) put(code % perLine, row + code / perLine, code);
    return;
  }

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
      if (c != 0) plot(rgba, palette_, ox + x, oy + y, c);
    }
  }
}

}  // namespace sc8
