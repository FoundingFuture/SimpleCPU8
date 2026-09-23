// assets.test.ts, one test per describe/it pair.
#include <doctest.h>

#include <array>

#include "asm/asm.h"
#include "assets/assets.h"

using namespace sc8;

namespace {

using Rgb = std::array<int, 3>;
using Rgba = std::array<int, 4>;

const std::vector<uint8_t> MACHINE = machinePalette();

// Build a tiny image from a list of RGB triples, one pixel per color, with
// index 0 reserved for transparency the way decodeImage reserves it.
DecodedImage image(const std::vector<Rgb>& colors, const std::vector<uint8_t>& pixels) {
  std::vector<uint8_t> palette(768);
  for (size_t i = 0; i < colors.size(); i++) {
    palette[(i + 1) * 3] = static_cast<uint8_t>(colors[i][0]);
    palette[(i + 1) * 3 + 1] = static_cast<uint8_t>(colors[i][1]);
    palette[(i + 1) * 3 + 2] = static_cast<uint8_t>(colors[i][2]);
  }
  return {static_cast<int>(pixels.size()), 1, pixels, palette};
}

// rgba the way getImageData hands it over: four bytes a pixel, row-major.
std::vector<uint8_t> rgba(const std::vector<Rgba>& px) {
  std::vector<uint8_t> out;
  for (const Rgba& p : px) {
    for (const int c : p) out.push_back(static_cast<uint8_t>(c));
  }
  return out;
}

std::vector<uint8_t> midiBytes() {
  std::vector<uint8_t> b(16);
  b[0] = 0x4d;
  b[1] = 0x54;
  b[2] = 0x68;
  b[3] = 0x64;
  return b;
}

using Bytes = std::vector<uint8_t>;
using Kinds = std::vector<SubItemKind>;

const std::set<std::string> NONE;

}  // namespace

TEST_CASE("paletteIndex maps packed rgb to the first index holding it") {
  const auto idx = paletteIndex(MACHINE);
  CHECK(idx.at(0x000000) == 0);
  CHECK(idx.at(0xffffff) == 255);
  CHECK(idx.at(0xff0000) == 224);
  CHECK(idx.size() == 256);  // 3-3-2 has no duplicate entries
}

TEST_CASE("nearestIndex returns the exact index for a color already in the palette") {
  CHECK(nearestIndex(MACHINE, 255, 0, 0) == 224);
  CHECK(nearestIndex(MACHINE, 0, 255, 0) == 28);
  CHECK(nearestIndex(MACHINE, 255, 255, 255) == 255);
  CHECK(nearestIndex(MACHINE, 0, 0, 0) == 0);
}

TEST_CASE("nearestIndex snaps a near miss to its closest neighbour") {
  // 254,1,1 is one step from full red on every channel.
  CHECK(nearestIndex(MACHINE, 254, 1, 1) == 224);
}

TEST_CASE("nearestIndex picks by distance across all three channels") {
  // 3-3-2 blue has only four levels: 0, 85, 170, 255. 200 is nearest 170.
  const size_t idx = nearestIndex(MACHINE, 0, 0, 200);
  CHECK(Bytes{MACHINE[idx * 3], MACHINE[idx * 3 + 1], MACHINE[idx * 3 + 2]} == Bytes{0, 0, 170});
}

TEST_CASE("nearestIndex is stable for a color equidistant between two entries") {
  CHECK(nearestIndex(MACHINE, 0, 0, 128) == nearestIndex(MACHINE, 0, 0, 128));
}

TEST_CASE("classifyImage remaps an image whose colors all exist in 3-3-2") {
  // 255,0,0 is index 224. 0,255,0 is index 28. Both are on-palette.
  const auto img = image({{255, 0, 0}, {0, 255, 0}}, {1, 2, 1});
  const auto out = classifyImage(img, MACHINE);
  CHECK(out.onPalette);
  CHECK(out.pixels == Bytes{224, 28, 224});
  CHECK(out.palette.empty());
  CHECK_FALSE(out.blackMerge);
}

TEST_CASE("classifyImage keeps the palette when any color is off-palette") {
  // 1,2,3 is not a 3-3-2 color.
  const auto img = image({{255, 0, 0}, {1, 2, 3}}, {1, 2});
  const auto out = classifyImage(img, MACHINE);
  CHECK_FALSE(out.onPalette);
  CHECK(out.pixels == Bytes{1, 2});  // untouched
  CHECK(out.palette == img.palette);
}

TEST_CASE("classifyImage leaves transparent pixels at index 0") {
  const auto out = classifyImage(image({{255, 0, 0}}, {0, 1, 0}), MACHINE);
  CHECK(out.onPalette);
  CHECK(out.pixels == Bytes{0, 224, 0});
}

TEST_CASE("classifyImage reports the merge when opaque black meets transparency") {
  const auto out = classifyImage(image({{0, 0, 0}}, {0, 1}), MACHINE);
  CHECK(out.onPalette);
  CHECK(out.pixels == Bytes{0, 0});  // both are index 0 now
  CHECK(out.blackMerge);
}

TEST_CASE("classifyImage reports no merge when the image has black but no transparency") {
  CHECK_FALSE(classifyImage(image({{0, 0, 0}, {255, 0, 0}}, {1, 2}), MACHINE).blackMerge);
}

TEST_CASE("classifyImage ignores palette entries the pixels never use") {
  // Index 2 is off-palette but unused, so the image is still on-palette.
  CHECK(classifyImage(image({{255, 0, 0}, {1, 2, 3}}, {1, 1}), MACHINE).onPalette);
}

TEST_CASE("imageAssetFor hands over remapped pixels and the machine palette when on-palette") {
  const auto out = imageAssetFor(image({{255, 0, 0}, {0, 255, 0}}, {1, 2, 1}), MACHINE);
  CHECK(out.pixels == Bytes{224, 28, 224});  // machine indexes, not 1 and 2
  CHECK(out.palette == MACHINE);
  CHECK(out.width == 3);
  CHECK(out.height == 1);
}

TEST_CASE("imageAssetFor hands over an off-palette image with its own pixels and palette") {
  const auto img = image({{255, 0, 0}, {1, 2, 3}}, {1, 2});
  const auto out = imageAssetFor(img, MACHINE);
  CHECK(out.pixels == Bytes{1, 2});
  CHECK(out.palette == img.palette);
}

// The assembler places img.pixels verbatim. An on-palette image ships no
// palette, so unremapped indexes would draw a different picture.
TEST_CASE("imageAssetFor puts machine indexes on the cartridge for an on-palette image") {
  Assets assets;
  assets.images["pic.png"] = imageAssetFor(image({{255, 0, 0}, {0, 255, 0}}, {1, 2}), MACHINE);
  const Assembled a = assemble("        HLT\n.data\npic:    .image('pic.png')\n", &assets);
  CHECK(a.errors.empty());
  CHECK(a.cart == Bytes{224, 28});
}

TEST_CASE("deriveLabel takes the stem of the file name, lowercased") {
  CHECK(deriveLabel("Cat.PNG", SubItemKind::Pixels, NONE) == "cat");
}

TEST_CASE("deriveLabel replaces anything outside a-z0-9_") {
  CHECK(deriveLabel("my sprite-01!.png", SubItemKind::Pixels, NONE) == "my_sprite_01_");
}

TEST_CASE("deriveLabel prefixes a leading digit") {
  CHECK(deriveLabel("8ball.png", SubItemKind::Pixels, NONE) == "_8ball");
}

TEST_CASE("deriveLabel appends pal for a palette") {
  CHECK(deriveLabel("cat.png", SubItemKind::PaletteBlob, NONE) == "catpal");
}

TEST_CASE("deriveLabel leaves the other kinds unsuffixed") {
  CHECK(deriveLabel("boom.wav", SubItemKind::Sample, NONE) == "boom");
  CHECK(deriveLabel("song.mid", SubItemKind::File, NONE) == "song");
}

TEST_CASE("deriveLabel counts up past a collision") {
  CHECK(deriveLabel("cat.png", SubItemKind::Pixels, {"cat", "cat2"}) == "cat3");
}

TEST_CASE("deriveLabel falls back when the name has no usable characters") {
  CHECK(deriveLabel("....", SubItemKind::Pixels, NONE) == "asset");
}

TEST_CASE("deriveLabel keeps a dotted name's full stem apart from the extension") {
  CHECK(deriveLabel("level.1.tiles.png", SubItemKind::Pixels, NONE) == "level_1_tiles");
}

TEST_CASE("directiveLine writes each kind to its own directive") {
  CHECK(directiveLine("cat", SubItemKind::Pixels, "cat.png") == "cat:    .image('cat.png')");
  CHECK(directiveLine("catpal", SubItemKind::PaletteBlob, "cat.png") == "catpal: .palette('cat.png')");
  CHECK(directiveLine("boom", SubItemKind::Sample, "boom.wav") == "boom:   .sample('boom.wav')");
  CHECK(directiveLine("song", SubItemKind::File, "song.mid") == "song:   .file('song.mid')");
}

TEST_CASE("directiveLine puts the directive at the editor's mnemonic column") {
  CHECK(directiveLine("cat", SubItemKind::Pixels, "cat.png").find(".image") == MNEM_COL);
  CHECK(directiveLine("catpal", SubItemKind::PaletteBlob, "cat.png").find(".palette") == MNEM_COL);
}

TEST_CASE("directiveLine gives a long label its own single space") {
  CHECK(directiveLine("averylonglabelname", SubItemKind::Pixels, "cat.png") ==
        "averylonglabelname: .image('cat.png')");
}

TEST_CASE("directiveLine escapes a quote in the file name") {
  CHECK(directiveLine("odd", SubItemKind::File, "it's.bin") == "odd:    .file(\"it's.bin\")");
}

// The pane writes source, the assembler reads it. Pin the seam, so a name
// holding a quote cannot be emitted in a form the assembler rejects.
TEST_CASE("directiveLine emits a line the assembler parses") {
  const std::string line = directiveLine("odd", SubItemKind::File, "it's.bin");
  Assets assets;
  assets.files["it's.bin"] = Bytes{7};
  const Assembled a = assemble("        HLT\n.data\n" + line + "\n", &assets);
  CHECK(a.errors.empty());
  CHECK(a.cart == Bytes{7});
}

TEST_CASE("quantizePcm puts silence at the midpoint and the rails at the ends") {
  CHECK(quantizePcm({-1, 0, 1}).bytes == Bytes{0, 128, 255});
}

TEST_CASE("quantizePcm clamps beyond the rails instead of wrapping") {
  CHECK(quantizePcm({-4, 4}).bytes == Bytes{0, 255});
}

TEST_CASE("quantizePcm rounds rather than truncating") {
  CHECK(quantizePcm({0.5f, -0.5f}).bytes == Bytes{191, 64});  // 191.25 and 63.75
}

TEST_CASE("quantizePcm caps at 65535 bytes, the largest APU_ARG pair") {
  CHECK(MAX_SAMPLE_BYTES == 65535);
  const auto lng = quantizePcm(std::vector<float>(70000));
  CHECK(lng.bytes.size() == 65535);
  CHECK(lng.truncated);
}

TEST_CASE("quantizePcm reports no truncation at exactly the ceiling") {
  const auto edge = quantizePcm(std::vector<float>(65535));
  CHECK(edge.bytes.size() == 65535);
  CHECK_FALSE(edge.truncated);
}

TEST_CASE("quantizePcm handles an empty buffer") {
  CHECK(quantizePcm({}).bytes.empty());
}

TEST_CASE("isMidi recognises the MThd magic") {
  CHECK(isMidi(midiBytes()));
}

TEST_CASE("isMidi rejects anything else, extension notwithstanding") {
  CHECK_FALSE(isMidi({0x52, 0x49, 0x46, 0x46}));  // RIFF
  CHECK_FALSE(isMidi({1, 2}));                     // too short
  CHECK_FALSE(isMidi({}));
}

TEST_CASE("sub-items offers pixels alone for an on-palette image") {
  const auto items = subItemsForImage(image({{255, 0, 0}}, {1, 1}), MACHINE);
  REQUIRE(items.size() == 1);
  CHECK(items[0].kind == SubItemKind::Pixels);
  CHECK_FALSE(items[0].note);
  CHECK(items[0].bytes == Bytes{224, 224});
}

TEST_CASE("sub-items offers pixels and palette for a unique palette") {
  const auto items = subItemsForImage(image({{1, 2, 3}}, {1}), MACHINE);
  REQUIRE(items.size() == 2);
  CHECK(items[0].kind == SubItemKind::Pixels);
  CHECK(items[1].kind == SubItemKind::PaletteBlob);
  CHECK(items[1].bytes.size() == 768);
}

TEST_CASE("sub-items notes the black merge, and only then") {
  const auto merged = subItemsForImage(image({{0, 0, 0}}, {0, 1}), MACHINE);
  REQUIRE(merged[0].note);
  CHECK(merged[0].note->find("black") != std::string::npos);
  const auto clean = subItemsForImage(image({{255, 0, 0}}, {0, 1}), MACHINE);
  CHECK_FALSE(clean[0].note);
}

TEST_CASE("sub-items notes a truncated sample, and only then") {
  const auto lng = subItemsForSample(std::vector<float>(70000));
  REQUIRE(lng[0].note);
  CHECK(lng[0].note->find("truncat") != std::string::npos);
  CHECK_FALSE(subItemsForSample(std::vector<float>(10))[0].note);
}

TEST_CASE("sub-items offers one file sub-item for raw bytes") {
  const auto items = subItemsForFile(midiBytes());
  REQUIRE(items.size() == 1);
  CHECK(items[0].kind == SubItemKind::File);
  CHECK_FALSE(items[0].note);
}

TEST_CASE("labelsIn finds labels in every section") {
  const auto found = labelsIn("start:  LD A <- 1\n        HLT\n.ram\nsum:    db 0\n.data\ncat:    .image('c.png')\n");
  CHECK(found == std::set<std::string>{"cat", "start", "sum"});
}

TEST_CASE("labelsIn ignores indented mnemonics and comments") {
  CHECK(labelsIn("        LD A <- 1\n; note: not a label\n").empty());
}

TEST_CASE("fitToScreen leaves an image already inside the box alone") {
  CHECK(fitToScreen(200, 100, 256) == Size{200, 100});
  CHECK(fitToScreen(256, 256, 256) == Size{256, 256});
}

TEST_CASE("fitToScreen scales the longest side down to the limit") {
  CHECK(fitToScreen(512, 256, 256) == Size{256, 128});
  CHECK(fitToScreen(100, 400, 256) == Size{64, 256});
}

TEST_CASE("fitToScreen keeps the aspect ratio on an awkward size") {
  CHECK(fitToScreen(1000, 333, 256) == Size{256, 85});
}

TEST_CASE("fitToScreen never rounds a side away to nothing") {
  CHECK(fitToScreen(4000, 3, 256) == Size{256, 1});
}

namespace {
const std::string LINE = "cat:    .image('c.png')";
std::string atLine(const Inserted& out) { return out.source.substr(out.at, LINE.size()); }
}  // namespace

TEST_CASE("insertDirective appends to an existing .data section") {
  const std::string src = "        HLT\n.data\nold:    db 1\n";
  const auto out = insertDirective(src, LINE, src.size());
  CHECK(out.source == "        HLT\n.data\nold:    db 1\n" + LINE + "\n");
  CHECK(atLine(out) == LINE);
}

TEST_CASE("insertDirective creates a .data section when there is none") {
  const auto out = insertDirective("        HLT\n", LINE, 0);
  CHECK(out.source == "        HLT\n\n.data\n" + LINE + "\n");
  CHECK(atLine(out) == LINE);
}

TEST_CASE("insertDirective inserts at the caret line when the caret is inside .data") {
  const std::string src = "        HLT\n.data\na:      db 1\nb:      db 2\n";
  const auto out = insertDirective(src, LINE, src.find("b:"));
  CHECK(out.source == "        HLT\n.data\na:      db 1\n" + LINE + "\nb:      db 2\n");
  CHECK(atLine(out) == LINE);
}

TEST_CASE("insertDirective appends when the caret sits above .data") {
  const std::string src = "        HLT\n.data\na:      db 1\n";
  const auto out = insertDirective(src, LINE, 3);  // inside the code section
  CHECK(out.source == "        HLT\n.data\na:      db 1\n" + LINE + "\n");
  CHECK(atLine(out) == LINE);
}

TEST_CASE("insertDirective ends a source with no trailing newline cleanly") {
  const auto out = insertDirective("        HLT\n.data\na:      db 1", LINE, 0);
  CHECK(out.source == "        HLT\n.data\na:      db 1\n" + LINE + "\n");
  CHECK(atLine(out) == LINE);
}

TEST_CASE("placedAssets finds each asset and the kinds already placed") {
  const auto placed = placedAssets(
      "        HLT\n.data\ncat:    .image('cat.png')\ncatpal: .palette('cat.png')\n"
      "boom:   .sample('boom.wav')\ntune:   .file('song.mid')");
  CHECK(placed.at("cat.png") == Kinds{SubItemKind::Pixels, SubItemKind::PaletteBlob});
  CHECK(placed.at("boom.wav") == Kinds{SubItemKind::Sample});
  CHECK(placed.at("song.mid") == Kinds{SubItemKind::File});
}

TEST_CASE("placedAssets ignores a directive inside a comment") {
  CHECK(placedAssets("; cat:  .image('cat.png')\n").empty());
}

TEST_CASE("placedAssets accepts either quote style") {
  CHECK(placedAssets("x: .file(\"it's.bin\")\n").contains("it's.bin"));
}

TEST_CASE("quantizeToPalette maps every opaque pixel to its nearest palette index") {
  const auto q = quantizeToPalette(rgba({{255, 0, 0, 255}, {0, 255, 0, 255}}), 2, 1, MACHINE);
  CHECK(q.pixels == Bytes{224, 28});
  CHECK_FALSE(q.opaqueBlack);
}

TEST_CASE("quantizeToPalette leaves a transparent pixel at index 0 without calling it a merge") {
  // Alpha under 128 is transparent, so the black those bytes carry is not
  // an opaque pixel that landed on index 0.
  const auto q = quantizeToPalette(rgba({{0, 0, 0, 0}, {255, 255, 255, 255}}), 2, 1, MACHINE);
  CHECK(q.pixels == Bytes{0, 255});
  CHECK_FALSE(q.opaqueBlack);
}

TEST_CASE("quantizeToPalette reports an opaque pixel that lands on index 0") {
  const auto q = quantizeToPalette(rgba({{1, 1, 1, 255}}), 1, 1, MACHINE);
  CHECK(q.pixels == Bytes{0});
  CHECK(q.opaqueBlack);
}

TEST_CASE("quantizeToPalette treats alpha 128 as opaque and 127 as transparent") {
  CHECK(quantizeToPalette(rgba({{8, 8, 8, 128}}), 1, 1, MACHINE).opaqueBlack);
  CHECK_FALSE(quantizeToPalette(rgba({{8, 8, 8, 127}}), 1, 1, MACHINE).opaqueBlack);
}

TEST_CASE("quantizeToPalette gives a repeated color the same index every time it appears") {
  // The lookup is cached by packed RGB. A cache that returned a stale or
  // wrong entry would show up as two indexes for one color.
  const auto q =
      quantizeToPalette(rgba({{200, 40, 90, 255}, {12, 240, 30, 255}, {200, 40, 90, 255}}), 3, 1, MACHINE);
  CHECK(q.pixels[0] == q.pixels[2]);
  CHECK(q.pixels[0] == nearestIndex(MACHINE, 200, 40, 90));
  CHECK(q.pixels[1] == nearestIndex(MACHINE, 12, 240, 30));
}

TEST_CASE("quantizeToPalette agrees with nearestIndex on every pixel of a mixed image") {
  std::vector<Rgba> px;
  for (int i = 0; i < 32; i++) px.push_back({(i * 7) & 0xff, (i * 13) & 0xff, (i * 29) & 0xff, 255});
  const auto q = quantizeToPalette(rgba(px), 32, 1, MACHINE);
  for (size_t i = 0; i < px.size(); i++) CHECK(q.pixels[i] == nearestIndex(MACHINE, px[i][0], px[i][1], px[i][2]));
}

namespace {
const std::string SCALED = "scaled to fit the screen";
const std::string QUANTIZED = "colors quantized to the current palette";
const std::string BLACK(QUANTIZE_BLACK_NOTE);
}  // namespace

TEST_CASE("conversionNote says nothing when the conversion lost nothing") {
  CHECK_FALSE(conversionNote({false, false, false}));
}

TEST_CASE("conversionNote reports each fact on its own") {
  CHECK(conversionNote({true, false, false}) == SCALED);
  CHECK(conversionNote({false, true, false}) == QUANTIZED);
  CHECK(conversionNote({false, false, true}) == BLACK);
}

TEST_CASE("conversionNote joins the facts it was given, scale first") {
  CHECK(conversionNote({true, true, false}) == SCALED + "; " + QUANTIZED);
  CHECK(conversionNote({true, false, true}) == SCALED + "; " + BLACK);
  CHECK(conversionNote({false, true, true}) == QUANTIZED + "; " + BLACK);
  CHECK(conversionNote({true, true, true}) == SCALED + "; " + QUANTIZED + "; " + BLACK);
}

TEST_CASE("conversionNote names the consequence the quantize path actually has") {
  // Nothing merged on this path: a dark pixel simply landed on index 0,
  // which a stamped sprite skips. So it does not borrow the on-palette
  // path's merge wording.
  CHECK(QUANTIZE_BLACK_NOTE != BLACK_MERGE_NOTE);
  CHECK(QUANTIZE_BLACK_NOTE.find("transparent index") != std::string_view::npos);
}

TEST_CASE("placedRefusal names the one directive holding the asset, singular") {
  CHECK(placedRefusal("cat.png", {SubItemKind::Pixels}) ==
        "cat.png is still placed by .image in .data. Remove that line first.");
}

TEST_CASE("placedRefusal turns plural when two directives hold the asset") {
  // An image that ships its own palette sits in .data twice, under two
  // labels. Telling the user to remove "that line" leaves the second one
  // in place and the next delete refuses again.
  CHECK(placedRefusal("cat.png", {SubItemKind::Pixels, SubItemKind::PaletteBlob}) ==
        "cat.png is still placed by .image, .palette in .data. Remove those lines first.");
}

TEST_CASE("placedRefusal lists the directives in the order the source placed them") {
  CHECK(placedRefusal("cat.png", {SubItemKind::PaletteBlob, SubItemKind::Pixels}).find(".palette, .image") !=
        std::string::npos);
}
