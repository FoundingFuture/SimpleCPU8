// The indentation keys against a real Dear ImGui text box. A context
// needs no window, so key events go in and the buffer is read back.

#include <doctest.h>

#include <string>

#include "imgui.h"

#include "ide/indent_keys.h"

namespace {

// A text box with the keys on it, and a person typing into it.
class Typist {
 public:
  explicit Typist(bool tabs = true) {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800.0f, 600.0f);
    io.IniFilename = nullptr;
    // Ctrl for the shortcuts on every platform the tests run on.
    io.ConfigMacOSXBehaviors = false;
    io.Fonts->AddFontDefault();
    sc8::IndentKeys::wideTabs();
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    box_.tabs = tabs;
    // A few frames give the box the keyboard.
    for (int i = 0; i < 5; i++) frame();
  }
  ~Typist() { ImGui::DestroyContext(); }
  Typist(const Typist&) = delete;
  Typist& operator=(const Typist&) = delete;

  void press(ImGuiKey key, ImGuiKey mod = ImGuiKey_None) {
    ImGuiIO& io = ImGui::GetIO();
    if (mod != ImGuiKey_None) io.AddKeyEvent(mod, true);
    io.AddKeyEvent(key, true);
    frame();
    io.AddKeyEvent(key, false);
    if (mod != ImGuiKey_None) io.AddKeyEvent(mod, false);
    frame();
  }
  void type(const char* text) {
    ImGui::GetIO().AddInputCharactersUTF8(text);
    frame();
    frame();
  }
  // One frame with no key.
  void wait() { frame(); }
  // The box's own callback, behind the keys.
  void chain(ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user) {
    box_.nextFlags = flags;
    box_.next = callback;
    box_.nextUser = user;
  }
  std::string text() const { return buffer_; }
  // How wide a text is drawn, in spaces.
  float spaces(const char* text) {
    ImGui::NewFrame();
    const float width = ImGui::CalcTextSize(text).x / ImGui::CalcTextSize(" ").x;
    ImGui::EndFrame();
    return width;
  }

 private:
  void frame() {
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
    ImGui::Begin("box");
    if (frames_++ < 3) ImGui::SetKeyboardFocusHere();
    box_.id = ImGui::GetID("##doc");
    ImGui::InputTextMultiline("##doc", buffer_, sizeof buffer_, ImVec2(-1.0f, -1.0f),
                              ImGuiInputTextFlags_AllowTabInput | sc8::IndentKeys::FLAGS | box_.nextFlags,
                              sc8::IndentKeys::callback, &box_);
    ImGui::End();
    ImGui::EndFrame();
  }

  char buffer_[1024] = "";
  sc8::IndentKeys::Box box_{0, true, 0, nullptr, nullptr};
  int frames_ = 0;
};

}  // namespace

TEST_SUITE("the indentation keys in a text box") {
  // The font's own tab glyph is one space wide. The caret, the selection
  // and the text drawn over the box all take their place from the font.
  TEST_CASE("a tab is drawn 8 spaces wide") {
    Typist t;
    CHECK_EQ(t.spaces("\t"), doctest::Approx(8.0f));
    CHECK_EQ(t.spaces("\t\tx"), doctest::Approx(17.0f));
  }

  TEST_CASE("Tab indents by 4 spaces, and a second Tab stores one tab character") {
    Typist t;
    t.press(ImGuiKey_Tab);
    CHECK_EQ(t.text(), "    ");
    t.press(ImGuiKey_Tab);
    CHECK_EQ(t.text(), "\t");
    t.press(ImGuiKey_Tab);
    CHECK_EQ(t.text(), "\t    ");
  }

  TEST_CASE("Tab after text adds spaces") {
    Typist t;
    t.press(ImGuiKey_Tab);
    t.press(ImGuiKey_Tab);
    t.type("LD");
    t.press(ImGuiKey_Tab);
    CHECK_EQ(t.text(), "\tLD  ");
  }

  TEST_CASE("Backspace in the indentation steps back 4") {
    Typist t;
    t.press(ImGuiKey_Tab);
    t.press(ImGuiKey_Tab);
    t.press(ImGuiKey_Tab);
    t.press(ImGuiKey_Backspace);
    CHECK_EQ(t.text(), "\t");
    t.press(ImGuiKey_Backspace);
    CHECK_EQ(t.text(), "    ");
    t.press(ImGuiKey_Backspace);
    CHECK_EQ(t.text(), "");
  }

  TEST_CASE("Backspace after text takes one character") {
    Typist t;
    t.type("LD  ");
    t.press(ImGuiKey_Backspace);
    CHECK_EQ(t.text(), "LD ");
    t.press(ImGuiKey_Backspace);
    t.press(ImGuiKey_Backspace);
    CHECK_EQ(t.text(), "L");
  }

  TEST_CASE("Backspace at a line's start joins it to the line above") {
    Typist t;
    t.press(ImGuiKey_Tab);
    t.type("x");
    t.press(ImGuiKey_Enter);
    t.press(ImGuiKey_Tab);
    CHECK_EQ(t.text(), "    x\n    ");
    t.press(ImGuiKey_Backspace);
    CHECK_EQ(t.text(), "    x\n");
    t.press(ImGuiKey_Backspace);
    CHECK_EQ(t.text(), "    x");
  }

  TEST_CASE("Tab over a selection replaces it") {
    Typist t;
    t.type("LD A");
    t.press(ImGuiKey_A, ImGuiMod_Ctrl);
    t.press(ImGuiKey_Tab);
    CHECK_EQ(t.text(), "    ");
  }

  // Undo takes a typed space away as Backspace would, one character left
  // of the caret. A Backspace there would step back to 4 columns.
  TEST_CASE("an undone space is not taken for a Backspace") {
    Typist t;
    t.press(ImGuiKey_Tab);
    t.type(" ");
    t.type(" ");
    CHECK_EQ(t.text(), "      ");
    t.press(ImGuiKey_Z, ImGuiMod_Ctrl);
    CHECK_EQ(t.text(), "     ");
  }

  TEST_CASE("a box without tabs indents with spaces") {
    Typist t(false);
    t.press(ImGuiKey_Tab);
    t.press(ImGuiKey_Tab);
    CHECK_EQ(t.text(), "        ");
    t.press(ImGuiKey_Backspace);
    CHECK_EQ(t.text(), "    ");
  }

  // BasicAssist numbers a line in its own callback. A Backspace in the
  // frame after such an edit is still seen for what it took.
  TEST_CASE("Backspace steps back 4 right after the box's own callback edited the text") {
    bool edit = false;
    Typist t;
    t.chain(ImGuiInputTextFlags_CallbackAlways,
            [](ImGuiInputTextCallbackData* data) {
              auto* armed = static_cast<bool*>(data->UserData);
              if (!*armed) return 0;
              *armed = false;
              data->InsertChars(0, "        ");
              data->CursorPos = 8;
              return 0;
            },
            &edit);
    edit = true;
    t.wait();
    CHECK_EQ(t.text(), "        ");
    t.press(ImGuiKey_Backspace);
    CHECK_EQ(t.text(), "    ");
  }

  TEST_CASE("the box's own callback still gets the events it asked for") {
    struct Seen {
      int always = 0;
      std::string typed;
    } seen;
    Typist t;
    t.chain(ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackCharFilter,
            [](ImGuiInputTextCallbackData* data) {
              auto* s = static_cast<Seen*>(data->UserData);
              if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways) s->always++;
              if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) s->typed += static_cast<char>(data->EventChar);
              return 0;
            },
            &seen);
    t.press(ImGuiKey_Tab);
    t.type("ab");
    CHECK_EQ(t.text(), "    ab");
    // The Tab's own character is dropped before the box's callback.
    CHECK_EQ(seen.typed, "ab");
    CHECK_GT(seen.always, 0);
  }
}
