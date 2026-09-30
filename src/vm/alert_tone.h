// The IDE's sound for a failed build: two falling notes, the second a
// fifth below the first, at the audio device's rate. Audio::alert plays it
// over whatever the machine plays.
#pragma once

#include "devices/apu_ports.h"

namespace sc8::alert {

constexpr int RATE = apu::AUDIO_RATE;
constexpr int FIRST_HZ = 660;
constexpr int SECOND_HZ = 440;
constexpr int FIRST_SAMPLES = RATE * 90 / 1000;
constexpr int SECOND_SAMPLES = RATE * 150 / 1000;
constexpr int SAMPLES = FIRST_SAMPLES + SECOND_SAMPLES;
// Each note rises and falls over 5 ms, so neither its start nor its end
// clicks.
constexpr int RAMP = RATE * 5 / 1000;
// The loudest the tone gets. The machine's own sound is mixed beside it.
constexpr float LEVEL = 0.3f;

// Sample i of the tone, 0 outside 0 to SAMPLES - 1.
float sample(int i);

}  // namespace sc8::alert
