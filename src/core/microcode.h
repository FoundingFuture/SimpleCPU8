// A microcode set: one microprogram per instruction plus the fetch entry.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/signals.h"

namespace sc8 {

using Rows = std::vector<Row>;

// Sections keep their insertion order, so a set serializes the way it was
// written, fetch first.
class Microcode {
 public:
  struct Section {
    std::string name;
    Rows rows;
    bool operator==(const Section&) const = default;
  };

  void set(std::string name, Rows rows);
  bool erase(std::string_view name);
  bool has(std::string_view name) const { return find(name) != nullptr; }
  const Rows* get(std::string_view name) const;
  size_t size() const { return sections_.size(); }
  const std::vector<Section>& sections() const { return sections_; }

 private:
  const Section* find(std::string_view name) const;
  std::vector<Section> sections_;
};

// The naive set defines the reference semantics of the ISA.
Microcode buildNaive();

// The optimal set exists to race against. Its rows merge what the naive set
// spreads over cycles.
Microcode buildOptimal();

}  // namespace sc8
