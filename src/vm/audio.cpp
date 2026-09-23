#include "vm/audio.h"

#include <atomic>
#include <vector>

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#include "miniaudio.h"

#include "devices/apu_ports.h"

namespace sc8 {

// A single producer, single consumer ring of samples. The emulation thread
// pushes, the device callback pops.
struct Audio::Impl {
  static constexpr size_t CAPACITY = 1 << 14;  // about two seconds at 8 kHz
  std::vector<uint8_t> ring = std::vector<uint8_t>(CAPACITY, 128);
  std::atomic<size_t> head{0};  // next write
  std::atomic<size_t> tail{0};  // next read
  ma_device device{};
  float gain = 0.4f;

  size_t queued() const { return (head.load() + CAPACITY - tail.load()) % CAPACITY; }

  static void callback(ma_device* dev, void* out, const void*, ma_uint32 frames) {
    auto* self = static_cast<Impl*>(dev->pUserData);
    auto* dst = static_cast<float*>(out);
    size_t t = self->tail.load();
    const size_t h = self->head.load();
    for (ma_uint32 i = 0; i < frames; i++) {
      float v = 0.0f;
      if (t != h) {
        v = (static_cast<float>(self->ring[t]) - 128.0f) / 128.0f * self->gain;
        t = (t + 1) % CAPACITY;
      }
      dst[i] = v;
    }
    self->tail.store(t);
  }
};

Audio::Audio() : impl_(new Impl) {}

Audio::~Audio() {
  stop();
  delete impl_;
}

bool Audio::start() {
  if (running_) return true;
  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_f32;
  config.playback.channels = 1;
  config.sampleRate = apu::AUDIO_RATE;
  config.dataCallback = Impl::callback;
  config.pUserData = impl_;
  impl_->gain = gain;
  if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) return false;
  if (ma_device_start(&impl_->device) != MA_SUCCESS) {
    ma_device_uninit(&impl_->device);
    return false;
  }
  running_ = true;
  return true;
}

void Audio::stop() {
  if (!running_) return;
  ma_device_uninit(&impl_->device);
  running_ = false;
}

void Audio::push(std::span<const uint8_t> samples) {
  impl_->gain = gain;
  size_t h = impl_->head.load();
  const size_t t = impl_->tail.load();
  for (uint8_t s : samples) {
    const size_t next = (h + 1) % Impl::CAPACITY;
    if (next == t) break;  // full: drop the rest
    impl_->ring[h] = s;
    h = next;
  }
  impl_->head.store(h);
}

size_t Audio::queued() const { return impl_->queued(); }

}  // namespace sc8
