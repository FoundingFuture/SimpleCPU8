#include "devices/midi.h"

#include <algorithm>
#include <cmath>

namespace sc8 {

namespace {

enum class RawKind : uint8_t { Tempo, Off, On };  // the merge order at one tick

// One raw event during parsing, timed in absolute ticks.
struct RawEvent {
  int64_t tick;
  RawKind kind;
  uint8_t channel;
  uint8_t note;
  uint8_t velocity;
  uint32_t usecPerQuarter;  // for tempo events
};

// A byte cursor that reads zero past the end, so a truncated file ends the
// parse quietly rather than out of bounds.
class Reader {
 public:
  explicit Reader(std::span<const uint8_t> d) : d_(d) {}

  size_t pos = 0;

  uint8_t peek() const { return pos < d_.size() ? d_[pos] : 0; }
  uint8_t u8() {
    uint8_t b = peek();
    pos++;
    return b;
  }
  // Big endian, one byte per statement: C++ does not order two reads in
  // one expression.
  uint32_t u16() {
    uint32_t hi = u8();
    uint32_t lo = u8();
    return (hi << 8) | lo;
  }
  uint32_t u32() {
    uint32_t hi = u16();
    uint32_t lo = u16();
    return (hi << 16) | lo;
  }
  // A variable length quantity: seven bits per byte, high bit continues.
  uint32_t vlq() {
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
      uint8_t b = u8();
      value = (value << 7) | (b & 0x7fu);
      if ((b & 0x80) == 0) break;
    }
    return value;
  }
  std::span<const uint8_t> slice(size_t len) const {
    size_t from = std::min(pos, d_.size());
    return d_.subspan(from, std::min(len, d_.size() - from));
  }

 private:
  std::span<const uint8_t> d_;
};

void parseTrack(std::span<const uint8_t> d, std::vector<RawEvent>& events) {
  Reader r(d);
  int64_t tick = 0;
  uint8_t status = 0;
  while (r.pos < d.size()) {
    tick += r.vlq();
    uint8_t b = r.peek();
    if (b & 0x80) {
      status = b;
      r.pos++;
    } else {
      b = status;  // running status: reuse the last status byte
    }
    uint8_t hi = b & 0xf0;
    uint8_t channel = b & 0x0f;
    if (hi == 0x90) {
      uint8_t note = r.u8();
      uint8_t velocity = r.u8();
      // A note on with velocity zero is a note off.
      RawKind kind = velocity > 0 ? RawKind::On : RawKind::Off;
      events.push_back({tick, kind, channel, note, velocity, 0});
    } else if (hi == 0x80) {
      uint8_t note = r.u8();
      uint8_t velocity = r.u8();
      events.push_back({tick, RawKind::Off, channel, note, velocity, 0});
    } else if (hi == 0xa0 || hi == 0xb0 || hi == 0xe0) {
      r.pos += 2;  // aftertouch, controller, pitch bend: two data bytes
    } else if (hi == 0xc0 || hi == 0xd0) {
      r.pos += 1;  // program change, channel pressure: one data byte
    } else if (b == 0xff) {
      uint8_t meta = r.u8();
      uint32_t len = r.vlq();
      if (meta == 0x51 && len == 3) {
        uint32_t b2 = r.u8();
        uint32_t b1 = r.u8();
        uint32_t b0 = r.u8();
        events.push_back({tick, RawKind::Tempo, 0, 0, 0, (b2 << 16) | (b1 << 8) | b0});
      } else {
        r.pos += len;  // any other meta, including end of track
      }
    } else if (b == 0xf0 || b == 0xf7) {
      r.pos += r.vlq();  // system exclusive
    } else {
      break;  // unknown byte: stop rather than misread the stream
    }
  }
}

}  // namespace

std::vector<MidiEvent> parseSmf(std::span<const uint8_t> bytes, int sampleRate) {
  Reader r(bytes);
  if (r.u32() != 0x4d546864) return {};  // no "MThd"
  r.u32();  // header length
  r.u16();  // format
  uint32_t ntracks = r.u16();
  uint32_t division = r.u16();
  if ((division & 0x8000) || division == 0) return {};  // SMPTE unsupported
  double ppq = division;

  std::vector<RawEvent> raw;
  for (uint32_t t = 0; t < ntracks; t++) {
    if (r.u32() != 0x4d54726b) break;  // no "MTrk"
    uint32_t len = r.u32();
    parseTrack(r.slice(len), raw);
    r.pos += len;
  }

  // Merge by tick, keeping tempo events before note events at the same tick.
  // The sort is stable, as the JavaScript sort is, so a track's own order
  // survives among equal keys.
  std::stable_sort(raw.begin(), raw.end(), [](const RawEvent& a, const RawEvent& b) {
    if (a.tick != b.tick) return a.tick < b.tick;
    return a.kind < b.kind;
  });

  std::vector<MidiEvent> out;
  uint32_t usecPerQuarter = 500000;  // 120 bpm until a tempo event says otherwise
  double seconds = 0;
  int64_t lastTick = 0;
  for (const RawEvent& ev : raw) {
    double secPerTick = usecPerQuarter / 1e6 / ppq;
    seconds += static_cast<double>(ev.tick - lastTick) * secPerTick;
    lastTick = ev.tick;
    if (ev.kind == RawKind::Tempo) {
      usecPerQuarter = ev.usecPerQuarter;
      continue;
    }
    // Math.round rounds a half up, which floor of x plus a half also does.
    auto at = static_cast<int64_t>(std::floor(seconds * sampleRate + 0.5));
    out.push_back({at, ev.channel, ev.kind == RawKind::On ? MidiKind::On : MidiKind::Off, ev.note, ev.velocity});
  }
  return out;
}

}  // namespace sc8
