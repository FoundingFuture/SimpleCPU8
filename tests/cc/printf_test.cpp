#include <doctest.h>

#include <cstring>
#include <regex>
#include <string>

#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

// gpu_printf. The template is ROM by hardware definition, so a string literal
// handed to it goes to the cartridge on its own. The arguments are marshalled
// into RAM, big-endian and sized by the format, which is what CMD_PRINTF
// reads. The widths come from the device's own formatter.
//
// What the overlay actually says cannot be read here, so these check the
// marshalling and the ports instead. The device's own tests cover the
// formatting.

TEST_SUITE("the call compiles to what the device wants") {
  TEST_CASE("puts the template on the cartridge") {
    const std::string a = compile(R"(
      #include <gpu.h>
      int main(void) { gpu_printf("hi"); return 0; }
    )");
    CHECK(has(a.substr(a.find(".data")), "__rs0: db 104, 105, 0"));
  }

  TEST_CASE("points the device at the template and at the argument block") {
    const std::string a = compile(R"(
      #include <gpu.h>
      unsigned int n;
      int main(void) { gpu_printf("n=%u", n); return 0; }
    )");
    for (const char* s : {"OUT GPU_CART_BANK", "OUT GPU_CART_HI", "OUT GPU_CART_LO",
                          "OUT GPU_TEXT_ARG_HI", "OUT GPU_TEXT_ARG_LO", "OUT GPU_CMD, CMD_PRINTF"}) {
      CHECK_MESSAGE(has(a, s), s);
    }
  }

  TEST_CASE("reserves exactly the bytes the format reads") {
    const std::string a = compile(R"(
      #include <gpu.h>
      unsigned char a; unsigned int b;
      int main(void) { gpu_printf("%hhu %u", a, b); return 0; }
    )");
    CHECK(has(a, "__pfargs: ds 3"));
  }

  TEST_CASE("reserves the widest call's block, not the sum of them") {
    const std::string a = compile(R"(
      #include <gpu.h>
      unsigned int b;
      int main(void) { gpu_printf("%u", b); gpu_printf("%u %u %u", b, b, b); return 0; }
    )");
    CHECK(has(a, "__pfargs: ds 6"));
  }

  TEST_CASE("shares one cartridge copy between two identical templates") {
    const std::string a = compile(R"(
      #include <gpu.h>
      int main(void) { gpu_printf("hi"); gpu_printf("hi"); return 0; }
    )");
    const std::regex rs("__rs[0-9]+: db");
    CHECK(std::distance(std::sregex_iterator(a.begin(), a.end(), rs), std::sregex_iterator()) == 1);
  }

  TEST_CASE("takes a __ROM template, which is what a long message wants") {
    CHECK(ran(R"(
      #include <gpu.h>
      __ROM const char msg[] = "ready";
      int main(void) { gpu_printf(msg); return 0; }
    )").halted());
  }
}

TEST_SUITE("the format is checked against the arguments") {
  TEST_CASE("counts them, and says what each length reads") {
    const std::string m = refuses(R"(
      #include <gpu.h>
      unsigned int n;
      int main(void) { gpu_printf("%u %u", n); return 0; }
    )");
    CHECK(has(m, "2 arguments"));
    CHECK(has(m, "%d reads two bytes"));
  }

  TEST_CASE("accepts a format with none") {
    CHECK(ran(R"(
      #include <gpu.h>
      int main(void) { gpu_printf("done"); return 0; }
    )").halted());
  }

  TEST_CASE("does not count a doubled percent as a conversion") {
    CHECK(ran(R"(
      #include <gpu.h>
      int main(void) { gpu_printf("100%% done"); return 0; }
    )").halted());
  }

  TEST_CASE("refuses a format the CPU would have to build, and says why") {
    CHECK(has(refuses(R"(
      #include <gpu.h>
      char buf[8];
      int main(void) { gpu_printf(buf); return 0; }
    )"), "cartridge"));
  }

  TEST_CASE("needs a format at all") {
    CHECK(has(refuses(R"(
      #include <gpu.h>
      int main(void) { gpu_printf(); return 0; }
    )"), "needs a format"));
  }
}

TEST_SUITE("it really prints") {
  // Needs the GPU's text overlay, which is not in this tree yet. The stub
  // GPU in the harness draws nothing, so there is nothing to read back.
  TEST_CASE("runs a program that prints a number over graphics" * doctest::skip()) {
    const Ran r = ran(R"(
      #include <gpu.h>
      unsigned int score;
      int main(void) {
        gpu_clear(0);
        gpu_text_style(0xFF, 0, 0);
        gpu_text_at(1, 1);
        score = 1234;
        gpu_printf("SCORE %u", score);
        return 0;
      }
    )");
    CHECK(r.halted());
  }
}

TEST_SUITE("a double goes through as its eight IEEE bytes") {
  // The device reads four bytes for %f and eight for %lf. C has one real
  // type here, so the compiler sends eight for both and writes the l into
  // the template's cartridge copy.
  TEST_CASE("widens %f to %lf on the cartridge and reserves eight bytes") {
    const std::string a = compile(R"(
      #include <gpu.h>
      double d;
      int main(void) { gpu_printf("%f", d); return 0; }
    )");
    CHECK(has(a, "__pfargs: ds 8"));
    // "%lf" is 37, 108, 102.
    CHECK(has(a.substr(a.find(".data")), "db 37, 108, 102, 0"));
    CHECK(has(a, "OUT GPU_CMD, CMD_RAM_MOVE"));
  }

  TEST_CASE("leaves %lf, a width and a precision as they were") {
    const std::string a = compile(R"(
      #include <gpu.h>
      double d;
      int main(void) { gpu_printf("%8.2lf %-6.1e", d, d); return 0; }
    )");
    CHECK(has(a, "__pfargs: ds 16"));
    // "%8.2lf %-6.1le"
    CHECK(has(a.substr(a.find(".data")), "db 37, 56, 46, 50, 108, 102, 32, 37, 45, 54, 46, 49, 108, 101, 0"));
  }

  TEST_CASE("marshals the value itself, big-endian, where the device reads it") {
    const Ran r = ran(R"(
      #include <gpu.h>
      double d;
      int main(void) { d = 1.5; gpu_printf("%f", d); return 0; }
    )");
    CHECK(r.f64("__pfargs") == 1.5);
    const size_t at = static_cast<size_t>(r.addr("__pfargs"));
    CHECK(r.m->ram[at] == 0x3f);
    CHECK(r.m->ram[at + 1] == 0xf8);
  }

  TEST_CASE("takes an expression, and an int argument under %f is widened") {
    const Ran r = ran(R"(
      #include <gpu.h>
      double d; int n;
      int main(void) { d = 10.0; n = 3; gpu_printf("%f %lf", d / 4.0, n); return 0; }
    )");
    CHECK(r.f64("__pfargs") == 2.5);
    const size_t at = static_cast<size_t>(r.addr("__pfargs")) + 8;
    uint64_t bits = 0;
    for (size_t i = 0; i < 8; i++) bits = (bits << 8) | r.m->ram[at + i];
    double v;
    std::memcpy(&v, &bits, 8);
    CHECK(v == 3.0);
  }

  TEST_CASE("puts a double after an int in the block, at the right offset") {
    const Ran r = ran(R"(
      #include <gpu.h>
      double d; unsigned int n;
      int main(void) { d = -0.25; n = 0x1234; gpu_printf("%u %f", n, d); return 0; }
    )");
    const size_t at = static_cast<size_t>(r.addr("__pfargs"));
    CHECK(r.m->ram[at] == 0x12);
    CHECK(r.m->ram[at + 1] == 0x34);
    uint64_t bits = 0;
    for (size_t i = 0; i < 8; i++) bits = (bits << 8) | r.m->ram[at + 2 + i];
    double v;
    std::memcpy(&v, &bits, 8);
    CHECK(v == -0.25);
  }
}
