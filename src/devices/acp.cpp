#include "devices/acp.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sc8 {

using namespace acp;

namespace {

constexpr int WORD_BITS = 64;
constexpr uint32_t RAM_MASK = 0xffff;

// ---- the 192 bit integer ----

// Where the TypeScript writes BigInt, this writes Wide. The port differs
// in one way: BigInt is unbounded and Wide is 192 bits, which every value
// the device forms fits. See the struct's comment for the bound.

uint64_t signExt(uint64_t v) { return (v >> 63) != 0 ? ~uint64_t{0} : 0; }

}  // namespace

namespace acp {

Wide Wide::fromI64(int64_t v) {
  Wide w;
  const auto u = static_cast<uint64_t>(v);
  w.limb[0] = u;
  w.limb[1] = signExt(u);
  w.limb[2] = w.limb[1];
  return w;
}

Wide Wide::fromU64(uint64_t v) {
  Wide w;
  w.limb[0] = v;
  return w;
}

Wide Wide::operator+(const Wide& o) const {
  Wide r;
  uint64_t carry = 0;
  for (int i = 0; i < 3; i++) {
    const uint64_t s = limb[i] + o.limb[i];
    const uint64_t c1 = s < limb[i] ? 1 : 0;
    const uint64_t t = s + carry;
    const uint64_t c2 = t < s ? 1 : 0;
    r.limb[i] = t;
    carry = c1 | c2;
  }
  return r;
}

Wide Wide::neg() const {
  Wide r;
  uint64_t carry = 1;
  for (int i = 0; i < 3; i++) {
    const uint64_t t = ~limb[i] + carry;
    carry = (carry == 1 && t == 0) ? 1 : 0;
    r.limb[i] = t;
  }
  return r;
}

Wide Wide::operator-(const Wide& o) const { return *this + o.neg(); }

Wide Wide::shl(int n) const {
  Wide r;
  const int word = n / 64;
  const int bit = n % 64;
  for (int i = 2; i >= 0; i--) {
    const int src = i - word;
    if (src < 0) continue;
    uint64_t v = limb[src] << bit;
    if (bit != 0 && src > 0) v |= limb[src - 1] >> (64 - bit);
    r.limb[i] = v;
  }
  return r;
}

int Wide::compare(const Wide& o) const {
  if (negative() != o.negative()) return negative() ? -1 : 1;
  for (int i = 2; i >= 0; i--) {
    if (limb[i] != o.limb[i]) return limb[i] < o.limb[i] ? -1 : 1;
  }
  return 0;
}

namespace {

// The magnitude of a value below 2^64 in size, which the product and the
// quotient need. -2^63 has magnitude 2^63, so it fits.
uint64_t magnitude(const Wide& w) { return w.negative() ? w.neg().limb[0] : w.limb[0]; }

Wide withSign(uint64_t lo, uint64_t hi, bool negative) {
  Wide w;
  w.limb[0] = lo;
  w.limb[1] = hi;
  return negative ? w.neg() : w;
}

}  // namespace

Wide Wide::mul(const Wide& a, const Wide& b) {
  const uint64_t x = magnitude(a);
  const uint64_t y = magnitude(b);
  const uint64_t x0 = x & 0xffffffffu, x1 = x >> 32;
  const uint64_t y0 = y & 0xffffffffu, y1 = y >> 32;
  const uint64_t p00 = x0 * y0, p01 = x0 * y1, p10 = x1 * y0, p11 = x1 * y1;
  const uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
  const uint64_t lo = (p00 & 0xffffffffu) | (mid << 32);
  const uint64_t hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
  return withSign(lo, hi, a.negative() != b.negative());
}

Wide Wide::div(const Wide& a, const Wide& b) {
  return withSign(magnitude(a) / magnitude(b), 0, a.negative() != b.negative());
}

Wide Wide::rem(const Wide& a, const Wide& b) {
  return withSign(magnitude(a) % magnitude(b), 0, a.negative());
}

bool Wide::fitsSigned(int bits) const {
  if (bits == 64) {
    const uint64_t ext = signExt(limb[0]);
    return limb[1] == ext && limb[2] == ext;
  }
  return limb[2] == signExt(limb[1]);
}

bool Wide::fitsUnsigned(int bits) const {
  if (bits == 64) return limb[1] == 0 && limb[2] == 0;
  return limb[2] == 0;
}

namespace {

int topBit(const Wide& m) {
  for (int i = 2; i >= 0; i--) {
    if (m.limb[i] == 0) continue;
    int bit = 63;
    while ((m.limb[i] >> bit) == 0) bit--;
    return i * 64 + bit;
  }
  return -1;
}

}  // namespace

double Wide::toDouble() const {
  const bool sign = negative();
  const Wide m = sign ? neg() : *this;
  const int top = topBit(m);
  double d;
  if (top < 64) {
    d = static_cast<double>(m.limb[0]);
  } else {
    // Keep the top 64 bits and fold everything below into a sticky bit.
    // The hardware's one rounding to 53 bits is then the correct one.
    const int s = top - 63;
    const int word = s / 64;
    const int off = s % 64;
    uint64_t kept = m.limb[word] >> off;
    if (off != 0 && word + 1 < 3) kept |= m.limb[word + 1] << (64 - off);
    bool sticky = (off != 0) && (m.limb[word] & ((uint64_t{1} << off) - 1)) != 0;
    for (int i = 0; i < word; i++) sticky = sticky || m.limb[i] != 0;
    if (sticky) kept |= 1;
    d = std::ldexp(static_cast<double>(kept), s);
  }
  return sign ? -d : d;
}

Wide Wide::fromDouble(double t) {
  if (t == 0) return Wide{};
  int e = 0;
  const double f = std::frexp(std::fabs(t), &e);
  auto m = static_cast<uint64_t>(std::ldexp(f, 53));
  const int sh = e - 53;
  Wide w;
  if (sh >= 0) {
    w = fromU64(m).shl(sh);
  } else {
    w = fromU64(m >> -sh);  // exact: t is an integer
  }
  return t < 0 ? w.neg() : w;
}

}  // namespace acp

namespace {

std::optional<Kind> kindOf(uint8_t fmt) {
  switch (fmt) {
    case ACP_I64:
    case ACP_U64:
      return Kind::Int;
    case ACP_F64:
      return Kind::Float;
    case ACP_C64:
      return Kind::Complex;
    default:
      return std::nullopt;
  }
}

int elementBytes(uint8_t fmt) { return fmt == ACP_C64 ? 16 : 8; }

// How wide one RESULT element is. A 1 by 1 integer multiply is the single
// widening case: two 64 bit integers multiply to 128 bits exactly, and only
// a 16 byte slot holds that. Everything else is the size its type says.
int resultElementBytes(uint8_t cmd, uint8_t rfmt, int rows, int cols) {
  const bool integer = rfmt == ACP_I64 || rfmt == ACP_U64;
  if (cmd == ACP_MUL && integer && rows == 1 && cols == 1) return 16;
  return elementBytes(rfmt);
}

std::optional<Shapes> shapesFor(uint8_t cmd, int rows, int cols, int colsB) {
  const Shapes same{Dim{rows, cols}, Dim{rows, cols}};
  const Shapes unary{std::nullopt, Dim{rows, cols}};
  switch (cmd) {
    case ACP_ADD:
    case ACP_SUB:
    case ACP_MUL:
    case ACP_DIV:
    case ACP_REM:
    case ACP_CMP:
    case ACP_ATAN2:
    case ACP_POW:
    case ACP_HYPOT:
      return same;
    case ACP_AND:
    case ACP_OR:
    case ACP_XOR:
    case ACP_SHL:
    case ACP_SHR:
    case ACP_ASR:
    case ACP_ROL:
    case ACP_ROR:
      return same;
    case ACP_NOT:
    case ACP_NEG:
    case ACP_ABS:
    case ACP_CVT:
    case ACP_SQRT:
    case ACP_SIN:
    case ACP_COS:
    case ACP_TAN:
    case ACP_ASIN:
    case ACP_ACOS:
    case ACP_ATAN:
    case ACP_LOG:
    case ACP_LOG10:
    case ACP_EXP:
    case ACP_ARG_OF:
    case ACP_CONJ:
      return unary;
    case ACP_SCALE:
      return Shapes{Dim{1, 1}, Dim{rows, cols}};
    case ACP_DOT:
    case ACP_DIST:
      if (cols != 1) return std::nullopt;
      return Shapes{Dim{rows, 1}, Dim{1, 1}};
    case ACP_CROSS:
      if (rows != 3 || cols != 1) return std::nullopt;
      return Shapes{Dim{3, 1}, Dim{3, 1}};
    case ACP_NORM:
      if (cols != 1) return std::nullopt;
      return Shapes{std::nullopt, Dim{1, 1}};
    case ACP_NORMALIZE:
      if (cols != 1) return std::nullopt;
      return Shapes{std::nullopt, Dim{rows, 1}};
    case ACP_MATMUL:
      if (colsB < 1) return std::nullopt;
      return Shapes{Dim{cols, colsB}, Dim{rows, colsB}};
    case ACP_MATVEC:
      return Shapes{Dim{cols, 1}, Dim{rows, 1}};
    case ACP_TRANSPOSE:
      return Shapes{std::nullopt, Dim{cols, rows}};
    case ACP_IDENTITY:
      return Shapes{std::nullopt, Dim{rows, rows}};
    case ACP_DET:
      if (rows != cols) return std::nullopt;
      return Shapes{std::nullopt, Dim{1, 1}};
    case ACP_INVERSE:
      if (rows != cols) return std::nullopt;
      return Shapes{std::nullopt, Dim{rows, cols}};
    case ACP_GEN_TABLE:
      // A and B are one number each. The result runs for `count` entries,
      // which the table fills in once it knows the count.
      return Shapes{Dim{1, 1}, Dim{1, 1}};
    default:
      return std::nullopt;
  }
}

std::vector<Kind> kindsFor(uint8_t cmd) {
  switch (cmd) {
    // A bit pattern is what an integer is. A double has bits too, but they
    // are an exponent and a mantissa. Shifting them is not an operation
    // anybody means to ask for.
    case ACP_AND:
    case ACP_OR:
    case ACP_XOR:
    case ACP_NOT:
    case ACP_SHL:
    case ACP_SHR:
    case ACP_ASR:
    case ACP_ROL:
    case ACP_ROR:
      return {Kind::Int};
    case ACP_REM:  // a complex remainder is not a thing
    case ACP_CMP:  // complex numbers do not order
      return {Kind::Int, Kind::Float};
    case ACP_SQRT:
    case ACP_SIN:
    case ACP_COS:
    case ACP_TAN:
    case ACP_ASIN:
    case ACP_ACOS:
    case ACP_ATAN:
    case ACP_LOG:
    case ACP_LOG10:
    case ACP_EXP:
    case ACP_ATAN2:
    case ACP_POW:
    case ACP_HYPOT:
    case ACP_NORM:
    case ACP_NORMALIZE:
    case ACP_DIST:
    case ACP_DET:
    case ACP_INVERSE:
    case ACP_GEN_TABLE:
      return {Kind::Float};
    case ACP_ARG_OF:
    case ACP_CONJ:
      return {Kind::Complex};
    default:
      return {Kind::Int, Kind::Float, Kind::Complex};
  }
}

bool knownCommand(uint8_t cmd) {
  for (const auto& c : CMDS) {
    if (c.value == cmd) return true;
  }
  return false;
}

// ---- complex arithmetic, kept together so the formulas are in one place ----

// Plain formulas, as the TypeScript writes them. std::complex is not used
// because its operator* recovers infinities the way C99 Annex G asks. That
// would change the bits on an operand with an infinite part.

Cx cxAdd(Cx a, Cx b) { return {a.re + b.re, a.im + b.im}; }
Cx cxSub(Cx a, Cx b) { return {a.re - b.re, a.im - b.im}; }
Cx cxMul(Cx a, Cx b) { return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re}; }

// DESIGN: Smith's method, not the textbook formula. The naive
// (ac+bd)/(cc+dd) overflows to infinity when c or d squares past the
// double's range. This does not. The test that separates them uses an
// operand near 1e200.
Cx cxDiv(Cx a, Cx b) {
  if (std::fabs(b.re) >= std::fabs(b.im)) {
    const double r = b.im / b.re;
    const double d = b.re + b.im * r;
    return {(a.re + a.im * r) / d, (a.im - a.re * r) / d};
  }
  const double r = b.re / b.im;
  const double d = b.re * r + b.im;
  return {(a.re * r + a.im) / d, (a.im * r - a.re) / d};
}

double cxAbs(Cx a) { return std::hypot(a.re, a.im); }
double cxArg(Cx a) { return std::atan2(a.im, a.re); }

// Math.pow, which differs from C's pow in two corners: a NaN exponent is
// NaN even on a base of one, and one to an infinite power is NaN too. C
// answers one for both. The JavaScript answers are kept because the
// reference is the JavaScript.
double hostPow(double x, double y) {
  if (std::isnan(y)) return y;
  if (std::fabs(x) == 1 && std::isinf(y)) return std::nan("");
  return std::pow(x, y);
}

// Math.hypot over a whole vector, which C++ has no spelling for. This is
// V8's algorithm: scale by the largest magnitude, sum the squares with
// Kahan compensation, and scale back. Two operand calls use std::hypot.
double hostHypot(const std::vector<double>& v) {
  double max = 0;
  bool anyNan = false;
  for (double x : v) {
    const double m = std::fabs(x);
    if (std::isinf(m)) return m;
    if (std::isnan(m)) anyNan = true;
    if (m > max) max = m;
  }
  if (anyNan) return std::nan("");
  if (max == 0) return 0;
  double sum = 0;
  double compensation = 0;
  for (double x : v) {
    const double n = std::fabs(x) / max;
    const double summand = n * n - compensation;
    const double preliminary = sum + summand;
    compensation = (preliminary - sum) - summand;
    sum = preliminary;
  }
  return std::sqrt(sum) * max;
}

// The transcendentals, in one place so ACP_GEN_TABLE and the element-wise
// path cannot disagree about what a function means.
double applyUnaryFloat(uint8_t cmd, double p) {
  switch (cmd) {
    case ACP_SQRT: return std::sqrt(p);
    case ACP_SIN: return std::sin(p);
    case ACP_COS: return std::cos(p);
    case ACP_TAN: return std::tan(p);
    case ACP_ASIN: return std::asin(p);
    case ACP_ACOS: return std::acos(p);
    case ACP_ATAN: return std::atan(p);
    case ACP_LOG: return std::log(p);
    case ACP_LOG10: return std::log10(p);
    case ACP_EXP: return std::exp(p);
    default: return p;
  }
}

size_t idx(int r, int c, int n) { return static_cast<size_t>(r * n + c); }

// DESIGN: cofactors up to 3 by 3, because they are exact and short. Above
// that, LU by Gaussian elimination with partial pivoting. That is the
// readable choice and the one the tests' reference uses.
double det(const std::vector<double>& m, int n) {
  if (n == 1) return m[0];
  if (n == 2) return m[0] * m[3] - m[1] * m[2];
  if (n == 3) {
    return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
           m[2] * (m[3] * m[7] - m[4] * m[6]);
  }
  std::vector<double> a = m;
  double sign = 1;
  for (int col = 0; col < n; col++) {
    int pivot = col;
    for (int r = col + 1; r < n; r++) {
      if (std::fabs(a[idx(r, col, n)]) > std::fabs(a[idx(pivot, col, n)])) pivot = r;
    }
    if (a[idx(pivot, col, n)] == 0) return 0;
    if (pivot != col) {
      for (int c = 0; c < n; c++) std::swap(a[idx(col, c, n)], a[idx(pivot, c, n)]);
      sign = -sign;
    }
    for (int r = col + 1; r < n; r++) {
      const double f = a[idx(r, col, n)] / a[idx(col, col, n)];
      for (int c = col; c < n; c++) a[idx(r, c, n)] = a[idx(r, c, n)] - f * a[idx(col, c, n)];
    }
  }
  double d = sign;
  for (int i = 0; i < n; i++) d *= a[idx(i, i, n)];
  return d;
}

// Gauss-Jordan with partial pivoting. Nothing means singular, which the
// caller turns into ACP_SINGULAR and a zeroed result.
std::optional<std::vector<double>> inverse(const std::vector<double>& m, int n) {
  std::vector<double> a = m;
  std::vector<double> inv(static_cast<size_t>(n * n), 0);
  for (int i = 0; i < n; i++) inv[idx(i, i, n)] = 1;
  for (int col = 0; col < n; col++) {
    int pivot = col;
    for (int r = col + 1; r < n; r++) {
      if (std::fabs(a[idx(r, col, n)]) > std::fabs(a[idx(pivot, col, n)])) pivot = r;
    }
    if (a[idx(pivot, col, n)] == 0 || !std::isfinite(a[idx(pivot, col, n)])) return std::nullopt;
    if (pivot != col) {
      for (int c = 0; c < n; c++) {
        std::swap(a[idx(col, c, n)], a[idx(pivot, c, n)]);
        std::swap(inv[idx(col, c, n)], inv[idx(pivot, c, n)]);
      }
    }
    const double p = a[idx(col, col, n)];
    for (int c = 0; c < n; c++) {
      a[idx(col, c, n)] = a[idx(col, c, n)] / p;
      inv[idx(col, c, n)] = inv[idx(col, c, n)] / p;
    }
    for (int r = 0; r < n; r++) {
      if (r == col) continue;
      const double f = a[idx(r, col, n)];
      if (f == 0) continue;
      for (int c = 0; c < n; c++) {
        a[idx(r, c, n)] = a[idx(r, c, n)] - f * a[idx(col, c, n)];
        inv[idx(r, c, n)] = inv[idx(r, c, n)] - f * inv[idx(col, c, n)];
      }
    }
  }
  return inv;
}

std::vector<double> floats(const std::vector<Val>& v) {
  std::vector<double> out;
  out.reserve(v.size());
  for (const Val& x : v) out.push_back(x.f);
  return out;
}

Val one(Kind kind, bool set) {
  if (kind == Kind::Int) return Val::ofInt(Wide::fromI64(set ? 1 : 0));
  if (kind == Kind::Float) return Val::ofFloat(set ? 1 : 0);
  return Val::ofCx({set ? 1.0 : 0.0, 0});
}

}  // namespace

std::optional<Shapes> acpShapesFor(uint8_t cmd, int rows, int cols, int colsB) {
  return shapesFor(cmd, rows, cols, colsB);
}

std::vector<Kind> acpKindsFor(uint8_t cmd) { return kindsFor(cmd); }

// ---- the device ----

Acp::Acp(IoBus* fallback) : ChainedDevice(fallback) { powerOn(); }

void Acp::powerOn() {
  addrHi_ = 0;
  addrLo_ = 0;
  fmt_ = ACP_I64;
  rfmt_ = ACP_I64;
  rows_ = 1;
  cols_ = 1;
  colsB_ = 1;
  arg_ = 0;
  arg2_ = 0;
  func_ = 0;
  flags_ = 0;
  pendingDivzero_ = false;
  pendingCarry_ = false;
}

void Acp::write(uint8_t port, uint8_t value) {
  if (port < PORT_LO || port > PORT_HI) {
    fallback_->write(port, value);
    return;
  }
  switch (port) {
    case ACP_ADDR_HI: addrHi_ = value; break;
    case ACP_ADDR_LO: addrLo_ = value; break;
    // Writing the operand type sets the result type with it. One OUT covers
    // the common case, and ACP_RFMT afterwards covers a conversion.
    case ACP_FMT: fmt_ = value; rfmt_ = value; break;
    case ACP_RFMT: rfmt_ = value; break;
    case ACP_ROWS: rows_ = value; break;
    case ACP_COLS: cols_ = value; break;
    case ACP_COLS_B: colsB_ = value; break;
    case ACP_ARG: arg_ = value; break;
    case ACP_ARG2: arg2_ = value; break;
    case ACP_FUNC: func_ = value; break;
    case ACP_CMD: run(value); break;
    default: break;  // claimed but unused
  }
}

uint8_t Acp::read(uint8_t port) {
  if (port < PORT_LO || port > PORT_HI) return fallback_->read(port);
  if (port == ACP_FLAGS) return flags_;
  return 0;
}

// ---- RAM, byte by byte, wrapping at 16 bits the way an address does ----

uint8_t Acp::byteAt(uint32_t at) const { return ram_ ? ram_[at & RAM_MASK] : 0; }

void Acp::putByte(uint32_t at, uint8_t v) {
  if (ram_) ram_[at & RAM_MASK] = v;
}

Wide Acp::readInt(uint32_t at, bool isUnsigned) const {
  uint64_t v = 0;
  for (uint32_t i = 0; i < 8; i++) v = (v << 8) | byteAt(at + i);
  return isUnsigned ? Wide::fromU64(v) : Wide::fromI64(static_cast<int64_t>(v));
}

double Acp::readF64(uint32_t at) const {
  uint64_t bits = 0;
  for (uint32_t i = 0; i < 8; i++) bits = (bits << 8) | byteAt(at + i);
  double d;
  std::memcpy(&d, &bits, sizeof d);
  return d;
}

void Acp::writeInt(uint32_t at, const Wide& value, int bytes) {
  for (int i = 0; i < bytes; i++) {
    const int j = bytes - 1 - i;  // byte j of the value lands last
    const uint64_t limb = value.bits64(j / 8);
    putByte(at + static_cast<uint32_t>(i), static_cast<uint8_t>(limb >> (8 * (j % 8))));
  }
}

void Acp::writeF64(uint32_t at, double v) {
  uint64_t bits;
  std::memcpy(&bits, &v, sizeof bits);
  for (uint32_t i = 0; i < 8; i++) putByte(at + i, static_cast<uint8_t>(bits >> (56 - 8 * i)));
}

Val Acp::readElem(uint32_t at, uint8_t fmt) const {
  switch (fmt) {
    case ACP_I64: return Val::ofInt(readInt(at, false));
    case ACP_U64: return Val::ofInt(readInt(at, true));
    case ACP_F64: return Val::ofFloat(readF64(at));
    default: return Val::ofCx({readF64(at), readF64(at + 8)});
  }
}

uint32_t Acp::base() const {
  return (static_cast<uint32_t>(addrHi_) << 8 | addrLo_) & RAM_MASK;
}

// ---- the one entry point ----

void Acp::run(uint8_t cmd) {
  const auto kind = kindOf(fmt_);
  const auto rkind = kindOf(rfmt_);
  if (!kind || !rkind) {
    flags_ = ACP_BADFMT;
    return;
  }
  if (rows_ < 1 || cols_ < 1) {
    flags_ = ACP_BADDIM;
    return;
  }
  const auto kinds = kindsFor(cmd);
  if (std::find(kinds.begin(), kinds.end(), *kind) == kinds.end()) {
    flags_ = ACP_BADFMT;
    return;
  }
  const auto shapes = shapesFor(cmd, rows_, cols_, colsB_);
  if (!shapes) {
    // An unknown command has no shapes either, so the two are told apart by
    // whether the command is one this device knows at all.
    flags_ = knownCommand(cmd) ? ACP_BADDIM : ACP_BADFMT;
    return;
  }

  const auto elem = static_cast<uint32_t>(elementBytes(fmt_));
  const uint32_t at = base();
  const auto aCount = static_cast<uint32_t>(rows_ * cols_);
  const auto bCount = shapes->b ? static_cast<uint32_t>(shapes->b->rows * shapes->b->cols) : 0;
  const uint32_t bAt = at + aCount * elem;
  const uint32_t rAt = bAt + bCount * elem;

  // DESIGN: both operands are read in full before anything is written. A
  // result region overlapping an operand is then defined, not undefined.
  std::vector<Val> a;
  a.reserve(aCount);
  for (uint32_t i = 0; i < aCount; i++) a.push_back(readElem(at + i * elem, fmt_));
  std::vector<Val> b;
  b.reserve(bCount);
  for (uint32_t i = 0; i < bCount; i++) b.push_back(readElem(bAt + i * elem, fmt_));

  const auto work = compute(cmd, *kind, a, b, *shapes);
  if (!work) return;  // compute set the flags itself
  writeAt(cmd, work->out, work->shape, *rkind, rAt);
}

// ---- computing ----

std::optional<Acp::Work> Acp::compute(uint8_t cmd, Kind kind, const std::vector<Val>& a,
                                      const std::vector<Val>& b, const Shapes& shapes) {
  switch (cmd) {
    case ACP_DOT:
    case ACP_DIST:
    case ACP_NORM:
      return Work{{reduce(cmd, kind, a, b)}, Dim{1, 1}};
    case ACP_NORMALIZE: {
      const double len = hostHypot(floats(a));
      if (len == 0) {
        zeroResult(cmd, a.size(), shapes.r, *kindOf(rfmt_));
        flags_ = static_cast<uint8_t>(flags_ | ACP_DIVZERO);
        return std::nullopt;
      }
      std::vector<Val> out;
      for (const Val& v : a) out.push_back(Val::ofFloat(v.f / len));
      return Work{out, shapes.r};
    }
    case ACP_CROSS: {
      // The TypeScript casts to number[] and lets the formula run on
      // whatever it was given. That is exact on BigInt and NaN on a complex
      // pair. Here the complex case takes the complex formula instead: the
      // spec says the command takes any type, and NaN is not a product.
      if (kind == Kind::Int) {
        const Wide &x = a[0].i, &y = a[1].i, &z = a[2].i;
        const Wide &u = b[0].i, &v = b[1].i, &w = b[2].i;
        return Work{{Val::ofInt(Wide::mul(y, w) - Wide::mul(z, v)),
                     Val::ofInt(Wide::mul(z, u) - Wide::mul(x, w)),
                     Val::ofInt(Wide::mul(x, v) - Wide::mul(y, u))},
                    shapes.r};
      }
      if (kind == Kind::Float) {
        const double x = a[0].f, y = a[1].f, z = a[2].f;
        const double u = b[0].f, v = b[1].f, w = b[2].f;
        return Work{{Val::ofFloat(y * w - z * v), Val::ofFloat(z * u - x * w),
                     Val::ofFloat(x * v - y * u)},
                    shapes.r};
      }
      const Cx x = a[0].c, y = a[1].c, z = a[2].c;
      const Cx u = b[0].c, v = b[1].c, w = b[2].c;
      return Work{{Val::ofCx(cxSub(cxMul(y, w), cxMul(z, v))),
                   Val::ofCx(cxSub(cxMul(z, u), cxMul(x, w))),
                   Val::ofCx(cxSub(cxMul(x, v), cxMul(y, u)))},
                  shapes.r};
    }
    case ACP_MATMUL:
    case ACP_MATVEC:
      return Work{matmul(kind, a, b, shapes), shapes.r};
    case ACP_TRANSPOSE: {
      std::vector<Val> out;
      for (int c = 0; c < cols_; c++) {
        for (int r = 0; r < rows_; r++) out.push_back(a[idx(r, c, cols_)]);
      }
      return Work{out, shapes.r};
    }
    case ACP_IDENTITY: {
      std::vector<Val> out;
      for (int r = 0; r < rows_; r++) {
        for (int c = 0; c < rows_; c++) out.push_back(one(kind, r == c));
      }
      return Work{out, shapes.r};
    }
    case ACP_DET:
      return Work{{Val::ofFloat(det(floats(a), rows_))}, Dim{1, 1}};
    case ACP_INVERSE: {
      const auto inv = inverse(floats(a), rows_);
      if (!inv) {
        zeroResult(cmd, a.size(), shapes.r, *kindOf(rfmt_));
        flags_ = static_cast<uint8_t>(flags_ | ACP_SINGULAR);
        return std::nullopt;
      }
      std::vector<Val> out;
      for (double v : *inv) out.push_back(Val::ofFloat(v));
      return Work{out, shapes.r};
    }
    case ACP_GEN_TABLE:
      return table(a[0].f, b[0].f);
    default:
      return Work{elementwise(cmd, kind, a, b), shapes.r};
  }
}

// DESIGN: every sum here runs from element zero upward. Doubles do not add
// associatively, so the order is part of the contract. A test whose
// reference sums the other way fails on some operand pair and passes on the
// one you tried.
Val Acp::reduce(uint8_t cmd, Kind kind, const std::vector<Val>& a, const std::vector<Val>& b) const {
  if (cmd == ACP_NORM) {
    double sum = 0;
    for (const Val& v : a) sum += v.f * v.f;
    return Val::ofFloat(std::sqrt(sum));
  }
  if (cmd == ACP_DIST) {
    double sum = 0;
    for (size_t i = 0; i < a.size(); i++) {
      const double d = a[i].f - b[i].f;
      sum += d * d;
    }
    return Val::ofFloat(std::sqrt(sum));
  }
  // ACP_DOT, in whichever kind the operands are.
  if (kind == Kind::Int) {
    Wide sum;
    for (size_t i = 0; i < a.size(); i++) sum = sum + Wide::mul(a[i].i, b[i].i);
    return Val::ofInt(sum);
  }
  if (kind == Kind::Float) {
    double sum = 0;
    for (size_t i = 0; i < a.size(); i++) sum += a[i].f * b[i].f;
    return Val::ofFloat(sum);
  }
  Cx sum{0, 0};
  for (size_t i = 0; i < a.size(); i++) sum = cxAdd(sum, cxMul(a[i].c, b[i].c));
  return Val::ofCx(sum);
}

std::vector<Val> Acp::matmul(Kind kind, const std::vector<Val>& a, const std::vector<Val>& b,
                             const Shapes& shapes) const {
  const int m = shapes.r.rows;
  const int n = shapes.r.cols;
  const int k = cols_;
  std::vector<Val> out;
  out.reserve(static_cast<size_t>(m * n));
  for (int i = 0; i < m; i++) {
    for (int j = 0; j < n; j++) {
      // Row major throughout: A[i][x] is a[i * k + x] and B[x][j] is
      // b[x * n + j]. The sum runs from x = 0 upward, as everywhere else.
      if (kind == Kind::Int) {
        Wide s;
        for (int x = 0; x < k; x++) s = s + Wide::mul(a[idx(i, x, k)].i, b[idx(x, j, n)].i);
        out.push_back(Val::ofInt(s));
      } else if (kind == Kind::Float) {
        double s = 0;
        for (int x = 0; x < k; x++) s += a[idx(i, x, k)].f * b[idx(x, j, n)].f;
        out.push_back(Val::ofFloat(s));
      } else {
        Cx s{0, 0};
        for (int x = 0; x < k; x++) s = cxAdd(s, cxMul(a[idx(i, x, k)].c, b[idx(x, j, n)].c));
        out.push_back(Val::ofCx(s));
      }
    }
  }
  return out;
}

Acp::Work Acp::table(double start, double step) const {
  const int count = (arg_ << 8) | arg2_;
  std::vector<Val> out;
  out.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; i++) {
    out.push_back(Val::ofFloat(applyUnaryFloat(func_, start + step * static_cast<double>(i))));
  }
  return Work{out, Dim{count, 1}};
}

std::vector<Val> Acp::elementwise(uint8_t cmd, Kind kind, const std::vector<Val>& a,
                                  const std::vector<Val>& b) {
  std::vector<Val> out;
  out.reserve(a.size());
  for (size_t i = 0; i < a.size(); i++) {
    // ACP_SCALE reads one number for every element; everything else pairs up.
    const Val y = b.empty() ? Val{} : b[cmd == ACP_SCALE ? 0 : i];
    out.push_back(oneOp(cmd, kind, a[i], y));
  }
  return out;
}

Val Acp::oneOp(uint8_t cmd, Kind kind, const Val& x, const Val& y) {
  if (kind == Kind::Int) {
    const Wide& p = x.i;
    const Wide& q = y.i;
    switch (cmd) {
      case ACP_ADD: return Val::ofInt(p + q);
      case ACP_SUB: return Val::ofInt(p - q);
      case ACP_MUL:
      case ACP_SCALE: return Val::ofInt(Wide::mul(p, q));
      // DESIGN: division truncates toward zero and the remainder takes the
      // sign of the dividend. That is what BigInt's / and % do and what C
      // does. Stated here rather than left for a reader to discover.
      case ACP_DIV:
        if (q.isZero()) { pendingDivzero_ = true; return Val::ofInt(Wide{}); }
        return Val::ofInt(Wide::div(p, q));
      case ACP_REM:
        if (q.isZero()) { pendingDivzero_ = true; return Val::ofInt(Wide{}); }
        return Val::ofInt(Wide::rem(p, q));
      case ACP_NEG: return Val::ofInt(p.neg());
      case ACP_ABS: return Val::ofInt(p.negative() ? p.neg() : p);
      case ACP_CMP: return Val::ofInt(Wide::fromI64(p.compare(q)));
      case ACP_AND: return Val::ofInt(fit(p.low64() & q.low64()));
      case ACP_OR: return Val::ofInt(fit(p.low64() | q.low64()));
      case ACP_XOR: return Val::ofInt(fit(p.low64() ^ q.low64()));
      case ACP_NOT: return Val::ofInt(fit(~p.low64()));
      case ACP_SHL:
      case ACP_SHR:
      case ACP_ASR:
      case ACP_ROL:
      case ACP_ROR:
        return Val::ofInt(fit(shift(cmd, p.low64(), q.low64())));
      default: return x;  // ACP_CVT
    }
  }
  if (kind == Kind::Float) {
    const double p = x.f;
    const double q = y.f;
    switch (cmd) {
      case ACP_ADD: return Val::ofFloat(p + q);
      case ACP_SUB: return Val::ofFloat(p - q);
      case ACP_MUL:
      case ACP_SCALE: return Val::ofFloat(p * q);
      // A float divide by zero writes infinity and sets the bit. That is
      // what a float divide by zero means, and hiding it would be the
      // machine lying.
      case ACP_DIV:
        if (q == 0) pendingDivzero_ = true;
        return Val::ofFloat(p / q);
      case ACP_REM:
        if (q == 0) { pendingDivzero_ = true; return Val::ofFloat(std::nan("")); }
        return Val::ofFloat(std::fmod(p, q));
      case ACP_NEG: return Val::ofFloat(-p);
      case ACP_ABS: return Val::ofFloat(std::fabs(p));
      case ACP_CMP: return Val::ofFloat(p < q ? -1 : p > q ? 1 : 0);
      case ACP_ATAN2: return Val::ofFloat(std::atan2(p, q));
      case ACP_POW: return Val::ofFloat(hostPow(p, q));
      case ACP_HYPOT: return Val::ofFloat(std::hypot(p, q));
      case ACP_CVT: return x;
      default: return Val::ofFloat(applyUnaryFloat(cmd, p));
    }
  }
  const Cx p = x.c;
  const Cx q = y.c;
  switch (cmd) {
    case ACP_ADD: return Val::ofCx(cxAdd(p, q));
    case ACP_SUB: return Val::ofCx(cxSub(p, q));
    case ACP_MUL:
    case ACP_SCALE: return Val::ofCx(cxMul(p, q));
    case ACP_DIV:
      if (q.re == 0 && q.im == 0) {
        pendingDivzero_ = true;
        return Val::ofCx({p.re / 0.0, p.im / 0.0});
      }
      return Val::ofCx(cxDiv(p, q));
    case ACP_NEG: return Val::ofCx({-p.re, -p.im});
    case ACP_ABS: return Val::ofFloat(cxAbs(p));
    case ACP_ARG_OF: return Val::ofFloat(cxArg(p));
    case ACP_CONJ: return Val::ofCx({p.re, -p.im});
    default: return x;  // ACP_CVT
  }
}

// DESIGN: a bitwise result comes back in the RESULT format's own signedness.
// The writer fits every integer to its width and calls a value that changed
// an OVERFLOW. Handed a raw unsigned pattern, it would call NOT 0 on an
// ACP_I64 an overflow while quietly writing the right bytes. The bits
// are the same either way. Only the label differs.
Wide Acp::fit(uint64_t u) const {
  return rfmt_ == ACP_U64 ? Wide::fromU64(u) : Wide::fromI64(static_cast<int64_t>(u));
}

// DESIGN: ACP_OVERFLOW is the bit that fell out, in both directions, and
// that is the owner's decision. It makes a shift the bit test the CPU
// cannot do on a 64 bit value: shift by one, read one flag. It is the LAST
// bit expelled and not "any bit was lost". Shifting 0b110 right by one
// reports the 0 and by two reports the 1.
//
// The count is read as an unsigned magnitude, so a shift is total: every
// count has an answer and none of them is an error. At or past the width a
// shift empties the register and a rotate wraps. That is the arithmetic
// rather than a special case.
uint64_t Acp::shift(uint8_t cmd, uint64_t u, uint64_t count) {
  const uint64_t W = WORD_BITS;
  const bool rotate = cmd == ACP_ROL || cmd == ACP_ROR;
  const uint64_t n = rotate ? count % W : std::min(count, W);
  const bool sign = (u >> (W - 1)) != 0;
  // Which bit leaves last. Nothing leaves on a count of zero.
  const auto bitAt = [u](uint64_t i) { return ((u >> i) & 1) == 1; };
  // Nothing left the register, so nothing is reported. It must not CLEAR
  // the flag either. Over a shape the bit is an OR across the elements, as
  // a result that did not fit already is.
  if (n == 0) return u;
  const auto expelled = [this](bool yes) { pendingCarry_ = pendingCarry_ || yes; };
  // A shift by the whole width is not a C++ shift, so it is spelled out.
  const bool whole = n >= W;
  switch (cmd) {
    case ACP_SHL:
      expelled(bitAt(W - n));
      return whole ? 0 : u << n;
    case ACP_SHR:
      expelled(bitAt(n - 1));
      return whole ? 0 : u >> n;
    case ACP_ASR:
      // At or past the width every bit expelled is the sign, and so is the
      // whole result: 0 for a positive, all ones for a negative.
      expelled(whole ? sign : bitAt(n - 1));
      if (whole) return sign ? ~uint64_t{0} : 0;
      return (u >> n) | (sign ? ~uint64_t{0} << (W - n) : 0);
    case ACP_ROL:
      expelled(bitAt(W - n));
      return (u << n) | (u >> (W - n));
    default:  // ACP_ROR
      expelled(bitAt(n - 1));
      return (u >> n) | (u << (W - n));
  }
}

// ---- writing, converting, and the flags ----

// A refused operation still writes zeros over its result region. The block
// then never holds a stale answer beside a fault flag.
void Acp::zeroResult(uint8_t cmd, size_t count, Dim shape, Kind rkind) {
  const auto shapes = shapesFor(cmd, rows_, cols_, colsB_);
  const auto elem = static_cast<uint32_t>(elementBytes(fmt_));
  const auto bCount = shapes->b ? static_cast<uint32_t>(shapes->b->rows * shapes->b->cols) : 0;
  const uint32_t rAt = base() + static_cast<uint32_t>(rows_ * cols_) * elem + bCount * elem;
  writeAt(cmd, std::vector<Val>(count, Val::ofFloat(0)), shape, rkind, rAt);
}

void Acp::writeAt(uint8_t cmd, const std::vector<Val>& out, Dim shape, Kind rkind, uint32_t rAt) {
  const int rElem = resultElementBytes(cmd, rfmt_, shape.rows, shape.cols);
  bool overflow = false;
  bool nan = false;
  bool allZero = true;
  bool negative = false;

  for (size_t i = 0; i < out.size(); i++) {
    const Val& v = out[i];
    const uint32_t at = rAt + static_cast<uint32_t>(i) * static_cast<uint32_t>(rElem);
    if (rkind == Kind::Int) {
      Wide big;
      bool huge = false;
      if (v.tag == Val::Tag::Int) {
        big = v.i;
      } else {
        const double r = v.tag == Val::Tag::Complex ? v.c.re : v.f;
        if (!std::isfinite(r)) {
          nan = nan || std::isnan(r);
          overflow = overflow || !std::isnan(r);
        } else {
          // DESIGN: a float to integer conversion truncates toward zero.
          // Rounding toward negative infinity is the other defensible
          // answer. The choice is written down rather than left for a
          // reader to find with a negative operand.
          const double t = std::trunc(r);
          // Past 2^190 the value is a multiple of 2^128, so its low bits
          // are zero and it cannot fit. Wide never needs to hold it.
          if (std::fabs(t) >= std::ldexp(1.0, 190)) huge = true;
          else big = Wide::fromDouble(t);
        }
      }
      const int bits = rElem * 8;
      const bool isUnsigned = rfmt_ == ACP_U64;
      const bool fits = !huge && (isUnsigned ? big.fitsUnsigned(bits) : big.fitsSigned(bits));
      if (!fits) overflow = true;
      // The fitted value is the low `bits` of big, in two's complement.
      bool zero = big.limb[0] == 0 && (bits == 64 || big.limb[1] == 0);
      if (!zero) allZero = false;
      const uint64_t topLimb = bits == 64 ? big.limb[0] : big.limb[1];
      if (!isUnsigned && (topLimb >> 63) != 0) negative = true;
      writeInt(at, big, rElem);
    } else if (rkind == Kind::Float) {
      const double r = v.tag == Val::Tag::Int ? v.i.toDouble() : v.tag == Val::Tag::Complex ? v.c.re : v.f;
      if (std::isnan(r)) nan = true;
      if (r != 0) allZero = false;
      if (r < 0) negative = true;
      writeF64(at, r);
    } else {
      const Cx c = v.tag == Val::Tag::Complex ? v.c
                   : Cx{v.tag == Val::Tag::Int ? v.i.toDouble() : v.f, 0};
      if (std::isnan(c.re) || std::isnan(c.im)) nan = true;
      if (c.re != 0 || c.im != 0) allZero = false;
      writeF64(at, c.re);
      writeF64(at + 8, c.im);
    }
  }

  int flags = 0;
  if (allZero) flags |= ACP_ZERO;
  // A sign belongs to one number. An aggregate has no single sign. The bit
  // is set only for a 1 by 1 result rather than guessed from element 0.
  if (out.size() == 1 && negative) flags |= ACP_NEGATIVE;
  if (pendingDivzero_) flags |= ACP_DIVZERO;
  // OVERFLOW carries two meanings, and they do not collide. On a shift or
  // a rotate it is the bit that fell out. That is the owner's decision and
  // the reason the family is usable as a bit test. Everywhere else it is a
  // result that did not fit the format. A bitwise result always fits, so
  // only one of the two can ever be true of one command.
  if (overflow || pendingCarry_) flags |= ACP_OVERFLOW;
  if (nan) flags |= ACP_NAN;
  pendingDivzero_ = false;
  pendingCarry_ = false;
  flags_ = static_cast<uint8_t>(flags);
}

}  // namespace sc8
