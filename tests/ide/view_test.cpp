// The IDE's four views: their names on the command line, the old level
// names they answer to, and the documents each one owns.

#include <doctest.h>

#include <optional>
#include <string>

#include "ide/view.h"

using sc8::DocKind;
using sc8::docKindOf;
using sc8::View;
using sc8::viewNamed;
using sc8::viewOfKind;
using sc8::viewOfLevel;

TEST_SUITE("the IDE's views") {
  TEST_CASE("--view names each view") {
    CHECK(viewNamed("basic") == View::Basic);
    CHECK(viewNamed("c") == View::C);
    CHECK(viewNamed("assembly") == View::Assembly);
    CHECK(viewNamed("microcode") == View::Microcode);
  }

  TEST_CASE("--view refuses a name that is not a view") {
    CHECK_FALSE(viewNamed("project"));
    CHECK_FALSE(viewNamed("BASIC"));
    CHECK_FALSE(viewNamed(""));
  }

  // The screenshot scripts still say --level with the names of the four
  // levels the views replaced.
  TEST_CASE("--level maps each old level to the view that took its place") {
    CHECK(viewOfLevel("basic") == View::Basic);
    CHECK(viewOfLevel("project") == View::C);
    CHECK(viewOfLevel("run") == View::Assembly);
    CHECK(viewOfLevel("cpu") == View::Microcode);
  }

  TEST_CASE("--level refuses a name that was never a level") {
    CHECK_FALSE(viewOfLevel("c"));
    CHECK_FALSE(viewOfLevel("assembly"));
    CHECK_FALSE(viewOfLevel(""));
  }

  TEST_CASE("every view has a key that names it back, and a label") {
    for (int i = 0; i < sc8::VIEW_COUNT; i++) {
      const auto v = static_cast<View>(i);
      CHECK(viewNamed(sc8::viewKey(v)) == v);
      CHECK_FALSE(std::string(sc8::viewLabel(v)).empty());
    }
    CHECK(std::string(sc8::viewLabel(View::Assembly)) == "ASSEMBLY");
  }

  TEST_CASE("a document's kind follows its file name") {
    CHECK(docKindOf("game.bas") == DocKind::Basic);
    CHECK(docKindOf("main.c") == DocKind::C);
    CHECK(docKindOf("ship.h") == DocKind::Header);
    CHECK(docKindOf("driver.asm") == DocKind::Assembly);
    CHECK(docKindOf("microcode.txt") == DocKind::Microcode);
    CHECK(docKindOf("notes.txt") == DocKind::Other);
    CHECK(docKindOf("ship.png") == DocKind::Other);
  }

  TEST_CASE("each kind of document belongs to one view") {
    CHECK(viewOfKind(DocKind::Basic) == View::Basic);
    CHECK(viewOfKind(DocKind::C) == View::C);
    CHECK(viewOfKind(DocKind::Header) == View::C);
    CHECK(viewOfKind(DocKind::Assembly) == View::Assembly);
    CHECK(viewOfKind(DocKind::Microcode) == View::Microcode);
    CHECK_FALSE(viewOfKind(DocKind::Other));
  }

  // The listing shows the editor's buttons with no document open, so it
  // asks the view which kind of document those buttons belong to.
  TEST_CASE("each view names the kind of document its editor holds") {
    CHECK(sc8::kindOfView(View::Basic) == DocKind::Basic);
    CHECK(sc8::kindOfView(View::C) == DocKind::C);
    CHECK(sc8::kindOfView(View::Assembly) == DocKind::Assembly);
    CHECK(sc8::kindOfView(View::Microcode) == DocKind::Microcode);
    for (int i = 0; i < sc8::VIEW_COUNT; i++) {
      const auto v = static_cast<View>(i);
      CHECK(viewOfKind(sc8::kindOfView(v)) == v);
    }
  }

  // docs/standalone.md, the speed ladder: BASIC runs frame locked, and
  // the microcode view starts where every row is drawn.
  TEST_CASE("each view starts at its own speed") {
    CHECK(std::string(sc8::defaultSpeed(View::Basic)) == "f60");
    CHECK(std::string(sc8::defaultSpeed(View::C)) == "f60");
    CHECK(std::string(sc8::defaultSpeed(View::Assembly)) == "f60");
    CHECK(std::string(sc8::defaultSpeed(View::Microcode)) == "trace");
  }

  // The listing shares the editor's tabs in the two views that have one,
  // and a run or a step brings it to the front. A step of BASIC or of C
  // shows its line in the editor.
  TEST_CASE("a run or a step brings the listing forward where it shares the editor's tabs") {
    CHECK(std::string(sc8::runTab(View::Assembly)) == "Listing");
    CHECK(std::string(sc8::runTab(View::Microcode)) == "Listing");
    CHECK(sc8::runTab(View::Basic) == nullptr);
    CHECK(sc8::runTab(View::C) == nullptr);
  }
}
