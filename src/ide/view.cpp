#include "ide/view.h"

#include <filesystem>

namespace sc8 {

namespace {

// One row per view, in the order of the enum, the tabs and F1 to F4.
struct ViewRow {
  const char* key;
  const char* label;
  const char* level;  // the level the view replaced
  const char* speed;
  const char* runTab;  // the pane a run brings forward, or null
  DocKind kind;        // the documents its editor holds
};

constexpr ViewRow VIEWS[VIEW_COUNT] = {
    {"basic", "BASIC", "basic", "f60", nullptr, DocKind::Basic},
    {"c", "C", "project", "f60", nullptr, DocKind::C},
    {"assembly", "ASSEMBLY", "run", "f60", "Listing", DocKind::Assembly},
    {"microcode", "MICROCODE", "cpu", "trace", "Listing", DocKind::Microcode},
};

const ViewRow& row(View view) { return VIEWS[static_cast<int>(view)]; }

}  // namespace

const char* viewKey(View view) { return row(view).key; }
const char* viewLabel(View view) { return row(view).label; }
const char* defaultSpeed(View view) { return row(view).speed; }
const char* runTab(View view) { return row(view).runTab; }

bool runGoing(const RunState& state) {
  if (!state.clocked) return false;
  return !state.basic || !state.atPrompt || state.pending;
}

std::optional<View> viewNamed(std::string_view key) {
  for (int i = 0; i < VIEW_COUNT; i++) {
    if (key == VIEWS[i].key) return static_cast<View>(i);
  }
  return std::nullopt;
}

std::optional<View> viewOfLevel(std::string_view level) {
  for (int i = 0; i < VIEW_COUNT; i++) {
    if (level == VIEWS[i].level) return static_cast<View>(i);
  }
  return std::nullopt;
}

DocKind docKindOf(const std::string& name) {
  const std::string ext = std::filesystem::path(name).extension().string();
  if (name == "microcode.txt") return DocKind::Microcode;
  if (ext == ".c") return DocKind::C;
  if (ext == ".h") return DocKind::Header;
  if (ext == ".asm") return DocKind::Assembly;
  if (ext == ".bas") return DocKind::Basic;
  return DocKind::Other;
}

std::optional<View> viewOfKind(DocKind kind) {
  switch (kind) {
    case DocKind::Basic: return View::Basic;
    case DocKind::C:
    case DocKind::Header: return View::C;
    case DocKind::Assembly: return View::Assembly;
    case DocKind::Microcode: return View::Microcode;
    case DocKind::Other: break;
  }
  return std::nullopt;
}

DocKind kindOfView(View view) { return row(view).kind; }

}  // namespace sc8
