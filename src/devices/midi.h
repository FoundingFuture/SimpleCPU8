// A small standard MIDI file reader. It turns a file into a time ordered list
// of note events, each stamped with the output sample it fires on. The APU
// sequencer walks the list. It reads note on, note off and tempo and skips
// the rest. See docs/design/audio-design.md for how tracks route to voices.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace sc8 {

enum class MidiKind : uint8_t { On, Off };

struct MidiEvent {
  int64_t at;        // output sample index when this event fires
  uint8_t channel;   // 0 to 15
  MidiKind type;
  uint8_t note;      // 0 to 127
  uint8_t velocity;  // 0 to 127
  bool operator==(const MidiEvent&) const = default;
};

// Parse a standard MIDI file into note events timed in output samples. The
// list is empty when the header is missing or the division is SMPTE.
std::vector<MidiEvent> parseSmf(std::span<const uint8_t> bytes, int sampleRate);

}  // namespace sc8
