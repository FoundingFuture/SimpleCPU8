#include <doctest.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "cc/build.h"
#include "cc/lex.h"

using namespace sc8::cc;

// The build line. A comment in the file that holds main, with the real
// flags, in place of a language of its own.

namespace {

std::string boom(const std::function<void()>& fn) {
  try { fn(); } catch (const CcError& e) { return e.message(); }
  return "no error";
}

bool has(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

using Lines = std::vector<std::pair<std::string, std::vector<std::string>>>;

std::vector<BuildSource> files(std::initializer_list<const char*> names) {
  std::vector<BuildSource> out;
  for (const char* n : names) out.push_back({n, ""});
  return out;
}

}  // namespace

TEST_SUITE("one line") {
  TEST_CASE("reads the tool, the output, the files and the archives") {
    const BuildLine b = parseBuildLine("cc -o game main.c menu.c -lgpu -lio", "main.c");
    CHECK(b.tool == "cc");
    CHECK(b.output == "game");
    CHECK(b.files == std::vector<std::string>{"main.c", "menu.c"});
    CHECK(b.libs == std::vector<std::string>{"gpu", "io"});
  }

  TEST_CASE("takes -l with a space after it, the way a real linker does") {
    CHECK(parseBuildLine("cc -o g main.c -l gpu", "main.c").libs == std::vector<std::string>{"gpu"});
  }

  TEST_CASE("drops a .a suffix, because -lgpu and -lgpu.a mean the same archive") {
    CHECK(parseBuildLine("cc -o g main.c -lgpu.a", "main.c").libs == std::vector<std::string>{"gpu"});
  }

  TEST_CASE("keeps a machine option as a flag") {
    const BuildLine b = parseBuildLine("cc -o g main.c -msoft-mul", "main.c");
    CHECK(std::find(b.flags.begin(), b.flags.end(), "-msoft-mul") != b.flags.end());
  }

  TEST_CASE("reads ld as well as cc, for the two step shape") {
    const BuildLine b = parseBuildLine("ld -o g main.o menu.o -lgpu", "main.c");
    CHECK(b.tool == "ld");
    CHECK(b.files == std::vector<std::string>{"main.o", "menu.o"});
  }

  TEST_CASE("refuses a line that starts with something else, and says what to write") {
    CHECK(has(boom([] { parseBuildLine("make all", "main.c"); }), "cc -o name main.c"));
  }

  TEST_CASE("refuses -o with nothing after it") {
    CHECK(has(boom([] { parseBuildLine("cc -o", "main.c"); }), "-o needs a name"));
  }
}

TEST_SUITE("a whole project") {
  TEST_CASE("compiles what it was given when no file carries a line") {
    const BuildPlan p = planBuild(files({"main.c", "two.c"}), {});
    CHECK(p.files == std::vector<std::string>{"main.c", "two.c"});
    CHECK(!p.from);
  }

  TEST_CASE("takes the order from the line, because the first file holds slot 0") {
    const BuildPlan p = planBuild(files({"a.c", "main.c"}), Lines{{"main.c", {"cc -o g main.c a.c"}}});
    CHECK(p.files == std::vector<std::string>{"main.c", "a.c"});
  }

  TEST_CASE("turns the .o names of an ld line back into sources") {
    const BuildPlan p = planBuild(files({"main.c", "menu.c"}),
                                  Lines{{"main.c", {"cc -c main.c menu.c", "ld -o g main.o menu.o -lgpu"}}});
    CHECK(p.files == std::vector<std::string>{"main.c", "menu.c"});
    CHECK(p.libs == std::vector<std::string>{"gpu"});
  }

  TEST_CASE("carries the machine option through") {
    const BuildPlan p = planBuild(files({"main.c"}), Lines{{"main.c", {"cc -o g main.c -msoft-mul"}}});
    CHECK(p.softMul);
  }

  TEST_CASE("names both files when two carry a line") {
    const std::string m = boom([] {
      planBuild(files({"a.c", "b.c"}), Lines{{"a.c", {"cc -o g a.c"}}, {"b.c", {"cc -o g b.c"}}});
    });
    CHECK(has(m, "a.c and b.c"));
    CHECK(has(m, "Exactly one file"));
  }

  TEST_CASE("names a file the line asks for and the project does not have") {
    const std::string m = boom([] {
      planBuild(files({"main.c"}), Lines{{"main.c", {"cc -o g main.c gone.c"}}});
    });
    CHECK(has(m, "gone.c"));
    CHECK(has(m, "main.c"));
  }

  TEST_CASE("keeps the lines as written, for the Files tab to show") {
    const BuildPlan p = planBuild(files({"main.c"}), Lines{{"main.c", {"cc -o g main.c -lgpu"}}});
    CHECK(p.lines == std::vector<std::string>{"cc -o g main.c -lgpu"});
  }
}
