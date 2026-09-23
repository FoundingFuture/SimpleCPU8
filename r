#!/usr/bin/env bash
# r: run a program from the build.
#
#   ./r                         simplecpu --basic, release build
#   ./r --debug                 the same from build/debug
#   ./r pacman                  simplecpu --rom build/release/roms/pacman.rom
#   ./r game.rom --fps 30       any ROM path, flags go to simplecpu
#   ./r --ide [file]            simplecpu-ide
#   ./r --asm main.asm          simplecpu-asm, --cc and --run likewise
#
# Builds first when the build directory is missing.
set -euo pipefail
cd "$(dirname "$0")"

dir=release
prog=simplecpu
args=()
while [ $# -gt 0 ]; do
  case "$1" in
    --debug) dir=debug ;;
    --release) dir=release ;;
    --ide) prog=simplecpu-ide ;;
    --asm) prog=simplecpu-asm ;;
    --cc) prog=simplecpu-cc ;;
    --run) prog=simplecpu-run ;;
    -h|--help) sed -n '2,11p' "$0"; exit 0 ;;
    *) args+=("$1") ;;
  esac
  shift
done

build="build/$dir"
if [ ! -f "$build/CMakeCache.txt" ]; then
  if [ "$dir" = debug ]; then ./c --debug; else ./c; fi
fi

case "$prog" in
  simplecpu) bin="$build/src/vm/simplecpu" ;;
  simplecpu-ide) bin="$build/src/ide/simplecpu-ide" ;;
  *) bin="$build/src/tools/$prog" ;;
esac
# Multi-config generators (Visual Studio, Xcode) put the binary one level down.
config=Release
if [ "$dir" = debug ]; then config=Debug; fi
if [ ! -x "$bin" ] && [ -x "$bin.exe" ]; then bin="$bin.exe"; fi
if [ ! -x "$bin" ] && [ -x "$(dirname "$bin")/$config/$(basename "$bin")" ]; then
  bin="$(dirname "$bin")/$config/$(basename "$bin")"
fi
if [ ! -x "$bin" ] && [ -x "$(dirname "$bin")/$config/$(basename "$bin").exe" ]; then
  bin="$(dirname "$bin")/$config/$(basename "$bin").exe"
fi

if [ "$prog" = simplecpu ]; then
  if [ ${#args[@]} -eq 0 ]; then
    exec "$bin" --basic
  fi
  first="${args[0]}"
  case "$first" in
    -*) exec "$bin" "${args[@]}" ;;
    *.rom) exec "$bin" --rom "${args[@]}" ;;
    *)
      rom="$build/roms/$first.rom"
      if [ -f "$rom" ]; then
        exec "$bin" --rom "$rom" "${args[@]:1}"
      fi
      exec "$bin" --rom "${args[@]}"
      ;;
  esac
fi
exec "$bin" "${args[@]}"
