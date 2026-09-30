// The sprite editor draws one sprite strip from the project's assets. It
// works pixel by pixel on the machine palette, frame by frame, with an
// animated preview through the screen's own display effects.
//
// A .font opens the same editor in font mode. It shows 256 one-bit glyphs
// of 8 by 8, the cell the GPU draws of each, a glyph sheet and a preview of
// text set in the font. The asset decides the mode. There is no toggle.
//
// The drawing tools are the functions of assets/sprite.h. SpriteEditor is
// the pane around them. It reads and writes the asset through the host,
// so a folder project and a project held in a ROM behave the same.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "assets/sprite.h"
#include "vm/display.h"

namespace sc8 {

class SpriteEditor {
 public:
  struct Host {
    std::function<std::optional<std::vector<uint8_t>>(const std::string&)> read;
    std::function<bool(const std::string&, const std::vector<uint8_t>&)> write;
    std::function<void(std::string)> note;
  };

  enum class Mode { Sprite, Font };
  // How font mode shows the codes. Single is one glyph. Strip is four
  // consecutive codes as frames. Block is codes n, n+1, n+16 and n+17
  // edited as one 16 by 16 picture.
  enum class FontView { Single, Strip, Block };
  static std::optional<FontView> fontViewNamed(std::string_view name);
  // A .font opens in font mode, anything else as a sprite.
  static bool isFont(const std::string& name);

  explicit SpriteEditor(Host host);

  // Open an asset. False, with a note, for a picture that is unreadable or
  // too large for a sprite, and for a font the reader refuses. `saveAs`
  // names the asset Save writes, when it is not the one read: a .bmp is
  // saved as a .png beside it.
  bool open(const std::string& name, const std::string& saveAs = "");
  // A new strip of transparent frames, not saved until Save.
  void create(const std::string& name, int width, int height, int frames);
  void close();

  bool isOpen() const { return open_; }
  bool dirty() const { return open_ && dirty_; }
  const std::string& name() const { return name_; }
  Mode mode() const { return mode_; }
  // "Sprite: ship.png" or "Font: small.font", a star when dirty, and
  // "Sprite" with nothing open.
  std::string title() const;
  void setFontView(FontView v);

  // Write the strip or the font to its asset. False when the host could
  // not. A font that still gives the text it was opened with is not
  // written, so an open and a save leave the file alone.
  bool save();

  // The pane's body, between Begin and End.
  void body();
  // The preview pane's body. `display` is the screen's own settings: the
  // preview uses them, and changing the effect there changes the Screen
  // pane too.
  void previewBody(DisplaySettings& display);
  // The sprite preview goes through the screen's effects. The font
  // preview shows the machine's pixels as they are.
  bool previewEffects() const { return mode_ == Mode::Sprite; }

  // The preview picture, 256 x 256 RGBA, at the time given in seconds.
  // Playing, it steps through the frames at the strip's rate. Paused, it
  // shows the frame being edited. A font shows text set in it.
  void previewPixels(double now, std::vector<uint8_t>& rgba) const;
  // The preview runs on the Screen pane instead of the machine's picture.
  bool previewOnScreen() const { return open_ && mode_ == Mode::Sprite && onScreen_; }
  // The editor's view as key=value lines for the layout file, each key
  // prefixed Sprite. or Font. for the mode it belongs to. setView reads
  // one line back and skips a line it does not know.
  std::string viewText() const;
  void setView(const std::string& line);
  // The preview picture is shown in the pane this frame.
  bool previewShown() const { return previewShown_; }
  // A preview in view is playing its frames, so it changes without input.
  bool animating() const { return open_ && playing_ && (previewShown_ || previewOnScreen()); }

 private:
  enum class Tool { Pencil, Line, Rect, Ellipse, Fill, Gradient, Roll, HalfCircle };
  static constexpr int TOOLS = 7;  // a mode's toolbar

  // What each mode keeps of the view on its own.
  struct View {
    Tool tool = Tool::Pencil;
    float zoom = 16.0f;
    bool grid = true;
    bool onion = false;
    bool filled = false;
    // A zoom read from the layout holds for the first asset opened after.
    bool keepZoom = false;
  };

  struct Snapshot {
    sprite::Strip strip;
    int frame = 0;
    int group = 0;
    int cellW = 0, cellH = 0;
  };

  View& view() { return views_[static_cast<size_t>(mode_)]; }
  const View& view() const { return views_[static_cast<size_t>(mode_)]; }
  const std::array<Tool, TOOLS>& tools() const;
  bool hasTool(Tool t) const;
  sprite::Frame& frame() { return strip_.frames[static_cast<size_t>(frame_)]; }
  // The picture the canvas edits: the frame, or in the block view the
  // four glyphs joined. storeBlock splits it back after every change.
  sprite::Frame& target() { return blockView() ? block_ : frame(); }
  bool blockView() const { return mode_ == Mode::Font && fontView_ == FontView::Block; }
  void loadBlock();
  void storeBlock();
  void pushUndo();
  void undo();
  void redo();
  void changed() { dirty_ = true; }

  void toolbar();
  void canvas();
  void paletteBox();
  void frameStrip();
  void sizeBox();
  void frameOps(const char* title);
  void shortcuts();
  // The tool acting on the picture, from the stroke's start to pixel (x, y).
  void strokeTo(int x, int y, float mouseX, float mouseY);

  // Font mode.
  void fontBody();
  void inkBox();
  void cellBox();
  void glyphSheet();
  void fourCells();
  void fontPreviewBody();
  // The four codes of the strip or the block, from group_.
  std::array<int, 4> groupCodes() const;
  // Edit `code`, moving the group so that it holds the code.
  void pickCode(int code);
  void stepCode(int d);
  bool outsideCell(int x, int y) const;

  Host host_;
  std::vector<uint8_t> palette_;
  bool open_ = false;
  bool dirty_ = false;
  Mode mode_ = Mode::Sprite;
  std::string name_;
  sprite::Strip strip_;
  int frame_ = 0;

  std::array<View, 2> views_{};
  uint8_t primary_ = 0xFF;
  uint8_t secondary_ = 0x03;
  bool dither_ = true;

  // The stroke in progress: the picture as it was when the button went
  // down, where it went down, and the pixel the pencil last reached.
  bool stroking_ = false;
  bool erasing_ = false;
  sprite::Frame base_;
  int startX_ = 0, startY_ = 0, lastX_ = 0, lastY_ = 0;
  float startMouseX_ = 0.0f, startMouseY_ = 0.0f;
  int rollAxis_ = 0;  // 0 undecided, 1 the row, 2 the column, 3 the whole frame

  std::vector<Snapshot> undo_, redo_;

  // The preview.
  bool playing_ = true;
  bool pingPong_ = false;
  int previewScale_ = 4;  // at most what fits the 256 pixel screen
  uint8_t backdrop_ = 0;
  bool onScreen_ = false;
  bool previewShown_ = false;
  int newWidth_ = 16, newHeight_ = 16;
  // Fit the zoom to the canvas over the next few frames: a freshly
  // docked pane takes a frame or two to reach its size.
  int fit_ = 3;
  // Heights measured last frame. The sprite palette shrinks by what follows
  // it, and font mode's cells pane grows to its content.
  float paletteEnd_ = 0.0f;
  float paletteTail_ = 0.0f;
  float cellsHeight_ = 0.0f;

  // Font mode. The cell is the header's, 4 to 8 each way. group_ is the
  // first of the four codes the strip and the block show.
  int cellW_ = 6, cellH_ = 8;
  FontView fontView_ = FontView::Single;
  int group_ = 0;
  bool ink_ = true;  // the tools set pixels, or clear them
  sprite::Facing facing_ = sprite::Facing::Up;
  int fontFps_ = 4;
  std::string sample_ = "The quick brown fox jumps over the lazy dog. 0123456789";
  sprite::Frame block_;
  // The text the font gave when it was opened or last written.
  std::vector<uint8_t> savedText_;
};

}  // namespace sc8
