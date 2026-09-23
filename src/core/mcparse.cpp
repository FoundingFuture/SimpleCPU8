#include "core/mcparse.h"

#include <algorithm>
#include <cctype>

#include "core/isa.h"

namespace sc8 {

namespace {

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

std::string upper(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

}  // namespace

McParsed parseMicrocode(std::string_view text) {
  McParsed out;
  std::string current;
  bool inSection = false;
  Rows rows;

  auto closeSection = [&] {
    if (inSection) out.microcode.set(current, std::move(rows));
    inSection = false;
    rows = {};
  };

  int lineNo = 0;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
    pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
    lineNo++;

    size_t hash = line.find('#');
    if (hash != std::string_view::npos) line = line.substr(0, hash);
    line = trim(line);
    if (line.empty()) continue;

    if (line.back() == ':') {
      closeSection();
      std::string name(trim(line.substr(0, line.size() - 1)));
      if (name != "fetch" && !opByName(name)) {
        out.errors.push_back({lineNo, "unknown instruction section: " + name});
        continue;
      }
      if (out.microcode.has(name)) {
        out.errors.push_back({lineNo, "duplicate section: " + name});
        continue;
      }
      current = name;
      inSection = true;
      continue;
    }

    if (!inSection) {
      out.errors.push_back({lineNo, "row outside a section"});
      continue;
    }
    Row row;
    size_t start = 0;
    while (start <= line.size()) {
      size_t comma = line.find(',', start);
      std::string_view item = trim(line.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
      start = comma == std::string_view::npos ? line.size() + 1 : comma + 1;
      if (item.empty()) continue;
      std::string name = upper(item);
      auto s = signalByName(name);
      if (!s) {
        out.errors.push_back({lineNo, "unknown signal: " + name});
        continue;
      }
      row.push_back(*s);
    }
    rows.push_back(std::move(row));
    if (rows.size() > ROW_CAP) {
      out.errors.push_back({lineNo, "section exceeds the " + std::to_string(ROW_CAP) + " row cap"});
    }
  }
  closeSection();

  if (!out.microcode.has("fetch")) {
    out.errors.push_back({0, "the fetch section is required"});
  }
  return out;
}

std::string serializeMicrocode(const Microcode& m) {
  std::string out;
  for (const auto& section : m.sections()) {
    out += section.name;
    out += ":\n";
    for (const Row& row : section.rows) {
      out += "  ";
      out += rowText(row);
      out += '\n';
    }
    out += '\n';
  }
  return out;
}

}  // namespace sc8
