#include <doctest.h>

#include <string>
#include <vector>

#include <algorithm>

#include "cc/headers.h"
#include "devices/gpu.h"
#include "cc/libs.h"
#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

// The friendly libraries: a header each, a unit compiled in behind it when
// the header is included, and nothing emitted that the program does not
// reach.

TEST_SUITE("the friendly libraries") {
  TEST_CASE("every library header is served with the generated ones") {
    for (const cc::Library& l : cc::libraries()) CHECK(cc::headers().count(l.header) == 1);
  }

  TEST_CASE("graphics.h compiles a program that draws with every shape") {
    const std::string src = R"(
#include <graphics.h>
int main(void) {
    cls(); setcolor(RED); plot(1, 2); line(0, 0, 10, 10);
    rect(1, 1, 4, 4); fillrect(1, 1, 4, 4);
    circle(50, 50, 5); fillcircle(50, 50, 5);
    ellipse(50, 50, 9, 4); fillellipse(50, 50, 9, 4);
    at(1, 1); print("HI"); printf("%d", random(10));
    return pixel(1, 2);
})";
    const std::string text = compile(src);
    CHECK(has(text, "fillellipse:"));
    CHECK(has(text, "OUT GPU_RADIUS_Y"));
    // The stub GPU keeps every command byte: both ellipse commands ran.
    const Ran r = run(src, 400000);
    const std::vector<uint8_t>& cmds = r.gpu->commands;
    CHECK(std::count(cmds.begin(), cmds.end(), gpu::CMD_RING) == 2);
    CHECK(std::count(cmds.begin(), cmds.end(), gpu::CMD_CIRCLE) == 2);
  }

  TEST_CASE("a library function nothing reaches is dropped") {
    const std::string text = compile("#include <graphics.h>\nint main(void) { cls(); return 0; }");
    CHECK(has(text, "cls:"));
    CHECK_FALSE(has(text, "fillellipse:"));
    CHECK_FALSE(has(text, "random:"));
  }

  TEST_CASE("a program that leaves a header out may use its names for itself") {
    const std::string text = compile("int line; int main(void) { line = 3; return line; }");
    CHECK(has(text, "main:"));
  }

  TEST_CASE("sound.h carries its square wave into the cartridge and defines it at init") {
    const std::string text = compile("#include <sound.h>\nint main(void) { sound_init(); beep(C5, 3); return 0; }");
    CHECK(has(text, "beepwave:"));
    CHECK(has(text, "OUT APU_CMD"));
    CHECK(has(text, "db 224, 224"));
  }

  TEST_CASE("keys.h reads the pad as a level and the keyboard as a queue") {
    const std::string text = compile("#include <keys.h>\nint main(void) { if (left()) return key(); return waitkey(); }");
    CHECK(has(text, "IN IO_CONTROLLER"));
    CHECK(has(text, "IN IO_KEY"));
  }

  TEST_CASE("math.h runs on the coprocessor and answers") {
    const Ran r = run(R"(
#include <math.h>
double a; double b; double c; int m;
int main(void) {
    a = sqrt(16.0);
    b = pow(2.0, 10.0);
    c = fabs(-2.5);
    m = max(3, 9) + min(3, 9) + abs(-4);
    return 0;
})");
    CHECK_EQ(r.f64("a"), 4.0);
    CHECK_EQ(r.f64("b"), 1024.0);
    CHECK_EQ(r.f64("c"), 2.5);
    CHECK_EQ(r.i16("m"), 16);
  }

  TEST_CASE("a double is read from an array and through a pointer") {
    const Ran r = run(R"(
double arr[3]; double *p; double out; double out2;
int main(void) {
    arr[1] = 2.5;
    p = arr + 1;
    out = arr[1] * 2.0;
    out2 = *p + 1.0;
    return 0;
})");
    CHECK_EQ(r.f64("out"), 5.0);
    CHECK_EQ(r.f64("out2"), 3.5);
  }

  TEST_CASE("disk.h wraps the four storage commands") {
    const std::string text = compile(R"(
#include <disk.h>
char buf[40];
int main(void) { save("A", "B"); load("A", buf, 40); erase("A"); catalog(buf, 40); return files(); })");
    CHECK(countOf(text, "OUT STO_CMD") >= 4);
    CHECK(has(text, "IN STO_STATUS"));
    CHECK(has(text, "IN STO_COUNT"));
  }

  // basicvars.h reaches BASIC's variables through the pointer at $0C of
  // the system page. A plain program has no BASIC, so the test plays the
  // interpreter: it points $0C at an array of its own and reads it back.
  TEST_CASE("basicvars.h reads and writes a variable through the pointer at 12") {
    // BASIC's system page is reserved the way the interpreter builds it,
    // or the pointer would sit in the compiler's own temporaries.
    CcOptions opts;
    opts.zpReserve = 32;
    const std::string text = compile(R"(
#include <basicvars.h>
int table[286];
int seen;
int seen_b;
int main(void) {
    *(unsigned char *)12 = ((unsigned int)table) >> 8;
    *(unsigned char *)13 = ((unsigned int)table) & 255;
    table[0] = 0x0304;
    seen = basic_get('A');
    basic_set('b', -2);
    seen_b = table[11];
    return 0;
})",
                               opts);
    const Ran r = runAsm(text, 400000, false);
    CHECK(r.halted());
    // BASIC keeps a word high byte first, so the C int reads swapped.
    CHECK_EQ(r.u16("seen"), 0x0304);
    CHECK_EQ(r.i16("seen_b"), -2);
  }

  TEST_CASE("a guest file's public functions survive the reachability pass") {
    CcOptions opts;
    opts.keepAllFrom = {"guest.c"};
    const std::string text = compileFiles(
        {{"host.c", "int main(void) { return 0; }"},
         {"guest.c", "int called_by_nobody(void) { return 1; }\nstatic int mine(void) { return 2; }"}},
        opts);
    CHECK(has(text, "called_by_nobody:"));
    CHECK_FALSE(has(text, "mine:"));
    CHECK_FALSE(has(text, "guest_c__mine:"));
  }

  TEST_CASE("a main in a guest file is refused") {
    CcOptions opts;
    opts.keepAllFrom = {"guest.c"};
    std::string msg;
    try {
      compileFiles({{"host.c", "int main(void) { return 0; }"}, {"guest.c", "int main(void) { return 1; }"}}, opts);
    } catch (const CcError& e) {
      msg = e.file + ":" + std::to_string(e.line) + ": " + e.message();
    }
    CHECK(has(msg, "guest.c:1: main belongs to the interpreter"));
  }

  TEST_CASE("the ROM_ names are in scope without ROM.h") {
    const std::string text = compile(
        "__ROM const unsigned char pic[] = { 1, 2, 3 };\nint main(void) { return ROM_pic_LO + ROM_pic_SIZE; }");
    CHECK(has(text, "main:"));
  }
}

TEST_SUITE("the shift by eight") {
  TEST_CASE("an unsigned shift by eight is a byte move, not a coprocessor call") {
    const std::string text = compile("unsigned int v; unsigned char h; int main(void) { h = v >> 8; return h; }");
    CHECK_FALSE(has(text, "__ushr16"));
  }
  TEST_CASE("and it answers the high byte") {
    const Ran r = run("unsigned int v; unsigned char h; int main(void) { v = 0x1234; h = v >> 8; return 0; }");
    CHECK_EQ(r.u8("h"), 0x12);
  }
  TEST_CASE("a signed shift by a constant keeps the sign with ASR") {
    const std::string text = compile("int v; int h; int main(void) { h = v >> 8; return h; }");
    CHECK_FALSE(has(text, "__sshr16"));
    CHECK(has(text, "ASR A"));
    const Ran r = run("int v; int h; int g; int main(void) { v = -4660; h = v >> 8; g = v >> 3; return 0; }");
    CHECK_EQ(r.i16("h"), -4660 >> 8);
    CHECK_EQ(r.i16("g"), -4660 >> 3);
  }

  TEST_CASE("a shift by a variable still goes to the coprocessor") {
    const std::string text = compile("int v; int n; int h; int main(void) { h = v >> n; return h; }");
    CHECK(has(text, "__sshr16"));
  }
}
