#!/usr/bin/env bash
# d: build a distribution of the release version for this machine's OS.
#
#   ./d                      dist/simplecpu-<version>-<os>-<arch>/ and an archive
#   ./d --arch arm64         macOS: build for one architecture
#   ./d --arch x86_64
#   ./d --arch universal     macOS: one binary for both, the default there
#   ./d --clean              configure the dist build from scratch
#
# The six targets (macOS, Linux and Windows, each on arm64 and x86_64) are
# built by .github/workflows/dist.yml on hosts of each kind. This script
# builds what the machine it runs on can: macOS builds both architectures,
# Linux and Windows build their own.
#
# The distribution holds the five programs, every example ROM, the example
# sources, the docs and the README.
set -euo pipefail
cd "$(dirname "$0")"

os="$(uname -s)"
case "$os" in
  Darwin) osname=macos ;;
  Linux) osname=linux ;;
  MINGW*|MSYS*|CYGWIN*|Windows_NT) osname=windows ;;
  *) osname="$(echo "$os" | tr '[:upper:]' '[:lower:]')" ;;
esac
hostarch="$(uname -m)"
case "$hostarch" in
  aarch64|arm64) hostarch=arm64 ;;
  x86_64|AMD64|amd64) hostarch=x86_64 ;;
esac

arch=""
clean=0
while [ $# -gt 0 ]; do
  case "$1" in
    --arch) shift; arch="$1" ;;
    --clean) clean=1 ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    *) echo "d: unknown flag $1" >&2; exit 2 ;;
  esac
  shift
done
if [ -z "$arch" ]; then
  if [ "$osname" = macos ]; then arch=universal; else arch="$hostarch"; fi
fi
if [ "$osname" != macos ] && [ "$arch" != "$hostarch" ]; then
  echo "d: $osname builds its own architecture here ($hostarch). The workflow builds the rest." >&2
  exit 2
fi

version="$(sed -n 's/^  VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1)"
name="simplecpu-$version-$osname-$arch"
build="build/dist-$osname-$arch"
if [ "$clean" = 1 ]; then rm -rf "$build"; fi

extra=()
if [ "$osname" = macos ]; then
  case "$arch" in
    universal) extra+=("-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64") ;;
    arm64|x86_64) extra+=("-DCMAKE_OSX_ARCHITECTURES=$arch") ;;
  esac
fi
generator=""
if command -v ninja >/dev/null 2>&1; then generator="-G Ninja"; fi
if [ ! -f "$build/CMakeCache.txt" ]; then
  # shellcheck disable=SC2086
  cmake -S . -B "$build" $generator -DCMAKE_BUILD_TYPE=Release -DSC8_BUILD_TESTS=OFF ${extra[@]+"${extra[@]}"}
fi
cmake --build "$build" --config Release --parallel

out="dist/$name"
rm -rf "$out"
mkdir -p "$out/bin" "$out/roms" "$out/examples" "$out/docs"
copybin() {
  local src="$1"
  if [ -f "$src" ]; then cp "$src" "$out/bin/"; return; fi
  if [ -f "$src.exe" ]; then cp "$src.exe" "$out/bin/"; return; fi
  local d; d="$(dirname "$src")"; local b; b="$(basename "$src")"
  if [ -f "$d/Release/$b" ]; then cp "$d/Release/$b" "$out/bin/"; return; fi
  if [ -f "$d/Release/$b.exe" ]; then cp "$d/Release/$b.exe" "$out/bin/"; return; fi
  echo "d: $b was not built" >&2; exit 1
}
copybin "$build/src/vm/simplecpu"
copybin "$build/src/ide/simplecpu-ide"
copybin "$build/src/tools/simplecpu-asm"
copybin "$build/src/tools/simplecpu-cc"
copybin "$build/src/tools/simplecpu-make"
copybin "$build/src/tools/simplecpu-run"
cp "$build"/roms/*.rom "$out/roms/"
cp -R examples/. "$out/examples/"
rm -f "$out/examples/CMakeLists.txt" "$out/examples/extract-demos.mjs"
cp -R docs/. "$out/docs/"
cp README.md "$out/"

cd dist
rm -f "$name.zip" "$name.tar.gz"
if [ "$osname" = windows ]; then
  if command -v zip >/dev/null 2>&1; then zip -qr "$name.zip" "$name"; else
    powershell -NoProfile -Command "Compress-Archive -Path '$name' -DestinationPath '$name.zip'"
  fi
  echo "dist/$name.zip"
else
  tar czf "$name.tar.gz" "$name"
  echo "dist/$name.tar.gz"
fi
