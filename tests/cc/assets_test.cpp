#include <doctest.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/assets.h"
#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

// The asset initializers. `__ROM const unsigned char ship[] = __image("ship.png")`
// puts a file's bytes on the cartridge in the shape the device command
// reads, the way .image, .sprite, .palette, .sample and .file do in assembly.

namespace {

// A 4 x 2 strip: two 2 x 2 frames side by side, pixels numbered row-major.
ImageAsset strip() {
  ImageAsset img;
  img.width = 4;
  img.height = 2;
  img.pixels = {1, 2, 3, 4, 5, 6, 7, 8};
  // On the machine's palette, so the pixel indexes come through as they are.
  img.palette = machinePalette();
  return img;
}

// Every test works from preloaded maps. Nothing here touches the disk.
Assets preloaded() {
  Assets a;
  a.images["ship.png"] = strip();
  a.samples["beep.wav"] = {128, 200, 56, 128};
  a.files["data.bin"] = {0xde, 0xad, 0xbe, 0xef};
  return a;
}

cc::Program prog(const std::string& src, const Assets& assets) {
  CcOptions opts;
  opts.assets = &assets;
  return cc::compileProgram({{"main.c", src}}, opts);
}

std::string refusesWith(const std::string& src, const Assets& assets) {
  CcOptions opts;
  opts.assets = &assets;
  try {
    compile(src, opts);
  } catch (const CcError& e) {
    return e.message();
  }
  return "no error";
}

std::vector<uint8_t> bytesOf(const cc::Program& p, const std::string& name) {
  for (const cc::RomEntry& e : p.rom) if (e.name == name) return e.bytes;
  throw std::runtime_error(name + " is not in the cartridge");
}

}  // namespace

TEST_SUITE("an asset initializer puts a file on the cartridge") {
  TEST_CASE("__image is the CMD_BLIT blob: width, height, then the pixels") {
    const Assets a = preloaded();
    const cc::Program p = prog(R"(
      __ROM const unsigned char pic[] = __image("ship.png");
      int main(void) { return 0; }
    )", a);
    CHECK(bytesOf(p, "pic") == std::vector<uint8_t>{4, 2, 1, 2, 3, 4, 5, 6, 7, 8});
  }

  TEST_CASE("__sprite is the CMD_SPRITE_DEF blob, each frame cut out of the strip") {
    const Assets a = preloaded();
    const cc::Program p = prog(R"(
      __ROM const unsigned char ship[] = __sprite("ship.png", 2);
      int main(void) { return 0; }
    )", a);
    CHECK(bytesOf(p, "ship") == std::vector<uint8_t>{2, 2, 2, 1, 2, 5, 6, 3, 4, 7, 8});
  }

  TEST_CASE("__sprite with no count is one frame, the whole image") {
    const Assets a = preloaded();
    const cc::Program p = prog(R"(
      __ROM const unsigned char ship[] = __sprite("ship.png");
      int main(void) { return 0; }
    )", a);
    CHECK(bytesOf(p, "ship") == std::vector<uint8_t>{1, 4, 2, 1, 2, 3, 4, 5, 6, 7, 8});
  }

  TEST_CASE("__sprite with no count takes the count the sprite editor's PNG states") {
    Assets a = preloaded();
    a.images["ship.png"].frames = 2;
    const cc::Program p = prog(R"(
      __ROM const unsigned char ship[] = __sprite("ship.png");
      int main(void) { return 0; }
    )", a);
    CHECK(bytesOf(p, "ship") == std::vector<uint8_t>{2, 2, 2, 1, 2, 5, 6, 3, 4, 7, 8});
  }

  TEST_CASE("an explicit count still wins over the PNG's") {
    Assets a = preloaded();
    a.images["ship.png"].frames = 2;
    const cc::Program p = prog(R"(
      __ROM const unsigned char ship[] = __sprite("ship.png", 1);
      int main(void) { return 0; }
    )", a);
    CHECK(bytesOf(p, "ship") == std::vector<uint8_t>{1, 4, 2, 1, 2, 3, 4, 5, 6, 7, 8});
  }

  TEST_CASE("__palette is the CMD_LOAD_PALETTE blob: a zero count, then 768 bytes") {
    const Assets a = preloaded();
    const cc::Program p = prog(R"(
      __ROM const unsigned char pal[] = __palette("ship.png");
      int main(void) { return 0; }
    )", a);
    const std::vector<uint8_t> b = bytesOf(p, "pal");
    REQUIRE(b.size() == 769);
    CHECK(b[0] == 0);
    CHECK(std::vector<uint8_t>(b.begin() + 1, b.end()) == machinePalette());
  }

  TEST_CASE("__sample is the raw unsigned 8 bit bytes") {
    const Assets a = preloaded();
    const cc::Program p = prog(R"(
      __ROM const unsigned char beep[] = __sample("beep.wav");
      int main(void) { return 0; }
    )", a);
    CHECK(bytesOf(p, "beep") == std::vector<uint8_t>{128, 200, 56, 128});
  }

  TEST_CASE("__file is the file as it is") {
    const Assets a = preloaded();
    const cc::Program p = prog(R"(
      __ROM const unsigned char data[] = __file("data.bin");
      int main(void) { return 0; }
    )", a);
    CHECK(bytesOf(p, "data") == std::vector<uint8_t>{0xde, 0xad, 0xbe, 0xef});
  }

  TEST_CASE("asks the loader when the map has no entry") {
    Assets a;
    int asked = 0;
    a.loadFile = [&](std::string_view name) -> std::optional<std::vector<uint8_t>> {
      asked++;
      if (name != "data.bin") return std::nullopt;
      return std::vector<uint8_t>{7, 8};
    };
    const cc::Program p = prog(R"(
      __ROM const unsigned char data[] = __file("data.bin");
      int main(void) { return 0; }
    )", a);
    CHECK(bytesOf(p, "data") == std::vector<uint8_t>{7, 8});
    CHECK(asked > 0);
  }
}

TEST_SUITE("the emitted assembly stands alone") {
  TEST_CASE("the bytes go down as db in .data, with no directive to resolve") {
    const Assets a = preloaded();
    CcOptions opts;
    opts.assets = &a;
    const std::string text = compile(R"(
      __ROM const unsigned char data[] = __file("data.bin");
      int main(void) { return 0; }
    )", opts);
    const std::string data = text.substr(text.find(".data"));
    CHECK(has(data, "data:"));
    CHECK(has(data, "db 222, 173, 190, 239"));
    CHECK(!has(text, ".file"));
    CHECK(!has(text, ".image"));
    // The assembler then needs no assets at all.
    const Assembled as = assemble(text);
    CHECK(as.errors.empty());
    CHECK(as.cart == std::vector<uint8_t>{0xde, 0xad, 0xbe, 0xef});
  }

  TEST_CASE("ROM.h carries the blob's size and address") {
    const Assets a = preloaded();
    const cc::Program p = prog(R"(
      __ROM const unsigned char pad[] = { 1, 2, 3 };
      __ROM const unsigned char ship[] = __sprite("ship.png", 2);
      int main(void) { return 0; }
    )", a);
    CHECK(has(p.romHeader, "ROM_ship_SIZE 11"));
    CHECK(has(p.romHeader, "ROM_ship_LO   0x03"));
    CHECK(has(p.romHeader, "ROM_ship      3"));
  }

  TEST_CASE("ROM_name_SIZE reaches the program, and rom_copy brings the bytes in") {
    const Assets a = preloaded();
    CcOptions opts;
    opts.assets = &a;
    const std::string text = compile(R"(
      #include <sys.h>
      #include <gpu.h>
      #include "ROM.h"
      __ROM const unsigned char data[] = __file("data.bin");
      unsigned char n, work[4], last;
      int main(void) {
        n = ROM_data_SIZE;
        rom_copy(work, ROM_data, ROM_data_SIZE);
        last = work[3];
        return 0;
      }
    )", opts);
    const Ran r = runAsm(text, 200000, false);
    CHECK(r.halted());
    CHECK(r.u8("n") == 4);
    CHECK(r.u8("last") == 0xef);
  }

  TEST_CASE("two objects from one file share an address") {
    const Assets a = preloaded();
    const cc::Program p = prog(R"(
      __ROM const unsigned char one[] = __file("data.bin");
      __ROM const unsigned char two[] = __file("data.bin");
      int main(void) { return 0; }
    )", a);
    REQUIRE(p.rom.size() == 2);
    CHECK(p.rom[1].addr == p.rom[0].addr);
    CHECK(p.rom[1].sameAs == "one");
  }
}

TEST_SUITE("an asset initializer is refused when it cannot be right") {
  TEST_CASE("a frame count of zero") {
    CHECK(has(refusesWith(R"(
      __ROM const unsigned char ship[] = __sprite("ship.png", 0);
      int main(void) { return 0; }
    )", preloaded()), "at least one frame"));
  }

  TEST_CASE("a frame count the width does not divide into") {
    CHECK(has(refusesWith(R"(
      __ROM const unsigned char ship[] = __sprite("ship.png", 3);
      int main(void) { return 0; }
    )", preloaded()), "does not divide into 3 frames"));
  }

  TEST_CASE("a frame wider than a sprite can be") {
    Assets a;
    ImageAsset wide;
    wide.width = 130;
    wide.height = 1;
    wide.pixels.assign(130, 1);
    a.images["wide.png"] = wide;
    CHECK(has(refusesWith(R"(
      __ROM const unsigned char s[] = __sprite("wide.png", 2);
      int main(void) { return 0; }
    )", a), "at most 64 pixels a side"));
  }

  TEST_CASE("an image too big for CMD_BLIT") {
    Assets a;
    ImageAsset tall;
    tall.width = 1;
    tall.height = 257;
    tall.pixels.assign(257, 1);
    a.images["tall.png"] = tall;
    CHECK(has(refusesWith(R"(
      __ROM const unsigned char s[] = __image("tall.png");
      int main(void) { return 0; }
    )", a), "CMD_BLIT"));
  }

  TEST_CASE("a missing asset names the file and the form") {
    CHECK(refusesWith(R"(
      __ROM const unsigned char ship[] = __sprite("missing.png", 2);
      int main(void) { return 0; }
    )", preloaded()) == "missing.png: cannot read the image for __sprite (is it beside the source?)");
    CHECK(refusesWith(R"(
      __ROM const unsigned char s[] = __sample("nope.wav");
      int main(void) { return 0; }
    )", preloaded()) == "nope.wav: cannot read the sample for __sample (is it beside the source?)");
  }

  TEST_CASE("no assets at all reads as every file missing") {
    CHECK(has(refuses(R"(
      __ROM const unsigned char d[] = __file("data.bin");
      int main(void) { return 0; }
    )"), "cannot read the file for __file"));
  }

  TEST_CASE("use on an object that is not __ROM") {
    CHECK(has(refusesWith(R"(
      const unsigned char ship[] = __image("ship.png");
      int main(void) { return 0; }
    )", preloaded()), "__image fills a __ROM object"));
  }

  TEST_CASE("use with a sized array") {
    CHECK(has(refusesWith(R"(
      __ROM const unsigned char ship[16] = __image("ship.png");
      int main(void) { return 0; }
    )", preloaded()), "Leave the brackets of ship empty"));
  }

  TEST_CASE("use on an element type other than unsigned char") {
    CHECK(has(refusesWith(R"(
      __ROM const int ship[] = __image("ship.png");
      int main(void) { return 0; }
    )", preloaded()), "fills an unsigned char array"));
    CHECK(has(refusesWith(R"(
      __ROM const unsigned char *ship = __image("ship.png");
      int main(void) { return 0; }
    )", preloaded()), "fills an unsigned char array"));
  }

  TEST_CASE("a second argument on a form other than __sprite") {
    CHECK(has(refusesWith(R"(
      __ROM const unsigned char pic[] = __image("ship.png", 2);
      int main(void) { return 0; }
    )", preloaded()), "one argument"));
  }

  TEST_CASE("a name that is not a string") {
    CHECK(has(refusesWith(R"(
      __ROM const unsigned char pic[] = __image(ship);
      int main(void) { return 0; }
    )", preloaded()), "file name in quotes"));
  }
}

TEST_SUITE("a __ROM object holds bytes and words") {
  TEST_CASE("refuses a double element and says what to declare instead") {
    const std::string msg = refuses(R"(
      __ROM const double d[] = { 1.5 };
      int main(void) { return 0; }
    )");
    CHECK(has(msg, "d is __ROM and holds double"));
    CHECK(has(msg, "char, unsigned char, int or unsigned int"));
  }

  TEST_CASE("refuses a long element") {
    CHECK(has(refuses(R"(
      __ROM const unsigned long n = 1;
      int main(void) { return 0; }
    )"), "holds ulong"));
  }

  TEST_CASE("still takes int elements as words, high byte first") {
    const cc::Program p = cc::compileProgram({{"main.c", R"(
      __ROM const int w[] = { 0x1234 };
      int main(void) { return 0; }
    )"}});
    REQUIRE(p.rom.size() == 1);
    CHECK(p.rom[0].bytes == std::vector<uint8_t>{0x12, 0x34});
  }
}

TEST_SUITE("C images land on the machine palette") {
  TEST_CASE("a picture in its own colours maps each pixel to the nearest machine colour, 0 stays clear") {
    ImageAsset img;
    img.width = 3;
    img.height = 1;
    img.pixels = {0, 1, 2};
    img.palette.assign(768, 0);
    // Entry 1 is pure red and entry 2 is a near black, which must still draw.
    img.palette[3] = 250;
    img.palette[6] = 5;
    img.palette[7] = 5;
    img.palette[8] = 5;
    Assets a;
    a.images["pic.png"] = img;
    const std::vector<uint8_t> machine = machinePalette();
    const cc::Program p = prog("__ROM const unsigned char pic[] = __image(\"pic.png\");\nint main(void) { return 0; }", a);
    const std::vector<uint8_t> b = bytesOf(p, "pic");
    REQUIRE_EQ(b.size(), 5u);
    CHECK_EQ(b[2], 0);
    CHECK_EQ(b[3], nearestIndex(machine, 250, 0, 0));
    CHECK_NE(b[4], 0);
  }

  // A picture with an off-palette colour keeps its own palette through the
  // loader, and black in it is then a drawn pixel, index 1 on the default
  // palette, which has no other black. The note says so, once per sprite.
  ImageAsset blackBacked() {
    ImageAsset img;
    img.width = 2;
    img.height = 1;
    img.pixels = {1, 2};
    img.palette.assign(768, 0);
    // Entry 1 is black, entry 2 an off-palette pink.
    img.palette[6] = 250;
    img.palette[7] = 120;
    img.palette[8] = 130;
    return img;
  }

  TEST_CASE("a black pixel in a sprite is drawn as index 1 and the build is told") {
    Assets a;
    a.images["ship.png"] = blackBacked();
    std::vector<std::string> notes;
    // The layout is worked out more than once in a build, so the remark
    // arrives more than once and the receiver keeps one, as a project does.
    a.note = [&](std::string_view name, const std::string& note) {
      const std::string line = std::string(name) + ": " + note;
      if (std::find(notes.begin(), notes.end(), line) == notes.end()) notes.push_back(line);
    };
    const cc::Program p = prog("__ROM const unsigned char ship[] = __sprite(\"ship.png\");\nint main(void) { return 0; }", a);
    const std::vector<uint8_t> b = bytesOf(p, "ship");
    REQUIRE_EQ(b.size(), 5u);
    CHECK_EQ(b[3], 1);
    REQUIRE_EQ(notes.size(), 1u);
    CHECK(notes[0].starts_with("ship.png: black pixels in a sprite are drawn as index 1 and look dark blue"));
    CHECK(has(notes[0], "transparent background"));
  }

  TEST_CASE("an image with black says nothing: nothing is skipped in a blit") {
    Assets a;
    a.images["pic.png"] = blackBacked();
    std::vector<std::string> notes;
    a.note = [&](std::string_view, const std::string& note) { notes.push_back(note); };
    const cc::Program p = prog("__ROM const unsigned char pic[] = __image(\"pic.png\");\nint main(void) { return 0; }", a);
    CHECK_EQ(bytesOf(p, "pic")[2], 1);
    CHECK(notes.empty());
  }

  TEST_CASE("a sprite with no black pixel earns no note") {
    Assets a;
    ImageAsset img = blackBacked();
    img.pixels = {2, 2};
    a.images["ship.png"] = img;
    std::vector<std::string> notes;
    a.note = [&](std::string_view, const std::string& note) { notes.push_back(note); };
    prog("__ROM const unsigned char ship[] = __sprite(\"ship.png\");\nint main(void) { return 0; }", a);
    CHECK(notes.empty());
  }
}
