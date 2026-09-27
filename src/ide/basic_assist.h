// The editor's help with BASIC line numbers, over the text box of a .bas
// document.
//
// Enter at the end of a numbered line opens the next line with a number
// already on it. Enter on a line that is only a number takes the number
// away again, which is how the numbering stops. Cmd-I (Ctrl-I elsewhere)
// or Shift+Enter opens a numbered line under the caret's line from
// anywhere in it. The number is ten on, or halfway to the next line. With
// no number free, the program is renumbered in tens first, GOTO, GOSUB and
// THEN included.
//
// A line typed out of order moves to its place when the caret leaves it.
// A line the caret leaves also takes the stored spelling, its reserved
// words in capitals, so the editor shows what the machine holds. While the
// box is not being typed in, the pane runs sortText over the whole text.
// A file opened lowercase or out of order is put right at once.
// basic/lines.h does the text work. BasicAssist watches the box through
// its callback.
#pragma once

#include <string>

#include "imgui.h"

namespace sc8 {

class BasicAssist {
 public:
  // The flags and callback to give the text box, with this as its user
  // data.
  static constexpr ImGuiInputTextFlags FLAGS = ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackCharFilter;
  static int callback(ImGuiInputTextCallbackData* data);

  // Forget the caret, for another document.
  void reset() { lastLine_ = -1; }
  // What the editor did, for the Messages pane, once.
  std::string takeNote() { return std::move(note_); }

  // The whole text in number order, with the reserved words of every
  // numbered row in capitals as they are stored. For when the box is not
  // active. True when it changed.
  static bool sortText(std::string& text);

 private:
  int event(ImGuiInputTextCallbackData* data);

  bool enter_ = false;
  bool shiftEnter_ = false;
  int lastLine_ = -1;
  std::string note_;
};

}  // namespace sc8
