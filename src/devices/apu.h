// The APU: an audio peripheral on the IO bus. It claims ports $30 to $3F and
// forwards the rest, the chain idiom the GPU and the controller use. It is a
// magic box. The CPU spends one OUT to trigger a sound or start a tune, and
// the chip does the mixing and the pitch shifting.
//
// The time model differs from the GPU on purpose. The chip renders on its own
// clock: the host pulls blocks of samples through render() as the speaker
// needs them, so audio plays now, at the right pitch, whatever the CPU speed.
// See docs/design/audio-design.md for the design and the open questions.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "devices/apu_ports.h"
#include "devices/device.h"
#include "devices/midi.h"

namespace sc8 {

namespace apu {

constexpr int OCTAVES = 11;  // MIDI notes 0 to 127 fall in 11 octaves

// The numbers the implementation dispatches on. apu_ports.h publishes the
// same names as text for the assembler, and a test pins the two together.
enum Port : uint8_t {
  APU_CART_LO = 0x30,
  APU_CART_HI = 0x31,
  APU_CART_BANK = 0x32,
  APU_TRACK = 0x33,
  APU_NOTE = 0x34,
  APU_ARG = 0x35,
  APU_ARG2 = 0x36,
  APU_SLOT = 0x37,
  APU_CMD = 0x38,
  APU_STATUS = 0x39,
  APU_VOICES = 0x3a,
};

enum Cmd : uint8_t {
  CMD_DEF_SAMPLE = 0x01,      // slot := cart range; length is the ARG pair, root is NOTE
  CMD_SET_INSTRUMENT = 0x02,  // bind the track to a slot; ARG 0 melodic, 1 keymap
  CMD_KEYMAP_ENTRY = 0x03,    // track keymap octave ARG := slot SLOT
  CMD_NOTE_ON = 0x04,         // start a note on the track; note NOTE, velocity ARG
  CMD_NOTE_OFF = 0x05,        // stop the note on the track; ARG 0 stops all
  CMD_TRIGGER = 0x06,         // one shot slot SLOT at note NOTE, velocity ARG
  CMD_LOAD_MIDI = 0x07,       // parse the MIDI file at the cart latches
  CMD_PLAY = 0x08,            // start the loaded tune from the beginning
  CMD_STOP = 0x09,            // stop the tune and silence its voices
  CMD_STOP_ALL = 0x0a,        // silence every voice at once
  CMD_LOOP_SLOT = 0x0b,       // slot SLOT repeats when ARG is 1, one shot otherwise
};

}  // namespace apu

// The playback pitch of a note against a root: an equal tempered ratio.
double pitchStep(int note, int root);

struct SampleSlot {
  int bank = 0;
  int start = 0;
  int length = 0;
  int root = 60;       // the note at which the sample plays at its stored rate
  bool loop = false;   // repeat from the start instead of ending, for a held sound
};

struct Instrument {
  bool keymap = false;  // false melodic, true keymap by octave
  int slot = 0;         // melodic: the sample slot
  std::array<int16_t, apu::OCTAVES> octaves{};  // keymap: octave to slot, -1 empty
};

// What is sounding right now, for the host and for tests. APU_VOICES gives a
// program the count and nothing else. A test outside the machine wants to
// know which sample is playing, whether it repeats, and whether the voice it
// is looking at is the same one it saw last frame. startedAt answers the last
// question: a held voice keeps its number and a restarted one takes a new one.
struct VoiceInfo {
  int track;
  int note;
  int bank;
  int start;
  int length;
  bool loop;
  double gain;
  uint64_t startedAt;
};

class Apu : public ChainedDevice {
 public:
  explicit Apu(IoBus* fallback = nullptr);

  // The device keeps the span, so the owner keeps the bytes alive.
  void attachCart(CartView cart) { cart_ = cart; }

  void powerOn();

  void write(uint8_t port, uint8_t value) override;
  uint8_t read(uint8_t port) override;

  // The master volume, applied to the mix. It survives powerOn, since it is
  // a host knob, not program state. Many voices at once would clip an 8 bit
  // sum, so the host lowers it for headroom.
  void setMasterGain(double g) { masterGain_ = g; }
  double masterGain() const { return masterGain_; }

  // The track MIDI channel 10 routes to, for a drum kit.
  void setDrumTrack(int track);
  int drumTrack() const { return drumTrack_; }

  // True while there is sound to render: a tune playing or any voice
  // sounding. The host pumps audio only while this holds.
  bool audioActive() const;
  bool playing() const { return playing_; }

  // Render 8 bit unsigned mono PCM at AUDIO_RATE into out, advancing every
  // voice and the sequencer. This is the pull the host calls for the speaker.
  void render(std::span<uint8_t> out);

  std::vector<VoiceInfo> activeVoices() const;
  const SampleSlot& slot(int i) const { return slots_[static_cast<size_t>(i)]; }
  const Instrument& instrument(int track) const { return instruments_[static_cast<size_t>(track)]; }
  const std::vector<MidiEvent>& song() const { return song_; }

 private:
  struct Voice {
    bool active = false;
    int track = -1;  // -1 for a one shot trigger
    int note = 0;
    int bank = 0;
    int start = 0;
    int length = 0;
    double pos = 0;   // play position in source samples
    double step = 1;  // advance per output sample
    double gain = 0;  // 0 to 1
    bool loop = false;  // copied from the slot when the voice starts
    uint64_t startedAt = 0;  // for voice stealing
  };

  uint32_t cartAddr() const;
  uint8_t cartByte(uint32_t addr) const;
  void command(uint8_t cmd);
  Voice& allocVoice(int track);
  void startVoice(Voice& voice, int track, int note, const SampleSlot& slot, int velocity);
  void noteOn(int track, int note, int velocity);
  void noteOff(int track, int note);
  void trigger(int track, int slotId, int note, int velocity, int root);
  uint8_t activeVoiceCount() const;
  void loadMidi(uint32_t addr);
  void play();
  void stop();
  void stepSequencer();

  CartView cart_;

  uint8_t cartLo_ = 0;
  uint8_t cartHi_ = 0;
  uint8_t cartBank_ = 0;
  int track_ = 0;
  int note_ = 0;
  int arg_ = 0;
  int arg2_ = 0;
  int slotSel_ = 0;

  std::array<SampleSlot, apu::SLOT_COUNT> slots_{};
  std::array<Instrument, apu::TRACK_COUNT> instruments_{};
  std::array<Voice, apu::VOICE_COUNT> voices_{};
  uint64_t clock_ = 0;  // monotonic voice age counter

  // The loaded tune and the sequencer position.
  double masterGain_ = 1;  // a host volume knob, unity by default
  std::vector<MidiEvent> song_;
  bool playing_ = false;
  size_t songPos_ = 0;    // index of the next event
  int64_t songSample_ = 0;  // output samples since the tune started
  std::array<int, 16> chanToTrack_{};
  int drumTrack_ = 7;  // the track MIDI channel 10 routes to, a valid track
};

}  // namespace sc8
