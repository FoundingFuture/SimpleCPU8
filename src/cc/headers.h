// The device libraries. Mostly macros, on purpose.
//
// The design's rule: a library function is inline unless it has real logic.
// gpu_clear is two port writes, and a call around them would be a JSR, a
// frame, the two OUTs and a RET, four times the cost, and stepping into it
// would show stack housekeeping where the layers pane should show hardware.
//
// So a wrapper is a macro. It expands to exactly the OUTs a student wrote by
// hand last week, and the header reads as a list of them. Real functions are
// in sys.h, where there is real work: memcpy, memset, and the printf
// marshalling the compiler cannot do alone.
//
// Every command gets a wrapper, generated from the device's own tables, so a
// command cannot be added without one appearing. A coverage test checks it
// from the other end.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sc8::cc {

// CMD_SPRITE_DEF becomes gpu_sprite_def. The manual's command table then maps
// one to one onto the header, which is the point of naming them this way.
std::string wrapperName(std::string_view prefix, std::string_view cmd);

// Which port aliases each GPU command reads, in argument order. The browser
// project keeps this beside the command table in gpu.ts. It moves to
// src/devices/gpu.h once the GPU lands in this tree.
struct CmdArgs {
  std::string_view cmd;
  std::vector<std::string_view> aliases;
};
const std::vector<CmdArgs>& gpuCmdArgs();
std::optional<std::vector<std::string_view>> gpuArgsOf(std::string_view cmd);

std::string gpuHeader();
std::string apuHeader();
std::string acpHeader();
std::string ioHeader();
std::string romHeader();
std::string sysHeader();

// The C source of the few real functions, linked whether or not anything
// asks, the way nobody writes -lc.
extern const char* const LIBC_SOURCE;

const std::map<std::string, std::string>& headers();
std::vector<std::string> headerNames();

}  // namespace sc8::cc
