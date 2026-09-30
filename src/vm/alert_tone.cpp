#include "vm/alert_tone.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sc8::alert {

namespace {

// One note of n samples: a sine at hz under a ramp at each end.
float note(int i, int n, int hz) {
  const int edge = std::min(i, n - 1 - i);
  const float envelope = edge >= RAMP ? 1.0f : static_cast<float>(edge) / static_cast<float>(RAMP);
  const double phase = 2.0 * std::numbers::pi * hz * i / RATE;
  return LEVEL * envelope * static_cast<float>(std::sin(phase));
}

}  // namespace

float sample(int i) {
  if (i < 0 || i >= SAMPLES) return 0.0f;
  if (i < FIRST_SAMPLES) return note(i, FIRST_SAMPLES, FIRST_HZ);
  return note(i - FIRST_SAMPLES, SECOND_SAMPLES, SECOND_HZ);
}

}  // namespace sc8::alert
