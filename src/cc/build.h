// The build line, in place of a Makefile.
//
// A program of many files needs somebody to say which ones, in what order,
// against which archives. Make is a language of its own and meeting it
// before you have met C is a bad first day. So the build is a comment, in
// the file that holds main:
//
//   // build: cc -o game main.c menu.c -lgpu -lio
//
// The flags are the real ones. -o names the output, -l links an archive, and
// -m is a machine option.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sc8::cc {

struct BuildLine {
  std::string tool;  // "cc" or "ld"
  std::string output;
  std::vector<std::string> files;
  std::vector<std::string> libs;
  std::vector<std::string> flags;
  // The file the line was written in, for an error that names it.
  std::string from;
};

struct BuildPlan {
  std::vector<std::string> files;
  std::vector<std::string> libs;
  bool softMul = false;
  std::string output;
  // The lines as written, for the Files tab to show.
  std::vector<std::string> lines;
  std::optional<std::string> from;
};

struct BuildSource {
  std::string name;
  std::string text;
};

BuildLine parseBuildLine(const std::string& text, const std::string& from);

// The plan for a whole project, from the build lines its files carry.
//
// Exactly one FILE may carry them. None is fine and means "compile the files
// I was given". Two is an error naming both, because a program with build
// lines in two places cannot say which one built it.
BuildPlan planBuild(const std::vector<BuildSource>& sources,
                    const std::vector<std::pair<std::string, std::vector<std::string>>>& builds);

}  // namespace sc8::cc
