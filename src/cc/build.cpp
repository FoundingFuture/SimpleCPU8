#include "cc/build.h"

#include <cctype>
#include <set>
#include <sstream>

#include "cc/lex.h"

namespace sc8::cc {

namespace {

bool endsWith(const std::string& s, std::string_view suffix) { return s.ends_with(suffix); }

bool contains(const std::vector<std::string>& v, const std::string& s) {
  for (const std::string& x : v) if (x == s) return true;
  return false;
}

std::string quote(const std::string& s) { return "\"" + s + "\""; }

}  // namespace

BuildLine parseBuildLine(const std::string& text, const std::string& from) {
  std::vector<std::string> words;
  {
    std::istringstream in(text);
    std::string w;
    while (in >> w) words.push_back(w);
  }
  std::string tool;
  if (!words.empty()) { tool = words.front(); words.erase(words.begin()); }
  if (tool != "cc" && tool != "ld") {
    throw CcError(from, 1,
                  "a build line starts with cc or ld, not " + quote(tool) + ". Write // build: cc -o name main.c");
  }
  BuildLine out;
  out.tool = tool;
  out.from = from;
  for (size_t i = 0; i < words.size(); i++) {
    const std::string& w = words[i];
    if (w == "-o") {
      if (i + 1 >= words.size()) throw CcError(from, 1, "-o needs a name after it");
      out.output = words[++i];
      continue;
    }
    if (w == "-c") { out.flags.push_back(w); continue; }
    if (w.starts_with("-l")) {
      std::string lib;
      if (w.size() > 2) lib = w.substr(2);
      else if (i + 1 < words.size()) lib = words[++i];
      if (lib.empty()) throw CcError(from, 1, "-l needs an archive after it");
      if (endsWith(lib, ".a")) lib = lib.substr(0, lib.size() - 2);
      out.libs.push_back(lib);
      continue;
    }
    if (w.starts_with("-")) { out.flags.push_back(w); continue; }
    out.files.push_back(w);
  }
  return out;
}

BuildPlan planBuild(const std::vector<BuildSource>& sources,
                    const std::vector<std::pair<std::string, std::vector<std::string>>>& builds) {
  std::vector<std::pair<std::string, std::vector<std::string>>> carriers;
  for (const auto& b : builds) if (!b.second.empty()) carriers.push_back(b);
  if (carriers.size() > 1) {
    std::string names;
    for (size_t i = 0; i < carriers.size(); i++) names += (i ? " and " : "") + carriers[i].first;
    throw CcError(carriers[0].first, 1,
                  names + " both carry a build line. Exactly one file may, because a program with build "
                          "lines in two places cannot say which one built it.");
  }

  BuildPlan plan;
  for (const BuildSource& s : sources) if (endsWith(s.name, ".c")) plan.files.push_back(s.name);
  if (carriers.empty()) return plan;

  const std::string& from = carriers[0].first;
  const std::vector<std::string>& lines = carriers[0].second;
  plan.from = from;
  plan.lines = lines;
  std::vector<std::string> named;
  for (const std::string& line : lines) {
    const BuildLine b = parseBuildLine(line, from);
    if (!b.output.empty()) plan.output = b.output;
    for (const std::string& l : b.libs) if (!contains(plan.libs, l)) plan.libs.push_back(l);
    for (const std::string& f : b.flags) if (f == "-msoft-mul") plan.softMul = true;
    // ld takes .o names, which are the .c names with the suffix swapped.
    for (const std::string& f : b.files) {
      std::string c = f;
      if (endsWith(c, ".o")) c = c.substr(0, c.size() - 2) + ".c";
      if (endsWith(c, ".c") && !contains(named, c)) named.push_back(c);
    }
  }
  if (named.empty()) return plan;

  std::set<std::string> have;
  for (const BuildSource& s : sources) have.insert(s.name);
  std::vector<std::string> missing;
  for (const std::string& n : named) if (!have.count(n)) missing.push_back(n);
  if (!missing.empty()) {
    std::string m, here;
    for (size_t i = 0; i < missing.size(); i++) m += (i ? " and " : "") + missing[i];
    bool first = true;
    for (const BuildSource& s : sources) {
      if (!endsWith(s.name, ".c")) continue;
      here += (first ? "" : ", ") + s.name;
      first = false;
    }
    throw CcError(from, 1,
                  "the build line names " + m + ", which " + (missing.size() == 1 ? "is" : "are") +
                      " not in this project. The files here are " + here + ".");
  }
  plan.files = named;
  return plan;
}

}  // namespace sc8::cc
