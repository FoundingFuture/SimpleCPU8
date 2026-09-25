#include <doctest.h>

#include <string>

#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

// Pointers, arrays and the multiply the ACP does. The design's table says a
// 16 bit multiply is about 300 instructions of carry chain on the CPU and
// about thirty through the coprocessor, so the compiler sends it there.

TEST_SUITE("arrays") {
  TEST_CASE("reads a global array by index") {
    CHECK(ran(R"(
      unsigned char t[4] = { 10, 20, 30, 40 };
      unsigned char r;
      int main(void) { r = t[2]; return 0; }
    )").u8("r") == 30);
  }

  TEST_CASE("writes one, which needs an address rather than an indexed store") {
    CHECK(ran(R"(
      unsigned char t[4];
      unsigned char r;
      int main(void) { t[1] = 55; r = t[1]; return 0; }
    )").u8("r") == 55);
  }

  TEST_CASE("indexes by a variable") {
    CHECK(ran(R"(
      unsigned char t[5] = { 1, 2, 3, 4, 5 };
      unsigned char r; int i;
      int main(void) { i = 3; r = t[i]; return 0; }
    )").u8("r") == 4);
  }

  TEST_CASE("scales a word array's index by two") {
    CHECK(ran(R"(
      unsigned int t[3] = { 100, 2000, 30000 };
      unsigned int r;
      int main(void) { r = t[2]; return 0; }
    )").u16("r") == 30000);
  }

  TEST_CASE("sums an array in a loop, which is the shape most programs use") {
    CHECK(ran(R"(
      unsigned char t[6] = { 1, 2, 3, 4, 5, 6 };
      unsigned int r;
      int main(void) { int i; r = 0; for (i = 0; i < 6; i++) r = r + t[i]; return 0; }
    )").u16("r") == 21);
  }

  TEST_CASE("holds a local array in the frame") {
    CHECK(ran(R"(
      unsigned char r;
      int main(void) { unsigned char b[3]; b[0] = 7; b[1] = 8; b[2] = 9; r = b[1]; return 0; }
    )").u8("r") == 8);
  }
}

TEST_SUITE("pointers") {
  TEST_CASE("takes an address and reads through it") {
    CHECK(ran(R"(
      unsigned char v = 42, r;
      int main(void) { unsigned char *p; p = &v; r = *p; return 0; }
    )").u8("r") == 42);
  }

  TEST_CASE("writes through one") {
    CHECK(ran(R"(
      unsigned char v, r;
      int main(void) { unsigned char *p; p = &v; *p = 77; r = v; return 0; }
    )").u8("r") == 77);
  }

  TEST_CASE("steps by the size of what it points at") {
    CHECK(ran(R"(
      unsigned int t[3] = { 11, 22, 33 };
      unsigned int r;
      int main(void) { unsigned int *p; p = t; p = p + 2; r = *p; return 0; }
    )").u16("r") == 33);
  }

  TEST_CASE("walks a string to its terminator") {
    CHECK(ran(R"(
      char msg[] = "hello";
      unsigned char r;
      int main(void) { char *p; r = 0; p = msg; while (*p) { r = r + 1; p = p + 1; } return 0; }
    )").u8("r") == 5);
  }

  TEST_CASE("takes the address of a local, which is D1 plus its offset") {
    CHECK(ran(R"(
      unsigned char r;
      int main(void) { unsigned char v; unsigned char *p; v = 3; p = &v; *p = 9; r = v; return 0; }
    )").u8("r") == 9);
  }

  TEST_CASE("passes a pointer to a function that writes through it") {
    CHECK(ran(R"(
      unsigned char r;
      void set(unsigned char *p, unsigned char v) { *p = v; }
      int main(void) { set(&r, 66); return 0; }
    )").u8("r") == 66);
  }
}

TEST_SUITE("multiply, divide and shift, which the coprocessor does") {
  auto v = [](const std::string& e) {
    return ran("unsigned int r; int main(void) { r = " + e + "; return 0; }").u16("r");
  };
  auto s = [](const std::string& e) {
    return ran("int r; int main(void) { r = " + e + "; return 0; }").i16("r");
  };

  TEST_CASE("multiplies") {
    CHECK(v("6 * 7") == 42);
    CHECK(v("300 * 4") == 1200);
    CHECK(v("1000 * 60") == 60000);
  }

  TEST_CASE("keeps only the low sixteen bits of a product, like C") {
    CHECK(v("1000 * 1000") == (1000000 & 0xffff));
  }

  TEST_CASE("multiplies negatives correctly, because the low word does not care") {
    CHECK(s("-3 * 5") == -15);
  }

  TEST_CASE("divides unsigned") {
    CHECK(v("100 / 7") == 14);
    CHECK(v("60000 / 3") == 20000);
  }

  TEST_CASE("divides signed, truncating toward zero the way C does") {
    CHECK(s("-7 / 2") == -3);
    CHECK(s("7 / -2") == -3);
  }

  TEST_CASE("takes a remainder, with the sign of the dividend") {
    CHECK(s("-7 % 2") == -1);
    CHECK(s("7 % 2") == 1);
  }

  TEST_CASE("shifts left, which is adds for a small constant") {
    CHECK(v("1 << 3") == 8);
    CHECK(v("300 << 2") == 1200);
  }

  TEST_CASE("shifts left by a variable, which goes to the coprocessor") {
    CHECK(ran(R"(
      unsigned int r; unsigned char n;
      int main(void) { n = 5; r = 3 << n; return 0; }
    )").u16("r") == 96);
  }

  TEST_CASE("shifts right, which the CPU cannot do at all") {
    CHECK(v("1000 >> 3") == 125);
    CHECK(v("0x8000 >> 15") == 1);
  }

  TEST_CASE("shifts a signed value right arithmetically, so it floors") { CHECK(s("-8 >> 1") == -4); }

  TEST_CASE("works on variables and inside a loop") {
    CHECK(ran(R"(
      unsigned int r;
      int main(void) { int i; r = 1; for (i = 0; i < 5; i++) r = r * 3; return 0; }
    )").u16("r") == 243);
  }
}

TEST_SUITE("the logical operators short circuit") {
  TEST_CASE("does not evaluate the right side of && when the left is false") {
    CHECK(ran(R"(
      unsigned char hit, r;
      int bump(void) { hit = 1; return 1; }
      int main(void) { hit = 0; r = (0 && bump()) ? 1 : 0; return 0; }
    )").u8("hit") == 0);
  }

  TEST_CASE("does not evaluate the right side of || when the left is true") {
    CHECK(ran(R"(
      unsigned char hit, r;
      int bump(void) { hit = 1; return 1; }
      int main(void) { hit = 0; r = (1 || bump()) ? 1 : 0; return 0; }
    )").u8("hit") == 0);
  }

  TEST_CASE("gives 1 and 0, not the operand") {
    CHECK(ran(R"(
      unsigned char a, b, c;
      int main(void) { a = (5 && 3); b = (0 || 7); c = !5; return 0; }
    )").u8("a") == 1);
  }
}

TEST_SUITE("switch") {
  TEST_CASE("picks the matching case") {
    auto one = [](int n) {
      return ran(R"(
      unsigned char r;
      int main(void) { int x; x = )" + std::to_string(n) + R"(;
        switch (x) { case 1: r = 10; break; case 2: r = 20; break; default: r = 99; }
        return 0; }
    )").u8("r");
    };
    CHECK(one(1) == 10);
    CHECK(one(2) == 20);
    CHECK(one(5) == 99);
  }

  TEST_CASE("falls through a case with no break, like C") {
    CHECK(ran(R"(
      unsigned char r;
      int main(void) { r = 0; switch (1) { case 1: r = r + 1; case 2: r = r + 10; break; case 3: r = 100; }
        return 0; }
    )").u8("r") == 11);
  }

  TEST_CASE("runs nothing when nothing matches and there is no default") {
    CHECK(ran(R"(
      unsigned char r = 5;
      int main(void) { switch (9) { case 1: r = 1; break; } return 0; }
    )").u8("r") == 5);
  }
}

TEST_SUITE("ports") {
  TEST_CASE("writes one with out(), naming the assembler's own constant") {
    // GPU_CMD then a colour: the program clears the screen to 3.
    const Ran r = ran("int main(void) { out(GPU_COLOR, 3); out(GPU_CMD, CMD_CLEAR); return 0; }");
    CHECK(r.halted());
  }

  TEST_CASE("reads one with in()") {
    CHECK(ran("unsigned char r; int main(void) { r = in(GPU_FRAME); return 0; }").halted());
  }

  TEST_CASE("names the alternative when a port is not constant") {
    CHECK(has(refuses("int main(void) { int p; p = 5; out(p, 1); return 0; }"), "constant"));
  }
}

TEST_SUITE("inline asm") {
  TEST_CASE("goes through unchanged") {
    CHECK(ran(R"(
      unsigned char r;
      int main(void) { asm("LD A <- 33"); asm("LD [r] <- A"); return 0; }
    )").u8("r") == 33);
  }
}

TEST_SUITE("what the compiler refuses, with the alternative named") {
  TEST_CASE("names static when __zp is put on a local") {
    CHECK(has(refuses("int main(void) { __zp int x; x = 1; return 0; }"), "static"));
  }

  TEST_CASE("refuses a break outside a loop") {
    CHECK(has(refuses("int main(void) { break; }"), "break"));
  }

  TEST_CASE("refuses a frame bigger than [D3+n] can reach, and says why") {
    CHECK(has(refuses("int main(void) { unsigned char big[300]; big[0] = 1; return 0; }"), "[D3+n]"));
  }

  TEST_CASE("refuses a long, which the coprocessor could do and the compiler does not") {
    CHECK(has(refuses("long n; int main(void) { return 0; }"), "long"));
  }
}
