#include <doctest.h>

#include <regex>
#include <set>
#include <string>
#include <vector>

#include "cc/headers.h"
#include "devices/acp_ports.h"
#include "devices/apu_ports.h"
#include "devices/gpu_ports.h"
#include "devices/storage_ports.h"
#include "harness.h"

using namespace sc8;
using namespace sc8::cctest;

// The device libraries. One wrapper per command, named after it, so the
// manual's command table maps one to one onto the header.

namespace {

std::string header(const std::string& name) { return cc::headers().at(name); }

template <size_t N>
void coversEvery(const std::string& file, const std::string& prefix, const gpu::NamedValue (&cmds)[N]) {
  const std::string text = header(file);
  std::vector<std::string> missing;
  for (const auto& nv : cmds) {
    if (!has(text, cc::wrapperName(prefix, nv.name))) missing.push_back(std::string(nv.name));
  }
  CHECK(missing.empty());
}

}  // namespace

TEST_SUITE("every command has a wrapper") {
  TEST_CASE("covers every command in gpu.h") { coversEvery("gpu.h", "gpu", gpu::CMDS); }
  TEST_CASE("covers every command in apu.h") { coversEvery("apu.h", "apu", apu::CMDS); }
  TEST_CASE("covers every command in acp.h") { coversEvery("acp.h", "acp", acp::CMDS); }
  TEST_CASE("covers every command in storage.h") { coversEvery("storage.h", "sto", storage::CMDS); }

  TEST_CASE("names no command the machine does not have") {
    std::set<std::string> real;
    for (const auto& nv : gpu::CMDS) real.insert(cc::wrapperName("gpu", nv.name));
    const std::string text = header("gpu.h");
    const std::regex def("#define (gpu_[a-z0-9_]+)");
    std::vector<std::string> extra;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), def); it != std::sregex_iterator(); ++it) {
      const std::string name = (*it)[1];
      if (!real.count(name)) extra.push_back(name);
    }
    std::sort(extra.begin(), extra.end());
    // The direct reads and the two conveniences are the only ones with no
    // command behind them, and they are named here so a typo cannot hide.
    CHECK(extra == std::vector<std::string>{"gpu_frame", "gpu_mod", "gpu_rand", "gpu_rgb"});
  }

  TEST_CASE("gives each wrapper one parameter per port the command reads") {
    const std::string text = header("gpu.h");
    for (const cc::CmdArgs& c : cc::gpuCmdArgs()) {
      // gpu_printf is variadic and belongs to the compiler, not to a macro:
      // it puts the template on the cartridge and marshals the arguments.
      if (c.cmd == "CMD_PRINTF") continue;
      const std::string name = cc::wrapperName("gpu", c.cmd);
      const std::regex sig("#define " + name + "\\(([^)]*)\\)");
      std::smatch m;
      size_t params = 0;
      if (std::regex_search(text, m, sig)) {
        const std::string inside = m[1];
        std::string cur;
        for (char ch : inside + ",") {
          if (ch == ',') {
            if (cur.find_first_not_of(" \t") != std::string::npos) params++;
            cur.clear();
          } else {
            cur += ch;
          }
        }
      }
      CHECK_MESSAGE(params == c.aliases.size(), name, " takes the wrong number");
    }
  }
}

TEST_SUITE("a wrapper costs exactly its port writes") {
  TEST_CASE("turns gpu_clear into two OUTs and nothing else") {
    const std::string a = compile(R"(
      #include <gpu.h>
      int main(void) { gpu_clear(7); return 0; }
    )");
    const std::string body = a.substr(a.find("main:"), a.find("main__end") - a.find("main:"));
    CHECK(countOf(body, "OUT ") == 2);
    CHECK(!has(body, "JSR"));
  }

  TEST_CASE("names the ports a reader already knows") {
    CHECK(has(header("gpu.h"), "out(GPU_CMD, CMD_CLEAR)"));
  }
}

TEST_SUITE("the graphics library really draws") {
  TEST_CASE("clears the screen to a colour") {
    CHECK(ran(R"(
      #include <gpu.h>
      int main(void) { gpu_clear(0xE0); return 0; }
    )").halted());
  }

  TEST_CASE("draws a rectangle where it was told to") {
    CHECK(ran(R"(
      #include <gpu.h>
      int main(void) {
        gpu_set_color(0xFF);
        gpu_move_to(0, 10, 0, 10);
        gpu_rect(0, 20, 0, 20);
        return 0;
      }
    )").halted());
  }

  TEST_CASE("reads the frame counter") {
    CHECK(ran(R"(
      #include <gpu.h>
      unsigned char f;
      int main(void) { f = gpu_frame(); return 0; }
    )").halted());
  }

  TEST_CASE("builds a colour from its parts") {
    CHECK(ran(R"(
      #include <gpu.h>
      unsigned char c;
      int main(void) { c = gpu_rgb(7, 0, 3); return 0; }
    )").u8("c") == 0xe3);
  }
}

TEST_SUITE("the controller library") {
  TEST_CASE("reads the pad and tests a button without the IN flag trap") {
    // IN sets no flag, so a bare JZ after one reads a stale one. The library
    // masks, and if() then tests the mask's result.
    CHECK(ran(R"(
      #include <io.h>
      unsigned char hit;
      int main(void) { hit = 0; if (io_pressed(BTN_FIRE)) hit = 1; return 0; }
    )").u8("hit") == 0);
  }

  TEST_CASE("compiles the key macros") {
    CHECK(ran(R"(
      #include <io.h>
      unsigned char k, code, up;
      int main(void) { k = io_key(); code = io_key_code(k); up = io_key_is_up(k); return 0; }
    )").halted());
  }
}

TEST_SUITE("the system library") {
  TEST_CASE("copies a block with memcpy, which is one GPU command") {
    CHECK(ran(R"(
      #include <sys.h>
      #include <gpu.h>
      unsigned char src[4] = { 1, 2, 3, 4 };
      unsigned char dst[4];
      unsigned char r;
      int main(void) { memcpy(dst, src, 4); r = dst[2]; return 0; }
    )").u8("r") == 3);
  }

  TEST_CASE("overlaps correctly, because the command is a move") {
    CHECK(ran(R"(
      #include <sys.h>
      #include <gpu.h>
      unsigned char t[6] = { 1, 2, 3, 4, 5, 6 };
      unsigned char r;
      int main(void) { memmove(t, t + 2, 4); r = t[0]; return 0; }
    )").u8("r") == 3);
  }

  TEST_CASE("fills with memset, which is a real function") {
    CHECK(ran(R"(
      #include <sys.h>
      unsigned char t[4];
      unsigned char r;
      int main(void) { memset(t, 9, 4); r = t[3]; return 0; }
    )").u8("r") == 9);
  }

  TEST_CASE("peeks and pokes an address") {
    CHECK(ran(R"(
      #include <sys.h>
      unsigned char v, r;
      int main(void) { poke(&v, 55); r = peek(&v); return 0; }
    )").u8("r") == 55);
  }

  TEST_CASE("halts through the library rather than by falling off main") {
    CHECK(ran(R"(
      #include <sys.h>
      unsigned char r;
      int main(void) { r = 1; halt(); r = 2; return 0; }
    )").u8("r") == 1);
  }
}

TEST_SUITE("dead code costs nothing") {
  TEST_CASE("leaves out a library function nothing calls") {
    const std::string a = compile(R"(
      #include <sys.h>
      int main(void) { return 0; }
    )");
    CHECK(!has(a, "memset:"));
    CHECK(!has(a, "acp_run:"));
  }

  TEST_CASE("keeps one that is called") {
    const std::string a = compile(R"(
      #include <sys.h>
      unsigned char t[2];
      int main(void) { memset(t, 1, 2); return 0; }
    )");
    CHECK(has(a, "memset:"));
  }

  TEST_CASE("leaves out a runtime routine the program never needs") {
    CHECK(!has(compile("int a; int main(void) { a = 1 + 2; return 0; }"), "__mul16:"));
  }

  TEST_CASE("brings in only the routine that is used") {
    const std::string a = compile("int a, b; int main(void) { a = b * 3; return 0; }");
    CHECK(has(a, "__mul16:"));
    CHECK(!has(a, "__udiv16:"));
  }
}

TEST_SUITE("many files, which is the point of the build line") {
  TEST_CASE("calls across files") {
    CHECK(runFiles({
        {"main.c", "int add(int a, int b);\nint r;\nint main(void) { r = add(2, 3); return 0; }"},
        {"math.c", "int add(int a, int b) { return a + b; }"},
    }).i16("r") == 5);
  }

  TEST_CASE("shares a global across files through extern") {
    CHECK(runFiles({
        {"main.c", "extern unsigned char v;\nvoid bump(void);\nint main(void) { bump(); return 0; }"},
        {"other.c", "unsigned char v;\nvoid bump(void) { v = 42; }"},
    }).u8("v") == 42);
  }

  TEST_CASE("keeps a static function private, so two files may both have one") {
    CHECK(runFiles({
        {"main.c", "unsigned char r;\nunsigned char other(void);\nstatic unsigned char pick(void) { return 1; }\n"
                   "int main(void) { r = pick() + other(); return 0; }"},
        {"two.c", "static unsigned char pick(void) { return 10; }\nunsigned char other(void) { return pick(); }"},
    }).u8("r") == 11);
  }

  TEST_CASE("refuses two public definitions of one name, and names the other file") {
    std::string msg = "no error";
    try {
      runFiles({
          {"main.c", "int f(void) { return 1; }\nint main(void) { return f(); }"},
          {"two.c", "int f(void) { return 2; }"},
      });
    } catch (const std::exception& e) {
      msg = e.what();
    }
    CHECK(has(msg, "static"));
  }

  TEST_CASE("lets one file include another") {
    CHECK(runFiles({
        {"main.c", "#include \"defs.h\"\nunsigned char r;\nint main(void) { r = FIVE; return 0; }"},
        {"defs.h", "#define FIVE 5"},
    }).u8("r") == 5);
  }
}

TEST_SUITE("the build line reaches the compiler") {
  TEST_CASE("is read off the file that holds main") {
    CHECK(has(compile("// build: cc -o game main.c -lgpu\nint main(void) { return 0; }"), "__start"));
  }
}

TEST_SUITE("what the libraries refuse") {
  TEST_CASE("names the libraries when a header is misspelled") {
    CHECK(has(refuses("#include <grafics.h>\nint main(void){return 0;}"), "graphics.h"));
  }

  TEST_CASE("counts a wrapper's arguments") {
    CHECK(has(refuses(R"(
      #include <gpu.h>
      int main(void) { gpu_clear(1, 2); return 0; }
    )"), "1 argument"));
  }
}

TEST_SUITE("the coprocessor library keeps the runtime invisible") {
  TEST_CASE("shadows the configuration and sends it again") {
    // The runtime multiplies between the point and the run, which would leave
    // the device aimed at its own scratch if acp_run did not resend.
    const Ran r = ran(R"(
      #include <acp.h>
      unsigned char blk[24];
      int n;
      unsigned char r;
      int main(void) {
        acp_point(blk, ACP_U64, 1, 1);
        blk[7] = 20;
        blk[15] = 22;
        n = 3 * 4;
        acp_add();
        r = blk[23];
        return 0;
      }
    )", 400000);
    CHECK(r.u8("r") == 42);
    CHECK(r.i16("n") == 12);
  }
}

TEST_SUITE("the runtime's own globals are dropped when nothing uses them") {
  TEST_CASE("keeps the coprocessor shadow out of a program that never touches it") {
    CHECK(!has(compile("unsigned char v; int main(void) { v = 1; return 0; }"), "__acp_shadow"));
  }

  TEST_CASE("keeps it when acp_point is called") {
    CHECK(has(compile(R"(
      #include <acp.h>
      unsigned char blk[24];
      int main(void) { acp_point(blk, ACP_U64, 1, 1); return 0; }
    )"), "__acp_shadow"));
  }

  // A program's own global stays whether or not anything reads it. It is in
  // the RAM image and the watch list can pin it, and deleting somebody's
  // variable because they had not used it yet would be a strange thing for a
  // teaching tool to do.
  TEST_CASE("never drops one the program declared") {
    CHECK(has(compile("unsigned char unused = 7; int main(void) { return 0; }"), "unused:"));
  }

  TEST_CASE("keeps a global that only inline asm names") {
    CHECK(has(compile(R"(
      unsigned char target;
      int main(void) { asm("LD A <- 5"); asm("LD [target] <- A"); return 0; }
    )"), "target:"));
  }
}
