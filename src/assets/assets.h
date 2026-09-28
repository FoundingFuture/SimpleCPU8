// The asset pipeline: what an image or an audio file becomes on the way
// into the cartridge. In the browser version an upload became bytes the
// assembler could place. Here the assembler reads .image, .palette and
// .sample from disk, so the decoders sit beside the same conversions.
//
// The pure conversion logic is ported from assets.ts one function at a
// time. The two loaders at the end stand in for the browser's canvas and
// OfflineAudioContext, decoding through stb_image and miniaudio.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "asm/asm.h"

namespace sc8 {

// The machine palette as a vector, the shape the conversions take. The GPU
// is its one home: this reads the GPU's table rather than keeping a copy.
std::vector<uint8_t> machinePalette();

// The editor's mnemonic column. The formatter owns it in the browser
// version. It lives here until the editor's formatter is ported.
constexpr size_t MNEM_COL = 8;

// PaletteBlob rather than Palette, which is the GPU's type for the table.
enum class SubItemKind { Pixels, PaletteBlob, Sample, File };

// DecodedImage in assets.ts. The assembler's ImageAsset already has the
// same four fields, so the port reuses it rather than carrying two names
// for one shape.
using DecodedImage = ImageAsset;

struct ClassifiedImage {
  bool onPalette = false;
  std::vector<uint8_t> pixels;   // remapped to machine indexes when onPalette
  std::vector<uint8_t> palette;  // empty when the machine palette covers it
  bool blackMerge = false;       // opaque black collapsed into transparent index 0
};

// Index 0 is transparent everywhere a sprite is drawn, so an opaque pixel
// that ends up there disappears. Two paths produce that loss and they say it
// differently, because they are not the same event. classifyImage really
// merges: opaque black and transparent pixels arrive at one index.
inline constexpr std::string_view BLACK_MERGE_NOTE = "opaque black merged into the transparent index";

// quantizeToPalette merges nothing. A dark pixel is snapped to its nearest
// entry, which happens to be index 0, the one a stamped sprite skips.
inline constexpr std::string_view QUANTIZE_BLACK_NOTE =
    "dark pixels landed on the transparent index, so a stamped sprite will not draw them";

// Packed RGB to palette index. The first index holding a color wins, so a
// palette with duplicates resolves to its lowest index.
std::map<uint32_t, uint8_t> paletteIndex(const std::vector<uint8_t>& palette);

// An image whose every used color exists in the machine palette does not
// need to ship a palette. Rewrite its indexes and drop it.
ClassifiedImage classifyImage(const DecodedImage& img, const std::vector<uint8_t>& machine);

// The image to hand the assembler: remapped pixels and the machine palette
// when on-palette, the image's own pixels and palette otherwise.
DecodedImage imageAssetFor(const DecodedImage& img, const std::vector<uint8_t>& machine);

// A label the assembler will accept, from a file name it will not.
std::string deriveLabel(std::string_view fileName, SubItemKind kind, const std::set<std::string>& taken);

// The directive each kind places: .image, .palette, .sample, .file.
std::string_view directiveFor(SubItemKind kind);

// The line a drop inserts, with the directive at the mnemonic column.
std::string directiveLine(std::string_view label, SubItemKind kind, std::string_view assetName);

// Every asset name already sitting in .data, and which kinds place it.
// Insertion order of the kinds is kept, because placedRefusal lists them in
// the order the source placed them.
std::map<std::string, std::vector<SubItemKind>> placedAssets(std::string_view source);

// Why a delete was refused: which directives still hold the asset.
std::string placedRefusal(std::string_view name, const std::vector<SubItemKind>& kinds);

// CMD_DEF_SAMPLE builds its length from two byte latches, so 65535 is the
// ceiling and a full 64KB is one byte out of reach.
inline constexpr size_t MAX_SAMPLE_BYTES = 65535;

struct QuantizedPcm {
  std::vector<uint8_t> bytes;
  bool truncated = false;
};

// Float samples in -1..1 to the unsigned bytes the APU mixes. Silence sits
// at 128. Anything past the rails clamps rather than wrapping.
QuantizedPcm quantizePcm(const std::vector<float>& pcm);

struct SubItem {
  SubItemKind kind;
  std::vector<uint8_t> bytes;
  std::optional<std::string> note;  // set only when the conversion lost something
};

// A standard MIDI file opens with "MThd". Sniff the magic rather than
// trusting the extension, and before offering the bytes to an audio decoder.
bool isMidi(const std::vector<uint8_t>& bytes);

std::vector<SubItem> subItemsForImage(const DecodedImage& img, const std::vector<uint8_t>& machine);
std::vector<SubItem> subItemsForSample(const std::vector<float>& pcm);
std::vector<SubItem> subItemsForFile(const std::vector<uint8_t>& bytes);

// Labels at column 0 of a source, comments stripped.
std::set<std::string> labelsIn(std::string_view source);

struct Size {
  int width = 0;
  int height = 0;
  bool operator==(const Size&) const = default;
};

// Scale the longest side to max and keep the ratio. A side never rounds
// away to zero.
Size fitToScreen(int w, int h, int max);

// Closest palette entry by squared distance in RGB.
uint8_t nearestIndex(const std::vector<uint8_t>& palette, int r, int g, int b);

struct Quantized {
  std::vector<uint8_t> pixels;
  bool opaqueBlack = false;  // an opaque pixel landed on index 0
};

// Snap raw RGBA, four bytes a pixel row-major, to machine palette indexes.
// Alpha under 128 is transparent and keeps index 0.
Quantized quantizeToPalette(const std::vector<uint8_t>& rgba, int width, int height,
                            const std::vector<uint8_t>& palette);

struct ConversionFacts {
  bool scaled = false;
  bool quantized = false;
  bool opaqueBlack = false;
};

// The note for an image conversion, or nothing when the conversion lost
// nothing worth saying. One clause per fact, in the order they happened.
std::optional<std::string> conversionNote(const ConversionFacts& f);

struct Inserted {
  std::string source;
  size_t at = 0;
};

// Place a directive line in the .data section. A caret inside .data drops
// the line above the caret's own line. Anywhere else appends to the end of
// .data, and a source with no .data section grows one.
Inserted insertDirective(std::string_view source, std::string_view line, size_t caret);

// decodeImage in main.ts, on raw RGBA rather than an ImageBitmap. Fit to
// the screen by nearest neighbour, index the opaque colors, and past 255 of
// them redraw with smoothing and quantize to the machine palette instead.
// The note reports what changed, or stays empty.
struct DecodedReport {
  DecodedImage image;
  std::optional<std::string> note;
};
DecodedReport decodeRgba(const std::vector<uint8_t>& rgba, int width, int height,
                         const std::vector<uint8_t>& machine);

// Decode an image file to the asset the assembler places. PNG, JPG, BMP and
// the first frame of a GIF. Nothing on a file that is missing or not an
// image. note, when given, receives the conversion note or stays untouched.
std::optional<ImageAsset> loadImageFile(const std::filesystem::path& path, std::string* note);
// The same from the file's bytes, for an image a ROM carries.
std::optional<ImageAsset> loadImageBytes(const std::vector<uint8_t>& bytes, std::string* note);

// Decode an audio file to unsigned 8 bit mono at AUDIO_RATE, capped at
// MAX_SAMPLE_BYTES. WAV, MP3 and FLAC. A MIDI file is refused, because the
// browser version never offered one as a sample.
std::optional<std::vector<uint8_t>> loadSampleFile(const std::filesystem::path& path, std::string* note);
std::optional<std::vector<uint8_t>> loadSampleBytes(const std::vector<uint8_t>& bytes, std::string* note);

// Read a .font and give the blob CMD_LOAD_FONT reads. Nothing on a file
// that is missing or does not read, and then note, when given, receives
// the reason with its line, as "line 3: ...".
std::optional<std::vector<uint8_t>> loadFontFile(const std::filesystem::path& path, std::string* note);
std::optional<std::vector<uint8_t>> loadFontBytes(const std::vector<uint8_t>& bytes, std::string* note);

}  // namespace sc8
