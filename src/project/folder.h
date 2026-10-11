// DESIGN: a folder has one spelling. `game/`, `game/.` and `pong/../game`
// name one folder, but the parent of `game/` taken as spelled is `game`,
// and a folder with a separator after it has one empty name more than
// the files inside it. So every folder a person, a command line or a
// settings file gives goes through normalFolder once, where it is set.
// Header only: the IDE's settings use it with the standard library alone.
#pragma once

#include <filesystem>

namespace sc8::project {

// The folder with every `.` and `..` worked out and no separator after
// its last name. A relative path stays relative, a root stays a root and
// an empty path stays empty. Nothing is asked of the disk.
inline std::filesystem::path normalFolder(const std::filesystem::path& folder) {
  std::filesystem::path out = folder.lexically_normal();
  if (!out.has_filename() && out.has_relative_path()) out = out.parent_path();
  return out;
}

}  // namespace sc8::project
