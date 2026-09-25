// The sprite editor: one sprite strip from the project's assets, drawn
// pixel by pixel on the machine palette, frame by frame, with an animated
// preview through the screen's own display effects.
//
// The drawing tools are the functions of assets/sprite.h; this class is
// the pane around them. It reads and writes the asset through the host,
// so a folder project and a project held in a ROM behave the same.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
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

  explicit SpriteEditor(Host host);

  // Open an asset. False, with a note, when it is no picture or a frame
  // is larger than a sprite may be. `saveAs` names the asset Save writes,
  // when it is not the one read: a .bmp is saved as a .png beside it.
  bool open(const std::string& name, const std::string& saveAs = "");
  // A new strip of transparent frames, not saved until Save.
  void create(const std::string& name, int width, int height, int frames);
  void close();

  bool isOpen() const { return open_; }
  bool dirty() const { return open_ && dirty_; }
  const std::string& name() const { return name_; }

  // Write the strip to its asset. False when the host could not.
  bool save();

  // The pane's body, between Begin and End.
  void body();
  // The preview pane's body. `display` is the screen's own settings: the
  // preview uses them, and changing the effect there changes the Screen
  // pane too.
  void previewBody(DisplaySettings& display);

  // The preview picture, 256 x 256 RGBA, at the time given in seconds.
  // Playing, it steps through the frames at the strip's rate. Paused, it
  // shows the frame being edited.
  void previewPixels(double now, std::vector<uint8_t>& rgba) const;
  // The preview runs on the Screen pane instead of the machine's picture.
  bool previewOnScreen() const { return open_ && onScreen_; }
  // The preview picture is shown in the pane this frame.
  bool previewShown() const { return previewShown_; }

 private:
  enum class Tool { Pencil, Line, Rect, Ellipse, Fill, Gradient, Roll };

  struct Snapshot {
    sprite::Strip strip;
    int frame = 0;
  };

  sprite::Frame& frame() { return strip_.frames[static_cast<size_t>(frame_)]; }
  void pushUndo();
  void undo();
  void redo();
  void changed() { dirty_ = true; }

  void toolbar();
  void canvas();
  void paletteBox();
  void frameStrip();
  void sizeBox();
  void shortcuts();
  // The tool acting on the frame, from the stroke's start to pixel (x, y).
  void strokeTo(int x, int y, float mouseX, float mouseY);

  Host host_;
  std::vector<uint8_t> palette_;
  bool open_ = false;
  bool dirty_ = false;
  std::string name_;
  sprite::Strip strip_;
  int frame_ = 0;

  Tool tool_ = Tool::Pencil;
  uint8_t primary_ = 0xFF;
  uint8_t secondary_ = 0x03;
  bool filled_ = false;
  bool dither_ = true;
  float zoom_ = 16.0f;
  bool grid_ = true;
  bool onion_ = false;

  // The stroke in progress: the frame as it was when the button went
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
};

}  // namespace sc8
