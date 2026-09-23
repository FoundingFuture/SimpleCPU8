// The storage device. The fifth device on the bus, on $50 to $5F, and the
// running machine's one way at the cartridge's BAS chunk. It moves program
// text between a named slot and data RAM, so BASIC can LOAD and SAVE.
//
// DESIGN: the host owns the slots. The device works on the cartridge's own
// list and reports a change. The virtual computer decides when the ROM
// file goes back to disk. The device never touches a file.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "devices/device.h"
#include "devices/storage_ports.h"

namespace sc8::storage {

// The numbers the implementation switches on. storage_ports.h publishes the
// same numbers as name tables for the assembler, and a test pins the two.
enum Port : uint8_t {
  STO_ADDR_HI = 0x50,
  STO_ADDR_LO = 0x51,
  STO_NAME_HI = 0x52,
  STO_NAME_LO = 0x53,
  STO_LEN_HI = 0x54,
  STO_LEN_LO = 0x55,
  STO_CMD = 0x56,
  STO_STATUS = 0x57,
  STO_COUNT = 0x58,
};

enum Cmd : uint8_t {
  STO_LOAD = 0x01,
  STO_SAVE = 0x02,
  STO_DELETE = 0x03,
  STO_CATALOG = 0x04,
};

enum Status : uint8_t {
  STO_OK = 0,
  STO_NOT_FOUND = 1,
  STO_FULL = 2,
  STO_BAD_NAME = 3,
  STO_BAD_CMD = 4,
};

}  // namespace sc8::storage

namespace sc8 {

using BasicSlots = std::vector<std::pair<std::string, std::string>>;

class Storage : public ChainedDevice {
 public:
  explicit Storage(IoBus* fallback = nullptr);

  // The latches go to zero and the status to STO_OK. The slots stay: they
  // are the cartridge's, and power is not what changes a ROM.
  void powerOn();

  // The cartridge's basic list, and what to call after SAVE or DELETE
  // changed it. Both may be null. A command then reports STO_NOT_FOUND or
  // STO_FULL, as if the cartridge held no slots and had no room.
  void attach(BasicSlots* slots, std::function<void()> onChange);

  // The machine's data RAM, RAM_SIZE bytes, like the ACP takes it.
  void attachRam(uint8_t* ram) { ram_ = ram; }

  void write(uint8_t port, uint8_t value) override;
  uint8_t read(uint8_t port) override;

 private:
  uint8_t* ram_ = nullptr;
  BasicSlots* slots_ = nullptr;
  std::function<void()> onChange_;

  uint8_t addrHi_ = 0;
  uint8_t addrLo_ = 0;
  uint8_t nameHi_ = 0;
  uint8_t nameLo_ = 0;
  uint16_t room_ = 0;
  uint16_t moved_ = 0;
  uint8_t status_ = storage::STO_OK;

  uint8_t byteAt(uint32_t at) const;
  void putByte(uint32_t at, uint8_t v);
  bool readName(std::string& name) const;
  BasicSlots::iterator find(const std::string& name);

  void run(uint8_t cmd);
  uint8_t load();
  uint8_t save();
  uint8_t remove();
  uint8_t catalog();
};

}  // namespace sc8
