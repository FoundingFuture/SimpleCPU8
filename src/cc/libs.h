// The friendly libraries: a header and the unit compiled in behind it.
// libs.cpp holds the text. headers() serves the header, and merge() adds
// the unit when a source includes the header.
#pragma once

#include <string>
#include <vector>

namespace sc8::cc {

struct Library {
  std::string header;  // the name a program includes: graphics.h
  std::string unit;    // the file name the unit is compiled under
  const char* headerText;
  const char* source;
};

const std::vector<Library>& libraries();

}  // namespace sc8::cc
