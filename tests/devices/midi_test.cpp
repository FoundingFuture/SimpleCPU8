#include <doctest.h>

#include <cstdint>
#include <vector>

#include "devices/midi.h"

using namespace sc8;

namespace {

// Build a standard MIDI file in memory. One track, a given division, and a
// list of events already encoded as raw MIDI.
std::vector<uint8_t> smf(int division, const std::vector<uint8_t>& track) {
  auto n = static_cast<uint32_t>(track.size());
  std::vector<uint8_t> out = {
      0x4d, 0x54, 0x68, 0x64,  // "MThd"
      0, 0, 0, 6,              // header length
      0, 0,                    // format 0
      0, 1,                    // one track
      static_cast<uint8_t>((division >> 8) & 0xff), static_cast<uint8_t>(division & 0xff),
      0x4d, 0x54, 0x72, 0x6b,  // "MTrk"
      static_cast<uint8_t>((n >> 24) & 0xff), static_cast<uint8_t>((n >> 16) & 0xff),
      static_cast<uint8_t>((n >> 8) & 0xff), static_cast<uint8_t>(n & 0xff),
  };
  out.insert(out.end(), track.begin(), track.end());
  return out;
}

}  // namespace

TEST_SUITE("parseSmf") {
  TEST_CASE("returns nothing for a buffer with no header") {
    std::vector<uint8_t> junk = {1, 2, 3, 4};
    CHECK(parseSmf(junk, 8000).empty());
  }

  TEST_CASE("reads a note on and note off at the default tempo") {
    // Division 480 ticks per quarter, default tempo 500000 usec per quarter.
    // One quarter note is 0.5s. At 8000 Hz that is 4000 samples.
    std::vector<uint8_t> track = {
        0x00, 0x90, 60, 100,      // tick 0: note on, channel 0, note 60, velocity 100
        0x83, 0x60, 0x80, 60, 0,  // 480 ticks later: note off
        0x00, 0xff, 0x2f, 0x00,   // end of track
    };
    auto events = parseSmf(smf(480, track), 8000);
    std::vector<MidiEvent> expected = {
        {0, 0, MidiKind::On, 60, 100},
        {4000, 0, MidiKind::Off, 60, 0},
    };
    CHECK(events == expected);
  }

  TEST_CASE("treats a note on with velocity zero as a note off") {
    std::vector<uint8_t> track = {
        0x00, 0x90, 62, 80,
        0x81, 0x00, 0x90, 62, 0,  // 128 ticks later: note on velocity 0
    };
    auto events = parseSmf(smf(480, track), 8000);
    REQUIRE_EQ(events.size(), 2u);
    CHECK(events[1].type == MidiKind::Off);
  }

  TEST_CASE("follows a tempo change") {
    // Set 250000 usec per quarter (240 bpm) at the start. A quarter is 0.25s,
    // so 480 ticks map to 2000 samples at 8000 Hz.
    std::vector<uint8_t> track = {
        0x00, 0xff, 0x51, 0x03, 0x03, 0xd0, 0x90,  // tempo 250000
        0x00, 0x90, 64, 90,
        0x83, 0x60, 0x80, 64, 0,
    };
    auto events = parseSmf(smf(480, track), 8000);
    REQUIRE_EQ(events.size(), 2u);
    CHECK_EQ(events[0].at, 0);
    CHECK_EQ(events[1].at, 2000);
  }

  TEST_CASE("uses running status") {
    // A second note on with the status byte omitted reuses 0x90.
    std::vector<uint8_t> track = {
        0x00, 0x90, 60, 100,
        0x00, 62, 100,  // running status: another note on
    };
    auto events = parseSmf(smf(480, track), 8000);
    REQUIRE_EQ(events.size(), 2u);
    CHECK_EQ(events[0].note, 60);
    CHECK_EQ(events[1].note, 62);
    for (const MidiEvent& e : events) CHECK(e.type == MidiKind::On);
  }
}
