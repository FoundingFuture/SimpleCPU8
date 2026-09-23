// The golden bytes of the browser project, checked against this assembler.
//
// tests/asm/golden-bytes/<name>.txt is a copy of the browser project's
// packages/ui/test/golden-bytes fixture. It holds the program, RAM image,
// data section, labels and RAM line map its assembler produced per demo.
// The sources are the ones examples/extract-demos.mjs wrote to
// examples/<name>/<name>.asm. A mismatch means the C++ assembler
// disagrees with the TypeScript one on a shipped program.
//
// The fixture has one section per keyword, each with its count. Values are
// hex except source lines.
//   program N    slot, opcode, operand, source line.
//   ram N        byte offset, then the bytes.
//   cart N       byte offset, then the bytes.
//   labels N     name, kind, value.
//   ramlines N   .ram source line, then the RAM address it defines.
// The labels come in definition order there and in name order here. So
// they are compared as a map.

#include <doctest.h>

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "asm/asm.h"

using namespace sc8;

namespace {

struct Golden {
  int sourceLines = 0;
  int errors = 0;
  std::vector<Instr> program;
  std::vector<int> lines;
  std::vector<uint8_t> ram;
  std::vector<uint8_t> cart;
  std::map<std::string, Label> labels;
  std::map<int, int> ramLines;
};

std::string readText(const std::string& path) {
  std::ifstream in(path);
  REQUIRE_MESSAGE(in, "cannot read ", path);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::vector<std::string> contentLines(const std::string& text) {
  std::vector<std::string> out;
  std::stringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) {
    if (line.empty() || line[0] == '#') continue;
    out.push_back(line);
  }
  return out;
}

void readBytes(const std::vector<std::string>& lines, size_t& i, size_t count, std::vector<uint8_t>& out) {
  while (out.size() < count) {
    REQUIRE(i < lines.size());
    std::stringstream ss(lines[i++]);
    std::string offset;
    ss >> offset;
    REQUIRE_EQ(std::stoul(offset, nullptr, 16), out.size());
    std::string byte;
    while (ss >> byte) out.push_back(static_cast<uint8_t>(std::stoul(byte, nullptr, 16)));
  }
  REQUIRE_EQ(out.size(), count);
}

Golden parseGolden(const std::string& text) {
  Golden g;
  const std::vector<std::string> lines = contentLines(text);
  size_t i = 0;
  while (i < lines.size()) {
    std::stringstream head(lines[i++]);
    std::string key;
    size_t count = 0;
    head >> key >> count;
    if (key == "source-lines") g.sourceLines = static_cast<int>(count);
    else if (key == "errors") g.errors = static_cast<int>(count);
    else if (key == "program") {
      for (size_t n = 0; n < count; n++) {
        std::stringstream ss(lines[i++]);
        std::string slot, op, operand;
        int line = 0;
        ss >> slot >> op >> operand >> line;
        REQUIRE_EQ(std::stoul(slot, nullptr, 16), n);
        g.program.push_back(Instr{static_cast<uint8_t>(std::stoul(op, nullptr, 16)),
                                  static_cast<uint16_t>(std::stoul(operand, nullptr, 16))});
        g.lines.push_back(line);
      }
    } else if (key == "ram") readBytes(lines, i, count, g.ram);
    else if (key == "cart") readBytes(lines, i, count, g.cart);
    else if (key == "labels") {
      for (size_t n = 0; n < count; n++) {
        std::stringstream ss(lines[i++]);
        std::string name, kind, value;
        ss >> name >> kind >> value;
        const Label::Kind k = kind == "code" ? Label::Kind::Code : kind == "ram" ? Label::Kind::Ram : Label::Kind::Data;
        g.labels[name] = Label{k, static_cast<int>(std::stoul(value, nullptr, 16))};
      }
    } else if (key == "ramlines") {
      for (size_t n = 0; n < count; n++) {
        std::stringstream ss(lines[i++]);
        int line = 0;
        std::string addr;
        ss >> line >> addr;
        g.ramLines[line] = static_cast<int>(std::stoul(addr, nullptr, 16));
      }
    } else FAIL("unknown golden section ", key);
  }
  return g;
}

constexpr const char* DEMOS[] = {"audio",  "circle", "cube",   "div16", "div8",     "fly",    "gpu",
                                 "groups", "hanoi",  "input",  "list",  "mandel",   "matrix", "mul16",
                                 "mul8",   "orbit",  "pacman", "pong",  "random",   "sprite", "star",
                                 "text",   "textmode", "tunnel", "world"};

}  // namespace

TEST_SUITE("golden demo bytes") {
  TEST_CASE("every demo assembles to the browser project's bytes") {
    for (const char* name : DEMOS) {
      SUBCASE(name) {
        const std::string n = name;
        const std::string source = readText(std::string(SC8_EXAMPLES_DIR) + "/" + n + "/" + n + ".asm");
        const Golden g = parseGolden(readText(std::string(SC8_GOLDEN_DIR) + "/" + n + ".txt"));

        // The fixture counts the pieces of a split on newline. That is one
        // more than the newlines of a file ending in one.
        int newlines = 0;
        for (char c : source) newlines += c == '\n';
        CHECK_EQ(newlines + 1, g.sourceLines);

        Assembled a = assemble(source);
        for (const AsmError& e : a.errors) MESSAGE(n, ":", e.line, ": ", e.message);
        REQUIRE_EQ(a.errors.size(), static_cast<size_t>(g.errors));
        CHECK(a.program == g.program);
        CHECK(a.instrToLine == g.lines);
        CHECK_EQ(a.ramLength, g.ram.size());
        CHECK(std::vector<uint8_t>(a.ram.begin(), a.ram.begin() + static_cast<long>(g.ram.size())) == g.ram);
        CHECK(a.cart == g.cart);
        CHECK(a.labels == g.labels);
        CHECK(a.ramLineAddr == g.ramLines);
      }
    }
  }
}
