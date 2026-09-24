// A file dialog drawn with Dear ImGui, the same on every platform: a
// folder to walk, its entries, a name field, and a way to make a folder
// while browsing. Three shapes: pick a file to open, pick a folder, and
// name a file to save. The caller opens it with a callback and draws it
// every frame; the callback runs once, on OK.
#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace sc8 {

class FileDialog {
 public:
  enum class Mode { OpenFile, OpenFolder, SaveFile };

  // Show the dialog. extensions filters the files listed (".rom", ".c");
  // empty lists every file. startIn is the folder shown first, or the
  // working directory. suggested fills the name field for SaveFile.
  void open(Mode mode, std::string title, std::filesystem::path startIn, std::vector<std::string> extensions,
            std::function<void(const std::filesystem::path&)> onOk, std::string suggested = "");

  // Draw the dialog when it is open. Call every frame.
  void draw();

  bool isOpen() const { return open_; }

 private:
  void readDir();

  bool open_ = false;
  bool justOpened_ = false;
  Mode mode_ = Mode::OpenFile;
  std::string title_;
  std::filesystem::path dir_;
  std::vector<std::string> extensions_;
  std::function<void(const std::filesystem::path&)> onOk_;
  std::string name_;
  std::string newFolder_;
  std::string error_;
  struct Entry {
    std::string name;
    bool isDir;
  };
  std::vector<Entry> entries_;
  std::string pathField_;
};

}  // namespace sc8
