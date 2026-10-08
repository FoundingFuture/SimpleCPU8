// The numbers on a line the IDE saves in layout.ini, such as Pos=40,300.
// They replace sscanf(line, "Pos=%d,%d"), which MSVC deprecates as unsafe
// (C4996). Unlike %d, a number here takes no leading space or plus sign.
// The IDE writes these lines itself and never writes either.
#pragma once

#include <charconv>
#include <string_view>

namespace sc8 {

namespace ini_detail {

// Parse one int at the front of text and drop it. False when none is there.
inline bool takeInt(std::string_view& text, int& out) {
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
  if (ec != std::errc()) return false;
  text.remove_prefix(static_cast<size_t>(end - text.data()));
  return true;
}

inline bool takeKey(std::string_view& text, std::string_view key) {
  if (!text.starts_with(key) || text.substr(key.size(), 1) != "=") return false;
  text.remove_prefix(key.size() + 1);
  return true;
}

}  // namespace ini_detail

// Read "key=a". Text after the number is ignored, as sscanf ignored it.
inline bool readInts(std::string_view line, std::string_view key, int& a) {
  return ini_detail::takeKey(line, key) && ini_detail::takeInt(line, a);
}

// Read "key=a,b".
inline bool readInts(std::string_view line, std::string_view key, int& a, int& b) {
  if (!ini_detail::takeKey(line, key) || !ini_detail::takeInt(line, a)) return false;
  if (!line.starts_with(',')) return false;
  line.remove_prefix(1);
  return ini_detail::takeInt(line, b);
}

}  // namespace sc8
