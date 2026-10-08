// The numbers on a saved settings line, and the environment the settings
// folder is found from.

#include <doctest.h>

#include <optional>
#include <string>

#include "fake_home.h"
#include "ide/ini_line.h"
#include "ide/settings.h"

using sc8::envVar;
using sc8::readInts;

TEST_SUITE("a settings line's numbers") {
  TEST_CASE("reads one number after its key") {
    int a = 0;
    CHECK(readInts("Zoom=12", "Zoom", a));
    CHECK_EQ(a, 12);
  }

  TEST_CASE("reads two numbers split by a comma") {
    int a = 0, b = 0;
    CHECK(readInts("Pos=-40,300", "Pos", a, b));
    CHECK_EQ(a, -40);
    CHECK_EQ(b, 300);
  }

  TEST_CASE("refuses another key, or a key that only starts the same") {
    int a = 0;
    CHECK_FALSE(readInts("Size=3", "Pos", a));
    CHECK_FALSE(readInts("PreviewSize=3", "Preview", a));
    CHECK_FALSE(readInts("Fullscreen=1", "NativeFullscreen", a));
  }

  TEST_CASE("refuses a line short of its numbers") {
    int a = 0, b = 0;
    CHECK_FALSE(readInts("Pos=", "Pos", a));
    CHECK_FALSE(readInts("Pos=x", "Pos", a));
    CHECK_FALSE(readInts("Pos=4", "Pos", a, b));
    CHECK_FALSE(readInts("Pos=4,", "Pos", a, b));
  }

  TEST_CASE("refuses a number past an int") {
    int a = 0;
    CHECK_FALSE(readInts("Zoom=99999999999", "Zoom", a));
  }
}

TEST_SUITE("an environment variable") {
  TEST_CASE("reads a variable that is set") {
    sc8test::setEnv("SC8_ENV_TEST", "a value");
    CHECK_EQ(envVar("SC8_ENV_TEST"), std::optional<std::string>("a value"));
    sc8test::unsetEnv("SC8_ENV_TEST");
  }

  TEST_CASE("gives nothing for a variable that is not set") {
    sc8test::unsetEnv("SC8_ENV_TEST");
    CHECK_FALSE(envVar("SC8_ENV_TEST").has_value());
  }
}
