# install.ps1: install SimpleCPU-8 for this user on Windows.
#
# In PowerShell:
#
#   irm https://foundingfuture.com/downloads/install.ps1 | iex
#
# From cmd.exe:
#
#   powershell -c "irm https://foundingfuture.com/downloads/install.ps1 | iex"
#
# A pipe into iex passes no parameters. Run the script as a script block
# to give them:
#
#   & ([scriptblock]::Create((irm https://foundingfuture.com/downloads/install.ps1))) -Path
#   -Path                  put the programs on PATH
#   -NoPath                leave PATH alone, and do not ask
#   -Version 0.1.0         a given release
#   -Archive <file>        an archive on disk, with no download, as
#                          .\install.ps1 -Archive dist\simplecpu-0.1.0-windows-x86_64.zip
#
# The archive comes from the GitHub release for this machine, arm64 or
# x86_64. Without -Version that is the latest release. GitHub never counts
# a pre-release as the latest, so a pre-release installs by its version
# alone.
#
# %LOCALAPPDATA%\Programs\SimpleCPU-8 holds the programs, the ROMs and the
# docs. The Start Menu gets SimpleCPU-8 for the IDE. -Path puts the bin
# folder on the user's PATH. The examples go to %APPDATA%\SimpleCPU-8\examples,
# where the IDE reads them (Settings::examplesDir in src/ide/settings.h).
# Each install replaces that folder. Without -Path or -NoPath the script
# asks.

param(
  [string]$Version = "",
  [string]$Archive = "",
  [switch]$Path,
  [switch]$NoPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 3

$Repo = "FoundingFuture/SimpleCPU8"

function Fail($message) { throw "install.ps1: $message" }

function Get-Arch {
  $os = ""
  try { $os = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString() } catch { }
  if (-not $os) {
    # A 32-bit or emulated PowerShell reports the machine in PROCESSOR_ARCHITEW6432.
    $os = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
  }
  switch -Regex ($os) {
    "^(Arm64|ARM64)$" { return "arm64" }
    "^(X64|AMD64)$" { return "x86_64" }
    default { Fail "no SimpleCPU-8 build for $os" }
  }
}

function Get-LatestVersion {
  try {
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/latest" -UseBasicParsing
  } catch {
    Fail "no SimpleCPU-8 release is published yet, or github.com cannot be reached"
  }
  return $release.tag_name.TrimStart("v")
}

# Windows PowerShell 5.1 does not offer TLS 1.2 by default, and GitHub needs it.
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$arch = Get-Arch
$work = Join-Path ([System.IO.Path]::GetTempPath()) ("simplecpu-install-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $work | Out-Null
try {
  if (-not $Archive) {
    if (-not $Version) { $Version = Get-LatestVersion }
    $Version = $Version.TrimStart("v")
    $name = "simplecpu-$Version-windows-$arch.zip"
    Write-Host "Downloading SimpleCPU-8 $Version for Windows $arch"
    $Archive = Join-Path $work $name
    try {
      $ProgressPreference = "SilentlyContinue"
      Invoke-WebRequest -Uri "https://github.com/$Repo/releases/download/v$Version/$name" -OutFile $Archive -UseBasicParsing
    } catch {
      Fail "cannot download $name from release v$Version"
    }
  }
  if (-not (Test-Path $Archive -PathType Leaf)) { Fail "no file $Archive" }

  $unpacked = Join-Path $work "unpacked"
  Expand-Archive -Path $Archive -DestinationPath $unpacked
  $tops = @(Get-ChildItem -Path $unpacked -Directory)
  if ($tops.Count -ne 1) { Fail "$Archive should hold one folder" }
  $top = $tops[0].FullName
  if (-not (Test-Path (Join-Path $top "bin\simplecpu-ide.exe"))) { Fail "the archive holds no bin\simplecpu-ide.exe" }

  $dest = Join-Path $env:LOCALAPPDATA "Programs\SimpleCPU-8"
  if (Test-Path $dest) {
    try { Remove-Item -Recurse -Force $dest } catch { Fail "cannot replace $dest. Close SimpleCPU-8 and run the installer again." }
  }
  New-Item -ItemType Directory -Path $dest | Out-Null
  Copy-Item -Path (Join-Path $top "*") -Destination $dest -Recurse

  # The settings folder, as src/ide/settings.cpp finds it. The examples
  # live there alone.
  $settings = Join-Path $env:APPDATA "SimpleCPU-8"
  $examples = Join-Path $settings "examples"
  New-Item -ItemType Directory -Force -Path $settings | Out-Null
  if (Test-Path $examples) { Remove-Item -Recurse -Force $examples }
  Move-Item -Path (Join-Path $dest "examples") -Destination $examples

  $bin = Join-Path $dest "bin"
  $startMenu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
  New-Item -ItemType Directory -Force -Path $startMenu | Out-Null
  $shell = New-Object -ComObject WScript.Shell
  $link = $shell.CreateShortcut((Join-Path $startMenu "SimpleCPU-8.lnk"))
  $link.TargetPath = Join-Path $bin "simplecpu-ide.exe"
  $link.WorkingDirectory = $env:USERPROFILE
  $link.IconLocation = (Join-Path $bin "simplecpu-ide.exe") + ",0"
  $link.Description = "SimpleCPU-8 IDE"
  $link.Save()

  Write-Host "SimpleCPU-8 is in $dest, and in the Start Menu"
  Write-Host "The examples are in $examples"

  $addPath = $false
  if ($Path) { $addPath = $true }
  elseif (-not $NoPath) {
    try {
      $answer = Read-Host "Put the SimpleCPU-8 command line programs (simplecpu, simplecpu-asm, simplecpu-cc, simplecpu-make, simplecpu-run, simplecpu-ide) on your PATH? [y/N]"
      $addPath = $answer -match "^(y|yes)$"
    } catch { $addPath = $false }
  }
  if ($addPath) {
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $parts = @(if ($userPath) { $userPath.Split(";") | Where-Object { $_ } } else { @() })
    if ($parts -notcontains $bin) {
      [Environment]::SetEnvironmentVariable("Path", (@($parts) + $bin) -join ";", "User")
      Write-Host "Added $bin to your PATH. A new terminal picks it up."
    }
    if (($env:Path.Split(";")) -notcontains $bin) { $env:Path = "$env:Path;$bin" }
  }
} finally {
  Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
