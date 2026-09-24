// Every built-in name the devices publish, in one lookup. The assembler and
// the C compiler both read this table: the design says every constant the
// assembler knows is a C constant too, and two copies would drift.
#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "devices/gpu_ports.h"

namespace sc8 {

// The kind says which table a name came from, for the manual's grouping.
// The assembler and the C compiler resolve every kind anywhere a number
// does.
enum class ConstantKind { Port, Command, System };

struct NamedConstant {
  std::string_view name;
  int value;
  ConstantKind kind;
};

// All device constants, grouped by device in table order.
std::span<const NamedConstant> deviceConstants();

std::optional<int> portNamed(std::string_view name);
std::optional<int> commandNamed(std::string_view name);
std::optional<int> systemConstant(std::string_view name);

// Any of the three kinds, ports first, then commands, then system constants.
std::optional<int> builtinConstant(std::string_view name);

}  // namespace sc8
