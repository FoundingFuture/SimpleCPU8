// The project column: the project's files in a fixed column at the left
// of the window, under the menu bar. It is drawn before the view's
// dockspace, and the dockspace fills the rest. So the column belongs to
// no view and a view switch never touches it. It cannot be hidden, only
// collapsed to a narrow bar. The toggle is the button at its top and
// Cmd-B or Ctrl-B.
//
// DESIGN: not a docked pane. A docked pane four views share needs a
// window name per view and moves between dockspaces at every switch
// (CLAUDE.md, gotchas). A fixed column has one state and no layout
// loses it.
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "imgui.h"

#include "ide/ide.h"
#include "ide/panes.h"

namespace fs = std::filesystem;

namespace sc8 {

namespace {

// Text that reads upward, its top left corner at pos. Dear ImGui draws
// text level only, so the glyphs are added level and their corners
// turned a quarter. The full screen clip keeps the glyphs that start
// outside the narrow bar from being dropped before the turn.
void verticalText(ImDrawList* dl, ImVec2 pos, ImU32 colour, const char* text) {
  const float length = ImGui::CalcTextSize(text).x;
  const int first = dl->VtxBuffer.Size;
  dl->PushClipRectFullScreen();
  dl->AddText(pos, colour, text);
  dl->PopClipRect();
  for (int i = first; i < dl->VtxBuffer.Size; i++) {
    ImVec2& p = dl->VtxBuffer[i].pos;
    const float dx = p.x - pos.x;
    const float dy = p.y - pos.y;
    p = ImVec2(pos.x + dy, pos.y + length - dx);
  }
}

// A small button beside the one before it when the row has room, under
// it otherwise.
bool flowButton(const char* label, bool first) {
  if (!first) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float w = ImGui::CalcTextSize(label).x + st.FramePadding.x * 2.0f;
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if (ImGui::GetItemRectMax().x + st.ItemSpacing.x + w <= right) ImGui::SameLine();
  }
  return ImGui::SmallButton(label);
}

constexpr ImGuiTreeNodeFlags GROUP = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

}  // namespace

void Ide::toggleColumn() {
  settings_.columnOpen = !settings_.columnOpen;
  settings_.columnKnown = true;
  columnFromSettings_ = true;
  if (!settings_.save()) note("the settings could not be written, so the column is as it was at the next start");
}

float Ide::projectColumn() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImGuiStyle& style = ImGui::GetStyle();
  const bool open = settings_.columnOpen;
  // Collapsed, the bar is as wide as its toggle.
  const float bar = ImGui::CalcTextSize(">>").x + style.FramePadding.x * 2.0f + style.WindowPadding.x * 2.0f;
  const float width = open ? std::clamp(columnWidth_, COLUMN_MIN, COLUMN_MAX) * style.FontScaleMain : bar;
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(ImVec2(width, vp->WorkSize.y));
  ImGui::SetNextWindowViewport(vp->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::Begin("##project-column", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  if (open) columnTree();
  else columnBar();
  ImGui::End();
  return width;
}

// The collapsed column: the toggle, then one upward label per group the
// project has. A click anywhere on the bar opens the column.
void Ide::columnBar() {
  if (ImGui::SmallButton(">>")) toggleColumn();
  const char* key = ImGui::GetIO().ConfigMacOSXBehaviors ? "Cmd-B" : "Ctrl-B";
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("open the project column, %s", key);
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const ImVec2 room = ImGui::GetContentRegionAvail();
  if (room.x < 1.0f || room.y < 1.0f) return;
  const bool clicked = ImGui::InvisibleButton("##column-open", room);
  const bool hot = ImGui::IsItemHovered();
  if (hot) ImGui::SetTooltip("%s: open the project column, %s", projectTitle_.c_str(), key);
  if (clicked) toggleColumn();

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float em = ImGui::GetFontSize();
  const float x = start.x + (room.x - em) * 0.5f;
  float y = start.y + em * 0.5f;
  const ImU32 plain = ImGui::GetColorU32(hot ? ImGuiCol_Text : ImGuiCol_TextDisabled);
  auto label = [&](const char* text, ImU32 colour) {
    const float length = ImGui::CalcTextSize(text).x;
    if (y + length > start.y + room.y) return;
    verticalText(dl, ImVec2(x, y), colour, text);
    y += length + em;
  };
  for (int v = 0; v < VIEW_COUNT; v++) {
    const View view = static_cast<View>(v);
    const bool any = std::any_of(docs_.begin(), docs_.end(), [&](const Doc& d) {
      return !d.library && viewOfKind(docKindOf(d.name)) == view;
    });
    if (any) label(viewLabel(view), view == view_ ? ImGui::GetColorU32(ImGuiCol_Text) : plain);
  }
  if (std::any_of(docs_.begin(), docs_.end(), [](const Doc& d) { return d.library; })) label("LIBRARIES", plain);
  label("ASSETS", plain);
  label("BUILD", plain);
}

// The open column: the toggle and the project's name, the buttons that
// add to the project, then the tree. Sources by kind in the order of the
// views, then Assets, then Build. A grip at the right edge drags the
// width.
void Ide::columnTree() {
  const float grip = 4.0f;
  ImGui::BeginChild("##column-body", ImVec2(-grip, 0.0f));
  if (ImGui::SmallButton("<<")) toggleColumn();
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("collapse the project column, %s", ImGui::GetIO().ConfigMacOSXBehaviors ? "Cmd-B" : "Ctrl-B");
  }
  ImGui::SameLine();
  ImGui::TextUnformatted(projectTitle_.c_str());
  const std::string where = projectDir_.empty() ? (romBase_ ? "a ROM, not saved as a folder yet" : "not saved yet")
                                                : projectDir_;
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", where.c_str());
  ImGui::PopStyleColor();

  if (flowButton("New file...", true)) {
    newDocName_.clear();
    askNewFile_ = true;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("a new source: name.c, .h, .asm or .bas, or microcode.txt");
  if (flowButton("Add asset...", false)) {
    dialog_.open(FileDialog::Mode::OpenFile, "Add a file to the project's assets", fs::current_path(), {},
                 [this](const fs::path& p) { addAsset(p.string()); });
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("copy a picture, a sound or any file into the project. A right click on an asset removes it");
  }
  if (flowButton("New sprite...", false)) askNewSprite();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("draw a new sprite strip, saved as a PNG under Assets");
  if (flowButton("New font...", false)) askNewFont_ = true;
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("a copy of the built-in font as a .font under Assets, opened in the font editor");
  }
  ImGui::Separator();

  ImGui::BeginChild("##column-tree");
  // A click only notes the document. Opening it sorts nothing, but
  // removing one changes the list the loop walks.
  std::optional<size_t> opened;
  for (int v = 0; v < VIEW_COUNT; v++) {
    const View view = static_cast<View>(v);
    const bool any = std::any_of(docs_.begin(), docs_.end(), [&](const Doc& d) {
      return !d.library && viewOfKind(docKindOf(d.name)) == view;
    });
    if (!any || !ImGui::TreeNodeEx(viewLabel(view), GROUP)) continue;
    for (size_t i = 0; i < docs_.size(); i++) {
      const Doc& d = docs_[i];
      if (d.library || viewOfKind(docKindOf(d.name)) != view) continue;
      const std::string label = d.name + (d.dirty ? " *" : "");
      const bool shown = view == view_ && d.open && activeName_[v] == d.name;
      if (ImGui::Selectable(label.c_str(), shown)) opened = i;
      if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem(("Remove " + d.name + "...").c_str())) askRemove(d.name);
        ImGui::EndPopup();
      }
    }
    ImGui::TreePop();
  }
  // The libraries the project's BASIC programs INCLUDE. They are the
  // libraries folder's files, not the project's, and open read only.
  const bool libraries = std::any_of(docs_.begin(), docs_.end(), [](const Doc& d) { return d.library; });
  if (libraries && ImGui::TreeNodeEx("Libraries", GROUP)) {
    for (size_t i = 0; i < docs_.size(); i++) {
      const Doc& d = docs_[i];
      if (!d.library) continue;
      const std::string label = d.name + (d.dirty ? " *" : "");
      const bool shown = view_ == View::Basic && d.open && activeName_[static_cast<int>(View::Basic)] == d.name;
      if (ImGui::Selectable(label.c_str(), shown)) opened = i;
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s\nA library a program of this project includes. It opens read only: Edit library on "
                          "its tab lets you change it, for every project that uses it.",
                          d.path.c_str());
      }
    }
    ImGui::TreePop();
  }
  if (opened) openDoc(*opened);
  if (docs_.empty()) ImGui::TextDisabled("no source yet");

  // The assets: pictures, sounds and any file the sources name.
  if (assetsStale_) {
    shownAssets_ = assetList();
    assetsStale_ = false;
  }
  if (ImGui::TreeNodeEx("Assets", GROUP)) {
    // A click below that changes the assets only marks the list stale, so
    // the list stays whole while the loop walks it.
    const std::vector<AssetEntry>& assets = shownAssets_;
    if (assets.empty()) ImGui::TextDisabled("none yet");
    for (const AssetEntry& a : assets) {
      ImGui::PushID(a.name.c_str());
      const bool picture = isDrawable(a.name);
      const bool isFontAsset = SpriteEditor::isFont(a.name);
      const char* editor = isFontAsset ? "font editor" : "sprite editor";
      const bool editing = sprite_.isOpen() && sprite_.name() == a.name;
      if (ImGui::Selectable(a.name.c_str(), editing, ImGuiSelectableFlags_AllowDoubleClick) && picture &&
          ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        openSprite(a.name);
      }
      if (ImGui::IsItemHovered() && isFontAsset) {
        const std::string stem = fs::path(a.name).stem().string();
        ImGui::SetTooltip("%s, %ju bytes. C names it as __font(\"%s\"), assembly as .font('%s'), BASIC as "
                          "LOADFONT %s. A double click opens it in the font editor.",
                          a.name.c_str(), a.bytes, a.name.c_str(), a.name.c_str(), stem.c_str());
      } else if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s, %ju bytes. C names it as __sprite(\"%s\"), __image, __sample or __file; "
                          "assembly as .sprite('%s'), .image, .sample or .file.%s",
                          a.name.c_str(), a.bytes, a.name.c_str(), a.name.c_str(),
                          picture ? " A double click opens it in the sprite editor." : "");
      }
      if (ImGui::BeginPopupContextItem()) {
        if (picture && ImGui::MenuItem((std::string("Edit in the ") + editor).c_str())) openSprite(a.name);
        if (ImGui::MenuItem(("Remove " + a.name + "...").c_str())) askRemove(a.name, true);
        ImGui::EndPopup();
      }
      ImGui::PopID();
    }
    ImGui::TreePop();
  }

  // What the last build made: the ROM in the machine and the assembly.
  if (ImGui::TreeNodeEx("Build", GROUP)) {
    const bool built = lastBuildOk_;
    if (!built) ImGui::TextDisabled(messages_.empty() ? "not built yet" : "the last build failed");
    if (built) {
      const std::string file = !builtRom_.empty()  ? fs::path(builtRom_).filename().string()
                               : !romPath_.empty() ? fs::path(romPath_).filename().string()
                                                   : projectTitle_ + ".rom";
      const std::string at = !builtRom_.empty()  ? builtRom_
                             : !romPath_.empty() ? romPath_
                                                 : "in the machine only. File, Burn ROM as writes it to a file";
      if (ImGui::Selectable(file.c_str())) note("the ROM: " + at);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("the ROM the last build made: %s", at.c_str());
    }
    if (built && !builtAssembly_.empty()) {
      if (ImGui::Selectable("assembly")) showAssembly();
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("the assembly the last build made, read only");
    }
    ImGui::TreePop();
  }

  if (romBase_ && !carriedProject_) {
    ImGui::Spacing();
    ImGui::TextWrapped("The ROM carries no project. Its program is not a document and its listing is a disassembly.");
  } else if (carriedProject_) {
    ImGui::Spacing();
    ImGui::TextWrapped("The project came out of the ROM. Save writes it back into the ROM file. "
                       "Save project as writes it to a folder.");
  }
  ImGui::EndChild();
  ImGui::EndChild();

  ImGui::SameLine(0.0f, 0.0f);
  ImGui::InvisibleButton("##column-grip", ImVec2(grip, std::max(1.0f, ImGui::GetContentRegionAvail().y)));
  const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
  if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
    const float scale = std::max(0.1f, ImGui::GetStyle().FontScaleMain);
    columnWidth_ = std::clamp(columnWidth_ + ImGui::GetIO().MouseDelta.x / scale, COLUMN_MIN, COLUMN_MAX);
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("drag to size the project column. Save layout keeps the width");
  if (hot) {
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    const float mid = (a.x + b.x) * 0.5f;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(mid, a.y), ImVec2(mid, b.y),
                                        ImGui::GetColorU32(ImGuiCol_SeparatorActive), 2.0f);
  }
}

void Ide::showAssembly() {
  const bool hasC = std::any_of(docs_.begin(), docs_.end(),
                                [](const Doc& d) { return docKindOf(d.name) == DocKind::C; });
  setView(hasC ? View::C : View::Assembly);
  focusAfterLayout_ = pane(hasC ? "Assembly" : "Listing");
}

void Ide::newFileDialog() {
  if (!askNewFile_) return;
  ImGui::OpenPopup("New file");
  if (!ImGui::BeginPopupModal("New file", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  ImGui::TextUnformatted("The file's name. Its ending picks its view.");
  ImGui::TextDisabled("name.bas, name.c, name.h, name.asm or microcode.txt");
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  ImGui::SetNextItemWidth(320.0f);
  const bool entered = panes::inputLine("##new-file", newDocName_, "", ImGuiInputTextFlags_EnterReturnsTrue);
  auto close = [this]() {
    askNewFile_ = false;
    ImGui::CloseCurrentPopup();
  };
  if ((ImGui::Button("Create", ImVec2(90, 0)) || entered) && newDoc(newDocName_)) close();
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(90, 0))) close();
  ImGui::EndPopup();
}

}  // namespace sc8
