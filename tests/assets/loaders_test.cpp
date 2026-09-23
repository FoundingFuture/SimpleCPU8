// The decoders behind .image and .sample, and decodeRgba, the port of the
// browser's decodeImage. These have no TypeScript test: the browser did the
// decoding through the canvas and the audio context.
#include <doctest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

#include "assets/assets.h"

using namespace sc8;
namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<uint8_t>;

const Bytes MACHINE = default332();

// A fresh directory under the system temp for each test, removed after.
struct TempDir {
  fs::path path;
  TempDir() {
    std::random_device rd;
    path = fs::temp_directory_path() / ("sc8_assets_" + std::to_string(rd()));
    fs::create_directories(path);
  }
  ~TempDir() { fs::remove_all(path); }
};

void writePng(const fs::path& p, const Bytes& rgba, int w, int h) {
  REQUIRE(stbi_write_png(p.string().c_str(), w, h, 4, rgba.data(), w * 4) != 0);
}

void put16(Bytes& b, uint16_t v) {
  b.push_back(static_cast<uint8_t>(v & 0xff));
  b.push_back(static_cast<uint8_t>(v >> 8));
}

void put32(Bytes& b, uint32_t v) {
  put16(b, static_cast<uint16_t>(v & 0xffff));
  put16(b, static_cast<uint16_t>(v >> 16));
}

// A canonical 44 byte RIFF WAVE header over 16 bit PCM.
void writeWav(const fs::path& p, const std::vector<int16_t>& samples, uint16_t channels, uint32_t rate) {
  Bytes b;
  const auto dataBytes = static_cast<uint32_t>(samples.size() * 2);
  b.insert(b.end(), {'R', 'I', 'F', 'F'});
  put32(b, 36 + dataBytes);
  b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  put32(b, 16);
  put16(b, 1);  // PCM
  put16(b, channels);
  put32(b, rate);
  put32(b, rate * channels * 2);
  put16(b, static_cast<uint16_t>(channels * 2));
  put16(b, 16);
  b.insert(b.end(), {'d', 'a', 't', 'a'});
  put32(b, dataBytes);
  for (const int16_t s : samples) put16(b, static_cast<uint16_t>(s));
  std::ofstream out(p, std::ios::binary);
  out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

Bytes rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return {r, g, b, a}; }

Bytes cat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const Bytes& p : parts) out.insert(out.end(), p.begin(), p.end());
  return out;
}

}  // namespace

TEST_CASE("decodeRgba indexes opaque colors from 1 in the order seen") {
  const Bytes px = cat({rgb(9, 8, 7), rgb(0, 0, 0, 10), rgb(1, 2, 3), rgb(9, 8, 7)});
  const DecodedReport d = decodeRgba(px, 4, 1, MACHINE);
  CHECK(d.image.pixels == Bytes{1, 0, 2, 1});
  CHECK(Bytes(d.image.palette.begin() + 3, d.image.palette.begin() + 9) == Bytes{9, 8, 7, 1, 2, 3});
  CHECK_FALSE(d.note);
}

TEST_CASE("decodeRgba scales by nearest neighbour and says so") {
  // 512 wide, alternating two colors: the fit keeps both, invents none.
  Bytes px;
  for (int x = 0; x < 512; x++) {
    const Bytes p = (x % 2 == 0) ? rgb(255, 0, 0) : rgb(0, 255, 0);
    px.insert(px.end(), p.begin(), p.end());
  }
  const DecodedReport d = decodeRgba(px, 512, 1, MACHINE);
  CHECK(d.image.width == 256);
  CHECK(d.image.height == 1);
  CHECK(d.image.pixels.size() == 256);
  CHECK(d.note == "scaled to fit the screen");
  // Every pixel maps to one of the two source colors.
  for (const uint8_t p : d.image.pixels) CHECK((p == 1 || p == 2));
}

TEST_CASE("decodeRgba quantizes past 255 opaque colors and reports it") {
  // Two rows of 300, every red level in each, at two greens: 512 colors.
  Bytes px;
  for (int y = 0; y < 2; y++) {
    for (int x = 0; x < 300; x++) {
      px.insert(px.end(), {static_cast<uint8_t>(x & 0xff), static_cast<uint8_t>(100 + y), 200, 255});
    }
  }
  const DecodedReport d = decodeRgba(px, 300, 2, MACHINE);
  CHECK(d.image.width == 256);
  CHECK(d.image.height == 2);
  CHECK(d.image.palette == MACHINE);
  REQUIRE(d.note);
  CHECK(*d.note == "scaled to fit the screen; colors quantized to the current palette");
  // Green 100 snaps to level 3 of 8, blue 200 to level 2 of 4, on every pixel.
  for (const uint8_t p : d.image.pixels) {
    CHECK(((p >> 2) & 7) == 3);
    CHECK((p & 3) == 2);
  }
}

TEST_CASE("decodeRgba names the dark pixels the quantize path loses") {
  Bytes px;
  for (int i = 0; i < 256; i++) px.insert(px.end(), {static_cast<uint8_t>(i), static_cast<uint8_t>(i), 0, 255});
  const DecodedReport d = decodeRgba(px, 256, 1, MACHINE);
  REQUIRE(d.note);
  CHECK(*d.note == "colors quantized to the current palette; " + std::string(QUANTIZE_BLACK_NOTE));
  CHECK(d.image.pixels[0] == 0);
}

TEST_CASE("loadImageFile decodes an on-palette PNG to machine indexes") {
  TempDir dir;
  writePng(dir.path / "pic.png", cat({rgb(255, 0, 0), rgb(0, 255, 0), rgb(0, 0, 0, 0)}), 3, 1);
  std::string note;
  const auto img = loadImageFile(dir.path / "pic.png", &note);
  REQUIRE(img);
  CHECK(img->width == 3);
  CHECK(img->height == 1);
  CHECK(img->pixels == Bytes{224, 28, 0});
  CHECK(img->palette == MACHINE);
  CHECK(note.empty());
}

TEST_CASE("loadImageFile keeps an off-palette PNG's own palette") {
  TempDir dir;
  writePng(dir.path / "pic.png", cat({rgb(1, 2, 3), rgb(1, 2, 3), rgb(255, 0, 0), rgb(0, 0, 0, 0)}), 2, 2);
  std::string note;
  const auto img = loadImageFile(dir.path / "pic.png", &note);
  REQUIRE(img);
  CHECK(img->width == 2);
  CHECK(img->height == 2);
  CHECK(img->pixels == Bytes{1, 1, 2, 0});
  CHECK(img->palette.size() == 768);
  CHECK(Bytes(img->palette.begin() + 3, img->palette.begin() + 9) == Bytes{1, 2, 3, 255, 0, 0});
  CHECK(note.empty());
}

TEST_CASE("loadImageFile folds the black merge into the note") {
  TempDir dir;
  writePng(dir.path / "pic.png", cat({rgb(0, 0, 0), rgb(0, 0, 0, 0)}), 2, 1);
  std::string note;
  const auto img = loadImageFile(dir.path / "pic.png", &note);
  REQUIRE(img);
  CHECK(img->pixels == Bytes{0, 0});
  CHECK(note == BLACK_MERGE_NOTE);
}

TEST_CASE("loadImageFile returns nothing for a missing file or a file that is no image") {
  TempDir dir;
  std::ofstream(dir.path / "junk.png") << "not a png";
  CHECK_FALSE(loadImageFile(dir.path / "missing.png", nullptr));
  CHECK_FALSE(loadImageFile(dir.path / "junk.png", nullptr));
}

TEST_CASE("loadSampleFile decodes a 16 bit mono WAV to unsigned bytes") {
  TempDir dir;
  writeWav(dir.path / "boom.wav", {-32768, 0, 32767}, 1, 8000);
  std::string note;
  const auto pcm = loadSampleFile(dir.path / "boom.wav", &note);
  REQUIRE(pcm);
  CHECK(*pcm == Bytes{0, 128, 255});
  CHECK(note.empty());
}

TEST_CASE("loadSampleFile downmixes and resamples to 8 kHz mono") {
  TempDir dir;
  // One second of stereo at 16 kHz: left silent, right at full scale.
  std::vector<int16_t> s;
  for (int i = 0; i < 16000; i++) {
    s.push_back(0);
    s.push_back(32767);
  }
  writeWav(dir.path / "wide.wav", s, 2, 16000);
  const auto pcm = loadSampleFile(dir.path / "wide.wav", nullptr);
  REQUIRE(pcm);
  // A second at 8 kHz, give or take the resampler's edge.
  CHECK(pcm->size() >= 7990);
  CHECK(pcm->size() <= 8010);
  // The mix of silence and full scale sits halfway up, well past 128.
  const uint8_t mid = (*pcm)[pcm->size() / 2];
  CHECK(mid >= 185);
  CHECK(mid <= 195);
}

TEST_CASE("loadSampleFile truncates at the APU ceiling and says so") {
  TempDir dir;
  writeWav(dir.path / "long.wav", std::vector<int16_t>(70000), 1, 8000);
  std::string note;
  const auto pcm = loadSampleFile(dir.path / "long.wav", &note);
  REQUIRE(pcm);
  CHECK(pcm->size() == MAX_SAMPLE_BYTES);
  CHECK(note == "truncated to 65535 bytes, the APU ceiling");
}

TEST_CASE("loadSampleFile refuses a MIDI file and anything it cannot decode") {
  TempDir dir;
  std::ofstream(dir.path / "song.mid", std::ios::binary) << "MThd" << std::string(12, '\0');
  std::ofstream(dir.path / "junk.wav") << "not audio at all";
  std::string note;
  CHECK_FALSE(loadSampleFile(dir.path / "song.mid", &note));
  CHECK(note.find("MIDI") != std::string::npos);
  CHECK_FALSE(loadSampleFile(dir.path / "junk.wav", nullptr));
  CHECK_FALSE(loadSampleFile(dir.path / "missing.wav", nullptr));
}
