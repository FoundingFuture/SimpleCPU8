// install.sh against an archive laid out as ./d lays one out, in a fake
// home. The examples must land where Settings::examplesDir() looks, so the
// script and the IDE agree on the folder they both spell.

#include <doctest.h>

#if !defined(_WIN32)

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "fake_home.h"
#include "ide/settings.h"

namespace fs = std::filesystem;
using sc8::Settings;
using sc8test::FakeHome;
using sc8test::setEnv;

namespace {

void write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << text;
}

std::string read(const fs::path& p) {
  std::ifstream in(p);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

int count(const std::string& text, const std::string& what) {
  int n = 0;
  for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) n++;
  return n;
}

// The folder ./d makes for this platform, with two programs and one
// example, packed as the release archive.
struct Archive {
  fs::path file;
  explicit Archive(const fs::path& root) {
    const std::string top = "simplecpu-0.0.0-test";
    const fs::path dir = root / "fixture" / top;
#if defined(__APPLE__)
    const fs::path bin = dir / "SimpleCPU-8.app" / "Contents" / "MacOS";
    const fs::path share = dir / "SimpleCPU-8.app" / "Contents" / "Resources";
#else
    const fs::path bin = dir / "bin";
    const fs::path share = dir;
    write(dir / "icon" / "simplecpu-8.png", "png");
#endif
    write(bin / "simplecpu-ide", "#!/bin/sh\n");
    write(bin / "simplecpu-asm", "#!/bin/sh\n");
    write(share / "examples" / "c" / "hello" / "README.md", "# Hello\n");
    file = root / "simplecpu-0.0.0-test.tar.gz";
    const std::string cmd = "tar czf \"" + file.string() + "\" -C \"" + (root / "fixture").string() + "\" " + top;
    REQUIRE(std::system(cmd.c_str()) == 0);
  }
};

// The stand-in for lsregister: it writes its arguments to a file, so no
// test registers an app with the real LaunchServices database.
fs::path fakeRegister(const fs::path& root) {
  const fs::path script = root / "lsregister";
  write(script, "#!/bin/sh\necho \"$@\" >> \"" + (root / "registered.txt").string() + "\"\n");
  fs::permissions(script, fs::perms::owner_all);
  return script;
}

int install(const Archive& a, const fs::path& root, const std::string& flags, const std::string& shell = "/bin/sh") {
  const std::string cmd = "env -u ZDOTDIR -u XDG_DATA_HOME SHELL=" + shell + " SIMPLECPU_LSREGISTER=\"" +
                          fakeRegister(root).string() + "\" sh \"" SC8_PACKAGING_DIR "/install.sh\" --archive \"" +
                          a.file.string() + "\" " + flags + " > /dev/null";
  return std::system(cmd.c_str());
}

}  // namespace

TEST_SUITE("install.sh") {
  // The usage is the header comment, every line of it after the #! line.
  TEST_CASE("--help prints the whole header") {
    FakeHome home;
    std::ifstream in(SC8_PACKAGING_DIR "/install.sh");
    std::string line, header;
    std::getline(in, line);
    while (std::getline(in, line) && line.rfind("#", 0) == 0) header += line + "\n";
    REQUIRE(header.find("--archive") != std::string::npos);
    const fs::path out = home.root / "help.txt";
    const std::string cmd = "sh \"" SC8_PACKAGING_DIR "/install.sh\" --help > \"" + out.string() + "\"";
    REQUIRE(std::system(cmd.c_str()) == 0);
    CHECK(read(out) == header);
  }

  TEST_CASE("the examples replace what the settings folder held") {
    FakeHome home;
    setEnv("HOME", home.root.string());
    Archive a(home.root);
    write(Settings::examplesDir() / "old" / "README.md", "# gone\n");
    REQUIRE(install(a, home.root, "--no-path") == 0);
    CHECK(read(Settings::examplesDir() / "c" / "hello" / "README.md") == "# Hello\n");
    CHECK_FALSE(fs::exists(Settings::examplesDir() / "old"));
  }

#if defined(__APPLE__)
  TEST_CASE("macOS: the app goes to ~/Applications") {
    FakeHome home;
    Archive a(home.root);
    REQUIRE(install(a, home.root, "--no-path") == 0);
    CHECK(fs::exists(home.root / "Applications" / "SimpleCPU-8.app" / "Contents" / "MacOS" / "simplecpu-ide"));
    CHECK_FALSE(fs::exists(home.root / ".zprofile"));
  }

  // The Apps view and Spotlight list what LaunchServices knows. Finder
  // registers an app it copies, and a cp from a script does not.
  TEST_CASE("macOS: the app is registered with LaunchServices") {
    FakeHome home;
    Archive a(home.root);
    REQUIRE(install(a, home.root, "--no-path") == 0);
    const fs::path app = home.root / "Applications" / "SimpleCPU-8.app";
    CHECK(read(home.root / "registered.txt") == "-f " + app.string() + "\n");
  }

  TEST_CASE("macOS: --path adds the programs' folder to the profile once") {
    FakeHome home;
    Archive a(home.root);
    REQUIRE(install(a, home.root, "--path", "/bin/zsh") == 0);
    REQUIRE(install(a, home.root, "--path", "/bin/zsh") == 0);
    const std::string profile = read(home.root / ".zprofile");
    CHECK(count(profile, "Applications/SimpleCPU-8.app/Contents/MacOS") == 1);
  }
#else
  TEST_CASE("Linux: the programs, the launcher and its icon go to ~/.local/share") {
    FakeHome home;
    setEnv("HOME", home.root.string());
    Archive a(home.root);
    REQUIRE(install(a, home.root, "--no-path") == 0);
    const fs::path share = home.root / ".local" / "share";
    CHECK(fs::exists(share / "simplecpu-8" / "bin" / "simplecpu-ide"));
    CHECK(fs::exists(share / "icons" / "hicolor" / "256x256" / "apps" / "simplecpu-8.png"));
    const std::string desktop = read(share / "applications" / "simplecpu-8.desktop");
    CHECK(count(desktop, (share / "simplecpu-8" / "bin" / "simplecpu-ide").string()) == 1);
    CHECK_FALSE(fs::exists(home.root / ".local" / "bin" / "simplecpu-asm"));
  }

  TEST_CASE("Linux: --path links the programs into ~/.local/bin") {
    FakeHome home;
    setEnv("HOME", home.root.string());
    Archive a(home.root);
    REQUIRE(install(a, home.root, "--path") == 0);
    CHECK(fs::is_symlink(home.root / ".local" / "bin" / "simplecpu-asm"));
    CHECK(fs::is_symlink(home.root / ".local" / "bin" / "simplecpu-ide"));
  }
#endif
}

#endif
