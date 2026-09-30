// When the IDE's host loop may sleep until the next input event.

#include <doctest.h>

#include "vm/redraw.h"

using sc8::RedrawPacer;

namespace {

// How many frames an idle loop draws before the one that waits.
int framesBeforeWait(RedrawPacer& p) {
  int n = 0;
  while (!p.waitAfterFrame(true)) {
    n++;
    REQUIRE(n < 100);
  }
  return n;
}

}  // namespace

TEST_SUITE("the idle redraw") {
  TEST_CASE("a busy loop never waits") {
    RedrawPacer p;
    for (int i = 0; i < 100; i++) CHECK_FALSE(p.waitAfterFrame(false));
  }

  TEST_CASE("an idle loop draws SETTLE frames, then waits after the next") {
    RedrawPacer p;
    CHECK(framesBeforeWait(p) == RedrawPacer::SETTLE);
  }

  TEST_CASE("the frame after a wait was woken by input, so SETTLE frames follow it") {
    RedrawPacer p;
    framesBeforeWait(p);
    CHECK(framesBeforeWait(p) == RedrawPacer::SETTLE);
    CHECK(framesBeforeWait(p) == RedrawPacer::SETTLE);
  }

  TEST_CASE("a busy frame starts the count again") {
    RedrawPacer p;
    CHECK_FALSE(p.waitAfterFrame(true));
    CHECK_FALSE(p.waitAfterFrame(true));
    CHECK_FALSE(p.waitAfterFrame(false));
    CHECK(framesBeforeWait(p) == RedrawPacer::SETTLE);
  }
}
