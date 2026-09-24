#include "cc/headers.h"

#include "cc/libs.h"

#include <cctype>

#include "devices/acp_ports.h"
#include "devices/apu_ports.h"
#include "devices/gpu.h"
#include "devices/gpu_ports.h"
#include "devices/storage_ports.h"

namespace sc8::cc {

namespace {

std::string lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string upper(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

std::string stripPrefix(std::string_view s, const std::string& prefix) {
  if (s.starts_with(prefix)) return std::string(s.substr(prefix.size()));
  return std::string(s);
}

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
  std::string out;
  for (size_t i = 0; i < parts.size(); i++) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

// An argument name for the C signature, from the port alias it feeds.
std::string argName(std::string_view alias, std::string_view prefix) {
  std::string base = lower(stripPrefix(alias, upper(prefix) + "_"));
  for (char& c : base) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) c = '_';
  }
  return base;
}

// Ports a command reads back rather than takes. A result is not an argument.
bool resultOnData0(std::string_view cmd) {
  static const std::string_view SET[] = {
      "CMD_HIT_TEST", "CMD_HIT_SCAN", "CMD_READ_PIXEL",
      "CMD_SPRITE_HITS", "CMD_GROUP_HITS", "CMD_HIT_IN_GROUP",
  };
  for (std::string_view s : SET) if (s == cmd) return true;
  return false;
}

std::string macro(const std::string& name, const std::vector<std::string>& params, const std::string& body) {
  const std::string sig = params.empty() ? name + "()" : name + "(" + join(params, ", ") + ")";
  return "#define " + sig + " \\\n    " + body;
}

// One wrapper: write each argument to its port, then run the command.
std::string commandMacro(std::string_view prefix, std::string_view cmd,
                         const std::vector<std::string_view>& aliases, const std::string& cmdPort) {
  std::map<std::string, int> seen;
  std::vector<std::string> params;
  for (std::string_view a : aliases) {
    const std::string n = argName(a, prefix);
    const int k = ++seen[n];
    params.push_back(k == 1 ? n : n + std::to_string(k));
  }
  std::vector<std::string> lines;
  for (size_t i = 0; i < aliases.size(); i++) {
    lines.push_back("out(" + std::string(aliases[i]) + ", (" + params[i] + "))");
  }
  const std::string run = "out(" + cmdPort + ", " + std::string(cmd) + ")";
  lines.push_back(run);
  const std::string body = join(lines, ", \\\n    ");
  const std::string name = wrapperName(prefix, cmd);
  if (resultOnData0(cmd)) return macro(name, params, "(" + body + ", \\\n    in(GPU_DATA0))");
  return macro(name, params, params.empty() ? run : "(" + body + ")");
}

std::vector<std::string> banner(const std::string& file, const std::string& what) {
  return {
      "/* " + file + ". " + what,
      " *",
      " * Generated from the machine's own tables, so every command has a",
      " * wrapper and no wrapper names a command the machine lacks.",
      " * A wrapper is a macro: it costs exactly the port writes it shows.",
      " */",
  };
}

std::string guard(const std::string& name, const std::vector<std::string>& body) {
  std::string g = name;
  for (char& c : g) if (c == '.') c = '_';
  g = "__" + upper(g) + "__";
  std::vector<std::string> lines = {"#ifndef " + g, "#define " + g, ""};
  lines.insert(lines.end(), body.begin(), body.end());
  lines.push_back("");
  lines.push_back("#endif");
  lines.push_back("");
  return join(lines, "\n");
}

}  // namespace

std::string wrapperName(std::string_view prefix, std::string_view cmd) {
  // The GPU and the audio chip spell theirs CMD_CLEAR. The coprocessor spells
  // its own ACP_ADD, so stripping only CMD_ gave acp_acp_add.
  const std::string bare = stripPrefix(stripPrefix(cmd, "CMD_"), upper(prefix) + "_");
  return std::string(prefix) + "_" + lower(bare);
}

// The GPU's own table, in src/devices/gpu.cpp, reshaped for the callers
// that want a vector per command.
const std::vector<CmdArgs>& gpuCmdArgs() {
  static const std::vector<CmdArgs> TABLE = [] {
    std::vector<CmdArgs> out;
    for (const gpu::CmdArgs& c : gpu::CMD_ARGS) {
      out.push_back({c.cmd, std::vector<std::string_view>(c.args().begin(), c.args().end())});
    }
    return out;
  }();
  return TABLE;
}

std::optional<std::vector<std::string_view>> gpuArgsOf(std::string_view cmd) {
  for (const CmdArgs& c : gpuCmdArgs()) if (c.cmd == cmd) return c.aliases;
  return std::nullopt;
}

std::string gpuHeader() {
  std::vector<std::string> out = banner("gpu.h", "The graphics chip, on ports $00 to $1F.");
  out.push_back("");
  for (const auto& nv : gpu::CMDS) {
    if (nv.name == "CMD_PRINTF") {
      // gpu_printf is variadic, so it is the compiler's rather than a macro's.
      // It puts the template on the cartridge, marshals the arguments into a
      // RAM block big-endian and sized by the FORMAT, and points the device
      // at both. A macro cannot do any of that.
      out.push_back("/* gpu_printf(fmt, ...) is built into the compiler.");
      out.push_back(" * The template goes to the cartridge on its own, and the");
      out.push_back(" * arguments are marshalled for you. An int is 16 bits, so %d");
      out.push_back(" * reads two bytes, %hhu one, %lu four and %llu eight, and the");
      out.push_back(" * compiler counts them against the format for you.");
      out.push_back(" *");
      out.push_back(" *   gpu_printf(\"score %u lives %hhu\", score, lives);");
      out.push_back(" */");
      out.push_back("");
      continue;
    }
    const auto aliases = gpuArgsOf(nv.name);
    out.push_back(commandMacro("gpu", nv.name, aliases.value_or(std::vector<std::string_view>{}), "GPU_CMD"));
    out.push_back("");
  }
  out.push_back("/* The two direct reads. No command, just a port. */");
  out.push_back(macro("gpu_frame", {}, "in(GPU_FRAME)"));
  out.push_back("");
  out.push_back(macro("gpu_rand", {}, "in(GPU_RAND)"));
  out.push_back("");
  out.push_back("/* The command modifier. A mode, not a one shot. */");
  out.push_back(macro("gpu_mod", {"m"}, "out(GPU_CMD_MOD, (m))"));
  out.push_back("");
  out.push_back("/* A colour from red, green and blue, in the 3-3-2 default palette. */");
  out.push_back("#define gpu_rgb(r, g, b) \\\n    ((((r) & 7) << 5) | (((g) & 7) << 2) | ((b) & 3))");
  return guard("gpu.h", out);
}

std::string apuHeader() {
  std::vector<std::string> out = banner("apu.h", "The audio chip, on ports $30 to $3F.");
  out.push_back("");
  // The APU has no per-command argument table, so each wrapper takes the
  // latches that command documents. Written out rather than guessed.
  static const std::map<std::string_view, std::vector<std::string_view>> ARGS = {
      {"CMD_DEF_SAMPLE", {"APU_SLOT", "APU_CART_BANK", "APU_CART_HI", "APU_CART_LO", "APU_ARG", "APU_ARG2", "APU_NOTE"}},
      {"CMD_SET_INSTRUMENT", {"APU_TRACK", "APU_SLOT", "APU_ARG"}},
      {"CMD_KEYMAP_ENTRY", {"APU_TRACK", "APU_ARG", "APU_SLOT"}},
      {"CMD_NOTE_ON", {"APU_TRACK", "APU_NOTE", "APU_ARG"}},
      {"CMD_NOTE_OFF", {"APU_TRACK", "APU_NOTE"}},
      {"CMD_TRIGGER", {"APU_TRACK", "APU_SLOT", "APU_NOTE", "APU_ARG"}},
      {"CMD_LOAD_MIDI", {"APU_CART_BANK", "APU_CART_HI", "APU_CART_LO"}},
      {"CMD_PLAY", {}},
      {"CMD_STOP", {}},
      {"CMD_STOP_ALL", {}},
      {"CMD_LOOP_SLOT", {"APU_SLOT", "APU_ARG"}},
  };
  for (const auto& nv : apu::CMDS) {
    auto it = ARGS.find(nv.name);
    out.push_back(commandMacro("apu", nv.name, it == ARGS.end() ? std::vector<std::string_view>{} : it->second, "APU_CMD"));
    out.push_back("");
  }
  out.push_back("/* The two read ports, the GPU_FRAME idiom. */");
  out.push_back(macro("apu_status", {}, "in(APU_STATUS)"));
  out.push_back("");
  out.push_back(macro("apu_voices", {}, "in(APU_VOICES)"));
  return guard("apu.h", out);
}

std::string acpHeader() {
  std::vector<std::string> out = banner("acp.h", "The arithmetic coprocessor, on ports $40 to $4F.");
  out.push_back("");
  out.push_back("/* DESIGN: the device latches ADDR, FMT, RFMT, ROWS and COLS, and none");
  out.push_back(" * of them reads back. The compiler's own runtime uses the device for");
  out.push_back(" * every multiply and divide, so it leaves those latches pointed at its");
  out.push_back(" * scratch. acp_point remembers what you asked for, and acp_run sends it");
  out.push_back(" * again before every command. Six extra writes, and the runtime becomes");
  out.push_back(" * invisible, which is the only way both can share one device.");
  out.push_back(" */");
  out.push_back("extern unsigned char __acp_shadow[6];");
  out.push_back("");
  out.push_back("void acp_point(void *block, unsigned char fmt, unsigned char rows, unsigned char cols);");
  out.push_back("void acp_run(unsigned char cmd);");
  out.push_back("");
  out.push_back(macro("acp_flags", {}, "in(ACP_FLAGS)"));
  out.push_back("");
  out.push_back(macro("acp_rfmt", {"f"}, "out(ACP_RFMT, (f))"));
  out.push_back("");
  out.push_back(macro("acp_cols_b", {"n"}, "out(ACP_COLS_B, (n))"));
  out.push_back("");
  out.push_back("/* One name per command, so the manual's table maps onto this file. */");
  for (const auto& nv : acp::CMDS) {
    out.push_back(macro(wrapperName("acp", nv.name), {}, "acp_run(" + std::string(nv.name) + ")"));
  }
  return guard("acp.h", out);
}

std::string storageHeader() {
  std::vector<std::string> out = banner("storage.h", "The storage device, on ports $50 to $5F.");
  out.push_back("");
  out.push_back("/* A slot is named by a NUL terminated string in RAM, and its text");
  out.push_back(" * moves through a block in RAM. LOAD and CATALOG take the room the");
  out.push_back(" * block has, NUL included, and write nothing when it is short. Read");
  out.push_back(" * sto_status() after a command: STO_OK, STO_NOT_FOUND, STO_FULL or");
  out.push_back(" * STO_BAD_NAME. sto_len() is how many bytes the last command moved.");
  out.push_back(" */");
  out.push_back(std::string("#define sto_load(block, name, room) \\\n    (out(STO_ADDR_HI, ((unsigned int)(block)) >> 8), \\\n") +
                "     out(STO_ADDR_LO, ((unsigned int)(block)) & 255), \\\n" +
                "     out(STO_NAME_HI, ((unsigned int)(name)) >> 8), \\\n" +
                "     out(STO_NAME_LO, ((unsigned int)(name)) & 255), \\\n" +
                "     out(STO_LEN_HI, ((unsigned int)(room)) >> 8), \\\n" +
                "     out(STO_LEN_LO, ((unsigned int)(room)) & 255), \\\n" +
                "     out(STO_CMD, STO_LOAD))");
  out.push_back("");
  out.push_back(std::string("#define sto_save(block, name) \\\n    (out(STO_ADDR_HI, ((unsigned int)(block)) >> 8), \\\n") +
                "     out(STO_ADDR_LO, ((unsigned int)(block)) & 255), \\\n" +
                "     out(STO_NAME_HI, ((unsigned int)(name)) >> 8), \\\n" +
                "     out(STO_NAME_LO, ((unsigned int)(name)) & 255), \\\n" +
                "     out(STO_CMD, STO_SAVE))");
  out.push_back("");
  out.push_back(std::string("#define sto_delete(name) \\\n    (out(STO_NAME_HI, ((unsigned int)(name)) >> 8), \\\n") +
                "     out(STO_NAME_LO, ((unsigned int)(name)) & 255), \\\n" +
                "     out(STO_CMD, STO_DELETE))");
  out.push_back("");
  out.push_back(std::string("#define sto_catalog(block, room) \\\n    (out(STO_ADDR_HI, ((unsigned int)(block)) >> 8), \\\n") +
                "     out(STO_ADDR_LO, ((unsigned int)(block)) & 255), \\\n" +
                "     out(STO_LEN_HI, ((unsigned int)(room)) >> 8), \\\n" +
                "     out(STO_LEN_LO, ((unsigned int)(room)) & 255), \\\n" +
                "     out(STO_CMD, STO_CATALOG))");
  out.push_back("");
  out.push_back("/* The three reads. No command, just a port. */");
  out.push_back(macro("sto_status", {}, "in(STO_STATUS)"));
  out.push_back("");
  out.push_back(macro("sto_count", {}, "in(STO_COUNT)"));
  out.push_back("");
  out.push_back(macro("sto_len", {}, "((((unsigned int)in(STO_LEN_HI)) << 8) | in(STO_LEN_LO))"));
  return guard("storage.h", out);
}

std::string ioHeader() {
  std::vector<std::string> out = banner("io.h", "The controller and the keyboard, on ports $20 and $21.");
  out.push_back("");
  out.push_back("/* The pad is a level, not an edge. Read it, then test a bit. */");
  out.push_back(macro("io_pad", {}, "in(IO_CONTROLLER)"));
  out.push_back("");
  out.push_back("/* Zero when the button is up, which is what if() wants. */");
  out.push_back(macro("io_pressed", {"b"}, "(in(IO_CONTROLLER) & (b))"));
  out.push_back("");
  out.push_back("/* One event, or zero when the buffer is empty. The release bit is $80. */");
  out.push_back(macro("io_key", {}, "in(IO_KEY)"));
  out.push_back("");
  out.push_back("/* Which modifier keys are held. A level, like the pad, so reading");
  out.push_back(" * it costs no key event. IO_KEY has no room for a modifier: it is");
  out.push_back(" * seven bits of code and the release bit, so Ctrl-C and C arrive");
  out.push_back(" * as the same byte and this is how you tell them apart.");
  out.push_back(" */");
  out.push_back(macro("io_mods", {}, "in(IO_MODS)"));
  out.push_back("");
  out.push_back(macro("io_held", {"m"}, "(in(IO_MODS) & (m))"));
  out.push_back("");
  out.push_back("#define IO_KEY_RELEASED 0x80");
  out.push_back("#define io_key_code(k)  ((k) & 0x7F)");
  out.push_back("#define io_key_is_up(k) ((k) & IO_KEY_RELEASED)");
  return guard("io.h", out);
}

std::string romHeader() {
  std::vector<std::string> out = banner("rom.h", "Cartridge addresses, which are 24 bits on a 16 bit machine.");
  out.push_back("");
  out.push_back("/* A rom_t is bank, high, low. The three macros give the bytes, and");
  out.push_back(" * each library wrapper knows which ports they go to.");
  out.push_back(" */");
  out.push_back("#define ROM_BANK(r) (((r) >> 16) & 0xFF)");
  out.push_back("#define ROM_HI(r)   (((r) >> 8) & 0xFF)");
  out.push_back("#define ROM_LO(r)   ((r) & 0xFF)");
  return guard("rom.h", out);
}

std::string sysHeader() {
  std::vector<std::string> out = banner("sys.h", "The machine itself, and the few real functions.");
  out.push_back("");
  // rom_copy below splits a cartridge address with the three macros, so the
  // header that defines them comes with it.
  out.push_back("#include <rom.h>");
  out.push_back("");
  out.push_back("#define halt()        asm(\"HLT\")");
  out.push_back("");
  out.push_back("/* Wait for the frame counter to move. A frame is 65536 cycles. */");
  out.push_back("void wait_frame(void);");
  out.push_back("");
  out.push_back("/* Data RAM, byte at a time, for when a pointer is not what you have. */");
  out.push_back("#define peek(a)       (*(unsigned char *)(a))");
  out.push_back("#define poke(a, v)    (*(unsigned char *)(a) = (v))");
  out.push_back("");
  out.push_back("/* Block moves. The GPU does them in one cycle, so these are not loops. */");
  out.push_back(std::string("#define memcpy(d, s, n) \\\n    (out(GPU_FROM_HI, ((unsigned int)(s)) >> 8), \\\n") +
                "     out(GPU_FROM_LO, ((unsigned int)(s)) & 255), \\\n" +
                "     out(GPU_DEST_HI, ((unsigned int)(d)) >> 8), \\\n" +
                "     out(GPU_DEST_LO, ((unsigned int)(d)) & 255), \\\n" +
                "     out(GPU_LEN_HI, ((unsigned int)(n)) >> 8), \\\n" +
                "     out(GPU_LEN_LO, ((unsigned int)(n)) & 255), \\\n" +
                "     out(GPU_CMD, CMD_RAM_MOVE))");
  out.push_back("");
  out.push_back("/* memmove is the same command: it already handles an overlap. */");
  out.push_back("#define memmove(d, s, n) memcpy((d), (s), (n))");
  out.push_back("");
  out.push_back("void memset(void *d, unsigned char v, unsigned int n);");
  out.push_back("");
  out.push_back("/* Copy from the cartridge, which is the one way to read it. */");
  out.push_back(std::string("#define rom_copy(d, r, n) \\\n    (out(GPU_CART_BANK, ROM_BANK(r)), \\\n") +
                "     out(GPU_CART_HI, ROM_HI(r)), \\\n" +
                "     out(GPU_CART_LO, ROM_LO(r)), \\\n" +
                "     out(GPU_DEST_HI, ((unsigned int)(d)) >> 8), \\\n" +
                "     out(GPU_DEST_LO, ((unsigned int)(d)) & 255), \\\n" +
                "     out(GPU_LEN_HI, ((unsigned int)(n)) >> 8), \\\n" +
                "     out(GPU_LEN_LO, ((unsigned int)(n)) & 255), \\\n" +
                "     out(GPU_CMD, CMD_COPY))");
  out.push_back("");
  out.push_back("/* A byte from the GPU's generator. Write it to reseed. */");
  out.push_back("#define rand()        in(GPU_RAND)");
  out.push_back("#define srand(s)      out(GPU_RAND, (s))");
  return guard("sys.h", out);
}

const char* const LIBC_SOURCE = R"(
void wait_frame(void) {
    unsigned char f;
    f = in(GPU_FRAME);
    while (in(GPU_FRAME) == f) { }
}

void memset(void *d, unsigned char v, unsigned int n) {
    unsigned char *p;
    p = (unsigned char *)d;
    while (n) { *p = v; p = p + 1; n = n - 1; }
}

unsigned char __acp_shadow[6];

void acp_point(void *block, unsigned char fmt, unsigned char rows, unsigned char cols) {
    __acp_shadow[0] = ((unsigned int)block) >> 8;
    __acp_shadow[1] = ((unsigned int)block) & 255;
    __acp_shadow[2] = fmt;
    __acp_shadow[3] = rows;
    __acp_shadow[4] = cols;
}

void acp_run(unsigned char cmd) {
    out(ACP_ADDR_HI, __acp_shadow[0]);
    out(ACP_ADDR_LO, __acp_shadow[1]);
    out(ACP_FMT, __acp_shadow[2]);
    out(ACP_ROWS, __acp_shadow[3]);
    out(ACP_COLS, __acp_shadow[4]);
    out(ACP_CMD, cmd);
}
)";

const std::map<std::string, std::string>& headers() {
  static const std::map<std::string, std::string> H = [] {
    std::map<std::string, std::string> h = {
        {"gpu.h", gpuHeader()},
        {"apu.h", apuHeader()},
        {"acp.h", acpHeader()},
        {"storage.h", storageHeader()},
        {"io.h", ioHeader()},
        {"sys.h", sysHeader()},
        {"rom.h", romHeader()},
    };
    for (const Library& l : libraries()) h[l.header] = l.headerText;
    return h;
  }();
  return H;
}

std::vector<std::string> headerNames() {
  std::vector<std::string> out;
  for (const auto& [k, v] : headers()) out.push_back(k);
  return out;
}

}  // namespace sc8::cc
