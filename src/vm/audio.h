// Audio output: the APU's 8 bit 8 kHz mono stream to the host's device
// through miniaudio. The APU renders on a real time clock, so audio plays
// now, at the right pitch, whatever the CPU speed.
#pragma once

#include <cstdint>
#include <span>

namespace sc8 {

class Audio {
 public:
  Audio();
  ~Audio();
  Audio(const Audio&) = delete;
  Audio& operator=(const Audio&) = delete;

  // Open the host device. False when no device could be opened, in which
  // case push() drops samples and the machine runs silent.
  bool start();
  void stop();
  bool running() const { return running_; }

  // Queue unsigned 8 bit mono samples at AUDIO_RATE. Called from the
  // emulation thread. Samples past the buffer are dropped.
  void push(std::span<const uint8_t> samples);

  // How many samples wait in the buffer, so the pump knows when to render.
  size_t queued() const;

  // Play the build error tone, alert_tone.h, over the machine's sound. A
  // second call starts it again. Safe from any thread: the device reads
  // one atomic position, so its callback stays free of locks.
  void alert();

  // Unity. The headroom for a dense mix is the APU's master gain.
  // Computer sets that to the browser's MASTER_GAIN, so a ROM sounds the same.
  float gain = 1.0f;

 private:
  struct Impl;
  Impl* impl_;
  bool running_ = false;
};

}  // namespace sc8
