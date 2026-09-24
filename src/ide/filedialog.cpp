#include "ide/filedialog.h"

#include <algorithm>
#include <cctype>

#include "imgui.h"

#include "ide/panes.h"

namespace fs = std::filesystem;

namespace sc8 {

namespace {

bool lessName(const std::string& a, const std::string& b) {
  return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
    return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
  });
}

}  // namespace

void FileDialog::open(Mode mode, std::string title, fs::path startIn, std::vector<std::string> extensions,
                      std::function<void(const fs::path&)> onOk, std::string suggested) {
  mode_ = mode;
  title_ = std::move(title);
  extensions_ = std::move(extensions);
  onOk_ = std::move(onOk);
  name_ = std::move(suggested);
  newFolder_.clear();
  error_.clear();
  std::error_code ec;
  dir_ = startIn.empty() ? fs::current_path(ec) : fs::absolute(startIn, ec);
  if (!fs::is_directory(dir_, ec)) dir_ = fs::current_path(ec);
  readDir();
  open_ = true;
  justOpened_ = true;
}

// The folder's entries: folders first, then the files the filter admits,
// each group in name order. Hidden entries are left out, since a dotfile
// is never what a person opens here.
void FileDialog::readDir() {
  entries_.clear();
  std::error_code ec;
  std::vector<std::string> dirs, files;
  for (const auto& entry : fs::directory_iterator(dir_, ec)) {
    const std::string name = entry.path().filename().string();
    if (name.empty() || name[0] == '.') continue;
    if (entry.is_directory(ec)) {
      dirs.push_back(name);
    } else if (mode_ != Mode::OpenFolder) {
      bool admit = extensions_.empty();
      for (const std::string& e : extensions_) admit = admit || entry.path().extension() == e;
      if (admit) files.push_back(name);
    }
  }
  std::sort(dirs.begin(), dirs.end(), lessName);
  std::sort(files.begin(), files.end(), lessName);
  for (const std::string& d : dirs) entries_.push_back({d, true});
  for (const std::string& f : files) entries_.push_back({f, false});
  pathField_ = dir_.string();
}

void FileDialog::draw() {
  if (!open_) return;
  if (justOpened_) {
    ImGui::OpenPopup(title_.c_str());
    justOpened_ = false;
  }
  ImGui::SetNextWindowSize(ImVec2(640, 460), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal(title_.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
    open_ = false;
    return;
  }
  std::error_code ec;

  // The folder line: up one, the path (typed paths are followed on Enter).
  if (ImGui::SmallButton("Up")) {
    if (dir_.has_parent_path() && dir_.parent_path() != dir_) {
      dir_ = dir_.parent_path();
      readDir();
    }
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-1.0f);
  if (panes::inputLine("##path", pathField_, "folder", ImGuiInputTextFlags_EnterReturnsTrue)) {
    if (fs::is_directory(pathField_, ec)) {
      dir_ = fs::absolute(pathField_, ec);
      readDir();
    } else {
      error_ = pathField_ + " is not a folder";
    }
  }

  // The entries. A folder opens on a double click. A file goes into the
  // name field on a click and confirms on a double click.
  const float footer = ImGui::GetFrameHeightWithSpacing() * 3.2f;
  ImGui::BeginChild("##entries", ImVec2(-1.0f, -footer), ImGuiChildFlags_Borders);
  bool confirm = false;
  for (const Entry& e : entries_) {
    const std::string label = e.isDir ? e.name + "/" : e.name;
    const bool selected = !e.isDir && e.name == name_;
    if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
      if (e.isDir) {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          dir_ /= e.name;
          readDir();
          ImGui::EndChild();
          ImGui::EndPopup();
          return;
        }
        if (mode_ == Mode::OpenFolder) name_ = e.name;
      } else {
        name_ = e.name;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) confirm = true;
      }
    }
  }
  ImGui::EndChild();

  // A new folder, made where the dialog stands, and entered.
  ImGui::SetNextItemWidth(220.0f);
  const bool madeFolder = panes::inputLine("##newfolder", newFolder_, "new folder name", ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  if ((ImGui::SmallButton("New folder") || madeFolder) && !newFolder_.empty()) {
    const fs::path made = dir_ / newFolder_;
    if (fs::create_directories(made, ec) || fs::is_directory(made, ec)) {
      dir_ = made;
      newFolder_.clear();
      readDir();
    } else {
      error_ = "cannot create " + made.string();
    }
  }

  // The name line and the buttons.
  const char* hint = mode_ == Mode::OpenFolder ? "folder name, or leave empty for this folder"
                     : mode_ == Mode::SaveFile ? "file name"
                                               : "file name";
  ImGui::SetNextItemWidth(-160.0f);
  if (panes::inputLine("##name", name_, hint, ImGuiInputTextFlags_EnterReturnsTrue)) confirm = true;
  ImGui::SameLine();
  const char* okLabel = mode_ == Mode::SaveFile ? "Save" : mode_ == Mode::OpenFolder ? "Choose" : "Open";
  if (ImGui::Button(okLabel, ImVec2(70, 0))) confirm = true;
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(70, 0))) {
    open_ = false;
    ImGui::CloseCurrentPopup();
  }
  if (!error_.empty()) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s", error_.c_str());

  if (confirm && open_) {
    fs::path chosen = name_.empty() ? dir_ : dir_ / name_;
    if (mode_ == Mode::OpenFile && !fs::is_regular_file(chosen, ec)) {
      error_ = "pick a file";
    } else if (mode_ == Mode::OpenFolder && !name_.empty() && !fs::is_directory(chosen, ec)) {
      // A folder that is not there yet is made: that is how a new project
      // gets its home.
      if (!fs::create_directories(chosen, ec)) error_ = "cannot create " + chosen.string();
    } else if (mode_ == Mode::SaveFile && name_.empty()) {
      error_ = "give the file a name";
    }
    if (error_.empty()) {
      if (mode_ == Mode::SaveFile && !extensions_.empty() && !chosen.has_extension()) chosen += extensions_.front();
      open_ = false;
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
      if (onOk_) onOk_(chosen);
      return;
    }
  }
  ImGui::EndPopup();
}

}  // namespace sc8
