// The arithmetic coprocessor. The fourth device on the bus, and the first
// that WRITES data RAM. It claims $40 to $4F and forwards the rest. One
// command reads two operands and writes one result, through a block of data
// RAM the program nominates. See docs/design/acp-design.md for the block,
// the shapes and the flags, and acp_ports.h for the numbers.
//
// DESIGN: one command is one cycle, like a GPU command. A 4 by 4 matrix
// product in one cycle is a larger cheat than any the GPU makes. It is the
// same cheat: the CPU stays 8 bit and honest, and the magic box does the
// work the CPU cannot.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "devices/acp_ports.h"
#include "devices/device.h"

namespace sc8::acp {

// The numbers the implementation switches on. acp_ports.h publishes the
// same numbers as name tables for the assembler. A test walks those tables
// against these enums, so the two cannot drift. The names keep their ACP_
// prefix: ACP_NAN cannot be spelled NAN, which <cmath> takes as a macro.
enum Port : uint8_t {
  ACP_ADDR_HI = 0x40,
  ACP_ADDR_LO = 0x41,
  ACP_FMT = 0x42,
  ACP_RFMT = 0x43,
  ACP_CMD = 0x44,
  ACP_FLAGS = 0x45,
  ACP_ROWS = 0x46,
  ACP_COLS = 0x47,
  ACP_COLS_B = 0x48,
  ACP_ARG = 0x49,
  ACP_ARG2 = 0x4a,
  ACP_FUNC = 0x4b,
};

enum Fmt : uint8_t {
  ACP_I64 = 0,
  ACP_U64 = 1,
  ACP_F64 = 2,
  ACP_C64 = 3,
};

enum Cmd : uint8_t {
  ACP_ADD = 0x01,
  ACP_SUB = 0x02,
  ACP_MUL = 0x03,
  ACP_DIV = 0x04,
  ACP_REM = 0x05,
  ACP_NEG = 0x06,
  ACP_ABS = 0x07,
  ACP_CMP = 0x08,
  ACP_CVT = 0x09,
  ACP_SCALE = 0x0a,
  ACP_SQRT = 0x10,
  ACP_SIN = 0x11,
  ACP_COS = 0x12,
  ACP_TAN = 0x13,
  ACP_ASIN = 0x14,
  ACP_ACOS = 0x15,
  ACP_ATAN = 0x16,
  ACP_LOG = 0x17,
  ACP_LOG10 = 0x18,
  ACP_EXP = 0x19,
  ACP_ATAN2 = 0x1a,
  ACP_POW = 0x1b,
  ACP_HYPOT = 0x1c,
  ACP_ARG_OF = 0x20,
  ACP_CONJ = 0x21,
  ACP_DOT = 0x30,
  ACP_CROSS = 0x31,
  ACP_NORM = 0x32,
  ACP_NORMALIZE = 0x33,
  ACP_DIST = 0x34,
  ACP_MATMUL = 0x40,
  ACP_MATVEC = 0x41,
  ACP_TRANSPOSE = 0x42,
  ACP_IDENTITY = 0x43,
  ACP_DET = 0x44,
  ACP_INVERSE = 0x45,
  ACP_GEN_TABLE = 0x50,
  ACP_AND = 0x60,
  ACP_OR = 0x61,
  ACP_XOR = 0x62,
  ACP_NOT = 0x63,
  ACP_SHL = 0x64,
  ACP_SHR = 0x65,
  ACP_ASR = 0x66,
  ACP_ROL = 0x67,
  ACP_ROR = 0x68,
};

enum Flag : uint8_t {
  ACP_ZERO = 1 << 0,
  ACP_NEGATIVE = 1 << 1,
  ACP_DIVZERO = 1 << 2,
  ACP_OVERFLOW = 1 << 3,
  ACP_NAN = 1 << 4,
  ACP_BADFMT = 1 << 5,
  ACP_SINGULAR = 1 << 6,
  ACP_BADDIM = 1 << 7,
};

// The three element kinds an operation can accept. Two formats are integers,
// so a kind is coarser than a format.
enum class Kind { Int, Float, Complex };

struct Dim {
  int rows;
  int cols;
  bool operator==(const Dim&) const = default;
};

// The shapes an operation gives B and the result, from A's shape. No B means
// B is not read and takes no room in the block, so the result moves up.
struct Shapes {
  std::optional<Dim> b;
  Dim r;
};

// A signed integer of 192 bits in two's complement, wide enough for every
// exact intermediate the device forms: a 65 bit sum, a 128 bit product, and
// a dot product of 65025 such products. The TypeScript uses BigInt for the
// same job. Only the writer narrows, and it reports what did not fit.
struct Wide {
  uint64_t limb[3]{};  // least significant first

  static Wide fromI64(int64_t v);
  static Wide fromU64(uint64_t v);
  // An integer valued double of magnitude below 2^190, exactly.
  static Wide fromDouble(double t);

  bool negative() const { return (limb[2] >> 63) != 0; }
  bool isZero() const { return limb[0] == 0 && limb[1] == 0 && limb[2] == 0; }
  uint64_t low64() const { return limb[0]; }
  uint64_t bits64(int i) const { return limb[i]; }

  Wide operator+(const Wide& o) const;
  Wide operator-(const Wide& o) const;
  Wide neg() const;
  Wide shl(int n) const;
  int compare(const Wide& o) const;

  // Operands of magnitude below 2^64, which every value read from RAM is.
  static Wide mul(const Wide& a, const Wide& b);
  static Wide div(const Wide& a, const Wide& b);  // b nonzero, toward zero
  static Wide rem(const Wide& a, const Wide& b);  // b nonzero, sign of a

  // Whether the value survives BigInt.asIntN or asUintN at that width.
  bool fitsSigned(int bits) const;
  bool fitsUnsigned(int bits) const;
  // Rounded to nearest even, as Number(bigint) does.
  double toDouble() const;
};

struct Cx {
  double re;
  double im;
};

// An element in flight. Integers are Wide so 64 bits and the 128 bit product
// are exact. Floats are doubles, which is what the spec promises.
struct Val {
  enum class Tag { Int, Float, Complex } tag = Tag::Int;
  Wide i{};
  double f = 0;
  Cx c{0, 0};

  static Val ofInt(Wide w) { Val v; v.tag = Tag::Int; v.i = w; return v; }
  static Val ofFloat(double d) { Val v; v.tag = Tag::Float; v.f = d; return v; }
  static Val ofCx(Cx c) { Val v; v.tag = Tag::Complex; v.c = c; return v; }
};

}  // namespace sc8::acp

namespace sc8 {

// The two rules the device decides with, for a reference to read rather than
// restate. acpShapesFor answers nothing for a shape the command refuses,
// which is what ACP_BADDIM means. acpKindsFor lists the element kinds it
// takes; an unknown command takes every kind and then fails on its shape.
std::optional<acp::Shapes> acpShapesFor(uint8_t cmd, int rows, int cols, int colsB);
std::vector<acp::Kind> acpKindsFor(uint8_t cmd);

class Acp : public ChainedDevice {
 public:
  explicit Acp(IoBus* fallback = nullptr);

  // DESIGN: the shape powers on 1 by 1, so a program doing scalar arithmetic
  // never writes a dimension port. The block address does not default to
  // anything useful. A command before ACP_ADDR is set works on address 0,
  // which is the zero page. That is the program's business: no data address
  // is illegal on this machine.
  void powerOn();

  // The machine's data RAM, RAM_SIZE bytes. The owner hands it over after
  // every new Machine, the way it hands the GPU its text mode reader. The
  // difference is that this device writes through it.
  void attachRam(uint8_t* ram) { ram_ = ram; }

  void write(uint8_t port, uint8_t value) override;
  uint8_t read(uint8_t port) override;

 private:
  struct Work {
    std::vector<acp::Val> out;
    acp::Dim shape;
  };

  uint8_t* ram_ = nullptr;

  uint8_t addrHi_ = 0;
  uint8_t addrLo_ = 0;
  uint8_t fmt_ = acp::ACP_I64;
  uint8_t rfmt_ = acp::ACP_I64;
  int rows_ = 1;
  int cols_ = 1;
  int colsB_ = 1;
  uint8_t arg_ = 0;
  uint8_t arg2_ = 0;
  uint8_t func_ = 0;
  uint8_t flags_ = 0;
  bool pendingDivzero_ = false;
  bool pendingCarry_ = false;

  uint8_t byteAt(uint32_t at) const;
  void putByte(uint32_t at, uint8_t v);
  acp::Wide readInt(uint32_t at, bool isUnsigned) const;
  double readF64(uint32_t at) const;
  void writeInt(uint32_t at, const acp::Wide& value, int bytes);
  void writeF64(uint32_t at, double v);
  acp::Val readElem(uint32_t at, uint8_t fmt) const;
  uint32_t base() const;

  void run(uint8_t cmd);
  std::optional<Work> compute(uint8_t cmd, acp::Kind kind, const std::vector<acp::Val>& a,
                              const std::vector<acp::Val>& b, const acp::Shapes& shapes);
  acp::Val reduce(uint8_t cmd, acp::Kind kind, const std::vector<acp::Val>& a,
                  const std::vector<acp::Val>& b) const;
  std::vector<acp::Val> matmul(acp::Kind kind, const std::vector<acp::Val>& a,
                               const std::vector<acp::Val>& b, const acp::Shapes& shapes) const;
  Work table(double start, double step) const;
  std::vector<acp::Val> elementwise(uint8_t cmd, acp::Kind kind, const std::vector<acp::Val>& a,
                                    const std::vector<acp::Val>& b);
  acp::Val oneOp(uint8_t cmd, acp::Kind kind, const acp::Val& x, const acp::Val& y);
  acp::Wide fit(uint64_t u) const;
  uint64_t shift(uint8_t cmd, uint64_t u, uint64_t count);
  void zeroResult(uint8_t cmd, size_t count, acp::Dim shape, acp::Kind rkind);
  void writeAt(uint8_t cmd, const std::vector<acp::Val>& out, acp::Dim shape, acp::Kind rkind,
               uint32_t rAt);
};

}  // namespace sc8
