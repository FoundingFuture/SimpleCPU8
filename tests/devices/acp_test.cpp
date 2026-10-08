#include <doctest.h>

#include <algorithm>
#include <memory>
#include <ostream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "asm/asm.h"
#include "core/machine.h"
#include "core/microcode.h"
#include "devices/acp.h"

using namespace sc8;
using namespace sc8::acp;

// The ACP is the fourth device on the bus and the first that WRITES data RAM.
// These tests drive it the way a program does: latch an address, a type and a
// shape, then write ACP_CMD and read the block back.

namespace {

class SpyBus : public IoBus {
 public:
  std::vector<std::pair<uint8_t, uint8_t>> writes;
  std::vector<uint8_t> reads;
  void write(uint8_t port, uint8_t value) override { writes.emplace_back(port, value); }
  uint8_t read(uint8_t port) override {
    reads.push_back(port);
    return 0x5a;
  }
};

// The device keeps a pointer into its own fallback, so it never moves. A rig
// owns it on the heap and is free to move itself.
struct Rig {
  std::unique_ptr<Acp> acp;
  std::vector<uint8_t> ram;

  explicit Rig(uint8_t fill = 0xaa) : acp(std::make_unique<Acp>()), ram(RAM_SIZE, fill) {
    acp->attachRam(ram.data());
  }
  void out(int port, int value) {
    acp->write(static_cast<uint8_t>(port), static_cast<uint8_t>(value & 0xff));
  }
  uint8_t flags() { return acp->read(ACP_FLAGS); }
};

// A block at `base`, with the operand type and shape latched.
void setup(Rig& r, int base, int fmt, int rows = 1, int cols = 1) {
  r.out(ACP_ADDR_HI, base >> 8);
  r.out(ACP_ADDR_LO, base & 0xff);
  r.out(ACP_FMT, fmt);
  r.out(ACP_ROWS, rows);
  r.out(ACP_COLS, cols);
}

// Big-endian, like every word here.
void putU64(std::vector<uint8_t>& ram, int at, uint64_t v) {
  for (int i = 0; i < 8; i++) ram[static_cast<size_t>(at + i)] = static_cast<uint8_t>(v >> (56 - 8 * i));
}
void putI64(std::vector<uint8_t>& ram, int at, int64_t v) { putU64(ram, at, static_cast<uint64_t>(v)); }
uint64_t getU64(const std::vector<uint8_t>& ram, int at) {
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v = (v << 8) | ram[static_cast<size_t>(at + i)];
  return v;
}
int64_t getI64(const std::vector<uint8_t>& ram, int at) { return static_cast<int64_t>(getU64(ram, at)); }

// Only a 1 by 1 integer MULTIPLY widens to 16 bytes. That is the one case
// where the exact answer needs 128 bits. Every other result element is the
// width its type says, so most of these read eight bytes.
struct U128 {
  uint64_t hi, lo;
  bool operator==(const U128&) const = default;
};
U128 getU128(const std::vector<uint8_t>& ram, int at) { return {getU64(ram, at), getU64(ram, at + 8)}; }

// The test's own 128 bit product, by 16 bit limbs. It shares no code with
// the device's 32 bit ones. The TypeScript leans on BigInt for this.
U128 mulU128(uint64_t a, uint64_t b) {
  uint64_t limbs[8] = {};
  for (int i = 0; i < 4; i++) {
    uint64_t carry = 0;
    const uint64_t ai = (a >> (16 * i)) & 0xffff;
    for (int j = 0; j < 4; j++) {
      const uint64_t bj = (b >> (16 * j)) & 0xffff;
      const uint64_t t = limbs[i + j] + ai * bj + carry;
      limbs[i + j] = t & 0xffff;
      carry = t >> 16;
    }
    limbs[i + 4] += carry;
  }
  U128 r{0, 0};
  for (int i = 0; i < 4; i++) {
    r.lo |= limbs[i] << (16 * i);
    r.hi |= limbs[i + 4] << (16 * i);
  }
  return r;
}
U128 negU128(U128 v) {
  U128 r{~v.hi, ~v.lo};
  r.lo += 1;
  if (r.lo == 0) r.hi += 1;
  return r;
}
U128 mulI128(int64_t a, int64_t b) {
  const uint64_t ma = a < 0 ? uint64_t{0} - static_cast<uint64_t>(a) : static_cast<uint64_t>(a);
  const uint64_t mb = b < 0 ? uint64_t{0} - static_cast<uint64_t>(b) : static_cast<uint64_t>(b);
  const U128 p = mulU128(ma, mb);
  return (a < 0) != (b < 0) ? negU128(p) : p;
}

int64_t wrapAdd(int64_t a, int64_t b) {
  return static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}
int64_t wrapSub(int64_t a, int64_t b) {
  return static_cast<int64_t>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}
int64_t wrapNeg(int64_t a) { return static_cast<int64_t>(uint64_t{0} - static_cast<uint64_t>(a)); }

// One scalar integer operation, start to finish.
Rig scalarOp(int cmd, int64_t a, int64_t b, int fmt = ACP_I64) {
  Rig r;
  const int base = 0x1000;
  setup(r, base, fmt);
  putI64(r.ram, base, a);
  putI64(r.ram, base + 8, b);
  r.out(ACP_CMD, cmd);
  return r;
}

bool bit(uint8_t flags, int mask) { return (flags & mask) != 0; }

constexpr int64_t I64_MIN = static_cast<int64_t>(uint64_t{1} << 63);
constexpr int64_t I64_MAX = static_cast<int64_t>((uint64_t{1} << 63) - 1);

}  // namespace

TEST_SUITE("the ACP on the bus") {
  TEST_CASE("claims $40 to $4F and forwards everything else") {
    SpyBus spy;
    Acp acp(&spy);
    for (int port = 0x3e; port <= 0x51; port++) acp.write(static_cast<uint8_t>(port), 1);
    std::vector<uint8_t> forwarded;
    for (const auto& w : spy.writes) forwarded.push_back(w.first);
    CHECK_EQ(forwarded, std::vector<uint8_t>{0x3e, 0x3f, 0x50, 0x51});
    CHECK_EQ(acp.read(0x50), 0x5a);  // the fallback answered
    CHECK_EQ(spy.reads, std::vector<uint8_t>{0x50});
  }

  TEST_CASE("reads exactly its operand bytes and writes exactly its result bytes") {
    // RAM is filled with $AA. After one add, only the result region may differ.
    Rig r = scalarOp(ACP_ADD, 2, 3);
    const int base = 0x1000;
    int moved = 0;
    for (int i = 0; i < RAM_SIZE; i++) {
      // Two 8 byte operands then one 8 byte result: 24 bytes, not 32.
      const bool inResult = i >= base + 16 && i < base + 24;
      const bool inOperands = i >= base && i < base + 16;
      if (inResult || inOperands) continue;
      if (r.ram[static_cast<size_t>(i)] != 0xaa) moved++;
    }
    CHECK_EQ(moved, 0);
    CHECK_EQ(getI64(r.ram, base + 16), 5);
  }

  TEST_CASE("reads both operands before writing, so an overlap is defined") {
    Rig r;
    const int base = 0x2000;
    setup(r, base, ACP_I64);
    putI64(r.ram, base, 7);
    putI64(r.ram, base + 8, 5);
    r.out(ACP_CMD, ACP_SUB);
    CHECK_EQ(getI64(r.ram, base + 16), 2);
  }

  TEST_CASE("latches the address, the type and the shape across commands") {
    Rig r;
    const int base = 0x3000;
    setup(r, base, ACP_I64);
    putI64(r.ram, base, 10);
    putI64(r.ram, base + 8, 4);
    r.out(ACP_CMD, ACP_ADD);
    CHECK_EQ(getI64(r.ram, base + 16), 14);
    // Nothing set again but the verb.
    r.out(ACP_CMD, ACP_SUB);
    CHECK_EQ(getI64(r.ram, base + 16), 6);
  }

  TEST_CASE("powers on 1 by 1, so scalar work never touches the dimension ports") {
    Rig r;
    const int base = 0x4000;
    r.out(ACP_ADDR_HI, base >> 8);
    r.out(ACP_ADDR_LO, base & 0xff);
    r.out(ACP_FMT, ACP_I64);
    putI64(r.ram, base, 6);
    putI64(r.ram, base + 8, 7);
    r.out(ACP_CMD, ACP_MUL);
    CHECK_EQ(getU128(r.ram, base + 16), U128{0, 42});
  }

  TEST_CASE("wraps a block that runs past the top of RAM") {
    // No data address is illegal on this machine and an effective address
    // wraps at 16 bits. The device is no exception.
    Rig r;
    const int base = 0xfff8;
    setup(r, base, ACP_I64);
    putI64(r.ram, base, 3);
    putI64(r.ram, 0, 4);  // operand B wrapped to address 0
    r.out(ACP_CMD, ACP_ADD);
    CHECK_EQ(getI64(r.ram, 8), 7);  // and so did the result
  }

  TEST_CASE("reads zero and drops writes with no RAM attached") {
    Acp acp;
    acp.write(ACP_CMD, ACP_ADD);
    CHECK(bit(acp.read(ACP_FLAGS), ACP_ZERO));
  }
}

TEST_SUITE("ACP integers") {
  const std::vector<std::pair<int64_t, int64_t>> CASES = {
      {0, 0},
      {1, 1},
      {-1, 1},
      {7, -3},
      {-7, 3},
      {123456789, 987654321},
      {(int64_t{1} << 62) - 1, 3},
      {I64_MIN, 1},
      {I64_MAX, I64_MAX},
  };

  TEST_CASE("adds, subtracts and negates with wrapping, across the signed range") {
    for (const auto& [a, b] : CASES) {
      CAPTURE(a);
      CAPTURE(b);
      CHECK_EQ(getI64(scalarOp(ACP_ADD, a, b).ram, 0x1010), wrapAdd(a, b));
      CHECK_EQ(getI64(scalarOp(ACP_SUB, a, b).ram, 0x1010), wrapSub(a, b));
      // NEG reads no B, so B takes no room and the result starts at offset 8.
      CHECK_EQ(getI64(scalarOp(ACP_NEG, a, b).ram, 0x1008), wrapNeg(a));
    }
  }

  TEST_CASE("multiplies two 64 bit integers into 128 bits exactly") {
    const int64_t big = I64_MAX;
    // (2^63 - 1)^2 = 2^126 - 2^64 + 1, written down so the reference multiply
    // is itself checked once.
    CHECK_EQ(mulI128(big, big), U128{0x3fffffffffffffffull, 1});
    CHECK_EQ(getU128(scalarOp(ACP_MUL, big, big).ram, 0x1010), U128{0x3fffffffffffffffull, 1});
    CHECK_EQ(getU128(scalarOp(ACP_MUL, -big, big).ram, 0x1010),
             U128{0xc000000000000000ull, 0xffffffffffffffffull});
    for (const auto& [a, b] : CASES) {
      CAPTURE(a);
      CAPTURE(b);
      CHECK_EQ(getU128(scalarOp(ACP_MUL, a, b).ram, 0x1010), mulI128(a, b));
    }
  }

  TEST_CASE("compares unsigned operands as unsigned, so the top bit is a value") {
    // As signed, 2^64 - 1 is -1 and compares BELOW 1. As unsigned it is the
    // largest value there is and compares above. This is the read side.
    Rig r;
    const int base = 0x8000;
    setup(r, base, ACP_U64);
    putI64(r.ram, base, -1);  // the bits of 2^64 - 1
    putI64(r.ram, base + 8, 1);
    r.out(ACP_CMD, ACP_CMP);
    CHECK_EQ(getI64(r.ram, base + 16), 1);
  }

  TEST_CASE("never calls an unsigned result negative") {
    // The write side. Signed and unsigned store the same bits, so only the
    // flags can tell them apart. A test that reads the bytes back cannot.
    Rig r;
    const int base = 0x8100;
    setup(r, base, ACP_U64);
    putI64(r.ram, base, -1);  // 2^64 - 1
    putI64(r.ram, base + 8, 0);
    r.out(ACP_CMD, ACP_ADD);
    CHECK_FALSE(bit(r.flags(), ACP_NEGATIVE));
    CHECK_FALSE(bit(r.flags(), ACP_OVERFLOW));
  }

  TEST_CASE("treats the unsigned type as unsigned") {
    Rig r = scalarOp(ACP_MUL, -1, -1, ACP_U64);
    // As unsigned, -1 is 2^64 - 1, so the product is (2^64 - 1)^2.
    CHECK_EQ(getU128(r.ram, 0x1010), mulU128(~uint64_t{0}, ~uint64_t{0}));
    CHECK_EQ(getU128(r.ram, 0x1010), U128{0xfffffffffffffffeull, 1});
    CHECK_FALSE(bit(r.flags(), ACP_OVERFLOW));
  }

  TEST_CASE("divides toward zero and takes the remainder's sign from the dividend") {
    for (const auto& [a, b] : std::vector<std::pair<int64_t, int64_t>>{{7, 2}, {-7, 2}, {7, -2}, {-7, -2}}) {
      CAPTURE(a);
      CAPTURE(b);
      CHECK_EQ(getI64(scalarOp(ACP_DIV, a, b).ram, 0x1010), a / b);
      CHECK_EQ(getI64(scalarOp(ACP_REM, a, b).ram, 0x1010), a % b);
    }
  }

  TEST_CASE("overflows the one quotient that does not fit, and says so") {
    // -2^63 / -1 is 2^63, which BigInt forms and the writer then refuses.
    Rig r = scalarOp(ACP_DIV, I64_MIN, -1);
    CHECK_EQ(getI64(r.ram, 0x1010), I64_MIN);
    CHECK(bit(r.flags(), ACP_OVERFLOW));
    Rig m = scalarOp(ACP_REM, I64_MIN, -1);
    CHECK_EQ(getI64(m.ram, 0x1010), 0);
    CHECK_FALSE(bit(m.flags(), ACP_OVERFLOW));
  }

  TEST_CASE("gives a unary operation no room for B, so the result follows A") {
    // Stated in docs/acp-design.md: a dash in the B column means B takes no
    // room. Pinned here because it is the one part of the layout that is not
    // obvious from looking at a block.
    Rig r = scalarOp(ACP_NEG, 5, 999);
    CHECK_EQ(getI64(r.ram, 0x1008), -5);
    CHECK_EQ(r.ram[0x1010], 0xaa);  // where B's region would have been
  }

  TEST_CASE("compares to minus one, zero or one") {
    CHECK_EQ(getI64(scalarOp(ACP_CMP, 1, 2).ram, 0x1010), -1);
    CHECK_EQ(getI64(scalarOp(ACP_CMP, 2, 2).ram, 0x1010), 0);
    CHECK_EQ(getI64(scalarOp(ACP_CMP, 3, 2).ram, 0x1010), 1);
  }

  TEST_CASE("scales every element by the one number in B") {
    // ACP_SCALE is the one operation whose B is a different shape from A.
    // So it is the one that can silently read B element by element instead.
    Rig r;
    const int base = 0x9000;
    setup(r, base, ACP_I64, 1, 3);
    for (int i = 0; i < 3; i++) putI64(r.ram, base + 8 * i, i + 1);
    putI64(r.ram, base + 24, 10);  // B is 1 by 1, so it is eight bytes
    r.out(ACP_CMD, ACP_SCALE);
    for (int i = 0; i < 3; i++) {
      CAPTURE(i);
      CHECK_EQ(getI64(r.ram, base + 32 + 8 * i), 10 * (i + 1));
    }
  }

  TEST_CASE("works element by element over a shape, with the same opcode") {
    Rig r;
    const int base = 0x5000;
    setup(r, base, ACP_I64, 2, 3);  // a 2 by 3 matrix, six elements
    for (int i = 0; i < 6; i++) {
      putI64(r.ram, base + 8 * i, i + 1);
      putI64(r.ram, base + 48 + 8 * i, 10);
    }
    r.out(ACP_CMD, ACP_ADD);
    for (int i = 0; i < 6; i++) {
      CAPTURE(i);
      CHECK_EQ(getI64(r.ram, base + 96 + 8 * i), i + 11);
    }
    // An element-wise result is 8 bytes wide, so six elements are 48 bytes.
    CHECK_EQ(r.ram[static_cast<size_t>(base + 96 + 48)], 0xaa);
  }
}

TEST_SUITE("ACP flags") {
  TEST_CASE("sets zero and negative on every operation") {
    CHECK(bit(scalarOp(ACP_SUB, 5, 5).flags(), ACP_ZERO));
    CHECK_FALSE(bit(scalarOp(ACP_SUB, 5, 5).flags(), ACP_NEGATIVE));
    CHECK(bit(scalarOp(ACP_SUB, 2, 5).flags(), ACP_NEGATIVE));
    CHECK_FALSE(bit(scalarOp(ACP_SUB, 2, 5).flags(), ACP_ZERO));
  }

  TEST_CASE("sets zero only when every element is zero") {
    Rig r;
    const int base = 0x6000;
    setup(r, base, ACP_I64, 1, 2);
    putI64(r.ram, base, 0);
    putI64(r.ram, base + 8, 1);
    putI64(r.ram, base + 16, 0);
    putI64(r.ram, base + 24, 0);
    r.out(ACP_CMD, ACP_ADD);  // gives [0, 1]
    CHECK_FALSE(bit(r.flags(), ACP_ZERO));
  }

  TEST_CASE("sets divzero and writes zero for an integer divide by zero") {
    Rig r = scalarOp(ACP_DIV, 7, 0);
    CHECK(bit(r.flags(), ACP_DIVZERO));
    CHECK_EQ(getI64(r.ram, 0x1010), 0);
  }

  TEST_CASE("sets overflow when a sum leaves the signed range") {
    Rig r = scalarOp(ACP_ADD, I64_MAX, 1);
    CHECK(bit(r.flags(), ACP_OVERFLOW));
    CHECK_EQ(getI64(r.ram, 0x1010), I64_MIN);
    Rig u = scalarOp(ACP_ADD, -1, 1, ACP_U64);
    CHECK(bit(u.flags(), ACP_OVERFLOW));  // 2^64 does not fit either
    CHECK(bit(u.flags(), ACP_ZERO));
  }

  TEST_CASE("sets baddim for a dimension of zero, and writes nothing") {
    Rig r;
    const int base = 0x7000;
    setup(r, base, ACP_I64, 0, 4);
    r.out(ACP_CMD, ACP_ADD);
    CHECK(bit(r.flags(), ACP_BADDIM));
    CHECK_EQ(r.ram[base], 0xaa);
  }

  TEST_CASE("clears the result bits when the operation was rejected") {
    Rig r;
    const int base = 0x7100;
    setup(r, base, ACP_I64, 0, 1);
    r.out(ACP_CMD, ACP_ADD);
    CHECK_FALSE(bit(r.flags(), ACP_ZERO));
    CHECK_FALSE(bit(r.flags(), ACP_NEGATIVE));
  }

  TEST_CASE("replaces the flags on every command rather than accumulating them") {
    Rig r;
    const int base = 0x7200;
    setup(r, base, ACP_I64);
    putI64(r.ram, base, 7);
    putI64(r.ram, base + 8, 0);
    r.out(ACP_CMD, ACP_DIV);
    CHECK(bit(r.flags(), ACP_DIVZERO));
    putI64(r.ram, base + 8, 1);
    r.out(ACP_CMD, ACP_DIV);
    CHECK_FALSE_MESSAGE(bit(r.flags(), ACP_DIVZERO), "a stale flag survived");
  }

  TEST_CASE("refuses an unknown command with badfmt rather than doing nothing quietly") {
    Rig r;
    const int base = 0x7300;
    setup(r, base, ACP_I64);
    r.out(ACP_CMD, 0xfe);
    CHECK(bit(r.flags(), ACP_BADFMT));
  }

  TEST_CASE("refuses an unknown element type with badfmt") {
    Rig r;
    setup(r, 0x7400, 9);
    r.out(ACP_CMD, ACP_ADD);
    CHECK(bit(r.flags(), ACP_BADFMT));
  }

  TEST_CASE("powers on with everything cleared") {
    Rig r;
    setup(r, 0x7500, ACP_F64, 2, 2);
    r.out(ACP_CMD, 0xfe);
    r.acp->powerOn();
    CHECK_EQ(r.flags(), 0);
    // Back to 1 by 1 integers at address 0: a multiply widens to 16 bytes.
    putI64(r.ram, 0, 3);
    putI64(r.ram, 8, 5);
    r.out(ACP_CMD, ACP_MUL);
    CHECK_EQ(getU128(r.ram, 16), U128{0, 15});
  }
}

// acp_ports.h publishes the names the assembler resolves and acp.h holds the
// numbers the device switches on. Each table entry must find its enum at the
// same value, and each enum its table entry. Neither can then drift.
TEST_SUITE("the ACP tables and the enums") {
  struct Named {
    std::string_view name;
    int value;
  };

  void pin(std::span<const NamedValue> table, const std::vector<Named>& enums, const char* what) {
    CHECK_EQ(table.size(), enums.size());
    for (const auto& entry : table) {
      bool found = false;
      for (const auto& e : enums) {
        if (e.name != entry.name) continue;
        found = true;
        CHECK_MESSAGE(e.value == entry.value, what, " ", entry.name, " differs");
      }
      CHECK_MESSAGE(found, what, " ", entry.name, " has no enum");
    }
  }

  TEST_CASE("every port name resolves to the same number") {
    pin(PORTS,
        {{"ACP_ADDR_HI", ACP_ADDR_HI}, {"ACP_ADDR_LO", ACP_ADDR_LO}, {"ACP_FMT", ACP_FMT},
         {"ACP_RFMT", ACP_RFMT}, {"ACP_CMD", ACP_CMD}, {"ACP_FLAGS", ACP_FLAGS},
         {"ACP_ROWS", ACP_ROWS}, {"ACP_COLS", ACP_COLS}, {"ACP_COLS_B", ACP_COLS_B},
         {"ACP_ARG", ACP_ARG}, {"ACP_ARG2", ACP_ARG2}, {"ACP_FUNC", ACP_FUNC}},
        "port");
  }

  TEST_CASE("every element type resolves to the same value") {
    pin(FMTS, {{"ACP_I64", ACP_I64}, {"ACP_U64", ACP_U64}, {"ACP_F64", ACP_F64}, {"ACP_C64", ACP_C64}},
        "type");
  }

  TEST_CASE("every command resolves to the same value") {
    pin(CMDS,
        {{"ACP_ADD", ACP_ADD},           {"ACP_SUB", ACP_SUB},
         {"ACP_MUL", ACP_MUL},           {"ACP_DIV", ACP_DIV},
         {"ACP_REM", ACP_REM},           {"ACP_NEG", ACP_NEG},
         {"ACP_ABS", ACP_ABS},           {"ACP_CMP", ACP_CMP},
         {"ACP_CVT", ACP_CVT},           {"ACP_SCALE", ACP_SCALE},
         {"ACP_SQRT", ACP_SQRT},         {"ACP_SIN", ACP_SIN},
         {"ACP_COS", ACP_COS},           {"ACP_TAN", ACP_TAN},
         {"ACP_ASIN", ACP_ASIN},         {"ACP_ACOS", ACP_ACOS},
         {"ACP_ATAN", ACP_ATAN},         {"ACP_LOG", ACP_LOG},
         {"ACP_LOG10", ACP_LOG10},       {"ACP_EXP", ACP_EXP},
         {"ACP_ATAN2", ACP_ATAN2},       {"ACP_POW", ACP_POW},
         {"ACP_HYPOT", ACP_HYPOT},       {"ACP_ARG_OF", ACP_ARG_OF},
         {"ACP_CONJ", ACP_CONJ},         {"ACP_DOT", ACP_DOT},
         {"ACP_CROSS", ACP_CROSS},       {"ACP_NORM", ACP_NORM},
         {"ACP_NORMALIZE", ACP_NORMALIZE}, {"ACP_DIST", ACP_DIST},
         {"ACP_MATMUL", ACP_MATMUL},     {"ACP_MATVEC", ACP_MATVEC},
         {"ACP_TRANSPOSE", ACP_TRANSPOSE}, {"ACP_IDENTITY", ACP_IDENTITY},
         {"ACP_DET", ACP_DET},           {"ACP_INVERSE", ACP_INVERSE},
         {"ACP_GEN_TABLE", ACP_GEN_TABLE}, {"ACP_AND", ACP_AND},
         {"ACP_OR", ACP_OR},             {"ACP_XOR", ACP_XOR},
         {"ACP_NOT", ACP_NOT},           {"ACP_SHL", ACP_SHL},
         {"ACP_SHR", ACP_SHR},           {"ACP_ASR", ACP_ASR},
         {"ACP_ROL", ACP_ROL},           {"ACP_ROR", ACP_ROR}},
        "command");
  }

  TEST_CASE("every flag resolves to the same bit") {
    pin(FLAG_BITS,
        {{"ACP_ZERO", ACP_ZERO}, {"ACP_NEGATIVE", ACP_NEGATIVE}, {"ACP_DIVZERO", ACP_DIVZERO},
         {"ACP_OVERFLOW", ACP_OVERFLOW}, {"ACP_NAN", ACP_NAN}, {"ACP_BADFMT", ACP_BADFMT},
         {"ACP_SINGULAR", ACP_SINGULAR}, {"ACP_BADDIM", ACP_BADDIM}},
        "flag");
  }

  TEST_CASE("the port range covers every port and nothing more") {
    CHECK_EQ(PORT_LO, 0x40);
    CHECK_EQ(PORT_HI, 0x4f);
    for (const auto& p : PORTS) {
      CHECK_GE(p.value, PORT_LO);
      CHECK_LE(p.value, PORT_HI);
    }
  }
}

// The reference's worked example, run on the machine. The assembler resolves
// the names, the CPU drives the ports, and the device writes the machine's
// own RAM.
TEST_SUITE("the ACP on the machine") {
  TEST_CASE("a program multiplies through the device and reads the flags") {
    Assembled a = assemble(
        "        OUT ACP_ADDR_HI, blk >> 8\n"
        "        OUT ACP_ADDR_LO, blk & 255\n"
        "        OUT ACP_FMT, ACP_I64\n"
        "        OUT ACP_CMD, ACP_MUL\n"
        "        IN ACP_FLAGS -> A\n"
        "        HLT\n"
        ".ram\n"
        "blk:    db 0,0,0,0,0,0,0,7\n"
        "        db 0,0,0,0,0,0,0,6\n"
        "        db 0,0,0,0,0,0,0,0\n"
        "        db 0,0,0,0,0,0,0,0\n");
    REQUIRE(a.errors.empty());
    Acp acp;
    Machine m(a.program, buildNaive(), &acp);
    acp.attachRam(m.ram.data());
    std::copy(a.ram.begin(), a.ram.begin() + static_cast<long>(a.ramLength), m.ram.begin());
    m.run(100);
    CHECK_EQ(m.status, Status::Halted);
    const int blk = a.labels.at("blk").value;
    const std::vector<uint8_t> ram(m.ram.begin(), m.ram.end());
    CHECK_EQ(getU128(ram, blk + 16), U128{0, 42});
    CHECK_EQ(m.acc, 0);  // not zero, not negative, nothing fell out
  }

  TEST_CASE("a program sees the zero flag on a difference of equals") {
    Assembled a = assemble(
        "        OUT ACP_ADDR_HI, blk >> 8\n"
        "        OUT ACP_ADDR_LO, blk & 255\n"
        "        OUT ACP_FMT, ACP_I64\n"
        "        OUT ACP_CMD, ACP_SUB\n"
        "        IN ACP_FLAGS -> A\n"
        "        AND A <- ACP_ZERO\n"
        "        HLT\n"
        ".ram\n"
        "blk:    db 0,0,0,0,0,0,0,9\n"
        "        db 0,0,0,0,0,0,0,9\n"
        "        db 0,0,0,0,0,0,0,0\n");
    REQUIRE(a.errors.empty());
    Acp acp;
    Machine m(a.program, buildNaive(), &acp);
    acp.attachRam(m.ram.data());
    std::copy(a.ram.begin(), a.ram.begin() + static_cast<long>(a.ramLength), m.ram.begin());
    m.run(100);
    CHECK_EQ(m.status, Status::Halted);
    CHECK_EQ(m.acc, ACP_ZERO);
  }
}
