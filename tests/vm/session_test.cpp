// The port of packages/ui/test/session.test.ts, against Computer. The
// wire shapes, the C loader and the custom microcode editor have no
// counterpart here. Their cases are left out. Device wiring cases are
// added, because Computer owns the chain the session owned.

#include <set>
#include <string>

#include "doctest.h"

#include "asm/asm.h"
#include "core/machine.h"
#include "devices/apu_ports.h"
#include "devices/gpu_ports.h"
#include "vm/audio.h"
#include "vm/computer.h"

using namespace sc8;

namespace {

const char* DEMO = R"(
        LD D1 <- [head]
loopA:  JZ done
        LD A <- [D1]
        ADD A <- [sum]
        LD [sum] <- A
        LD D2 <- [D1+1]
loopB:  JZ done
        LD A <- [D2]
        ADD A <- [sum]
        LD [sum] <- A
        LD D1 <- [D2+1]
        JMP loopA
done:   HLT
.ram
sum:    db 0
head:   dw &node1
node1:  db 5,  dw &node2
node2:  db 3,  dw &node3
node3:  db 11, dw &node4
node4:  db 23, dw 0
)";

const char* WAIT = R"(
loop:   IN GPU_FRAME
        SUB A <- [fr]
        JZ loop
        IN GPU_FRAME
        LD [fr] <- A
        JMP loop
.ram
fr: db 0
)";

const char* RAND = R"(        IN GPU_RAND -> A
        LD [$10] <- A
        IN GPU_RAND -> A
        LD [$11] <- A
        IN GPU_RAND -> A
        LD [$12] <- A
        HLT
)";

Cartridge romOf(const std::string& src, const Assets* assets = nullptr) {
  Assembled a = assemble(src, assets);
  REQUIRE(a.errors.empty());
  return a.cartridge();
}

void load(Computer& c, const std::string& src, const std::string& mc = "naive") {
  Cartridge cart = romOf(src);
  cart.microcode = "@" + mc;
  c.insert(std::move(cart));
}

std::string randTriple(Computer& c) {
  load(c, RAND);
  c.runBudget(1000);
  const auto& ram = c.machine().ram;
  return std::to_string(ram[0x10]) + "," + std::to_string(ram[0x11]) + "," + std::to_string(ram[0x12]);
}

// The RGBA pixel at x, y of the composed frame.
std::array<uint8_t, 4> pixel(const std::vector<uint8_t>& f, int x, int y) {
  const size_t i = static_cast<size_t>((y * 256 + x) * 4);
  return {f[i], f[i + 1], f[i + 2], f[i + 3]};
}

}  // namespace

TEST_SUITE("vm session") {
  TEST_CASE("loads, runs to halt, and reports the sum") {
    Computer c;
    load(c, DEMO);
    c.runBudget(10000);
    const Machine& m = c.machine();
    CHECK_EQ(m.status, Status::Halted);
    CHECK_EQ(m.ram[0], 42);  // 5 + 3 + 11 + 23
    bool anyRead = false;
    for (uint32_t v : m.ramReadAt) anyRead = anyRead || v > 0;
    CHECK(anyRead);
  }

  TEST_CASE("reset keeps RAM, power on reloads the .ram image") {
    Computer c;
    load(c, DEMO);
    c.runBudget(10000);
    c.reset();
    CHECK_EQ(c.machine().ram[0], 42);  // survived reset
    CHECK_EQ(c.machine().pc, 0);
    c.powerOn();
    CHECK_EQ(c.machine().ram[0], 0);  // image reloaded
  }

  TEST_CASE("switching microcode restarts and agrees on the result") {
    Computer c;
    load(c, DEMO);
    c.runBudget(10000);
    const uint64_t naiveCycles = c.machine().cycles;
    CHECK(c.selectMicrocode("optimal").empty());
    CHECK_EQ(c.machine().cycles, 0);  // a fresh machine
    c.runBudget(10000);
    CHECK_EQ(c.machine().ram[0], 42);
    CHECK_LT(c.machine().cycles, naiveCycles);
    CHECK_FALSE(c.microcodeInspectable());
  }

  TEST_CASE("runBudget stops at the budget and resumes") {
    Computer c;
    load(c, DEMO);
    CHECK_EQ(c.runBudget(3), 3);
    CHECK_EQ(c.machine().status, Status::Running);
    c.runBudget(10000);
    CHECK_EQ(c.machine().status, Status::Halted);
  }

  TEST_CASE("breakpoints stop a run before the marked instruction") {
    Computer c;
    load(c, "        LD A <- 1\n        LD A <- 2\n        LD A <- 3\n        HLT\n");
    c.setBreakpoints({2});
    const uint64_t done = c.runBudget(100);
    CHECK(c.hitBreakpoint);
    CHECK_EQ(done, 2);  // executed instructions 0 and 1
    CHECK_EQ(c.machine().pc, 2);
    CHECK_EQ(c.machine().acc, 2);  // instruction 2 not yet executed
    c.setBreakpoints({});
    c.runBudget(100);
    CHECK_EQ(c.machine().status, Status::Halted);
  }

  TEST_CASE("lastMicro rides the machine") {
    Computer c;
    load(c, "        HLT\n");
    c.machine().microStep();
    REQUIRE(c.machine().lastMicro.has_value());
    CHECK_EQ(c.machine().lastMicro->op, "fetch");
  }
}

TEST_SUITE("vm frame advance") {
  TEST_CASE("runs to the next device frame and stops") {
    Computer c;
    load(c, WAIT);
    const uint64_t done = c.runToNextFrame();
    const Machine& m = c.machine();
    CHECK_GE(m.cycles, 65536);       // frame 1 reached
    CHECK_LT(m.cycles, 65536 + 40);  // and barely a step past it
    CHECK_GT(done, 5000);            // the 5461 iteration wait, skipped
    CHECK_EQ(c.frameCounter(), 1);
    const uint64_t before = m.cycles;
    c.runToNextFrame();
    CHECK_GE(m.cycles, 131072);
    CHECK_GT(m.cycles, before);
    CHECK_EQ(c.frameCounter(), 2);
  }

  TEST_CASE("runFrame reports a running machine and a halted one") {
    Computer c;
    load(c, WAIT);
    CHECK(c.runFrame());
    load(c, "        HLT\n");
    CHECK_FALSE(c.runFrame());
  }

  TEST_CASE("a breakpoint wins over the frame target") {
    Computer c;
    load(c, WAIT);
    c.setBreakpoints({1});  // the SUB, hit almost immediately
    c.runToNextFrame();
    CHECK(c.hitBreakpoint);
    CHECK_LT(c.machine().cycles, 100);
  }

  TEST_CASE("fast-frame latched: the poll loop falls straight through") {
    Computer c;
    load(c, WAIT);
    c.setFastFrame(true, 0.0);  // gap 0: force on every read, deterministic
    c.runBudget(12);
    CHECK_EQ(c.machine().instructions, 12);
    CHECK_LT(c.machine().cycles, 200);
    // The switch survives a power on: it belongs to the user.
    c.powerOn();
    c.runBudget(12);
    CHECK_EQ(c.machine().instructions, 12);
  }
}

TEST_SUITE("vm runMicroBudget for the microcode view run") {
  TEST_CASE("advances one microcode row per step, an instruction only at its last row") {
    Computer c;
    load(c, "        LD A <- 5\n        HLT\n");
    CHECK_EQ(c.runMicroBudget(1), 1);
    REQUIRE(c.machine().lastMicro.has_value());
    CHECK_EQ(c.machine().lastMicro->op, "fetch");
    CHECK_EQ(c.machine().instructions, 0);
    CHECK_EQ(c.machine().cycles, 1);
    int completedAt = -1;
    for (int i = 0; i < 6; i++) {
      c.runMicroBudget(1);
      if (c.machine().instructions == 1 && completedAt < 0) completedAt = i;
    }
    CHECK_GT(completedAt, 0);  // the LD finished after more than one row
  }

  TEST_CASE("returns zero once the program halts") {
    Computer c;
    load(c, "        HLT\n");
    int guard = 0;
    while (c.runMicroBudget(1) > 0 && guard < 50) guard++;
    CHECK_EQ(c.machine().status, Status::Halted);
    CHECK_EQ(c.runMicroBudget(4), 0);
  }

  TEST_CASE("a breakpoint stops the micro run at the instruction boundary") {
    Computer c;
    load(c, "        LD A <- 1\n        LD A <- 2\n        HLT\n");
    c.setBreakpoints({1});
    c.runMicroBudget(100);
    CHECK(c.hitBreakpoint);
    CHECK_EQ(c.machine().pc, 1);
    CHECK_EQ(c.machine().acc, 1);
  }
}

TEST_SUITE("vm gpu random is random per run") {
  TEST_CASE("gives a different sequence to each power on with the wall clock seed") {
    Computer c;
    std::set<std::string> seen;
    for (int i = 0; i < 10; i++) seen.insert(randTriple(c));
    CHECK_GT(seen.size(), 1);
  }

  TEST_CASE("repeats when a fixed seed is set, which is what a test wants") {
    Computer c;
    c.setSeed(1234);
    const std::string once = randTriple(c);
    CHECK_EQ(once, randTriple(c));
    Computer d;
    d.setSeed(1234);
    CHECK_EQ(once, randTriple(d));
    d.setSeed(99);
    CHECK_NE(once, randTriple(d));
  }

  TEST_CASE("repeats exactly when the program seeds it first") {
    Computer c;
    const std::string seeded = std::string("        OUT GPU_RAND, 77\n") + RAND;
    auto once = [&] {
      load(c, seeded);
      c.runBudget(1000);
      const auto& ram = c.machine().ram;
      return std::to_string(ram[0x10]) + "," + std::to_string(ram[0x11]) + "," + std::to_string(ram[0x12]);
    };
    CHECK_EQ(once(), once());
  }
}

TEST_SUITE("vm device chain") {
  TEST_CASE("the chain runs GPU, ACP, APU, input, and each answers its own ports") {
    Computer c;
    // The GPU answers the frame counter, the ACP adds and the APU counts
    // voices. The controller byte comes through three forwards.
    load(c,
         "        IN IO_CONTROLLER -> A\n"
         "        LD [$20] <- A\n"
         "        IN IO_KEY -> A\n"
         "        LD [$21] <- A\n"
         "        IN IO_MODS -> A\n"
         "        LD [$22] <- A\n"
         "        OUT ACP_ADDR_HI, blk >> 8\n"
         "        OUT ACP_ADDR_LO, blk & 255\n"
         "        OUT ACP_FMT, ACP_I64\n"
         "        OUT ACP_CMD, ACP_ADD\n"
         "        IN APU_VOICES -> A\n"
         "        LD [$23] <- A\n"
         "        IN GPU_FRAME -> A\n"
         "        LD [$24] <- A\n"
         "        HLT\n"
         ".ram\n"
         "        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
         "        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
         "        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
         "        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
         "blk:    db 0,0,0,0,0,0,0,7\n"
         "        db 0,0,0,0,0,0,0,6\n"
         "        db 0,0,0,0,0,0,0,0\n");
    c.input().buttons = 0x21;
    c.input().mods = 0x03;
    c.input().pushKey(65, false);
    c.runBudget(100);
    const Machine& m = c.machine();
    CHECK_EQ(m.status, Status::Halted);
    CHECK_EQ(m.ram[0x20], 0x21);
    CHECK_EQ(m.ram[0x21], 65);
    CHECK_EQ(m.ram[0x22], 0x03);
    CHECK_EQ(m.ram[0x23], 0);
    CHECK_EQ(m.ram[0x24], 0);
    CHECK_EQ(m.ram[0x40 + 23], 13);  // the ACP wrote 7 + 6 into the machine's RAM
  }

  TEST_CASE("every new machine hands its RAM to the GPU and the ACP") {
    Computer c;
    load(c, "        HLT\n");
    const uint8_t* ram = c.machine().ram.data();
    CHECK_EQ(c.gpu().ram(), ram);
    c.powerOn();
    CHECK_NE(c.machine().ram.data(), ram);
    CHECK_EQ(c.gpu().ram(), c.machine().ram.data());
    c.selectMicrocode("optimal");
    CHECK_EQ(c.gpu().ram(), c.machine().ram.data());
    // The ACP writes through its pointer: a stale one would miss this RAM.
    load(c,
         "        OUT ACP_ADDR_HI, 0\n"
         "        OUT ACP_ADDR_LO, 0\n"
         "        OUT ACP_FMT, ACP_I64\n"
         "        OUT ACP_CMD, ACP_ADD\n"
         "        HLT\n"
         ".ram\n"
         "        db 0,0,0,0,0,0,0,2\n"
         "        db 0,0,0,0,0,0,0,3\n");
    c.selectMicrocode("naive");
    c.runBudget(100);
    CHECK_EQ(c.machine().ram[23], 5);
  }

  TEST_CASE("the cartridge data reaches the GPU and the APU") {
    Computer c;
    Assets assets;
    assets.samples["boom.wav"] = {130, 131, 132, 133};
    Assembled a = assemble(
        "        OUT APU_CART_BANK, get_bankbyte(boom)\n"
        "        OUT APU_CART_HI, get_highbyte(boom)\n"
        "        OUT APU_CART_LO, get_lowbyte(boom)\n"
        "        OUT APU_ARG,  get_sizelo(boom)\n"
        "        OUT APU_ARG2, get_sizehi(boom)\n"
        "        OUT APU_NOTE, 60\n"
        "        OUT APU_SLOT, 0\n"
        "        OUT APU_CMD,  CMD_DEF_SAMPLE\n"
        "        OUT APU_SLOT, 0\n"
        "        OUT APU_ARG,  1\n"
        "        OUT APU_CMD,  CMD_LOOP_SLOT\n"
        "        OUT APU_TRACK, 0\n"
        "        OUT APU_NOTE, 60\n"
        "        OUT APU_ARG,  127\n"
        "        OUT APU_CMD,  CMD_TRIGGER\n"
        "        HLT\n"
        ".data\n"
        "boom:   .sample('boom.wav')\n",
        &assets);
    REQUIRE(a.errors.empty());
    c.insert(a.cartridge());
    CHECK_EQ(c.gpu().cart().size(), 4);
    CHECK_FALSE(c.apu().audioActive());
    c.runBudget(100);
    CHECK_EQ(c.machine().status, Status::Halted);
    CHECK(c.apu().audioActive());
    CHECK_EQ(c.apu().masterGain(), MASTER_GAIN);

    // The pump fills the output up to the high mark, then rests. The Audio
    // has no device open. The ring alone is measured.
    Audio audio;
    c.pumpAudio(audio);
    const size_t high = static_cast<size_t>(apu::AUDIO_RATE * AUDIO_AHEAD_HIGH_MS / 1000);
    CHECK_GE(audio.queued(), high);
    const size_t queued = audio.queued();
    c.pumpAudio(audio);
    CHECK_EQ(audio.queued(), queued);
    CHECK(c.apu().audioActive());  // a looping slot holds the voice

    c.powerOn();
    CHECK_FALSE(c.apu().audioActive());  // the chip powered on fresh
  }
}

TEST_SUITE("vm frame") {
  TEST_CASE("a program that plots a pixel shows it in frame()") {
    Computer c;
    load(c,
         "        OUT GPU_X, 10\n"
         "        OUT GPU_Y, 20\n"
         "        OUT GPU_PIXEL, $FF\n"
         "        OUT GPU_CMD, CMD_PLOT\n"
         "        HLT\n");
    const std::vector<uint8_t>& before = c.frame();
    CHECK_EQ(before.size(), 256u * 256u * 4u);
    CHECK_EQ(pixel(before, 10, 20), std::array<uint8_t, 4>{0, 0, 0, 255});
    c.runBudget(100);
    const std::vector<uint8_t>& after = c.frame();
    CHECK_EQ(pixel(after, 10, 20), std::array<uint8_t, 4>{255, 255, 255, 255});
    CHECK_EQ(pixel(after, 11, 20), std::array<uint8_t, 4>{0, 0, 0, 255});
  }

  TEST_CASE("the frame is composed again only when the display key moves") {
    Computer c;
    load(c,
         "        OUT GPU_X, 1\n"
         "        OUT GPU_Y, 1\n"
         "        OUT GPU_PIXEL, $E0\n"
         "        OUT GPU_CMD, CMD_PLOT\n"
         "        HLT\n");
    c.runBudget(100);
    std::vector<uint8_t> shown = c.frame();
    CHECK_EQ(pixel(shown, 1, 1)[0], 255);
    // Scribble on the returned buffer. A still screen hands it back as is,
    // proof no compose ran.
    const_cast<std::vector<uint8_t>&>(c.frame())[0] = 7;
    CHECK_EQ(c.frame()[0], 7);
    c.gpu().write(gpu::GPU_CMD, gpu::CMD_CLEAR);
    CHECK_EQ(c.frame()[0], 0);
    CHECK_EQ(pixel(c.frame(), 1, 1)[0], 0);
  }

  TEST_CASE("power on shows a fresh screen") {
    Computer c;
    load(c,
         "        OUT GPU_X, 3\n"
         "        OUT GPU_Y, 3\n"
         "        OUT GPU_PIXEL, $FF\n"
         "        OUT GPU_CMD, CMD_PLOT\n"
         "        HLT\n");
    c.runBudget(100);
    CHECK_EQ(pixel(c.frame(), 3, 3)[0], 255);
    c.powerOn();
    CHECK_EQ(pixel(c.frame(), 3, 3)[0], 0);
  }
}
