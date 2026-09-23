#include <doctest.h>

#include <map>
#include <vector>

#include "devices/gpu.h"
#include "gpu_rig.h"

using namespace sc8;
using namespace sc8::gpu;

// Sprite groups. See docs/design/sprite-groups-design.md.
//
// A game asks about kinds of thing, not about sprite numbers, and the machine
// only answered the second question. The invaders demo decoded "was that an
// invader" from an id range with SUB and JC, on a CPU with no CMP.

namespace {

// A one pixel sprite blob: frames, width, height, then the pixels.
const std::vector<uint8_t> DOT = {1, 1, 1, 0xff};

using Args = std::map<int, int>;

struct Rig {
  gpu_rig::Clock clock;
  std::vector<uint8_t> cart = std::vector<uint8_t>(64, 0);
  Gpu& gpu = clock.g;
  Rig() {
    for (size_t i = 0; i < DOT.size(); i++) cart[i] = DOT[i];
    gpu.attachCart(cart);
    gpu.powerOn();
  }
  void w(int port, int v) { gpu.write(static_cast<uint8_t>(port), static_cast<uint8_t>(v & 0xff)); }
  int run(int cmd, const Args& args = {}) {
    for (const auto& [port, v] : args) w(port, v);
    w(GPU_CMD, cmd);
    return gpu.read(GPU_DATA0);
  }
  // A sprite is one pixel at the origin of the cartridge, in the group given.
  void def(int id, int group = -1) {
    w(GPU_DATA0, id);
    w(GPU_DATA1, 0);  // cart bank
    w(GPU_DATA2, 0);  // cart high
    w(GPU_DATA3, 0);  // cart low
    if (group >= 0) w(GPU_DATA4, group);
    w(GPU_CMD, CMD_SPRITE_DEF);
  }
  void at(int id, int x, int y) {
    w(GPU_DATA0, id);
    w(GPU_DATA1, 0);
    w(GPU_DATA2, x);
    w(GPU_DATA3, 0);
    w(GPU_DATA4, y);
    w(GPU_CMD, CMD_SPRITE_MOVE);
  }
  void show(int id) { run(CMD_SPRITE_SHOW, {{GPU_DATA0, id}}); }
  void hide(int id) { run(CMD_SPRITE_HIDE, {{GPU_DATA0, id}}); }
};

// Three sprites stacked on one pixel, in three groups, all visible.
struct Stack : Rig {
  Stack() {
    def(1, 0x01);  // group 0
    def(2, 0x02);  // group 1
    def(3, 0x04);  // group 2
    for (int id : {1, 2, 3}) {
      at(id, 10, 10);
      show(id);
    }
  }
};

}  // namespace

TEST_SUITE("a sprite's group membership") {
  TEST_CASE("defaults to no groups, so an old program is unchanged") {
    Rig r;
    r.def(1);  // no group byte written at all
    r.at(1, 10, 10);
    r.show(1);
    CHECK_EQ(r.run(CMD_SPRITE_HITS, {{GPU_DATA0, 1}}), 0);
  }

  TEST_CASE("is a mask, so one sprite can be in several groups at once") {
    Rig r;
    r.def(1, 0x01);
    r.def(2, 0x05);  // groups 0 and 2
    for (int id : {1, 2}) {
      r.at(id, 10, 10);
      r.show(id);
    }
    // Sprite 1 sees both of sprite 2's groups.
    CHECK_EQ(r.run(CMD_SPRITE_HITS, {{GPU_DATA0, 1}}), 0x05);
  }
}

TEST_SUITE("CMD_SPRITE_HITS: which groups touch this sprite") {
  TEST_CASE("reports every group overlapping it, not just the first") {
    // The limit that started this. CMD_COLLIDE_ALL answers one partner, so a
    // player touched by a bullet AND an enemy hears about one of them.
    Stack r;
    CHECK_EQ(r.run(CMD_SPRITE_HITS, {{GPU_DATA0, 1}}), 0x06);
  }

  TEST_CASE("says nothing about a sprite that touches nothing") {
    Stack r;
    r.at(1, 200, 200);
    CHECK_EQ(r.run(CMD_SPRITE_HITS, {{GPU_DATA0, 1}}), 0);
  }

  TEST_CASE("ignores a hidden sprite, the way every other collision does") {
    Stack r;
    r.hide(2);
    CHECK_EQ(r.run(CMD_SPRITE_HITS, {{GPU_DATA0, 1}}), 0x04);
  }

  TEST_CASE("ignores an ungrouped sprite, which is what a mask of 0 means") {
    Rig r;
    r.def(1, 0x01);
    r.def(2, 0);  // in no group
    for (int id : {1, 2}) {
      r.at(id, 10, 10);
      r.show(id);
    }
    CHECK_EQ(r.run(CMD_SPRITE_HITS, {{GPU_DATA0, 1}}), 0);
    // and it is still a sprite: the ungrouped test finds it
    CHECK_EQ(r.run(CMD_HIT_SCAN, {{GPU_DATA0, 1}}), 2);
  }

  TEST_CASE("never counts the sprite's own groups from itself") {
    Rig r;
    r.def(1, 0x01);
    r.at(1, 10, 10);
    r.show(1);
    CHECK_EQ(r.run(CMD_SPRITE_HITS, {{GPU_DATA0, 1}}), 0);
  }
}

TEST_SUITE("CMD_GROUP_HITS: which groups touch this group") {
  TEST_CASE("reports the groups its members overlap") {
    Stack r;
    CHECK_EQ(r.run(CMD_GROUP_HITS, {{GPU_DATA0, 0x01}}), 0x06);
  }

  // The owner's rule: a group reports against itself, and the program masks
  // its own bit off if it does not care. A hidden rule would be worse.
  TEST_CASE("reports a group against itself when two of its own overlap") {
    Rig r;
    r.def(1, 0x02);
    r.def(2, 0x02);  // both in group 1
    for (int id : {1, 2}) {
      r.at(id, 10, 10);
      r.show(id);
    }
    CHECK_EQ(r.run(CMD_GROUP_HITS, {{GPU_DATA0, 0x02}}), 0x02);
  }

  TEST_CASE("takes a mask, so several groups can be asked about at once") {
    Stack r;
    // Groups 0 and 1 together touch group 2, and each other.
    CHECK_EQ(r.run(CMD_GROUP_HITS, {{GPU_DATA0, 0x03}}), 0x07);
  }

  TEST_CASE("answers nothing for a group with no members") {
    Stack r;
    CHECK_EQ(r.run(CMD_GROUP_HITS, {{GPU_DATA0, 0x80}}), 0);
  }
}

TEST_SUITE("CMD_HIT_IN_GROUP: which sprite from that group") {
  TEST_CASE("finds the sprite, not merely the group") {
    Stack r;
    CHECK_EQ(r.run(CMD_HIT_IN_GROUP, {{GPU_DATA0, 1}, {GPU_DATA1, 0x04}}), 3);
  }

  TEST_CASE("answers zero when that group is not touching it") {
    Stack r;
    CHECK_EQ(r.run(CMD_HIT_IN_GROUP, {{GPU_DATA0, 1}, {GPU_DATA1, 0x80}}), 0);
  }

  // Without this a blast radius damages one enemy and the rest walk away.
  TEST_CASE("walks every overlapping sprite through the start argument") {
    Rig r;
    r.def(1, 0x01);  // the asker
    for (int id : {4, 5, 6}) r.def(id, 0x02);
    for (int id : {1, 4, 5, 6}) {
      r.at(id, 10, 10);
      r.show(id);
    }
    std::vector<int> found;
    int from = 0;
    for (int guard = 0; guard < 10; guard++) {
      const int hit = r.run(CMD_HIT_IN_GROUP, {{GPU_DATA0, 1}, {GPU_DATA1, 0x02}, {GPU_DATA2, from}});
      if (hit == 0) break;
      found.push_back(hit);
      from = hit + 1;
    }
    CHECK(found == std::vector<int>{4, 5, 6});
  }

  TEST_CASE("starts from zero when the start argument is not written") {
    Stack r;
    CHECK_EQ(r.run(CMD_HIT_IN_GROUP, {{GPU_DATA0, 1}, {GPU_DATA1, 0x06}}), 2);
  }
}

TEST_SUITE("CMD_COLLIDE_GROUP_ALL: every sprite's mask, in one command") {
  TEST_CASE("writes a mask per sprite to the mapped address") {
    Stack r;
    auto ram = gpu_rig::newRam();
    r.gpu.attachRam(ram->data());
    r.run(CMD_MEMMAP, {{GPU_DATA0, MAP_GROUPS}, {GPU_DATA1, 0x20}, {GPU_DATA2, 0x00}});
    r.run(CMD_COLLIDE_GROUP_ALL);
    CHECK_EQ((*ram)[0x2001], 0x06);
    CHECK_EQ((*ram)[0x2002], 0x05);
    CHECK_EQ((*ram)[0x2003], 0x03);
  }

  TEST_CASE("leaves a sprite that touches nothing at zero") {
    Stack r;
    auto ram = gpu_rig::newRam();
    r.gpu.attachRam(ram->data());
    r.run(CMD_MEMMAP, {{GPU_DATA0, MAP_GROUPS}, {GPU_DATA1, 0x20}, {GPU_DATA2, 0x00}});
    r.at(1, 200, 200);
    r.run(CMD_COLLIDE_GROUP_ALL);
    CHECK_EQ((*ram)[0x2001], 0);
    CHECK_EQ((*ram)[0x2002], 0x04);
    CHECK_EQ((*ram)[0x2003], 0x02);
    // and the whole table is written, not only the sprites in use
    CHECK_EQ((*ram)[0x2000 + SPRITE_COUNT - 1], 0);
  }
}

// The answers are cached per frame, so three group commands do not test every
// pair three times. The cache is keyed on the GPU's version counter.
TEST_SUITE("the frame cache notices what matters") {
  TEST_CASE("answers again after a sprite moves apart") {
    Stack r;
    CHECK_EQ(r.run(CMD_SPRITE_HITS, {{GPU_DATA0, 1}}), 0x06);
    r.at(2, 200, 200);
    CHECK_EQ(r.run(CMD_SPRITE_HITS, {{GPU_DATA0, 1}}), 0x04);
  }

  TEST_CASE("answers again after a sprite is hidden and shown") {
    Stack r;
    r.hide(3);
    CHECK_EQ(r.run(CMD_SPRITE_HITS, {{GPU_DATA0, 1}}), 0x02);
    r.show(3);
    CHECK_EQ(r.run(CMD_SPRITE_HITS, {{GPU_DATA0, 1}}), 0x06);
  }

  // The case the version counter alone could miss: a hidden sprite moving
  // does not bump it, because a hidden sprite collides with nothing. What
  // matters is that showing it afterwards gives the right answer.
  TEST_CASE("answers again after a hidden sprite moves and then appears") {
    Stack r;
    r.hide(2);
    r.at(2, 200, 200);
    CHECK_EQ(r.run(CMD_SPRITE_HITS, {{GPU_DATA0, 1}}), 0x04);
    r.show(2);
    CHECK_EQ(r.run(CMD_SPRITE_HITS, {{GPU_DATA0, 1}}), 0x04);
    r.at(2, 10, 10);
    CHECK_EQ(r.run(CMD_SPRITE_HITS, {{GPU_DATA0, 1}}), 0x06);
  }

  TEST_CASE("answers again after a sprite is redefined into another group") {
    Stack r;
    CHECK_EQ(r.run(CMD_GROUP_HITS, {{GPU_DATA0, 0x01}}), 0x06);
    r.def(3, 0x08);  // was group 2, now group 3
    CHECK_EQ(r.run(CMD_GROUP_HITS, {{GPU_DATA0, 0x01}}), 0x0a);
  }
}
