# Every source of the project lives below this directory.
#
#   core/      the machine: ISA, signals, conflict rules, microcode, sequencer
#   devices/   the peripherals on the IO bus and their port and command tables
#   asm/       the assembler and its constant expression evaluator
#   assets/    image and audio decoding into what the assembler places
#   cc/        the C compiler
#   basic/     the BASIC interpreter, written in the machine's own C
#   tools/     command line front ends: simplecpu-asm, simplecpu-run, simplecpu-cc
#   vm/        the virtual computer and the simplecpu executable
#   ide/       simplecpu-ide, raylib and Dear ImGui

add_subdirectory(core)
add_subdirectory(devices)
add_subdirectory(asm)
add_subdirectory(assets)
add_subdirectory(cc)

if(SC8_BUILD_TOOLS)
  add_subdirectory(tools)
  # The BASIC ROM is compiled by simplecpu-cc and burnt by simplecpu-asm
  # from this same build, so it needs the tools.
  add_subdirectory(basic)
endif()

if(SC8_BUILD_IDE)
  add_subdirectory(vm)
  add_subdirectory(ide)
endif()
