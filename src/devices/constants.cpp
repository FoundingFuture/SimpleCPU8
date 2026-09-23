#include "devices/constants.h"

#include <unordered_map>
#include <vector>

#include "devices/acp_ports.h"
#include "devices/apu_ports.h"
#include "devices/gpu_ports.h"
#include "devices/input_ports.h"

namespace sc8 {

namespace {

struct Registry {
  std::vector<NamedConstant> all;
  std::unordered_map<std::string_view, int> ports, commands, system;

  void add(std::span<const gpu::NamedValue> table, ConstantKind kind) {
    for (const auto& nv : table) {
      all.push_back({nv.name, nv.value, kind});
      auto& map = kind == ConstantKind::Port ? ports
                  : kind == ConstantKind::Command ? commands
                                                   : system;
      map.emplace(nv.name, nv.value);
    }
  }

  Registry() {
    using enum ConstantKind;
    add(gpu::PORTS, Port);
    add(gpu::ALIASES, Port);
    add(apu::PORTS, Port);
    add(acp::PORTS, Port);
    add(gpu::CMDS, Command);
    add(apu::CMDS, Command);
    add(acp::CMDS, Command);
    add(input::PORTS, System);
    add(input::BUTTONS, System);
    add(input::MODS, System);
    add(gpu::MODS, System);
    add(gpu::MAPS, System);
    add(gpu::MODES, System);
    add(gpu::WORLD_FLAGS, System);
    add(acp::FMTS, System);
    add(acp::FLAG_BITS, System);
  }
};

const Registry& registry() {
  static const Registry r;
  return r;
}

std::optional<int> lookup(const std::unordered_map<std::string_view, int>& m, std::string_view name) {
  auto it = m.find(name);
  if (it == m.end()) return std::nullopt;
  return it->second;
}

}  // namespace

std::span<const NamedConstant> deviceConstants() { return registry().all; }

std::optional<int> portNamed(std::string_view name) { return lookup(registry().ports, name); }
std::optional<int> commandNamed(std::string_view name) { return lookup(registry().commands, name); }
std::optional<int> systemConstant(std::string_view name) { return lookup(registry().system, name); }

std::optional<int> builtinConstant(std::string_view name) {
  if (auto v = systemConstant(name)) return v;
  if (auto v = portNamed(name)) return v;
  return commandNamed(name);
}

}  // namespace sc8
