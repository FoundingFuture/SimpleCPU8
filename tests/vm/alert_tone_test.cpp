// The IDE's sound for a failed build: two falling notes, mixed into the
// audio output beside the machine's own sound. The tone is a function of
// the sample index, so its shape is checked here without a sound device.

#include <doctest.h>

#include <cmath>

#include "vm/alert_tone.h"

using namespace sc8;

namespace {

// Sign changes from one sample to the next over [from, to).
int crossings(int from, int to) {
  int n = 0;
  for (int i = from + 1; i < to; i++) {
    if ((alert::sample(i - 1) < 0) != (alert::sample(i) < 0)) n++;
  }
  return n;
}

}  // namespace

TEST_SUITE("the build error tone") {
  TEST_CASE("it is silent outside its length and at both ends") {
    CHECK_EQ(alert::sample(-1), 0.0f);
    CHECK_EQ(alert::sample(alert::SAMPLES), 0.0f);
    CHECK_EQ(alert::sample(0), 0.0f);
    CHECK(std::fabs(alert::sample(alert::SAMPLES - 1)) < 0.02f);
  }

  TEST_CASE("it lasts about a quarter of a second") {
    CHECK(alert::SAMPLES >= alert::RATE / 5);
    CHECK(alert::SAMPLES <= alert::RATE * 3 / 10);
  }

  TEST_CASE("it stays at its level, and reaches it") {
    float peak = 0.0f;
    for (int i = 0; i < alert::SAMPLES; i++) peak = std::fmax(peak, std::fabs(alert::sample(i)));
    CHECK(peak <= alert::LEVEL);
    CHECK(peak > alert::LEVEL * 0.9f);
  }

  TEST_CASE("the first note is higher than the second, at their pitches") {
    // Two sign changes a cycle, counted over each note's middle.
    const int a0 = alert::RAMP;
    const int a1 = alert::FIRST_SAMPLES - alert::RAMP;
    const int b0 = alert::FIRST_SAMPLES + alert::RAMP;
    const int b1 = alert::SAMPLES - alert::RAMP;
    const double first = crossings(a0, a1) / 2.0 / ((a1 - a0) / static_cast<double>(alert::RATE));
    const double second = crossings(b0, b1) / 2.0 / ((b1 - b0) / static_cast<double>(alert::RATE));
    CHECK(first == doctest::Approx(alert::FIRST_HZ).epsilon(0.05));
    CHECK(second == doctest::Approx(alert::SECOND_HZ).epsilon(0.05));
  }

  TEST_CASE("the notes meet at silence, so the change of pitch does not click") {
    CHECK(std::fabs(alert::sample(alert::FIRST_SAMPLES - 1)) < 0.02f);
    CHECK_EQ(alert::sample(alert::FIRST_SAMPLES), 0.0f);
  }
}
