#!/bin/sh
# install.sh: install SimpleCPU-8 for this user, on macOS or Linux.
#
#   curl -fsSL <url>/install.sh | sh
#   curl -fsSL <url>/install.sh | sh -s -- --path     put the programs on PATH
#   curl -fsSL <url>/install.sh | sh -s -- --no-path  and do not ask
#   sh install.sh --version 0.1.0                       a given release
#   sh install.sh --archive dist/simplecpu-0.1.0-macos-arm64.tar.gz
#
# The archive comes from the GitHub release for this machine: macOS or
# Linux, arm64 or x86_64. Without --version that is the latest release.
#
# macOS   ~/Applications/SimpleCPU-8.app holds everything. The programs sit
#         in Contents/MacOS, so --path adds that folder to the shell's
#         profile.
# Linux   ~/.local/share/simplecpu-8 holds the programs, the ROMs and the
#         docs. The IDE gets a launcher entry and an icon under
#         ~/.local/share. --path links the programs into ~/.local/bin.
#
# Either way the examples go to the settings folder's examples/, where the
# IDE reads them (Settings::examplesDir in src/ide/settings.h). Each install
# replaces that folder. Without --path or --no-path the script asks, when
# there is a terminal to ask on.
#
# Everything runs from main at the end, so a download cut short runs
# nothing.

set -eu

REPO=FoundingFuture/SimpleCPU8
PROGRAMS="simplecpu simplecpu-ide simplecpu-asm simplecpu-cc simplecpu-make simplecpu-run"

say() { printf '%s\n' "$*"; }
fail() { printf 'install.sh: %s\n' "$*" >&2; exit 1; }

platform() {
  case "$(uname -s)" in
    Darwin) os=macos ;;
    Linux) os=linux ;;
    *) fail "this script installs on macOS and Linux. On Windows run install.ps1 in PowerShell." ;;
  esac
  case "$(uname -m)" in
    arm64|aarch64) arch=arm64 ;;
    x86_64|amd64) arch=x86_64 ;;
    *) fail "no SimpleCPU-8 build for $(uname -m)" ;;
  esac
  # A shell under Rosetta reports x86_64 on Apple silicon.
  if [ "$os" = macos ] && [ "$arch" = x86_64 ] && [ "$(sysctl -n sysctl.proc_translated 2>/dev/null || echo 0)" = 1 ]; then
    arch=arm64
  fi
}

# The settings folder, as src/ide/settings.cpp finds it.
settings_dir() {
  if [ "$os" = macos ]; then
    printf '%s\n' "$HOME/Library/Application Support/SimpleCPU-8"
  else
    printf '%s\n' "${XDG_CONFIG_HOME:-$HOME/.config}/simplecpu-8"
  fi
}

latest_version() {
  # The latest release page redirects to the tag page, /releases/tag/v<version>.
  url="$(curl -fsSLI -o /dev/null -w '%{url_effective}' "https://github.com/$REPO/releases/latest")" ||
    fail "cannot reach github.com"
  case "$url" in
    */releases/tag/v*) printf '%s\n' "${url##*/releases/tag/v}" ;;
    *) fail "no SimpleCPU-8 release is published yet" ;;
  esac
}

# Unpacks the archive into $work/unpacked and sets $top to the folder
# inside it.
unpack() {
  mkdir "$work/unpacked"
  tar xzf "$1" -C "$work/unpacked" || fail "cannot unpack $1"
  top=""
  for d in "$work/unpacked"/*/; do
    [ -d "$d" ] || continue
    [ -z "$top" ] || fail "$1 holds more than one folder"
    top="${d%/}"
  done
  [ -n "$top" ] || fail "$1 is empty"
}

install_examples() {
  cfg="$(settings_dir)"
  mkdir -p "$cfg"
  rm -rf "$cfg/examples"
  cp -R "$1" "$cfg/examples"
  examples="$cfg/examples"
}

install_macos() {
  apps="$HOME/Applications"
  app="$apps/SimpleCPU-8.app"
  [ -d "$top/SimpleCPU-8.app" ] || fail "the archive holds no SimpleCPU-8.app"
  mkdir -p "$apps"
  rm -rf "$app"
  cp -R "$top/SimpleCPU-8.app" "$app"
  # tar passes a downloaded archive's quarantine on to what it unpacks.
  xattr -dr com.apple.quarantine "$app" 2>/dev/null || true
  install_examples "$app/Contents/Resources/examples"
  bindir="$app/Contents/MacOS"
  say "SimpleCPU-8 is in $app"
}

install_linux() {
  data="${XDG_DATA_HOME:-$HOME/.local/share}"
  home="$data/simplecpu-8"
  [ -d "$top/bin" ] || fail "the archive holds no bin folder"
  rm -rf "$home"
  mkdir -p "$home"
  cp -R "$top"/. "$home"/
  # The examples live in the settings folder alone.
  install_examples "$home/examples"
  rm -rf "$home/examples"
  bindir="$home/bin"

  if [ -f "$home/icon/simplecpu-8.png" ]; then
    mkdir -p "$data/icons/hicolor/256x256/apps"
    cp "$home/icon/simplecpu-8.png" "$data/icons/hicolor/256x256/apps/simplecpu-8.png"
  fi
  mkdir -p "$data/applications"
  # StartupWMClass is the window's class, which GLFW takes from its title.
  cat > "$data/applications/simplecpu-8.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=SimpleCPU-8
GenericName=8-bit computer IDE
Comment=Write, build and run programs for the SimpleCPU-8 computer
Exec="$bindir/simplecpu-ide" %f
Icon=simplecpu-8
Terminal=false
Categories=Development;IDE;Education;
StartupWMClass=SimpleCPU-8 IDE
DESKTOP
  say "SimpleCPU-8 is in $home, and the IDE is in the applications menu"
}

# Asks on the terminal, since stdin is the script when it is piped in.
ask_path() {
  if [ -n "$path_choice" ]; then return; fi
  path_choice=no
  if (: < /dev/tty) 2>/dev/null; then
    printf 'Put the SimpleCPU-8 command line programs (%s) on your PATH? [y/N] ' "$PROGRAMS" > /dev/tty
    read -r answer < /dev/tty || answer=""
    case "$answer" in y|Y|yes|YES|Yes) path_choice=yes ;; esac
  fi
}

# The profile the user's login shell reads, or empty for a shell this
# script does not know.
profile_file() {
  case "$(basename "${SHELL:-sh}")" in
    zsh) if [ "$os" = macos ]; then printf '%s\n' "${ZDOTDIR:-$HOME}/.zprofile"; else printf '%s\n' "${ZDOTDIR:-$HOME}/.zshrc"; fi ;;
    bash) if [ "$os" = macos ]; then printf '%s\n' "$HOME/.bash_profile"; else printf '%s\n' "$HOME/.bashrc"; fi ;;
    sh|dash|ksh) printf '%s\n' "$HOME/.profile" ;;
    *) printf '\n' ;;
  esac
}

# Adds dir to PATH in the profile, once.
add_to_profile() {
  dir="$1"
  profile="$(profile_file)"
  # Written relative to $HOME when the folder is under it.
  case "$dir" in
    "$HOME"/*) shown="\$HOME${dir#"$HOME"}" ;;
    *) shown="$dir" ;;
  esac
  line="export PATH=\"$shown:\$PATH\""
  if [ -z "$profile" ]; then
    say "Add $dir to your PATH in your shell's startup file."
    return
  fi
  if [ -f "$profile" ] && grep -qF "$line" "$profile"; then return; fi
  printf '\n# SimpleCPU-8 command line programs\n%s\n' "$line" >> "$profile"
  say "Added $shown to PATH in $profile. A new terminal picks it up."
}

put_on_path() {
  if [ "$os" = macos ]; then
    add_to_profile "$bindir"
    return
  fi
  links="$HOME/.local/bin"
  mkdir -p "$links"
  for p in $PROGRAMS; do
    if [ -f "$bindir/$p" ]; then ln -sf "$bindir/$p" "$links/$p"; fi
  done
  say "Linked the programs into $links"
  case ":${PATH:-}:" in
    *":$links:"*) ;;
    *) add_to_profile "$links" ;;
  esac
}

main() {
  archive=""
  version=""
  path_choice=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --archive) [ $# -ge 2 ] || fail "--archive needs a file"; archive="$2"; shift ;;
      --version) [ $# -ge 2 ] || fail "--version needs a number"; version="${2#v}"; shift ;;
      --path) path_choice=yes ;;
      --no-path) path_choice=no ;;
      -h|--help)
        if [ -f "$0" ]; then sed -n '2,26p' "$0"; else say "flags: --path --no-path --version <v> --archive <file>"; fi
        exit 0 ;;
      *) fail "unknown flag $1" ;;
    esac
    shift
  done
  [ -n "${HOME:-}" ] || fail "HOME is not set"
  platform

  work="$(mktemp -d)"
  trap 'rm -rf "$work"' EXIT INT TERM

  if [ -z "$archive" ]; then
    command -v curl >/dev/null 2>&1 || fail "curl is needed to download SimpleCPU-8"
    [ -n "$version" ] || version="$(latest_version)"
    name="simplecpu-$version-$os-$arch.tar.gz"
    say "Downloading SimpleCPU-8 $version for $os $arch"
    curl -fSL --progress-bar -o "$work/$name" "https://github.com/$REPO/releases/download/v$version/$name" ||
      fail "cannot download $name from release v$version"
    archive="$work/$name"
  fi
  [ -f "$archive" ] || fail "no file $archive"
  unpack "$archive"

  if [ "$os" = macos ]; then install_macos; else install_linux; fi
  say "The examples are in $examples"

  ask_path
  if [ "$path_choice" = yes ]; then put_on_path; fi
}

main "$@"
