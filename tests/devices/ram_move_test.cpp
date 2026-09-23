#include <doctest.h>

#include <vector>

#include "devices/gpu.h"
#include "gpu_rig.h"

using namespace sc8;
using namespace sc8::gpu;

// CMD_RAM_MOVE. See docs/design/c-compiler-design.md.
//
// There was no way to move a block of RAM on this machine. CMD_COPY brings the
// cartridge in and the ACP writes only its own block, so a copy was a byte at a
// time through the CPU. That is memcpy, every struct assignment, and a
// collector compacting a string heap.
//
// It is a MOVE and not a copy: the regions may overlap, and a compacting
// collector is exactly the case where they do.

namespace {

using Bytes = std::vector<int>;

struct Rig {
  gpu_rig::Clock clock;
  std::unique_ptr<gpu_rig::Bytes> ram = gpu_rig::newRam();
  Gpu& gpu = clock.g;
  Rig() {
    gpu.attachRam(ram->data());
    gpu.powerOn();
  }
  void move(int from, int to, int len) {
    gpu.write(GPU_DATA1, static_cast<uint8_t>((from >> 8) & 0xff));  // GPU_FROM_HI
    gpu.write(GPU_DATA2, static_cast<uint8_t>(from & 0xff));         // GPU_FROM_LO
    gpu.write(GPU_DATA3, static_cast<uint8_t>((to >> 8) & 0xff));    // GPU_DEST_HI
    gpu.write(GPU_DATA4, static_cast<uint8_t>(to & 0xff));           // GPU_DEST_LO
    gpu.write(GPU_DATA5, static_cast<uint8_t>((len >> 8) & 0xff));   // GPU_LEN_HI
    gpu.write(GPU_DATA6, static_cast<uint8_t>(len & 0xff));          // GPU_LEN_LO
    gpu.write(GPU_CMD, CMD_RAM_MOVE);
  }
  void put(int at, const Bytes& bytes) {
    for (size_t i = 0; i < bytes.size(); i++) (*ram)[static_cast<size_t>(at) + i] = static_cast<uint8_t>(bytes[i]);
  }
  Bytes get(int at, int n) const {
    Bytes out;
    for (int i = 0; i < n; i++) out.push_back((*ram)[static_cast<size_t>(at + i)]);
    return out;
  }
  int byte(int at) const { return (*ram)[static_cast<size_t>(at)]; }
};

}  // namespace

TEST_SUITE("CMD_RAM_MOVE") {
  TEST_CASE("moves a block that does not overlap") {
    Rig r;
    r.put(0x1000, {1, 2, 3, 4, 5});
    r.move(0x1000, 0x2000, 5);
    CHECK(r.get(0x2000, 5) == Bytes{1, 2, 3, 4, 5});
  }

  TEST_CASE("leaves the source where it was, because it is a move and not a cut") {
    Rig r;
    r.put(0x1000, {1, 2, 3, 4, 5});
    r.move(0x1000, 0x2000, 5);
    CHECK(r.get(0x1000, 5) == Bytes{1, 2, 3, 4, 5});
  }

  TEST_CASE("writes nothing outside the block") {
    Rig r;
    r.put(0x1000, {1, 2, 3});
    r.put(0x1fff, {0xaa});
    r.put(0x2003, {0xbb});
    r.move(0x1000, 0x2000, 3);
    CHECK_EQ(r.byte(0x1fff), 0xaa);
    CHECK_EQ(r.byte(0x2003), 0xbb);
  }

  // The case the command exists for. A collector compacts a heap downward
  // over itself, so the regions overlap and a naive forward loop would smear
  // the first byte through the whole block.
  TEST_CASE("moves down over itself without smearing") {
    Rig r;
    r.put(0x1000, {1, 2, 3, 4, 5, 6});
    r.move(0x1002, 0x1000, 4);  // pull [3,4,5,6] back by two
    CHECK(r.get(0x1000, 6) == Bytes{3, 4, 5, 6, 5, 6});
  }

  // And the other direction, which a forward loop gets wrong instead.
  TEST_CASE("moves up over itself without smearing") {
    Rig r;
    r.put(0x1000, {1, 2, 3, 4, 5, 6});
    r.move(0x1000, 0x1002, 4);  // push [1,2,3,4] forward by two
    CHECK(r.get(0x1000, 6) == Bytes{1, 2, 1, 2, 3, 4});
  }

  TEST_CASE("does nothing at all when the source is the destination") {
    Rig r;
    r.put(0x1000, {1, 2, 3, 4});
    r.move(0x1000, 0x1000, 4);
    CHECK(r.get(0x1000, 4) == Bytes{1, 2, 3, 4});
  }

  TEST_CASE("does nothing on a length of zero") {
    Rig r;
    r.put(0x2000, {9, 9, 9});
    r.move(0x1000, 0x2000, 0);
    CHECK(r.get(0x2000, 3) == Bytes{9, 9, 9});
  }

  // No data address is illegal on this machine, so both ends wrap at 16 bits
  // the way every other effective address does.
  TEST_CASE("wraps at the top of RAM rather than stopping") {
    Rig r;
    r.put(0xfffe, {1, 2});
    r.put(0x0000, {3, 4});
    r.move(0xfffe, 0x1000, 4);
    CHECK(r.get(0x1000, 4) == Bytes{1, 2, 3, 4});
  }

  // Overlap and wrap at once, which is where a plain "is the destination
  // higher" test gets the direction backwards. Here the destination is
  // NUMERICALLY higher and yet sits below the source once the block wraps,
  // so a backward loop would read bytes it had already overwritten.
  TEST_CASE("chooses the direction correctly when the block wraps") {
    Rig r;
    r.put(0x0000, {1, 2, 3, 4});
    r.move(0x0000, 0xffff, 4);
    CHECK(Bytes{r.byte(0xffff), r.byte(0x0000), r.byte(0x0001), r.byte(0x0002)} == Bytes{1, 2, 3, 4});
  }

  TEST_CASE("moves a block bigger than a byte can count") {
    Rig r;
    for (int i = 0; i < 300; i++) (*r.ram)[static_cast<size_t>(0x1000 + i)] = static_cast<uint8_t>(i & 0xff);
    r.move(0x1000, 0x4000, 300);
    CHECK(r.get(0x4000 + 297, 3) == Bytes{297 & 0xff, 298 & 0xff, 299 & 0xff});
  }

  TEST_CASE("does nothing when no RAM is attached") {
    gpu_rig::Clock c;
    c.g.powerOn();
    c.g.write(GPU_DATA6, 4);
    c.g.write(GPU_CMD, CMD_RAM_MOVE);
    CHECK_EQ(c.g.read(GPU_DATA6), 0);  // ran, and cleared its arguments
  }

  TEST_CASE("clears its arguments after the command, like every other") {
    Rig r;
    r.put(0x1000, {1, 2, 3});
    r.move(0x1000, 0x2000, 3);
    for (int i = 1; i <= 6; i++) CHECK_EQ(r.gpu.read(static_cast<uint8_t>(GPU_DATA0 + i)), 0);
  }
}
