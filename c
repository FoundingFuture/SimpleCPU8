#!/usr/bin/env bash
# c: configure and build.
#
#   ./c              release build into build/release
#   ./c --debug      debug build into build/debug
#   ./c --clean      remove the build directory first, then configure again
#   ./c --headless   tools and tests only, no window
#   ./c --test       run the tests after the build
#   ./c -j 8         parallel jobs (default: all cores)
#
# Release and debug keep their own directory and their own CMake cache, so
# each can be rebuilt without touching the other. Every flag combines.
set -euo pipefail
cd "$(dirname "$0")"

config=Release
dir=release
clean=0
headless=OFF
runtests=0
jobs=""
while [ $# -gt 0 ]; do
  case "$1" in
    --debug) config=Debug; dir=debug ;;
    --release) config=Release; dir=release ;;
    --clean) clean=1 ;;
    --headless) headless=ON ;;
    --test) runtests=1 ;;
    -j) shift; jobs="$1" ;;
    -j*) jobs="${1#-j}" ;;
    -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
    *) echo "c: unknown flag $1" >&2; exit 2 ;;
  esac
  shift
done

build="build/$dir"
if [ "$headless" = ON ]; then build="$build-headless"; fi
if [ "$clean" = 1 ]; then rm -rf "$build"; fi

generator=""
if command -v ninja >/dev/null 2>&1; then generator="-G Ninja"; fi
if [ ! -f "$build/CMakeCache.txt" ]; then
  ide=ON
  if [ "$headless" = ON ]; then ide=OFF; fi
  # shellcheck disable=SC2086
  cmake -S . -B "$build" $generator -DCMAKE_BUILD_TYPE="$config" -DSC8_BUILD_IDE="$ide"
fi

if [ -n "$jobs" ]; then
  cmake --build "$build" --config "$config" -j "$jobs"
else
  cmake --build "$build" --config "$config" --parallel
fi

if [ "$runtests" = 1 ]; then
  ctest --test-dir "$build" -C "$config" --output-on-failure
fi
