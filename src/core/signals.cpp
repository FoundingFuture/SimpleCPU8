#include "core/signals.h"

#include <array>
#include <stdexcept>
#include <unordered_map>

namespace sc8 {

namespace {

using enum MemAccess;
using enum AluOp;

constexpr uint16_t atoms(auto... a) { return static_cast<uint16_t>((0 | ... | a)); }

#define W(...) atoms(__VA_ARGS__)
#define SC8_META(name, writes, mem, step, alu, aluWrite, io, base, mod, stk) \
  SignalMeta{#name, writes, mem, step != 0, alu, aluWrite != 0, io != 0, base != 0, mod != 0, stk != 0},
constexpr std::array<SignalMeta, SIGNAL_COUNT> METAS = {{SC8_SIGNALS(SC8_META)}};
#undef SC8_META
#undef W

struct NameTable {
  std::array<std::string_view, SIGNAL_COUNT> names;
  std::unordered_map<std::string_view, Signal> byName;
  NameTable() {
    for (int i = 0; i < SIGNAL_COUNT; i++) {
      names[static_cast<size_t>(i)] = METAS[static_cast<size_t>(i)].name;
      byName.emplace(METAS[static_cast<size_t>(i)].name, static_cast<Signal>(i));
    }
  }
};

const NameTable& nameTable() {
  static const NameTable t;
  return t;
}

}  // namespace

const SignalMeta& signalMeta(Signal s) { return METAS[static_cast<size_t>(s)]; }

std::string_view signalName(Signal s) { return METAS[static_cast<size_t>(s)].name; }

std::optional<Signal> signalByName(std::string_view name) {
  const auto& m = nameTable().byName;
  auto it = m.find(name);
  if (it == m.end()) return std::nullopt;
  return it->second;
}

std::span<const std::string_view> signalNames() { return nameTable().names; }

Row rowOf(std::initializer_list<std::string_view> names) {
  Row row;
  row.reserve(names.size());
  for (std::string_view n : names) {
    auto s = signalByName(n);
    if (!s) throw std::invalid_argument("unknown signal " + std::string(n));
    row.push_back(*s);
  }
  return row;
}

std::string rowText(const Row& row) {
  std::string out;
  for (size_t i = 0; i < row.size(); i++) {
    if (i) out += ", ";
    out += signalName(row[i]);
  }
  return out;
}

}  // namespace sc8
