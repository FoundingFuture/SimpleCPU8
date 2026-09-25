// The screen's effects as settings: the names, the presets, the strength
// and the text the IDE keeps them in. The shader itself needs a window,
// so the screenshots check it.
#include <doctest.h>

#include "vm/display.h"

using namespace sc8;

namespace {

double wide(float f) { return static_cast<double>(f); }

}  // namespace

TEST_SUITE("display settings") {
  TEST_CASE("every effect has a key that finds it, and a name") {
    for (int i = 0; i < EFFECT_COUNT; i++) {
      const auto e = static_cast<Effect>(i);
      Effect found = Effect::Sharp;
      REQUIRE(effectByKey(effectKey(e), &found));
      CHECK(found == e);
      CHECK(effectByKey(effectName(e), &found));
      CHECK(found == e);
    }
    Effect e{};
    CHECK(effectByKey("APERTURE-GRILLE", &e));
    CHECK(e == Effect::ApertureGrille);
    CHECK_FALSE(effectByKey("vhs", &e));
  }

  TEST_CASE("a preset sets the effect's strengths, and the mask only where one is drawn") {
    DisplaySettings d;
    d.usePreset(Effect::ShadowMask);
    CHECK(d.effect == Effect::ShadowMask);
    CHECK(d.mask > 0.0f);
    d.usePreset(Effect::Classic);
    CHECK(d.mask == 0.0f);
    CHECK(wide(d.scanlines) == doctest::Approx(0.5));
    CHECK(effectUses(Effect::ShadowMask, Knob::Mask));
    CHECK_FALSE(effectUses(Effect::Classic, Knob::Mask));
    CHECK_FALSE(effectUses(Effect::Sharp, Knob::Scanlines));
  }

  TEST_CASE("the strength scales the preset: half is the preset, none is off") {
    DisplaySettings d;
    d.usePreset(Effect::Classic);
    d.setStrength(0.5f);
    CHECK(d.enabled);
    CHECK(wide(d.scanlines) == doctest::Approx(0.5));
    d.setStrength(1.0f);
    CHECK(wide(d.scanlines) == doctest::Approx(1.0));
    CHECK(wide(d.curvature) == doctest::Approx(0.6));
    d.setStrength(0.0f);
    CHECK_FALSE(d.enabled);
  }

  TEST_CASE("the settings survive the text they are kept in") {
    DisplaySettings d;
    d.usePreset(Effect::SlotMask);
    d.bloom = 0.8f;
    d.integerScale = true;
    DisplaySettings back;
    back.fromText(d.toText());
    CHECK(back == d);
    // A line it does not know is skipped, and a bad number too.
    back.fromText("crt bloom = lots\nwhatever = 3\ncrt effect = lcd\n");
    CHECK(back.effect == Effect::Lcd);
    CHECK(wide(back.bloom) == doctest::Approx(0.8));
  }
}
