#!/usr/bin/env bash
# d: build a distribution of the release version for this machine's OS.
#
#   ./d                      dist/simplecpu-<version>-<os>-<arch>/ and an archive
#   ./d --arch arm64         macOS: build for one architecture
#   ./d --arch x86_64
#   ./d --arch universal     macOS: one binary for both, the default there
#   ./d --clean              configure the dist build from scratch
#   ./d --upload             also attach the archive to the GitHub Release
#                            v<version>, which the dist workflow made
#
# The six targets are macOS, Linux and Windows, each on arm64 and x86_64.
# .github/workflows/dist.yml builds Linux and Windows on hosts of each kind
# and drafts the release. macOS is built here, on a Mac, one architecture
# at a time, because the installer fetches the one for its machine:
#
#   ./d --arch arm64 --upload && ./d --arch x86_64 --upload
#
# The release is published by hand once all six archives are on it.
#
# The distribution holds the six programs, every example ROM, the example
# sources, the docs and the README. On macOS they sit in SimpleCPU-8.app:
# the programs in Contents/MacOS, the rest in Contents/Resources. The
# bundle targets macOS 13 and later and is signed ad hoc. Linux adds the
# launcher's icon. install.sh and install.ps1 read these layouts.
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
# Git Bash can be an x64 program under emulation on ARM64 Windows. Then
# uname -m, PROCESSOR_ARCHITECTURE and PowerShell, which inherits the
# emulation, all say x64. The machine's own value is in the registry.
if [ "$osname" = windows ]; then
  winarch="$(reg query 'HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment' //v PROCESSOR_ARCHITECTURE 2>/dev/null | tr -d '\r' | awk '/PROCESSOR_ARCHITECTURE/ {print $NF}')"
  case "$winarch" in
    ARM64) hostarch=arm64 ;;
    AMD64) hostarch=x86_64 ;;
  esac
fi

arch=""
clean=0
upload=0
while [ $# -gt 0 ]; do
  case "$1" in
    --arch) shift; arch="$1" ;;
    --clean) clean=1 ;;
    --upload) upload=1 ;;
    -h|--help) sed -n '2,27p' "$0"; exit 0 ;;
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
  # Info.plist's LSMinimumSystemVersion says the same.
  extra+=("-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0")
  case "$arch" in
    universal) extra+=("-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64") ;;
    arm64|x86_64) extra+=("-DCMAKE_OSX_ARCHITECTURES=$arch") ;;
  esac
fi
generator=()
if [ "$osname" = windows ]; then
  # DESIGN: Eddie's decision, 2026-10-08. Windows builds with MSVC and the
  # static C runtime, so each .exe stands alone and needs no redistributable.
  # Ninja is left out here: on a runner it finds MinGW's GCC first.
  case "$arch" in
    arm64) generator=(-G "Visual Studio 17 2022" -A ARM64) ;;
    *) generator=(-G "Visual Studio 17 2022" -A x64) ;;
  esac
  extra+=("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded")
elif command -v ninja >/dev/null 2>&1; then
  generator=(-G Ninja)
fi
# Configured every time, so a flag added here reaches an existing cache.
if [ -f "$build/CMakeCache.txt" ]; then generator=(); fi
cmake -S . -B "$build" ${generator[@]+"${generator[@]}"} -DCMAKE_BUILD_TYPE=Release -DSC8_BUILD_TESTS=OFF ${extra[@]+"${extra[@]}"}
cmake --build "$build" --config Release --parallel

out="dist/$name"
rm -rf "$out"
if [ "$osname" = macos ]; then
  app="$out/SimpleCPU-8.app"
  bin="$app/Contents/MacOS"
  share="$app/Contents/Resources"
else
  bin="$out/bin"
  share="$out"
fi
mkdir -p "$bin" "$share/roms" "$share/examples" "$share/docs"
copybin() {
  local src="$1"
  if [ -f "$src" ]; then cp "$src" "$bin/"; return; fi
  if [ -f "$src.exe" ]; then cp "$src.exe" "$bin/"; return; fi
  local d; d="$(dirname "$src")"; local b; b="$(basename "$src")"
  if [ -f "$d/Release/$b" ]; then cp "$d/Release/$b" "$bin/"; return; fi
  if [ -f "$d/Release/$b.exe" ]; then cp "$d/Release/$b.exe" "$bin/"; return; fi
  echo "d: $b was not built" >&2; exit 1
}
copybin "$build/src/vm/simplecpu"
copybin "$build/src/ide/simplecpu-ide"
copybin "$build/src/tools/simplecpu-asm"
copybin "$build/src/tools/simplecpu-cc"
copybin "$build/src/tools/simplecpu-make"
copybin "$build/src/tools/simplecpu-run"
cp "$build"/roms/*.rom "$share/roms/"
cp -R examples/. "$share/examples/"
rm -f "$share/examples/CMakeLists.txt" "$share/examples/extract-demos.mjs"
# Build output inside an example folder is not part of the example.
find "$share/examples" -type d -name build -prune -exec rm -rf {} +
cp -R docs/. "$share/docs/"
cp README.md "$share/"
case "$osname" in
  macos)
    sed "s/@VERSION@/$version/g" packaging/macos/Info.plist.in > "$app/Contents/Info.plist"
    cp packaging/icon/simplecpu-8.icns "$share/"
    # Ad hoc: Apple silicon runs nothing unsigned, and only an arm64 link
    # signs on its own. The other programs first, then the bundle, which
    # signs simplecpu-ide as its main executable.
    for f in "$bin"/*; do
      if [ "$(basename "$f")" != simplecpu-ide ]; then codesign --force --sign - "$f"; fi
    done
    codesign --force --sign - "$app"
    ;;
  linux)
    mkdir -p "$out/icon"
    cp packaging/icon/simplecpu-8.png "$out/icon/"
    ;;
esac

cd dist
rm -f "$name.zip" "$name.tar.gz"
if [ "$osname" = windows ]; then
  if command -v zip >/dev/null 2>&1; then zip -qr "$name.zip" "$name"; else
    powershell -NoProfile -Command "Compress-Archive -Path '$name' -DestinationPath '$name.zip'"
  fi
  archive="$name.zip"
else
  tar czf "$name.tar.gz" "$name"
  archive="$name.tar.gz"
fi
echo "dist/$archive"

if [ "$upload" = 1 ]; then
  gh release upload "v$version" "$archive" --clobber --repo FoundingFuture/SimpleCPU8
  echo "uploaded $archive to release v$version"
fi
