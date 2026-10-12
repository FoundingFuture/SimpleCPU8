// The IDE's four views, one per language: BASIC, C, ASSEMBLY and
// MICROCODE. A view is what the person writes and debugs. Each owns the
// documents of its language, a dock layout, a guide and a speed.
// Only the standard library is used here, so the tests need no window.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace sc8 {

enum class View { Basic, C, Assembly, Microcode };
constexpr int VIEW_COUNT = 4;

// The name --view takes and the layout file writes: basic, c, assembly,
// microcode.
const char* viewKey(View view);
// The name on the view's tab in the menu bar.
const char* viewLabel(View view);

// A view by its key, for --view.
std::optional<View> viewNamed(std::string_view key);
// A view by the name of the level it replaced, for --level: basic to
// BASIC, project to C, run to ASSEMBLY, cpu to MICROCODE.
std::optional<View> viewOfLevel(std::string_view level);

// The speed a view starts at, a value of the speed ladder.
const char* defaultSpeed(View view);

// The pane a run or a step brings to the front: the listing, in the
// views where it shares the editor's tab area. Null for a view whose
// editor shows the line the machine stands on.
const char* runTab(View view);

// One document of the project is a source, and its kind follows its name.
enum class DocKind { C, Header, Assembly, Basic, Microcode, Other };
DocKind docKindOf(const std::string& name);

// The view whose editor holds a kind of document. Nothing for Other.
std::optional<View> viewOfKind(DocKind kind);

}  // namespace sc8
