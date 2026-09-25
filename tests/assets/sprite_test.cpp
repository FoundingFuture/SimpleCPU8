// The sprite editor's model: the drawing tools, the rolls, and the indexed
// PNG with its frame count.
#include <doctest.h>

#include <string>

#include "assets/assets.h"
#include "assets/sprite.h"

using namespace sc8;
using namespace sc8::sprite;

namespace {

const std::vector<uint8_t> PAL = machinePalette();

// A frame as rows of characters: '.' is 0, a digit is that index.
std::string show(const Frame& f) {
  std::string s;
  for (int y = 0; y < f.height; y++) {
    for (int x = 0; x < f.width; x++) {
      const uint8_t c = f.at(x, y);
      s += c == 0 ? '.' : c < 10 ? static_cast<char>('0' + c) : '#';
    }
    s += '\n';
  }
  return s;
}

// The PNG with its text chunks taken out, as another editor might save it.
std::vector<uint8_t> withoutText(const std::vector<uint8_t>& png) {
  std::vector<uint8_t> out(png.begin(), png.begin() + 8);
  size_t at = 8;
  while (at + 12 <= png.size()) {
    const size_t len = (static_cast<size_t>(png[at]) << 24) | (static_cast<size_t>(png[at + 1]) << 16) |
                       (static_cast<size_t>(png[at + 2]) << 8) | png[at + 3];
    const std::string type(png.begin() + static_cast<long>(at + 4), png.begin() + static_cast<long>(at + 8));
    if (type != "tEXt") out.insert(out.end(), png.begin() + static_cast<long>(at), png.begin() + static_cast<long>(at + len + 12));
    at += len + 12;
  }
  return out;
}

}  // namespace

TEST_SUITE("sprite tools") {
  TEST_CASE("a line runs corner to corner, and off the edge without harm") {
    Frame f(4, 4);
    line(f, 0, 0, 3, 3, 1);
    line(f, 2, 0, 9, 0, 2);
    CHECK_EQ(show(f), "1.22\n.1..\n..1.\n...1\n");
  }

  TEST_CASE("a rectangle is outline or filled, from corners in either order") {
    Frame f(5, 4);
    rect(f, 3, 2, 0, 0, 1, false);
    CHECK_EQ(show(f), "1111.\n1..1.\n1111.\n.....\n");
    rect(f, 1, 1, 2, 1, 2, true);
    CHECK_EQ(show(f), "1111.\n1221.\n1111.\n.....\n");
  }

  TEST_CASE("an ellipse is symmetric, and its outline closes") {
    Frame f(7, 7);
    ellipse(f, 0, 0, 6, 6, 1, false);
    CHECK_EQ(show(f),
             "..111..\n"
             ".1...1.\n"
             "1.....1\n"
             "1.....1\n"
             "1.....1\n"
             ".1...1.\n"
             "..111..\n");
    Frame g(4, 4);
    ellipse(g, 0, 0, 3, 3, 2, true);
    CHECK_EQ(show(g), ".22.\n2222\n2222\n.22.\n");
  }

  TEST_CASE("a fill stays inside its colour, four ways connected") {
    Frame f(4, 3);
    rect(f, 1, 0, 1, 2, 1, true);
    f.set(3, 0, 1);
    fill(f, 0, 0, 2);
    CHECK_EQ(show(f), "21.1\n21..\n21..\n");
    fill(f, 2, 2, 3);
    CHECK_EQ(show(f), "2131\n2133\n2133\n");
  }

  TEST_CASE("a gradient runs from one colour to the other over the filled area") {
    Frame f(8, 1);
    const uint8_t red = 0xE0, blue = 0x03;  // 3-3-2: rrrgggbb
    gradient(f, 0, 0, 0, 0, 7, 0, red, blue, PAL, false);
    CHECK_EQ(f.at(0, 0), red);
    CHECK_EQ(f.at(7, 0), blue);
    for (int x = 0; x < 8; x++) CHECK_NE(f.at(x, 0), 0);
  }

  TEST_CASE("a gradient into transparency stipples, and dithering mixes the ends") {
    Frame f(16, 4);
    gradient(f, 0, 0, 0, 0, 15, 0, 5, 0, PAL, true);
    CHECK_EQ(f.at(0, 0), 5);
    CHECK_EQ(f.at(15, 3), 0);
    int drawn = 0;
    for (uint8_t p : f.px) drawn += p != 0;
    CHECK(drawn > 16);
    CHECK(drawn < 48);
  }

  TEST_CASE("rolls wrap, so no pixel is lost") {
    Frame f(3, 2);
    f.set(0, 0, 1);
    f.set(2, 1, 2);
    rollRow(f, 0, -1);
    CHECK_EQ(show(f), "..1\n..2\n");
    rollColumn(f, 2, 1);
    CHECK_EQ(show(f), "..2\n..1\n");
    roll(f, 1, 1);
    CHECK_EQ(show(f), "1..\n2..\n");
    flipH(f);
    flipV(f);
    CHECK_EQ(show(f), "..2\n..1\n");
  }

  TEST_CASE("a resize keeps the top left and clears the new pixels") {
    Strip s = blank(2, 2, 2);
    s.frames[1].set(1, 1, 7);
    resize(s, 3, 1);
    CHECK_EQ(s.width, 3);
    CHECK_EQ(show(s.frames[1]), "...\n");
    resize(s, 2, 2);
    CHECK_EQ(show(s.frames[1]), "..\n..\n");
  }
}

TEST_SUITE("sprite PNG") {
  TEST_CASE("a strip survives the round trip, index for index, with its frames and speed") {
    Strip s = blank(3, 2, 3);
    s.fps = 12;
    s.frames[0].set(0, 0, 1);
    s.frames[1].set(1, 1, 0xE0);
    s.frames[2].set(2, 0, 0xFF);
    const std::vector<uint8_t> png = encodePng(s, PAL);
    CHECK_EQ(pngFrames(png), 3);
    CHECK_EQ(pngText(png, FPS_KEY), "12");
    std::string error;
    const std::optional<Loaded> back = load(png, PAL, &error);
    REQUIRE_MESSAGE(back, error);
    CHECK(back->note.empty());
    CHECK(back->strip == s);
  }

  TEST_CASE("the build reads the same frame count from the file") {
    Strip s = blank(4, 4, 2);
    s.frames[0].set(1, 1, 9);
    const std::vector<uint8_t> png = encodePng(s, PAL);
    std::string note;
    const std::optional<ImageAsset> img = loadImageBytes(png, &note);
    REQUIRE(img);
    CHECK_EQ(img->width, 8);
    CHECK_EQ(img->frames, 2);
    CHECK_EQ(img->pixels[static_cast<size_t>(1 * 8 + 1)], 9);
  }

  TEST_CASE("a picture without the chunk is square frames when it can be, else one frame") {
    std::string error;
    const std::optional<Loaded> a = load(withoutText(encodePng(blank(12, 4, 1), PAL)), PAL, &error);
    REQUIRE(a);
    CHECK_EQ(a->strip.frames.size(), 3u);
    CHECK_EQ(a->strip.width, 4);
    CHECK_EQ(a->strip.fps, DEFAULT_FPS);
    const std::optional<Loaded> b = load(withoutText(encodePng(blank(10, 4, 1), PAL)), PAL, &error);
    REQUIRE(b);
    CHECK_EQ(b->strip.frames.size(), 1u);
    CHECK_FALSE(pngFrames({1, 2, 3}));
  }

  TEST_CASE("a frame larger than a sprite is refused") {
    Strip s = blank(65, 2, 1);
    std::string error;
    CHECK_FALSE(load(encodePng(s, PAL), PAL, &error));
    CHECK(error.find("at most 64") != std::string::npos);
  }
}
