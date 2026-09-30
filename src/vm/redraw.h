// When the IDE's host loop may sleep until the next input event.
#pragma once

namespace sc8 {

// PERF DECISION: an idle IDE draws nothing. Dear ImGui redraws the whole
// window each frame, which costs 2.7 ms of CPU in the macOS GPU driver.
// A paused IDE drawing 60 frames a second used a fifth of a core.
// While nothing changes without input, EndDrawing waits for an event
// instead. After a wake the loop draws SETTLE frames before it waits
// again. Dear ImGui takes a frame or two to size a popup or a hover.
class RedrawPacer {
 public:
  static constexpr int SETTLE = 3;

  // Called once a frame, before EndDrawing, with whether anything changes
  // without input. True when this frame's EndDrawing should wait.
  bool waitAfterFrame(bool idle) {
    // A busy frame, or a wait that input ended, starts the count again.
    if (!idle || waited_) settle_ = SETTLE;
    waited_ = false;
    if (!idle) return false;
    if (settle_ > 0) {
      settle_--;
      return false;
    }
    waited_ = true;
    return true;
  }

 private:
  int settle_ = SETTLE;
  bool waited_ = false;
};

}  // namespace sc8
