// DESIGN: every text file a tool writes has LF line ends, on every
// platform. A project's files and its ROM are then the same bytes anywhere.
// Header only: the IDE's settings use it with the standard library alone.
#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace sc8 {

// The text with each CR LF made LF. A lone CR stays.
inline std::string lf(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
    out += text[i];
  }
  return out;
}

// Binary, as a text-mode stream on Windows writes CR LF. False on failure.
inline bool writeText(const std::filesystem::path& p, std::string_view text) {
  std::ofstream o(p, std::ios::binary);
  const std::string out = lf(text);
  o.write(out.data(), static_cast<std::streamsize>(out.size()));
  return static_cast<bool>(o);
}

}  // namespace sc8
