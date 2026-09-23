#include "core/cartridge.h"

#include <cstring>
#include <string_view>

#include "core/mcparse.h"
#include "core/microcode.h"

namespace sc8 {

namespace {

constexpr char MAGIC[8] = {'S', 'C', '8', 'R', 'O', 'M', 0, 0};
constexpr uint32_t VERSION = 1;

void putU32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; i++) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

uint32_t getU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void chunk(std::vector<uint8_t>& out, const char tag[4], const std::vector<uint8_t>& payload) {
  out.insert(out.end(), tag, tag + 4);
  putU32(out, static_cast<uint32_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> bytesOf(const std::string& s) { return {s.begin(), s.end()}; }

std::vector<std::string_view> lines(std::string_view text) {
  std::vector<std::string_view> out;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t nl = text.find('\n', pos);
    if (nl == std::string_view::npos) nl = text.size();
    if (nl > pos) out.push_back(text.substr(pos, nl - pos));
    pos = nl + 1;
  }
  return out;
}

}  // namespace

std::vector<uint8_t> encodeCartridge(const Cartridge& c) {
  std::vector<uint8_t> out;
  out.insert(out.end(), MAGIC, MAGIC + 8);
  putU32(out, VERSION);

  std::vector<uint8_t> prog;
  prog.reserve(c.program.size() * 3);
  for (const Instr& i : c.program) {
    prog.push_back(i.op);
    prog.push_back(static_cast<uint8_t>(i.operand >> 8));
    prog.push_back(static_cast<uint8_t>(i.operand & 0xff));
  }
  chunk(out, "PROG", prog);
  if (!c.ram.empty()) chunk(out, "RAM ", c.ram);
  if (!c.data.empty()) chunk(out, "DATA", c.data);
  if (!c.assets.empty()) {
    std::string table;
    for (const RomAsset& a : c.assets) {
      table += a.kind + "\t" + a.name + "\t" + std::to_string(a.offset) + "\t" + std::to_string(a.size) + "\n";
    }
    chunk(out, "ASET", bytesOf(table));
  }
  // The seal: the optimal set lives in the computer and never in a file.
  // Text that is the optimal set, however it got here, is written as the
  // reference instead.
  std::string ucod = c.microcode.empty() ? "@naive" : c.microcode;
  if (ucod[0] != '@') {
    McParsed parsed = parseMicrocode(ucod);
    if (parsed.errors.empty() && parsed.microcode.sections() == buildOptimal().sections()) {
      ucod = "@optimal";
    }
  }
  chunk(out, "UCOD", bytesOf(ucod));
  if (!c.meta.empty()) {
    std::string text;
    for (const auto& [k, v] : c.meta) text += k + "=" + v + "\n";
    chunk(out, "META", bytesOf(text));
  }
  if (!c.basic.empty()) {
    std::string text;
    for (size_t i = 0; i < c.basic.size(); i++) {
      if (i) text += '\f';
      text += c.basic[i].first + "\n" + c.basic[i].second;
    }
    chunk(out, "BAS ", bytesOf(text));
  }
  return out;
}

CartridgeResult decodeCartridge(const std::vector<uint8_t>& bytes) {
  if (bytes.size() < 12 || std::memcmp(bytes.data(), MAGIC, 8) != 0) {
    return {std::nullopt, "not a SimpleCPU-8 ROM"};
  }
  const uint32_t version = getU32(bytes.data() + 8);
  if (version != VERSION) {
    return {std::nullopt, "ROM format version " + std::to_string(version) + " is not the " +
                              std::to_string(VERSION) + " this build reads"};
  }
  Cartridge c;
  bool sawProg = false;
  size_t pos = 12;
  while (pos < bytes.size()) {
    if (pos + 8 > bytes.size()) return {std::nullopt, "ROM is truncated inside a chunk header"};
    const std::string_view tag(reinterpret_cast<const char*>(bytes.data() + pos), 4);
    const uint32_t len = getU32(bytes.data() + pos + 4);
    pos += 8;
    if (pos + len > bytes.size()) return {std::nullopt, "ROM is truncated inside chunk " + std::string(tag)};
    const uint8_t* p = bytes.data() + pos;
    const std::string_view text(reinterpret_cast<const char*>(p), len);
    if (tag == "PROG") {
      if (len % 3 != 0) return {std::nullopt, "PROG chunk is not a whole number of instructions"};
      if (len / 3 > 65536) return {std::nullopt, "program is longer than 65536 instructions"};
      c.program.clear();
      for (uint32_t i = 0; i < len; i += 3) {
        c.program.push_back({p[i], static_cast<uint16_t>((p[i + 1] << 8) | p[i + 2])});
      }
      sawProg = true;
    } else if (tag == "RAM ") {
      if (len > 65536) return {std::nullopt, "RAM image is longer than 65536 bytes"};
      c.ram.assign(p, p + len);
    } else if (tag == "DATA") {
      if (len > static_cast<uint32_t>(CART_DATA_SIZE)) return {std::nullopt, "DATA chunk is larger than 1MB"};
      c.data.assign(p, p + len);
    } else if (tag == "ASET") {
      for (std::string_view line : lines(text)) {
        RomAsset a;
        size_t t1 = line.find('\t');
        size_t t2 = line.find('\t', t1 + 1);
        size_t t3 = line.find('\t', t2 + 1);
        if (t1 == std::string_view::npos || t2 == std::string_view::npos || t3 == std::string_view::npos) {
          return {std::nullopt, "ASET chunk has a malformed line"};
        }
        a.kind = std::string(line.substr(0, t1));
        a.name = std::string(line.substr(t1 + 1, t2 - t1 - 1));
        a.offset = static_cast<uint32_t>(std::stoul(std::string(line.substr(t2 + 1, t3 - t2 - 1))));
        a.size = static_cast<uint32_t>(std::stoul(std::string(line.substr(t3 + 1))));
        c.assets.push_back(std::move(a));
      }
    } else if (tag == "UCOD") {
      c.microcode = std::string(text);
    } else if (tag == "BAS ") {
      size_t start = 0;
      while (start <= text.size()) {
        size_t ff = text.find('\f', start);
        if (ff == std::string_view::npos) ff = text.size();
        std::string_view entry = text.substr(start, ff - start);
        size_t nl = entry.find('\n');
        if (nl != std::string_view::npos) {
          c.basic.emplace_back(std::string(entry.substr(0, nl)), std::string(entry.substr(nl + 1)));
        }
        start = ff + 1;
      }
    } else if (tag == "META") {
      for (std::string_view line : lines(text)) {
        size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        c.meta.emplace_back(std::string(line.substr(0, eq)), std::string(line.substr(eq + 1)));
      }
    }
    // An unknown chunk is skipped, so a newer tool's ROM still boots here.
    pos += len;
  }
  if (!sawProg) return {std::nullopt, "ROM has no PROG chunk"};
  return {std::move(c), ""};
}

}  // namespace sc8
