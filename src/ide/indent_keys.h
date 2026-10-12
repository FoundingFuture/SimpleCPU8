// Tab and Backspace over a document's text box. ide/indent.h says what
// the keys do to a line, and IndentKeys watches the box through its
// callback, as BasicAssist does, and applies it.
#pragma once

#include "imgui.h"

namespace sc8 {

class IndentKeys {
 public:
  // The flags a box needs beside its own.
  static constexpr ImGuiInputTextFlags FLAGS = ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackCharFilter;

  // One box for one frame: its id, and whether its indentation may hold
  // tabs. Then the callback the box had, with its flags and user data.
  struct Box {
    ImGuiID id;
    bool tabs;
    ImGuiInputTextFlags nextFlags;
    ImGuiInputTextCallback next;
    void* nextUser;
  };

  // The box's callback, with a Box as its user data.
  static int callback(ImGuiInputTextCallbackData* data);

  // Draw a tab as wide as asm/tabs.h counts it. Call it once, after the
  // fonts are added and before the first frame.
  static void wideTabs();
};

}  // namespace sc8
