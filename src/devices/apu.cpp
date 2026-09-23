#include "devices/apu.h"

#include <algorithm>
#include <cmath>

namespace sc8 {

using namespace apu;

double pitchStep(int note, int root) { return std::pow(2.0, (note - root) / 12.0); }

namespace {

// Math.round rounds a half up. std::round rounds it away from zero, which
// differs below zero, so floor of x plus a half keeps the rendered bytes
// identical to the browser's.
double roundHalfUp(double x) { return std::floor(x + 0.5); }

}  // namespace

Apu::Apu(IoBus* fallback) : ChainedDevice(fallback) { powerOn(); }

void Apu::powerOn() {
  cartLo_ = 0;
  cartHi_ = 0;
  cartBank_ = 0;
  track_ = 0;
  note_ = 0;
  arg_ = 0;
  arg2_ = 0;
  slotSel_ = 0;
  slots_.fill(SampleSlot{});
  for (Instrument& inst : instruments_) {
    inst = Instrument{};
    inst.octaves.fill(-1);
  }
  voices_.fill(Voice{});
  clock_ = 0;
  song_.clear();
  playing_ = false;
  songPos_ = 0;
  songSample_ = 0;
  // Default routing: MIDI channel N to track N, channel 10 to the drum track.
  for (size_t c = 0; c < chanToTrack_.size(); c++) chanToTrack_[c] = static_cast<int>(c) % TRACK_COUNT;
  chanToTrack_[9] = drumTrack_;  // channel 10, index 9, is the drum kit
}

uint32_t Apu::cartAddr() const {
  return (static_cast<uint32_t>(cartBank_) << 16) | (static_cast<uint32_t>(cartHi_) << 8) | cartLo_;
}

uint8_t Apu::cartByte(uint32_t addr) const { return addr < cart_.size() ? cart_[addr] : 128; }

void Apu::write(uint8_t port, uint8_t value) {
  if (port < PORT_LO || port > PORT_HI) {
    fallback_->write(port, value);
    return;
  }
  switch (port) {
    case APU_CART_LO: cartLo_ = value; break;
    case APU_CART_HI: cartHi_ = value; break;
    case APU_CART_BANK: cartBank_ = value & 0x0f; break;
    case APU_TRACK: track_ = value % TRACK_COUNT; break;
    case APU_NOTE: note_ = value & 0x7f; break;
    case APU_ARG: arg_ = value; break;
    case APU_ARG2: arg2_ = value; break;
    case APU_SLOT: slotSel_ = value % SLOT_COUNT; break;
    case APU_CMD: command(value); break;
    default: break;  // claimed but unused
  }
}

uint8_t Apu::read(uint8_t port) {
  if (port < PORT_LO || port > PORT_HI) return fallback_->read(port);
  switch (port) {
    case APU_STATUS: return playing_ ? 1 : 0;
    case APU_VOICES: return activeVoiceCount();
    default: return 0;
  }
}

void Apu::command(uint8_t cmd) {
  switch (cmd) {
    case CMD_DEF_SAMPLE: {
      SampleSlot& slot = slots_[static_cast<size_t>(slotSel_)];
      slot.bank = cartBank_;
      slot.start = (cartHi_ << 8) | cartLo_;
      // DESIGN: high byte first, like every other word on this machine.
      // APU_ARG is the first argument, so for a 16 bit value it is the most
      // significant half.
      slot.length = (arg_ << 8) | arg2_;
      slot.root = note_;
      // DESIGN: defining a sample defines the whole slot, so the loop flag
      // resets with the rest of it. A recycled slot would otherwise behave
      // by what a previous program left in it. A program that wants the new
      // sample to repeat asks again with CMD_LOOP_SLOT. This line is also
      // what makes CMD_LOOP_SLOT the only way a sounding voice can loop: a
      // slot sounds only once it has a length, which only this command
      // gives it, and this command clears the flag on the way.
      slot.loop = false;
      break;
    }
    case CMD_SET_INSTRUMENT: {
      Instrument& inst = instruments_[static_cast<size_t>(track_)];
      inst.keymap = arg_ == 1;
      inst.slot = slotSel_;
      break;
    }
    case CMD_KEYMAP_ENTRY: {
      Instrument& inst = instruments_[static_cast<size_t>(track_)];
      inst.octaves[static_cast<size_t>(arg_ % OCTAVES)] = static_cast<int16_t>(slotSel_);
      break;
    }
    case CMD_NOTE_ON: noteOn(track_, note_, arg_); break;
    case CMD_NOTE_OFF: noteOff(track_, arg_ == 0 ? -1 : note_); break;
    case CMD_TRIGGER:
      trigger(track_, slotSel_, note_, arg_, slots_[static_cast<size_t>(slotSel_)].root);
      break;
    case CMD_LOAD_MIDI: loadMidi(cartAddr()); break;
    case CMD_PLAY: play(); break;
    case CMD_STOP: stop(); break;
    case CMD_STOP_ALL:
      for (Voice& voice : voices_) voice.active = false;
      break;
    case CMD_LOOP_SLOT:
      // DESIGN: a separate command, not a fourth argument to CMD_DEF_SAMPLE.
      // Defining a sample and deciding it repeats are two questions, and the
      // define command already carries three arguments. The flag rides on
      // the slot, so it reaches every way of playing it. A voice copies the
      // flag when it starts, so this never changes a note already sounding.
      // DESIGN: APU_ARG is a mode here, not a boolean, so the test is
      // equality with 1. That keeps 2 and up free for a mode the chip may
      // grow, ping-pong looping first.
      slots_[static_cast<size_t>(slotSel_)].loop = arg_ == 1;
      break;
    default: break;
  }
}

// Find a free player on a track, or steal the oldest one.
Apu::Voice& Apu::allocVoice(int track) {
  size_t lo = static_cast<size_t>(track) * PLAYERS_PER_TRACK;
  size_t hi = lo + PLAYERS_PER_TRACK;
  size_t oldest = lo;
  for (size_t i = lo; i < hi; i++) {
    Voice& voice = voices_[i];
    if (!voice.active) return voice;
    if (voice.startedAt < voices_[oldest].startedAt) oldest = i;
  }
  return voices_[oldest];
}

void Apu::startVoice(Voice& voice, int track, int note, const SampleSlot& slot, int velocity) {
  voice.active = slot.length > 0 && velocity > 0;
  voice.track = track;
  voice.note = note;
  voice.bank = slot.bank;
  voice.start = slot.start;
  voice.length = slot.length;
  voice.pos = 0;
  voice.step = pitchStep(note, slot.root);
  voice.gain = std::min(127, velocity) / 127.0;
  voice.loop = slot.loop;
  voice.startedAt = clock_++;
}

void Apu::noteOn(int track, int note, int velocity) {
  const Instrument& inst = instruments_[static_cast<size_t>(track)];
  const SampleSlot* slot;
  if (inst.keymap) {
    int oct = std::min(OCTAVES - 1, note / 12);
    int id = inst.octaves[static_cast<size_t>(oct)];
    if (id < 0) return;  // no drum mapped here
    slot = &slots_[static_cast<size_t>(id)];
  } else {
    slot = &slots_[static_cast<size_t>(inst.slot)];
  }
  startVoice(allocVoice(track), track, note, *slot, velocity);
}

void Apu::noteOff(int track, int note) {
  size_t lo = static_cast<size_t>(track) * PLAYERS_PER_TRACK;
  size_t hi = lo + PLAYERS_PER_TRACK;
  for (size_t i = lo; i < hi; i++) {
    Voice& voice = voices_[i];
    if (voice.active && (note < 0 || voice.note == note)) voice.active = false;
  }
}

void Apu::trigger(int track, int slotId, int note, int velocity, int root) {
  SampleSlot slot = slots_[static_cast<size_t>(slotId)];
  slot.root = root;
  startVoice(allocVoice(track), track, note, slot, velocity);
}

std::vector<VoiceInfo> Apu::activeVoices() const {
  std::vector<VoiceInfo> out;
  for (const Voice& v : voices_) {
    if (!v.active) continue;
    out.push_back({v.track, v.note, v.bank, v.start, v.length, v.loop, v.gain, v.startedAt});
  }
  return out;
}

uint8_t Apu::activeVoiceCount() const {
  int n = 0;
  for (const Voice& voice : voices_) {
    if (voice.active) n++;
  }
  return static_cast<uint8_t>(std::min(255, n));
}

bool Apu::audioActive() const {
  if (playing_) return true;
  for (const Voice& voice : voices_) {
    if (voice.active) return true;
  }
  return false;
}

void Apu::loadMidi(uint32_t addr) {
  size_t from = std::min<size_t>(addr, cart_.size());
  song_ = parseSmf(cart_.subspan(from), AUDIO_RATE);
  playing_ = false;
  songPos_ = 0;
  songSample_ = 0;
}

void Apu::play() {
  if (song_.empty()) return;
  playing_ = true;
  songPos_ = 0;
  songSample_ = 0;
}

void Apu::stop() {
  playing_ = false;
  for (Voice& voice : voices_) voice.active = false;
}

void Apu::setDrumTrack(int track) {
  drumTrack_ = track % TRACK_COUNT;
  chanToTrack_[9] = drumTrack_;
}

// Advance the sequencer by one output sample, firing any due events.
void Apu::stepSequencer() {
  if (!playing_) return;
  while (songPos_ < song_.size() && song_[songPos_].at <= songSample_) {
    const MidiEvent& ev = song_[songPos_];
    int track = chanToTrack_[ev.channel];
    if (ev.type == MidiKind::On) noteOn(track, ev.note, ev.velocity);
    else noteOff(track, ev.note);
    songPos_++;
  }
  songSample_++;
  if (songPos_ >= song_.size()) playing_ = false;
}

void Apu::render(std::span<uint8_t> out) {
  for (uint8_t& sample : out) {
    stepSequencer();
    double sum = 0;
    for (Voice& voice : voices_) {
      if (!voice.active) continue;
      auto idx = static_cast<int>(std::floor(voice.pos));
      if (idx >= voice.length) {
        // A one shot voice is done. A looping one starts over, and where it
        // starts matters. The step need not be whole, so the position can
        // overshoot the end by a fraction. The remainder keeps that fraction.
        // Assigning zero would throw it away, and a pitched loop would fall
        // behind on every wrap and drift flat. The remainder also covers a
        // step longer than the sample, where one subtraction would leave the
        // position outside it. voice.length is above zero here, because
        // startVoice refuses an empty slot.
        if (!voice.loop) {
          voice.active = false;
          continue;
        }
        voice.pos = std::fmod(voice.pos, voice.length);
        idx = static_cast<int>(std::floor(voice.pos));
      }
      uint32_t addr = (static_cast<uint32_t>(voice.bank) << 16) + static_cast<uint32_t>(voice.start + idx);
      sum += (cartByte(addr) - 128) * voice.gain;
      voice.pos += voice.step;
    }
    double s = roundHalfUp(128 + sum * masterGain_);
    sample = static_cast<uint8_t>(s < 0 ? 0 : s > 255 ? 255 : s);
  }
}

}  // namespace sc8
