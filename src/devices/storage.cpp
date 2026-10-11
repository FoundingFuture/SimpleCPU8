#include "devices/storage.h"

#include <algorithm>

namespace sc8 {

using namespace storage;

namespace {

constexpr uint32_t RAM_MASK = 0xffff;

// CATALOG prints a name per line and the BAS chunk keys on names. So a name
// holds no newline, no form feed and no space, and it stays short.
bool printable(const std::string& name) {
  return std::all_of(name.begin(), name.end(), [](char c) {
    const auto u = static_cast<unsigned char>(c);
    return u >= 33 && u <= 126;
  });
}

bool validName(const std::string& name) {
  return !name.empty() && name.size() <= static_cast<size_t>(NAME_LIMIT) && printable(name);
}

// A name with a slash or a dot in it is a path to a program's file.
bool isPath(const std::string& name) { return name.find_first_of("/\\.") != std::string::npos; }

bool validPath(const std::string& name) {
  return !name.empty() && name.size() <= static_cast<size_t>(PATH_LIMIT) && printable(name);
}

// What follows a path's last slash, up to its first dot: the name the
// file has in BASIC's store. file_of in src/basic/store.c is the same rule.
std::string stemOf(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
  return file.substr(0, file.find('.'));
}

char lowerOf(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool sameName(const std::string& a, const std::string& b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return lowerOf(x) == lowerOf(y); });
}

}  // namespace

Storage::Storage(IoBus* fallback) : ChainedDevice(fallback) { powerOn(); }

void Storage::powerOn() {
  addrHi_ = 0;
  addrLo_ = 0;
  nameHi_ = 0;
  nameLo_ = 0;
  room_ = 0;
  moved_ = 0;
  status_ = STO_OK;
}

void Storage::attach(BasicSlots* slots, std::function<void()> onChange) {
  slots_ = slots;
  onChange_ = std::move(onChange);
}

void Storage::write(uint8_t port, uint8_t value) {
  if (port < PORT_LO || port > PORT_HI) {
    fallback_->write(port, value);
    return;
  }
  switch (port) {
    case STO_ADDR_HI: addrHi_ = value; break;
    case STO_ADDR_LO: addrLo_ = value; break;
    case STO_NAME_HI: nameHi_ = value; break;
    case STO_NAME_LO: nameLo_ = value; break;
    case STO_LEN_HI: room_ = static_cast<uint16_t>((value << 8) | (room_ & 0xff)); break;
    case STO_LEN_LO: room_ = static_cast<uint16_t>((room_ & 0xff00) | value); break;
    case STO_CMD: run(value); break;
    default: break;  // claimed but unused
  }
}

uint8_t Storage::read(uint8_t port) {
  if (port < PORT_LO || port > PORT_HI) return fallback_->read(port);
  switch (port) {
    case STO_STATUS: return status_;
    case STO_LEN_HI: return static_cast<uint8_t>(moved_ >> 8);
    case STO_LEN_LO: return static_cast<uint8_t>(moved_ & 0xff);
    case STO_COUNT: return static_cast<uint8_t>(slots_ ? slots_->size() : 0);
    default: return 0;
  }
}

uint8_t Storage::byteAt(uint32_t at) const { return ram_ ? ram_[at & RAM_MASK] : 0; }

void Storage::putByte(uint32_t at, uint8_t v) {
  if (ram_) ram_[at & RAM_MASK] = v;
}

// The name from RAM up to its NUL. The scan stops at NAME_LIMIT plus one.
// An unterminated name then reads as too long, not as the whole of RAM.
bool Storage::readName(std::string& name, bool path) const {
  const uint32_t at = static_cast<uint32_t>(nameHi_) << 8 | nameLo_;
  name.clear();
  for (uint32_t i = 0; i <= static_cast<uint32_t>(PATH_LIMIT); i++) {
    const uint8_t c = byteAt(at + i);
    if (c == 0) return path && isPath(name) ? validPath(name) : validName(name);
    name.push_back(static_cast<char>(c));
  }
  return false;
}

BasicSlots::iterator Storage::find(const std::string& name) {
  const auto exact = std::find_if(slots_->begin(), slots_->end(), [&](const auto& slot) { return slot.first == name; });
  if (exact != slots_->end()) return exact;
  return std::find_if(slots_->begin(), slots_->end(), [&](const auto& slot) { return sameName(slot.first, name); });
}

void Storage::run(uint8_t cmd) {
  moved_ = 0;
  switch (cmd) {
    case STO_LOAD: status_ = load(); break;
    case STO_SAVE: status_ = save(); break;
    case STO_DELETE: status_ = remove(); break;
    case STO_CATALOG: status_ = catalog(); break;
    case STO_FIND: status_ = findAsset(); break;
    default: status_ = STO_BAD_CMD; break;
  }
}

uint8_t Storage::load() {
  std::string name;
  if (!readName(name, true)) return STO_BAD_NAME;
  // The host first: a project's own file is newer than the slot a build
  // made of it. Then the slots, where a path is the slot its stem names.
  std::optional<std::string> found;
  if (host_.find) found = host_.find(name);
  if (!found && slots_) {
    const auto it = find(isPath(name) ? stemOf(name) : name);
    if (it != slots_->end()) found = it->second;
  }
  if (!found) return STO_NOT_FOUND;
  const std::string& text = *found;
  // Nothing is written on a short block: half a program is worse than none.
  if (text.size() + 1 > room_) return STO_FULL;
  const uint32_t at = static_cast<uint32_t>(addrHi_) << 8 | addrLo_;
  for (size_t i = 0; i < text.size(); i++) putByte(at + static_cast<uint32_t>(i), static_cast<uint8_t>(text[i]));
  putByte(at + static_cast<uint32_t>(text.size()), 0);
  moved_ = static_cast<uint16_t>(text.size());
  return STO_OK;
}

uint8_t Storage::save() {
  std::string name;
  if (!readName(name)) return STO_BAD_NAME;
  if (!slots_) return STO_FULL;
  const uint32_t at = static_cast<uint32_t>(addrHi_) << 8 | addrLo_;
  std::string text;
  for (uint32_t i = 0;; i++) {
    // A text without a NUL inside the cap is not a program. The cap is the
    // whole of RAM less one, so the scan cannot lap.
    if (i >= static_cast<uint32_t>(TEXT_MAX)) return STO_FULL;
    const uint8_t c = byteAt(at + i);
    if (c == 0) break;
    text.push_back(static_cast<char>(c));
  }
  const auto it = find(name);
  if (it != slots_->end()) {
    it->second = text;
  } else {
    if (slots_->size() >= static_cast<size_t>(SLOT_MAX)) return STO_FULL;
    slots_->emplace_back(name, text);
  }
  moved_ = static_cast<uint16_t>(text.size());
  if (onChange_) onChange_();
  if (host_.saved) host_.saved(name, text);
  return STO_OK;
}

uint8_t Storage::remove() {
  std::string name;
  if (!readName(name)) return STO_BAD_NAME;
  if (!slots_) return STO_NOT_FOUND;
  const auto it = find(name);
  if (it == slots_->end()) return STO_NOT_FOUND;
  slots_->erase(it);
  if (onChange_) onChange_();
  return STO_OK;
}

uint8_t Storage::catalog() {
  std::string text;
  if (slots_) {
    for (const auto& slot : *slots_) text += slot.first + "\n";
  }
  if (text.size() + 1 > room_) return STO_FULL;
  const uint32_t at = static_cast<uint32_t>(addrHi_) << 8 | addrLo_;
  for (size_t i = 0; i < text.size(); i++) putByte(at + static_cast<uint32_t>(i), static_cast<uint8_t>(text[i]));
  putByte(at + static_cast<uint32_t>(text.size()), 0);
  moved_ = static_cast<uint16_t>(text.size());
  return STO_OK;
}

// The name comes through STO_NAME like every other command's, and the
// seven byte answer goes to the block.
uint8_t Storage::findAsset() {
  std::string name;
  if (!readName(name)) return STO_BAD_NAME;
  if (!assets_) return STO_NOT_FOUND;
  const uint32_t at = static_cast<uint32_t>(addrHi_) << 8 | addrLo_;
  const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
  const auto same = [&](const std::string& a) {
    if (a.size() != name.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
      if (lower(a[i]) != lower(name[i])) return false;
    }
    return true;
  };
  for (const RomAsset& a : *assets_) {
    if (!same(a.name)) continue;
    const uint8_t answer[7] = {static_cast<uint8_t>(kindCode(a.kind)),
                               static_cast<uint8_t>(a.offset >> 16), static_cast<uint8_t>(a.offset >> 8),
                               static_cast<uint8_t>(a.offset),       static_cast<uint8_t>(a.size >> 16),
                               static_cast<uint8_t>(a.size >> 8),    static_cast<uint8_t>(a.size)};
    for (uint32_t i = 0; i < 7; i++) putByte(at + i, answer[i]);
    moved_ = 7;
    return STO_OK;
  }
  return STO_NOT_FOUND;
}

}  // namespace sc8
