# SimpleCPU-8

An educational 8 bit CPU with user-editable microcode. A GPU, an audio chip
and an arithmetic coprocessor sit on its IO ports. This version is a native
program for Windows, macOS and Linux. The machine, the assembler and the tools are C++20. The
BASIC interpreter is written in the machine's own C dialect and runs on the
CPU.

This is the standalone successor of the browser version in the SimpleCPU
repository. docs/design/ carries that project's design record, and
docs/standalone.md records what this version adds.

## Install

packaging/install.sh installs the latest GitHub release on macOS and Linux.
packaging/install.ps1 does the same on Windows. Each one picks the archive for
the machine's system and processor, arm64 or x86_64. Each install installs
for the current user and needs no administrator rights.

```bash
sh packaging/install.sh                 # asks whether to add the programs to PATH
sh packaging/install.sh --path          # adds them without asking
sh packaging/install.sh --version 0.1.0
sh packaging/install.sh --archive dist/simplecpu-0.1.0-macos-arm64.tar.gz
```

```powershell
.\packaging\install.ps1 -Path
```

| System | Programs | Examples |
|---|---|---|
| macOS | `~/Applications/SimpleCPU-8.app` | `~/Library/Application Support/SimpleCPU-8/examples` |
| Linux | `~/.local/share/simplecpu-8`, launcher in the applications menu | `~/.config/simplecpu-8/examples` |
| Windows | `%LOCALAPPDATA%\Programs\SimpleCPU-8`, Start Menu entry | `%APPDATA%\SimpleCPU-8\examples` |

The IDE's File, Open example lists the examples folder. A build from the
checkout reads the same folder, so it shows the examples of the last
install. Every install replaces that folder.

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
./d --arch arm64 --upload   # macOS: attach the archive to the draft release
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
build/src/tools/simplecpu-asm examples/assembly/hello/hello.asm --title "List sum"
build/src/vm/simplecpu --rom examples/assembly/hello/hello.rom --fps 60
build/src/ide/simplecpu-ide examples/assembly/hello/hello.asm
build/src/tools/simplecpu-run examples/assembly/hello/hello.rom --ram 0 4
build/src/vm/simplecpu --basic
build/src/vm/simplecpu --basic hello.bas --run
build/src/vm/simplecpu --rom build/roms/basic.rom
build/src/vm/simplecpu --rom build/roms/pacman.rom --fps 60
build/src/tools/simplecpu-make examples/c/hello-c
```

The build burns every example into build/roms. examples/README.md lists
them. Pac-Man is build/roms/pacman.rom: arrows move, Z fires.

Keys in simplecpu: F1 toggles the CRT look. F2 and F3 turn it down and up.
F5 powers the machine on again. F11 goes fullscreen. Quit with the
operating system's gesture. That is Command-Q on macOS, and Alt-F4 or the
close button elsewhere. Every key reaches the machine. So Escape and Ctrl-C
break a running BASIC program, as they did on the old home computers.

Keys in simplecpu-ide: F1 to F4 switch the level: BASIC, Project, Run and
CPU. F7 builds and F8 burns a ROM. F5 runs or pauses. F6 runs one frame.
F10 steps an instruction and F11 steps a microcycle. F12 shows the
machine's screen alone, and Escape comes back.

## Manuals

docs/guides holds five manuals. basic.md teaches BASIC from the start,
c.md then C, assembly.md the machine's own instructions and microcode.md
the rows inside them. command-line.md covers the tools. It explains how
to start a project, build a ROM and run it without the IDE.

## Distributions

`./d` builds the release version and packs the six programs, every
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
