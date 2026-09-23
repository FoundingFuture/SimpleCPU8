#include <doctest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "asm/asm.h"
#include "core/machine.h"
#include "core/microcode.h"
#include "devices/apu.h"
#include "devices/apu_ports.h"

using namespace sc8;
using namespace sc8::apu;

namespace doctest {
template <>
struct StringMaker<std::vector<int>> {
  static String convert(const std::vector<int>& v) {
    std::ostringstream s;
    s << "[";
    for (size_t i = 0; i < v.size(); i++) s << (i ? ", " : "") << v[i];
    s << "]";
    return s.str().c_str();
  }
};
}  // namespace doctest

namespace {

using Bytes = std::vector<int>;

// Render n samples as ints, so a failure prints the bytes.
Bytes pull(Apu& apu, size_t n) {
  std::vector<uint8_t> out(n);
  apu.render(out);
  return Bytes(out.begin(), out.end());
}

// The chip points at the log, so a rig is built in place and never copied.
struct Rig {
  Rig() = default;
  Rig(const Rig&) = delete;
  Rig& operator=(const Rig&) = delete;
  std::vector<uint8_t> cart;
  LogIoBus log;
  Apu apu{&log};
  void w(int port, int v) { apu.write(static_cast<uint8_t>(port), static_cast<uint8_t>(v)); }
  int r(int port) { return apu.read(static_cast<uint8_t>(port)); }
};

void put(std::vector<uint8_t>& cart, size_t at, std::initializer_list<uint8_t> bytes) {
  size_t i = at;
  for (uint8_t b : bytes) cart[i++] = b;
}

// An APU with a cartridge holding two ramp samples, so tests can assert the
// exact PCM the mixer renders.
void makeApu(Rig& rig) {
  rig.cart.assign(256, 0);
  put(rig.cart, 0x10, {130, 131, 132, 133});  // A: signed +2, +3, +4, +5 around 128
  put(rig.cart, 0x20, {118, 108});            // B: signed -10, -20
  rig.apu.attachCart(rig.cart);
}

// Define sample A into a slot with a root note.
void defSampleA(Rig& g, int slot = 0, int root = 60) {
  g.w(APU_CART_BANK, 0);
  g.w(APU_CART_HI, 0);
  g.w(APU_CART_LO, 0x10);
  g.w(APU_ARG, 0);   // length high
  g.w(APU_ARG2, 4);  // length low
  g.w(APU_NOTE, root);
  g.w(APU_SLOT, slot);
  g.w(APU_CMD, CMD_DEF_SAMPLE);
}

// A one track SMF: note 60 on at tick 0, off at tick 480 (4000 samples).
std::vector<uint8_t> tinyMidi() {
  std::vector<uint8_t> track = {
      0x00, 0x90, 60, 127,
      0x83, 0x60, 0x80, 60, 0,
      0x00, 0xff, 0x2f, 0x00,
  };
  std::vector<uint8_t> out = {0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xe0};
  std::vector<uint8_t> trk = {0x4d, 0x54, 0x72, 0x6b, 0, 0, 0, static_cast<uint8_t>(track.size())};
  out.insert(out.end(), trk.begin(), trk.end());
  out.insert(out.end(), track.begin(), track.end());
  return out;
}

}  // namespace

TEST_SUITE("apu port bus") {
  TEST_CASE("forwards ports outside its range to the fallback") {
    Rig g;
    makeApu(g);
    g.apu.write(0x05, 42);  // a GPU port, not the APU's
    CHECK(g.log.log == std::vector<LogIoBus::Entry>{{0x05, 42}});
    CHECK_EQ(g.r(0x05), 0);  // LogIoBus reads zero
  }

  TEST_CASE("claims its own range and does not forward it") {
    Rig g;
    makeApu(g);
    g.w(APU_ARG, 7);
    CHECK(g.log.log.empty());
  }
}

TEST_SUITE("apu sample playback") {
  TEST_CASE("triggers a sample at its root and renders the exact bytes") {
    Rig g;
    makeApu(g);
    defSampleA(g);
    g.w(APU_SLOT, 0);
    g.w(APU_NOTE, 60);  // the root: play at the stored rate
    g.w(APU_ARG, 127);  // full velocity: gain is exactly one
    g.w(APU_TRACK, 0);
    g.w(APU_CMD, CMD_TRIGGER);
    CHECK_EQ(pull(g.apu, 4), Bytes{130, 131, 132, 133});
    CHECK_EQ(pull(g.apu, 2), Bytes{128, 128});  // silent past the end
  }

  TEST_CASE("plays an octave up at double the step") {
    Rig g;
    makeApu(g);
    defSampleA(g);
    g.w(APU_SLOT, 0);
    g.w(APU_NOTE, 72);  // one octave above the root
    g.w(APU_ARG, 127);
    g.w(APU_CMD, CMD_TRIGGER);
    // Step is two, so the render reads samples 0 and 2, then runs off the end.
    CHECK_EQ(pull(g.apu, 4), Bytes{130, 132, 128, 128});
  }

  TEST_CASE("frees the voice when the sample ends") {
    Rig g;
    makeApu(g);
    defSampleA(g);
    g.w(APU_NOTE, 60);
    g.w(APU_ARG, 127);
    g.w(APU_CMD, CMD_TRIGGER);
    CHECK_EQ(g.r(APU_VOICES), 1);
    pull(g.apu, 4);  // exhaust the sample
    pull(g.apu, 1);  // one more render retires the voice
    CHECK_EQ(g.r(APU_VOICES), 0);
  }

  TEST_CASE("mixes two voices as a centered sum") {
    Rig g;
    makeApu(g);
    defSampleA(g, 0, 60);  // +2 first byte
    // Sample B into slot 1.
    g.w(APU_CART_LO, 0x20);
    g.w(APU_ARG, 2);
    g.w(APU_ARG2, 0);
    g.w(APU_NOTE, 60);
    g.w(APU_SLOT, 1);
    g.w(APU_CMD, CMD_DEF_SAMPLE);
    // Trigger both at the root, full velocity.
    g.w(APU_SLOT, 0); g.w(APU_NOTE, 60); g.w(APU_ARG, 127); g.w(APU_CMD, CMD_TRIGGER);
    g.w(APU_SLOT, 1); g.w(APU_NOTE, 60); g.w(APU_ARG, 127); g.w(APU_CMD, CMD_TRIGGER);
    // First sample: +2 from A and -10 from B, so 128 - 8 = 120.
    CHECK_EQ(pull(g.apu, 1)[0], 120);
  }

  TEST_CASE("applies the master gain as headroom") {
    Rig g;
    makeApu(g);
    g.apu.setMasterGain(0.5);
    defSampleA(g);
    g.w(APU_SLOT, 0); g.w(APU_NOTE, 60); g.w(APU_ARG, 127); g.w(APU_CMD, CMD_TRIGGER);
    // First byte is +2, halved: 128 + round(2 * 0.5) = 129.
    CHECK_EQ(pull(g.apu, 1)[0], 129);
  }

  TEST_CASE("scales by velocity") {
    Rig g;
    makeApu(g);
    defSampleA(g);
    g.w(APU_SLOT, 0);
    g.w(APU_NOTE, 60);
    g.w(APU_ARG, 64);  // about half velocity
    g.w(APU_CMD, CMD_TRIGGER);
    // First byte is +2, gain 64/127, so 128 + round(2 * 0.504) = 129.
    CHECK_EQ(pull(g.apu, 1)[0], 129);
  }
}

TEST_SUITE("a .sample blob from assembly") {
  // The whole path in one test: the assembler places the blob, the size
  // macros give its length, and the APU plays the bytes that landed. Neither
  // half is proof on its own.
  TEST_CASE("defines and triggers a cartridge sample, and sound comes out") {
    std::vector<uint8_t> pcm = {130, 131, 132, 133};
    const char* src = R"(
        OUT APU_CART_BANK, get_bankbyte(boom)
        OUT APU_CART_HI, get_highbyte(boom)
        OUT APU_CART_LO, get_lowbyte(boom)
        OUT APU_ARG,  get_sizelo(boom)
        OUT APU_ARG2, get_sizehi(boom)
        OUT APU_NOTE, 60
        OUT APU_SLOT, 0
        OUT APU_CMD,  CMD_DEF_SAMPLE
        OUT APU_TRACK, 0
        OUT APU_NOTE, 60
        OUT APU_ARG,  127
        OUT APU_CMD,  CMD_TRIGGER
        HLT
.data
pad:    db 0, 0
boom:   .sample('boom.wav')
)";
    Assets assets;
    assets.samples["boom.wav"] = pcm;
    Assembled a = assemble(src, &assets);
    REQUIRE(a.errors.empty());
    CHECK_EQ(a.labels.at("boom").value, 2);  // the pad pushed it off zero

    LogIoBus log;
    Apu apu(&log);
    apu.attachCart(a.cart);
    Machine m(a.program, buildOptimal(), &apu);
    m.run(100);
    CHECK(m.status == Status::Halted);

    CHECK_EQ(apu.read(APU_VOICES), 1);
    Bytes out = pull(apu, 4);
    CHECK_EQ(out, Bytes{130, 131, 132, 133});  // the assembled bytes
    bool sound = false;
    for (int b : out) sound = sound || b != 128;
    CHECK(sound);  // not silence
  }
}

TEST_SUITE("apu tracks and instruments") {
  TEST_CASE("plays a melodic note through the track instrument") {
    Rig g;
    makeApu(g);
    defSampleA(g, 0, 60);
    g.w(APU_TRACK, 3);
    g.w(APU_SLOT, 0);
    g.w(APU_ARG, 0);  // melodic mode
    g.w(APU_CMD, CMD_SET_INSTRUMENT);
    g.w(APU_NOTE, 60);
    g.w(APU_ARG, 127);
    g.w(APU_CMD, CMD_NOTE_ON);
    CHECK_EQ(pull(g.apu, 1)[0], 130);
  }

  TEST_CASE("stops a note on note-off") {
    Rig g;
    makeApu(g);
    defSampleA(g, 0, 60);
    g.w(APU_TRACK, 0); g.w(APU_SLOT, 0); g.w(APU_ARG, 0); g.w(APU_CMD, CMD_SET_INSTRUMENT);
    g.w(APU_NOTE, 60); g.w(APU_ARG, 127); g.w(APU_CMD, CMD_NOTE_ON);
    CHECK_EQ(g.r(APU_VOICES), 1);
    g.w(APU_NOTE, 60); g.w(APU_ARG, 1); g.w(APU_CMD, CMD_NOTE_OFF);
    CHECK_EQ(g.r(APU_VOICES), 0);
  }

  TEST_CASE("gives each track eight players and steals the oldest") {
    Rig g;
    makeApu(g);
    defSampleA(g, 0, 60);
    g.w(APU_TRACK, 0); g.w(APU_SLOT, 0); g.w(APU_ARG, 0); g.w(APU_CMD, CMD_SET_INSTRUMENT);
    for (int i = 0; i < 9; i++) {
      g.w(APU_NOTE, 60 + i); g.w(APU_ARG, 127); g.w(APU_CMD, CMD_NOTE_ON);
    }
    CHECK_EQ(g.r(APU_VOICES), 8);  // nine notes, eight players
  }

  TEST_CASE("keeps tracks independent, up to 64 voices") {
    Rig g;
    makeApu(g);
    defSampleA(g, 0, 60);
    for (int t = 0; t < 8; t++) {
      g.w(APU_TRACK, t); g.w(APU_SLOT, 0); g.w(APU_ARG, 0); g.w(APU_CMD, CMD_SET_INSTRUMENT);
      for (int i = 0; i < 8; i++) { g.w(APU_NOTE, 60 + i); g.w(APU_ARG, 127); g.w(APU_CMD, CMD_NOTE_ON); }
    }
    CHECK_EQ(g.r(APU_VOICES), VOICE_COUNT);  // 64
  }
}

TEST_SUITE("apu drum keymap") {
  TEST_CASE("picks a different sample per octave") {
    Rig g;
    makeApu(g);
    // Slot 0 is sample A (+2 first byte), slot 1 is sample B (-10 first byte).
    defSampleA(g, 0, 0);  // root 0 so any note plays near its stored rate
    g.w(APU_CART_LO, 0x20); g.w(APU_ARG, 2); g.w(APU_ARG2, 0); g.w(APU_NOTE, 0);
    g.w(APU_SLOT, 1); g.w(APU_CMD, CMD_DEF_SAMPLE);
    // Track 5 in keymap mode: octave 0 to slot 0, octave 5 to slot 1.
    g.w(APU_TRACK, 5); g.w(APU_ARG, 1); g.w(APU_CMD, CMD_SET_INSTRUMENT);
    g.w(APU_SLOT, 0); g.w(APU_ARG, 0); g.w(APU_CMD, CMD_KEYMAP_ENTRY);
    g.w(APU_SLOT, 1); g.w(APU_ARG, 5); g.w(APU_CMD, CMD_KEYMAP_ENTRY);
    // A note in octave 0 sounds slot 0.
    g.w(APU_NOTE, 0); g.w(APU_ARG, 127); g.w(APU_CMD, CMD_NOTE_ON);
    CHECK_EQ(pull(g.apu, 1)[0], 130);
    g.w(APU_CMD, CMD_STOP_ALL);
    // A note in octave 5, note 60, sounds slot 1.
    g.w(APU_NOTE, 60); g.w(APU_ARG, 127); g.w(APU_CMD, CMD_NOTE_ON);
    CHECK_EQ(pull(g.apu, 1)[0], 118);
  }
}

TEST_SUITE("apu midi playback") {
  TEST_CASE("plays a loaded tune, firing notes on time") {
    Rig g;
    g.cart.assign(512, 0);
    put(g.cart, 0x10, {130, 131, 132, 133});  // sample A
    std::vector<uint8_t> midi = tinyMidi();
    std::copy(midi.begin(), midi.end(), g.cart.begin() + 0x40);
    g.apu.attachCart(g.cart);
    // Track 0 plays sample A, melodic, root 60.
    g.w(APU_CART_LO, 0x10); g.w(APU_ARG, 4); g.w(APU_ARG2, 0); g.w(APU_NOTE, 60);
    g.w(APU_SLOT, 0); g.w(APU_CMD, CMD_DEF_SAMPLE);
    g.w(APU_TRACK, 0); g.w(APU_SLOT, 0); g.w(APU_ARG, 0); g.w(APU_CMD, CMD_SET_INSTRUMENT);
    // Load and play the tune from cartridge address 0x40.
    g.w(APU_CART_LO, 0x40); g.w(APU_CART_HI, 0); g.w(APU_CART_BANK, 0);
    g.w(APU_CMD, CMD_LOAD_MIDI);
    g.w(APU_CMD, CMD_PLAY);
    CHECK_EQ(g.r(APU_STATUS), 1);  // playing
    // The note on fires on the first rendered sample.
    CHECK_EQ(pull(g.apu, 1)[0], 130);
    CHECK_EQ(g.r(APU_VOICES), 1);
  }

  TEST_CASE("stops on command and silences voices") {
    Rig g;
    g.cart.assign(512, 0);
    put(g.cart, 0x10, {130, 131});
    std::vector<uint8_t> midi = tinyMidi();
    std::copy(midi.begin(), midi.end(), g.cart.begin() + 0x40);
    g.apu.attachCart(g.cart);
    g.w(APU_CART_LO, 0x10); g.w(APU_ARG, 2); g.w(APU_ARG2, 0); g.w(APU_NOTE, 60);
    g.w(APU_SLOT, 0); g.w(APU_CMD, CMD_DEF_SAMPLE);
    g.w(APU_TRACK, 0); g.w(APU_SLOT, 0); g.w(APU_ARG, 0); g.w(APU_CMD, CMD_SET_INSTRUMENT);
    g.w(APU_CART_LO, 0x40); g.w(APU_CMD, CMD_LOAD_MIDI); g.w(APU_CMD, CMD_PLAY);
    pull(g.apu, 1);
    g.w(APU_CMD, CMD_STOP);
    CHECK_EQ(g.r(APU_STATUS), 0);
    CHECK_EQ(g.r(APU_VOICES), 0);
  }
}

TEST_SUITE("apu drum channel routing") {
  TEST_CASE("routes MIDI channel 10 to the drum track keymap") {
    // A one event SMF: channel 9 (the tenth channel), note 48, at tick 0.
    std::vector<uint8_t> track = {0x00, 0x99, 48, 100, 0x00, 0xff, 0x2f, 0x00};
    std::vector<uint8_t> midi = {0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xe0,
                                 0x4d, 0x54, 0x72, 0x6b, 0, 0, 0, static_cast<uint8_t>(track.size())};
    midi.insert(midi.end(), track.begin(), track.end());
    Rig g;
    g.cart.assign(512, 0);
    put(g.cart, 0x10, {200, 200});  // a loud drum sample
    std::copy(midi.begin(), midi.end(), g.cart.begin() + 0x40);
    g.apu.attachCart(g.cart);
    // Define slot 0, put it on octave 4 of the default drum track, track 7.
    g.w(APU_CART_LO, 0x10); g.w(APU_ARG, 2); g.w(APU_ARG2, 0); g.w(APU_NOTE, 48);
    g.w(APU_SLOT, 0); g.w(APU_CMD, CMD_DEF_SAMPLE);
    g.w(APU_TRACK, 7); g.w(APU_ARG, 1); g.w(APU_CMD, CMD_SET_INSTRUMENT);
    g.w(APU_SLOT, 0); g.w(APU_ARG, 4); g.w(APU_CMD, CMD_KEYMAP_ENTRY);  // note 48 is octave 4
    g.w(APU_CART_LO, 0x40); g.w(APU_CMD, CMD_LOAD_MIDI); g.w(APU_CMD, CMD_PLAY);
    pull(g.apu, 1);
    CHECK_EQ(g.r(APU_VOICES), 1);  // the drum fired on track 7
  }
}

TEST_SUITE("pitchStep") {
  TEST_CASE("is one at the root and doubles an octave up") {
    CHECK_EQ(pitchStep(60, 60), 1.0);
    CHECK_EQ(pitchStep(72, 60), doctest::Approx(2).epsilon(1e-10));
    CHECK_EQ(pitchStep(48, 60), doctest::Approx(0.5).epsilon(1e-10));
  }
}

TEST_SUITE("multi-byte arguments are big-endian, like the CPU") {
  TEST_CASE("reads a 16-bit sample length high byte first") {
    // The CPU is big-endian everywhere: dw stores high first, a word load
    // fills a D register high first, printf pushes params high first. The
    // audio chip read its one 16 bit pair the other way round once.
    // ARG 1 and ARG2 0 is 256 bytes read high first, and 1 byte read low
    // first, so how long the voice lasts tells the two apart.
    Rig g;
    makeApu(g);
    g.w(APU_CART_BANK, 0);
    g.w(APU_CART_HI, 0);
    g.w(APU_CART_LO, 0);
    g.w(APU_ARG, 1);   // high
    g.w(APU_ARG2, 0);  // low
    g.w(APU_NOTE, 60);
    g.w(APU_SLOT, 0);
    g.w(APU_CMD, CMD_DEF_SAMPLE);
    g.w(APU_TRACK, 0);
    g.w(APU_NOTE, 60);
    g.w(APU_ARG, 127);  // velocity, a single byte argument
    g.w(APU_CMD, CMD_TRIGGER);
    pull(g.apu, 50);
    CHECK_EQ(g.r(APU_VOICES), 1);  // 256 bytes is still sounding
    pull(g.apu, 400);
    CHECK_EQ(g.r(APU_VOICES), 0);  // and done well before 450
  }
}

// A sample slot that repeats. Without it every sound this machine can make is
// a one shot, so a siren, an engine, or a held chord cannot exist. The tests
// below pin the wrap arithmetic, because a loop that restarts at zero throws
// away the fraction the step overshot by and drifts flat.

namespace {

// A cartridge for the loop tests. Sample A at 0x10 is four bytes, and 0x14
// holds the bytes right after it, so a wrap that lands outside the sample
// reads something a correct wrap never reaches. Sample B at 0x20 is two bytes
// that pull the mix down where sample A pushes it up, so two slots playing
// together stay separable. RAMP at 0x40 is five bytes whose value is 129 plus
// the index it sits at, so a rendered byte says which source sample was read.
void makeLoopApu(Rig& rig) {
  rig.cart.assign(256, 0);
  put(rig.cart, 0x10, {130, 131, 132, 133});  // A: +2 +3 +4 +5
  put(rig.cart, 0x14, {200, 201, 202, 203});
  put(rig.cart, 0x20, {118, 108});  // B: -10 -20
  put(rig.cart, 0x40, {129, 130, 131, 132, 133});
  rig.apu.attachCart(rig.cart);
}

struct SlotDef {
  int at;
  int len;
  int slot;
  int root;
};

void defSlot(Rig& g, SlotDef o) {
  g.w(APU_CART_BANK, 0);
  g.w(APU_CART_HI, (o.at >> 8) & 0xff);
  g.w(APU_CART_LO, o.at & 0xff);
  g.w(APU_ARG, (o.len >> 8) & 0xff);  // length high, big-endian like the CPU
  g.w(APU_ARG2, o.len & 0xff);
  g.w(APU_NOTE, o.root);
  g.w(APU_SLOT, o.slot);
  g.w(APU_CMD, CMD_DEF_SAMPLE);
}

void setLoop(Rig& g, int slot, bool on) {
  g.w(APU_SLOT, slot);
  g.w(APU_ARG, on ? 1 : 0);
  g.w(APU_CMD, CMD_LOOP_SLOT);
}

// Bind a track to a slot in melodic mode and start a note at full velocity.
void noteOn(Rig& g, int track, int slot, int note) {
  g.w(APU_TRACK, track);
  g.w(APU_SLOT, slot);
  g.w(APU_ARG, 0);  // melodic
  g.w(APU_CMD, CMD_SET_INSTRUMENT);
  g.w(APU_NOTE, note);
  g.w(APU_ARG, 127);
  g.w(APU_CMD, CMD_NOTE_ON);
}

}  // namespace

TEST_SUITE("apu looping slots") {
  TEST_CASE("plays a looping slot past its end") {
    Rig g;
    makeLoopApu(g);
    defSlot(g, {0x10, 4, 0, 60});
    setLoop(g, 0, true);
    noteOn(g, 0, 0, 60);
    CHECK_EQ(pull(g.apu, 10), Bytes{130, 131, 132, 133, 130, 131, 132, 133, 130, 131});
    CHECK_EQ(g.r(APU_VOICES), 1);  // still sounding, with no end of its own
    CHECK(g.apu.audioActive());    // so the host keeps pumping audio
  }

  TEST_CASE("does not loop a slot that was never told to") {
    Rig g;
    makeLoopApu(g);
    defSlot(g, {0x10, 4, 0, 60});
    noteOn(g, 0, 0, 60);
    CHECK_EQ(pull(g.apu, 8), Bytes{130, 131, 132, 133, 128, 128, 128, 128});
    CHECK_EQ(g.r(APU_VOICES), 0);
  }

  TEST_CASE("returns a slot to a one shot when the flag is cleared") {
    Rig g;
    makeLoopApu(g);
    defSlot(g, {0x10, 4, 0, 60});
    setLoop(g, 0, true);
    setLoop(g, 0, false);
    noteOn(g, 0, 0, 60);
    CHECK_EQ(pull(g.apu, 8), Bytes{130, 131, 132, 133, 128, 128, 128, 128});
    CHECK_EQ(g.r(APU_VOICES), 0);
  }

  TEST_CASE("loops a slot fired by CMD_TRIGGER too") {
    Rig g;
    makeLoopApu(g);
    defSlot(g, {0x10, 4, 0, 60});
    setLoop(g, 0, true);
    g.w(APU_TRACK, 0);
    g.w(APU_SLOT, 0);
    g.w(APU_NOTE, 60);
    g.w(APU_ARG, 127);
    g.w(APU_CMD, CMD_TRIGGER);
    CHECK_EQ(pull(g.apu, 6), Bytes{130, 131, 132, 133, 130, 131});
  }

  TEST_CASE("stops a looping voice on note off") {
    Rig g;
    makeLoopApu(g);
    defSlot(g, {0x10, 4, 0, 60});
    setLoop(g, 0, true);
    noteOn(g, 0, 0, 60);
    pull(g.apu, 12);  // three times round the sample
    CHECK_EQ(g.r(APU_VOICES), 1);
    g.w(APU_TRACK, 0);
    g.w(APU_NOTE, 60);
    g.w(APU_ARG, 1);  // a named note, not the stop-everything form
    g.w(APU_CMD, CMD_NOTE_OFF);
    CHECK_EQ(g.r(APU_VOICES), 0);
    CHECK_EQ(pull(g.apu, 4), Bytes{128, 128, 128, 128});
    CHECK_FALSE(g.apu.audioActive());
  }

  TEST_CASE("stops a looping voice on stop all") {
    Rig g;
    makeLoopApu(g);
    defSlot(g, {0x10, 4, 0, 60});
    setLoop(g, 0, true);
    noteOn(g, 0, 0, 60);
    pull(g.apu, 12);
    CHECK_EQ(g.r(APU_VOICES), 1);
    g.w(APU_CMD, CMD_STOP_ALL);
    CHECK_EQ(g.r(APU_VOICES), 0);
    CHECK_EQ(pull(g.apu, 4), Bytes{128, 128, 128, 128});
    CHECK_FALSE(g.apu.audioActive());
  }

  TEST_CASE("stops a looping voice on stop") {
    Rig g;
    makeLoopApu(g);
    defSlot(g, {0x10, 4, 0, 60});
    setLoop(g, 0, true);
    noteOn(g, 0, 0, 60);
    pull(g.apu, 12);
    CHECK_EQ(g.r(APU_VOICES), 1);
    g.w(APU_CMD, CMD_STOP);
    CHECK_EQ(g.r(APU_VOICES), 0);
    CHECK_EQ(pull(g.apu, 4), Bytes{128, 128, 128, 128});
  }

  TEST_CASE("keeps a pitched loop in phase over many wraps") {
    Rig g;
    makeLoopApu(g);
    const int LEN = 5;
    defSlot(g, {0x40, LEN, 0, 60});
    setLoop(g, 0, true);
    noteOn(g, 0, 0, 59);  // a semitone below the root, so the step is not whole
    double step = pitchStep(59, 60);
    // The model, written without any wrap bookkeeping: output sample n reads
    // source index floor((n * step) mod length). A wrap that assigns zero
    // instead of subtracting the length loses the overshoot and falls behind.
    const int N = 200;
    Bytes expected;
    double nearest = 1;
    for (int n = 0; n < N; n++) {
      double phase = std::fmod(n * step, LEN);
      // The chip accumulates the position by adding, the model multiplies,
      // so the two differ in the last bits. Skip n zero, where both are
      // exactly zero, and measure how close the rest come to a boundary.
      if (n > 0) nearest = std::min(nearest, std::fabs(phase - std::round(phase)));
      expected.push_back(129 + static_cast<int>(std::floor(phase)));
    }
    // No read sits near a boundary, so the two cannot disagree over rounding.
    CHECK_GT(nearest, 1e-6);
    CHECK_EQ(pull(g.apu, N), expected);
  }

  TEST_CASE("wraps a step longer than the sample back inside it") {
    Rig g;
    makeLoopApu(g);
    defSlot(g, {0x10, 4, 0, 60});
    setLoop(g, 0, true);
    noteOn(g, 0, 0, 96);  // three octaves up, so the step is 8, twice the length
    // Every read lands on index 0, because 8 mod 4 is 0. Subtracting the
    // length once leaves the position outside the sample, where the bytes
    // are 200 and up.
    CHECK_EQ(pull(g.apu, 4), Bytes{130, 130, 130, 130});
  }

  TEST_CASE("sets the flag on the selected slot alone") {
    Rig g;
    makeLoopApu(g);
    defSlot(g, {0x10, 4, 0, 60});  // A, four bytes
    defSlot(g, {0x20, 2, 1, 60});  // B, two bytes
    setLoop(g, 0, true);           // slot 0 only
    noteOn(g, 0, 0, 60);           // track 0 plays slot 0
    noteOn(g, 1, 1, 60);           // track 1 plays slot 1
    // Sample 0 mixes +2 and -10, sample 1 mixes +3 and -20. B is spent after
    // that, so samples 2 and 3 are A alone, and sample 4 is A come round
    // again. A slot flag that reached every slot would keep B sounding.
    CHECK_EQ(pull(g.apu, 6), Bytes{120, 111, 132, 133, 130, 131});
    CHECK_EQ(g.r(APU_VOICES), 1);  // A alone, B ended
  }

  TEST_CASE("clears the flag when the slot is redefined") {
    Rig g;
    makeLoopApu(g);
    defSlot(g, {0x10, 4, 0, 60});
    setLoop(g, 0, true);
    defSlot(g, {0x20, 2, 0, 60});  // the slot is recycled
    noteOn(g, 0, 0, 60);
    CHECK_EQ(pull(g.apu, 6), Bytes{118, 108, 128, 128, 128, 128});
    CHECK_EQ(g.r(APU_VOICES), 0);
  }

  TEST_CASE("clears the flag only on the slot being defined") {
    Rig g;
    makeLoopApu(g);
    defSlot(g, {0x10, 4, 0, 60});
    setLoop(g, 0, true);
    defSlot(g, {0x20, 2, 1, 60});  // a different slot
    noteOn(g, 0, 0, 60);
    CHECK_EQ(pull(g.apu, 6), Bytes{130, 131, 132, 133, 130, 131});
  }

  TEST_CASE("loops a redefined slot when the flag is set again") {
    Rig g;
    makeLoopApu(g);
    defSlot(g, {0x10, 4, 0, 60});
    setLoop(g, 0, true);
    defSlot(g, {0x20, 2, 0, 60});
    setLoop(g, 0, true);  // asked for again, on the new sample
    noteOn(g, 0, 0, 60);
    CHECK_EQ(pull(g.apu, 6), Bytes{118, 108, 118, 108, 118, 108});
  }

  TEST_CASE("keeps a sounding voice looping when the flag is cleared") {
    Rig g;
    makeLoopApu(g);
    defSlot(g, {0x10, 4, 0, 60});
    setLoop(g, 0, true);
    noteOn(g, 0, 0, 60);
    CHECK_EQ(pull(g.apu, 2), Bytes{130, 131});
    setLoop(g, 0, false);  // mid-note, so it must not reach the voice
    CHECK_EQ(pull(g.apu, 6), Bytes{132, 133, 130, 131, 132, 133});
    CHECK_EQ(g.r(APU_VOICES), 1);
  }

  TEST_CASE("does not start a sounding voice looping when the flag is set") {
    Rig g;
    makeLoopApu(g);
    defSlot(g, {0x10, 4, 0, 60});
    noteOn(g, 0, 0, 60);  // a one shot, already sounding
    CHECK_EQ(pull(g.apu, 2), Bytes{130, 131});
    setLoop(g, 0, true);  // mid-note again, the other direction
    CHECK_EQ(pull(g.apu, 6), Bytes{132, 133, 128, 128, 128, 128});
    CHECK_EQ(g.r(APU_VOICES), 0);
  }

  TEST_CASE("reads APU_ARG as a mode, so 2 is not looping today") {
    // DESIGN: APU_ARG here is a mode, not a boolean. 1 loops, and the values
    // above it stay free for a mode this chip may grow, ping-pong looping
    // first. Widening the test to any non-zero value would spend them all.
    Rig g;
    makeLoopApu(g);
    defSlot(g, {0x10, 4, 0, 60});
    g.w(APU_SLOT, 0);
    g.w(APU_ARG, 2);
    g.w(APU_CMD, CMD_LOOP_SLOT);
    noteOn(g, 0, 0, 60);
    CHECK_EQ(pull(g.apu, 6), Bytes{130, 131, 132, 133, 128, 128});
    CHECK_EQ(g.r(APU_VOICES), 0);
  }

  TEST_CASE("refuses to loop a zero length slot") {
    Rig g;
    makeLoopApu(g);
    defSlot(g, {0x10, 0, 0, 60});
    setLoop(g, 0, true);
    noteOn(g, 0, 0, 60);
    CHECK_EQ(g.r(APU_VOICES), 0);  // nothing to play, so no voice
    CHECK_EQ(pull(g.apu, 8), Bytes{128, 128, 128, 128, 128, 128, 128, 128});
    CHECK_EQ(g.r(APU_VOICES), 0);
    CHECK_FALSE(g.apu.audioActive());  // the host stops pumping audio
  }
}

TEST_SUITE("apu command opcodes") {
  // The GPU once gave CMD_COLLIDE_ALL an opcode CMD_ROT_X already held, and
  // the dispatch matched collision first, so CMD_ROT_X could not be reached.
  // A duplicate is invisible until the shadowed command is called. A switch
  // with two equal case labels fails to compile in C++, so the dispatch in
  // apu.cpp guards the enum. This test guards the published table.
  TEST_CASE("gives every command its own opcode") {
    std::set<int> values;
    size_t count = 0;
    for (const auto& c : CMDS) {
      values.insert(c.value);
      count++;
    }
    CHECK_EQ(values.size(), count);
  }
}

// apu_ports.h publishes the names as text for the assembler and apu.h holds
// the numbers the chip dispatches on. Neither can read the other, so this
// test walks both tables and refuses a name that resolves to two values.
TEST_SUITE("apu enum matches the published tables") {
  struct Named {
    std::string_view name;
    int value;
  };
  constexpr Named PORT_ENUM[] = {
      {"APU_CART_LO", APU_CART_LO},     {"APU_CART_HI", APU_CART_HI}, {"APU_CART_BANK", APU_CART_BANK},
      {"APU_TRACK", APU_TRACK},         {"APU_NOTE", APU_NOTE},       {"APU_ARG", APU_ARG},
      {"APU_ARG2", APU_ARG2},           {"APU_SLOT", APU_SLOT},       {"APU_CMD", APU_CMD},
      {"APU_STATUS", APU_STATUS},       {"APU_VOICES", APU_VOICES},
  };
  constexpr Named CMD_ENUM[] = {
      {"CMD_DEF_SAMPLE", CMD_DEF_SAMPLE}, {"CMD_SET_INSTRUMENT", CMD_SET_INSTRUMENT},
      {"CMD_KEYMAP_ENTRY", CMD_KEYMAP_ENTRY}, {"CMD_NOTE_ON", CMD_NOTE_ON},
      {"CMD_NOTE_OFF", CMD_NOTE_OFF}, {"CMD_TRIGGER", CMD_TRIGGER},
      {"CMD_LOAD_MIDI", CMD_LOAD_MIDI}, {"CMD_PLAY", CMD_PLAY},
      {"CMD_STOP", CMD_STOP}, {"CMD_STOP_ALL", CMD_STOP_ALL},
      {"CMD_LOOP_SLOT", CMD_LOOP_SLOT},
  };

  template <size_t N, size_t M>
  void checkTable(const NamedValue (&table)[N], const Named (&enumerators)[M]) {
    CHECK_EQ(N, M);
    for (const NamedValue& entry : table) {
      bool found = false;
      for (const Named& e : enumerators) {
        if (e.name != entry.name) continue;
        found = true;
        CHECK_MESSAGE(e.value == entry.value, std::string(entry.name), " differs between apu_ports.h and apu.h");
      }
      CHECK_MESSAGE(found, std::string(entry.name), " has no enumerator in apu.h");
    }
  }

  TEST_CASE("every port name resolves to the same number") { checkTable(PORTS, PORT_ENUM); }
  TEST_CASE("every command name resolves to the same number") { checkTable(CMDS, CMD_ENUM); }

  TEST_CASE("the claimed range matches the ports") {
    CHECK_EQ(PORT_LO, 0x30);
    CHECK_EQ(PORT_HI, 0x3f);
    for (const NamedValue& p : PORTS) {
      CHECK_GE(p.value, PORT_LO);
      CHECK_LE(p.value, PORT_HI);
    }
  }
}

// docs/design/audio-design.md is the repository's audio reference, and it is
// written by hand. The GPU reference drifted by seven entries before a test
// pinned it. Pin this one the same way: every port and command the chip has
// is named there.
namespace {

// Only the reference section counts, and only the name a bullet defines. A
// plain text search would take a cross reference for an entry, and would
// take CMD_STOP_ALL for CMD_STOP, since one name contains the other.
std::set<std::string> definedNames() {
  // The test executable has no notion of the source tree, so the doc is
  // found from this file's own compile time path.
  std::filesystem::path doc =
      std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "docs" / "design" / "audio-design.md";
  std::ifstream in(doc);
  REQUIRE_MESSAGE(in.good(), "cannot open ", doc.string());
  std::set<std::string> defined;
  std::string line;
  bool inSection = false;
  while (std::getline(in, line)) {
    if (line.rfind("## ", 0) == 0) {
      inSection = line == "## Ports and commands";
      continue;
    }
    if (!inSection || line.rfind("- ", 0) != 0) continue;
    // The names before the colon, separated by ", ".
    size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string names = line.substr(2, colon - 2);
    std::vector<std::string> found;
    size_t start = 0;
    bool valid = true;
    while (valid) {
      size_t comma = names.find(", ", start);
      std::string name = names.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
      valid = !name.empty() && name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789") == std::string::npos;
      found.push_back(name);
      if (comma == std::string::npos) break;
      start = comma + 2;
    }
    // A bullet counts only when everything before its colon is a name list.
    if (valid) defined.insert(found.begin(), found.end());
  }
  return defined;
}

bool inTable(std::string_view name) {
  for (const NamedValue& p : PORTS) {
    if (p.name == name) return true;
  }
  for (const NamedValue& c : CMDS) {
    if (c.name == name) return true;
  }
  return false;
}

}  // namespace

TEST_SUITE("docs/design/audio-design.md") {
  TEST_CASE("gives every APU command its own entry") {
    std::set<std::string> defined = definedNames();
    for (const NamedValue& c : CMDS) {
      CHECK_MESSAGE(defined.count(std::string(c.name)) == 1, std::string(c.name), " has no entry in the reference section");
    }
  }

  TEST_CASE("gives every APU port an entry") {
    std::set<std::string> defined = definedNames();
    for (const NamedValue& p : PORTS) {
      CHECK_MESSAGE(defined.count(std::string(p.name)) == 1, std::string(p.name), " has no entry in the reference section");
    }
  }

  // The other direction. A test that only walks machine to doc lets the doc
  // grow entries for things the chip never had, or keep one for a command
  // that was removed.
  TEST_CASE("has no entry the chip does not have") {
    for (const std::string& name : definedNames()) {
      CHECK_MESSAGE(inTable(name), "ghost entry ", name, " in the reference section");
    }
  }
}
