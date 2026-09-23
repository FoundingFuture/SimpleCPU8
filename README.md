# SimpleCPU-8

An educational 8 bit CPU with user-editable microcode. A GPU, an audio chip
and an arithmetic coprocessor sit on its IO ports. This version is a native
program for Windows, macOS and Linux. The machine, the assembler and the tools are C++20. The
BASIC interpreter is written in the machine's own C dialect and runs on the
CPU.

This is the standalone successor of the browser version in the SimpleCPU
repository. docs/design/ carries that project's design record, and
docs/standalone.md records what this version adds.

## Build

Requirements: CMake 3.24 or newer and a C++20 compiler such as GCC 13,
Clang 16, MSVC 2022 or Xcode 15. The first configure needs network access.
It fetches raylib, miniaudio, Dear ImGui, rlImGui and doctest. Linux also needs
the X11 development packages raylib builds against: on Debian and Ubuntu,
`libx11-dev libxrandr-dev libxi-dev libgl1-mesa-dev libxcursor-dev
libxinerama-dev`.

```bash
./c            # release build into build/release
./c --debug    # debug build into build/debug, its own CMake cache
./c --clean    # remove the build directory and configure again
./c --test     # run the tests after the build
./r            # boot BASIC from the release build
./r pacman     # run build/release/roms/pacman.rom
./r --ide      # the IDE
./d            # a distribution for this machine's OS under dist/
```

The scripts are bash and run on macOS, Linux and Windows under Git Bash.
They call the CMake presets below, which work on their own too:

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

The `headless` preset builds the tools and tests without raylib, for a
server or a CI box with no display. `xcode` and `vs2022` generate IDE
projects.

## Run

```bash
build/src/tools/simplecpu-asm examples/hello/hello.asm --title "List sum"
build/src/vm/simplecpu --rom examples/hello/hello.rom --fps 60
build/src/ide/simplecpu-ide examples/hello/hello.asm
build/src/tools/simplecpu-run examples/hello/hello.rom --ram 0 4
build/src/vm/simplecpu --basic
build/src/vm/simplecpu --basic hello.bas --run
build/src/vm/simplecpu --rom build/roms/basic.rom
build/src/vm/simplecpu --rom build/roms/pacman.rom --fps 60
```

The build burns every example into build/roms. examples/README.md lists
them. Pac-Man is build/roms/pacman.rom: arrows move, Z fires.

Keys in simplecpu: F1 toggles the CRT look. F2 and F3 turn it down and up.
F5 powers the machine on again. F11 goes fullscreen. Quit with the
operating system's gesture. That is Command-Q on macOS, and Alt-F4 or the
close button elsewhere. Every key reaches the machine. So Escape and Ctrl-C
break a running BASIC program, as they did on the old home computers.

Keys in simplecpu-ide: F7 assembles and F8 burns a ROM. F5 runs or pauses.
F6 runs one frame. F10 steps an instruction and F11 steps a microcycle.

## Distributions

`./d` builds the release version and packs the five programs, every
example ROM, the example sources and the docs into
`dist/simplecpu-<version>-<os>-<arch>`, as a tar.gz or a zip. On a Mac it
builds a universal binary by default and `--arch arm64` or `--arch x86_64`
picks one. Linux and Windows build their own architecture. The six
distributions, macOS, Linux and Windows on arm64 and x86_64, come from
.github/workflows/dist.yml, which runs `./d` on a host of each kind on
every version tag.

## Layout

```text
CMakeLists.txt      the project
CMakePresets.json   configure, build and test presets
c, r, d            build, run and distribute (see below)
cmake/              FetchContent pins, warning flags, the embedders
src/                every source file, one directory per layer
tests/              doctest suites, one per library
docs/               the design record and the standalone notes
examples/           programs to assemble and run
```

src/CMakeLists.txt names each layer. docs/standalone.md describes the
targets and what depends on what.

## Status

The core, the assembler and the ROM format are ported and tested. The
virtual computer opens a window and paces the machine at a chosen frame
rate. The CRT shader runs over a test card until the GPU is ported. The IDE
assembles, runs, steps and shows the microcode. The GPU, input, audio chip,
coprocessor, C compiler and BASIC are the next ports, in that order.
