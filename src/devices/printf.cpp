#include "devices/printf.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

namespace sc8 {

namespace {

constexpr uint8_t PCT = 0x25;  // '%'

constexpr std::string_view INT_CONVS = "diuoxXb";
constexpr std::string_view FLOAT_CONVS = "fFeEgG";

struct Flags {
  bool minus = false;  // left justify
  bool plus = false;   // always a sign
  bool space = false;  // a space before a positive number
  bool zero = false;   // pad with zeros
  bool hash = false;   // alternate form
};

enum class Length { None, HH, H, L, LL };

struct Spec {
  Flags flags;
  int width = 0;  // 0 for none
  std::optional<int> precision;
  Length length = Length::None;
  char conv = 0;
  size_t next = 0;  // template index after the spec
};

int intBytes(Length length) {
  switch (length) {
    case Length::HH: return 1;
    case Length::H: return 2;
    case Length::L: return 4;
    case Length::LL: return 8;
    default: return 2;  // int is 16 bits
  }
}

bool isFloatConv(char c) { return FLOAT_CONVS.find(c) != std::string_view::npos; }
bool isIntConv(char c) { return INT_CONVS.find(c) != std::string_view::npos; }

std::optional<Spec> parseSpec(std::span<const uint8_t> tpl, size_t start) {
  size_t p = start;
  auto at = [&]() -> char { return p < tpl.size() ? static_cast<char>(tpl[p]) : '\0'; };
  Spec s;
  for (;;) {
    const char c = at();
    if (c == '-') s.flags.minus = true;
    else if (c == '+') s.flags.plus = true;
    else if (c == ' ') s.flags.space = true;
    else if (c == '0') s.flags.zero = true;
    else if (c == '#') s.flags.hash = true;
    else break;
    p++;
  }
  while (at() >= '0' && at() <= '9') {
    s.width = s.width * 10 + (tpl[p] - 0x30);
    p++;
  }
  if (at() == '.') {
    p++;
    int precision = 0;
    while (at() >= '0' && at() <= '9') {
      precision = precision * 10 + (tpl[p] - 0x30);
      p++;
    }
    s.precision = precision;
  }
  if (at() == 'h') {
    p++;
    if (at() == 'h') {
      s.length = Length::HH;
      p++;
    } else {
      s.length = Length::H;
    }
  } else if (at() == 'l') {
    p++;
    if (at() == 'l') {
      s.length = Length::LL;
      p++;
    } else {
      s.length = Length::L;
    }
  }
  const char conv = at();
  if (conv == '\0') return std::nullopt;
  if (conv != '%' && conv != 'c' && conv != 's' && conv != 'r' && !isIntConv(conv) && !isFloatConv(conv)) {
    return std::nullopt;
  }
  s.conv = conv;
  s.next = p + 1;
  return s;
}

// Pad a formatted body to the field width, honoring the minus and zero flags.
// The sign and any prefix stay ahead of zero padding.
std::string pad(const std::string& sign, const std::string& prefix, const std::string& digits, const Spec& s,
                bool zeroOk) {
  const std::string bare = sign + prefix + digits;
  const size_t width = static_cast<size_t>(s.width);
  if (bare.size() >= width) return bare;
  const size_t gap = width - bare.size();
  if (s.flags.minus) return bare + std::string(gap, ' ');
  if (s.flags.zero && zeroOk) return sign + prefix + std::string(gap, '0') + digits;
  return std::string(gap, ' ') + bare;
}

std::string toBase(uint64_t value, int base, bool upper) {
  if (value == 0) return "0";
  std::string out;
  const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  while (value > 0) {
    out.insert(out.begin(), digits[value % static_cast<uint64_t>(base)]);
    value /= static_cast<uint64_t>(base);
  }
  return out;
}

std::string formatInt(const Spec& s, std::span<const uint8_t> bytes) {
  uint64_t raw = 0;
  for (uint8_t b : bytes) raw = (raw << 8) | b;
  const char conv = s.conv;
  bool neg = false;
  uint64_t value = raw;
  if (conv == 'd' || conv == 'i') {
    const unsigned bits = static_cast<unsigned>(bytes.size() * 8);
    const uint64_t half = uint64_t{1} << (bits - 1);
    if (raw >= half) {
      // Two's complement magnitude. At 64 bits the modulus wraps to zero on
      // its own, which is the same subtraction.
      value = uint64_t{0} - raw;
      if (bits < 64) value &= (uint64_t{1} << bits) - 1;
      neg = true;
    }
  }
  int base = 10;
  if (conv == 'o') base = 8;
  else if (conv == 'x' || conv == 'X') base = 16;
  else if (conv == 'b') base = 2;
  std::string digits = toBase(value, base, conv == 'X');
  if (s.precision) {
    if (*s.precision == 0 && value == 0) digits = "";
    else if (digits.size() < static_cast<size_t>(*s.precision))
      digits = std::string(static_cast<size_t>(*s.precision) - digits.size(), '0') + digits;
  }
  std::string sign;
  if (neg) sign = "-";
  else if (conv == 'd' || conv == 'i') sign = s.flags.plus ? "+" : s.flags.space ? " " : "";
  std::string prefix;
  if (s.flags.hash && value != 0) {
    if (conv == 'x') prefix = "0x";
    else if (conv == 'X') prefix = "0X";
    else if (conv == 'b') prefix = "0b";
  }
  if (s.flags.hash && conv == 'o' && !(digits.size() > 0 && digits[0] == '0')) prefix = "0";
  const bool zeroOk = !s.precision.has_value();
  return pad(sign, prefix, digits, s, zeroOk);
}

std::string upperCase(std::string s) {
  for (char& c : s) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return s;
}

// Force at least two exponent digits, the C style. JavaScript's
// toExponential writes e+5 where C writes e+05.
std::string padExp(std::string s) {
  const size_t n = s.size();
  if (n >= 3) {
    const char e = s[n - 3];
    const char sign = s[n - 2];
    const char d = s[n - 1];
    if ((e == 'e' || e == 'E') && (sign == '+' || sign == '-') && d >= '0' && d <= '9') {
      s.insert(n - 1, "0");
    }
  }
  return s;
}

std::string cFormat(const char* fmt, int prec, double v) {
  char buf[512];
  std::snprintf(buf, sizeof buf, fmt, prec, v);
  return buf;
}

// JavaScript's toPrecision, built from the C exponential form. JavaScript
// switches to the exponent form below 1e-6 where C's %g switches below
// 1e-4, so %g cannot stand in for it.
std::string toPrecision(double av, int p) {
  const std::string e = cFormat("%.*e", p - 1, av);
  const size_t epos = e.find('e');
  std::string mantissa = e.substr(0, epos);
  const int exp = std::stoi(e.substr(epos + 1));
  if (exp < -6 || exp >= p) {
    return mantissa + "e" + (exp < 0 ? "-" : "+") + std::to_string(exp < 0 ? -exp : exp);
  }
  std::string digits;
  for (char c : mantissa) {
    if (c != '.') digits += c;
  }
  if (exp >= 0) {
    const size_t whole = static_cast<size_t>(exp) + 1;
    std::string out = digits.substr(0, whole);
    if (digits.size() > whole) out += "." + digits.substr(whole);
    return out;
  }
  return "0." + std::string(static_cast<size_t>(-exp - 1), '0') + digits;
}

// The general form's trim: trailing zeros go, and a dot left bare goes too.
std::string trimZeros(std::string digits) {
  const size_t epos = digits.find('e');
  std::string head = epos == std::string::npos ? digits : digits.substr(0, epos);
  const std::string tail = epos == std::string::npos ? "" : digits.substr(epos);
  while (!head.empty() && head.back() == '0') head.pop_back();
  if (!head.empty() && head.back() == '.') head.pop_back();
  return head + tail;
}

std::string formatFloat(const Spec& s, std::span<const uint8_t> bytes) {
  double v;
  if (bytes.size() == 8) {
    uint64_t bits = 0;
    for (uint8_t b : bytes) bits = (bits << 8) | b;
    std::memcpy(&v, &bits, sizeof v);
  } else {
    uint32_t bits = 0;
    for (uint8_t b : bytes) bits = (bits << 8) | b;
    float f;
    std::memcpy(&f, &bits, sizeof f);
    v = static_cast<double>(f);
  }
  const bool upper = s.conv >= 'A' && s.conv <= 'Z';
  // A NaN carries no sign in JavaScript, whatever its bit says here.
  const bool neg = std::signbit(v) && !std::isnan(v);
  const double av = std::fabs(v);
  std::string digits;
  if (!std::isfinite(av)) {
    digits = std::isnan(av) ? "nan" : "inf";
    if (upper) digits = upperCase(digits);
    const std::string sgn = neg ? "-" : s.flags.plus ? "+" : s.flags.space ? " " : "";
    return pad(sgn, "", digits, s, false);
  }
  const char lc = static_cast<char>(upper ? s.conv - 'A' + 'a' : s.conv);
  // DESIGN: the browser used JavaScript's toFixed and toExponential, which
  // round an exact half upward. C's printf rounds it to even, so 2.5 with no
  // decimals prints 2 here and 3 there. Every other value agrees.
  if (lc == 'f') {
    digits = cFormat("%.*f", s.precision.value_or(6), av);
  } else if (lc == 'e') {
    digits = cFormat("%.*e", s.precision.value_or(6), av);
  } else {
    // g: significant digits, trailing zeros trimmed unless the hash flag.
    const int p = !s.precision ? 6 : *s.precision == 0 ? 1 : *s.precision;
    digits = toPrecision(av, p);
    if (!s.flags.hash && digits.find('.') != std::string::npos) digits = trimZeros(digits);
    digits = padExp(digits);
  }
  if (upper) digits = upperCase(digits);
  const std::string sign = neg ? "-" : s.flags.plus ? "+" : s.flags.space ? " " : "";
  return pad(sign, "", digits, s, true);
}

void pushAscii(std::vector<uint8_t>& out, std::string_view s) {
  for (char c : s) out.push_back(static_cast<uint8_t>(c));
}

// The next n parameter bytes, or nothing when the run is short.
std::optional<std::span<const uint8_t>> take(std::span<const uint8_t> params, size_t& cursor, size_t n) {
  if (cursor + n > params.size()) return std::nullopt;
  auto out = params.subspan(cursor, n);
  cursor += n;
  return out;
}

}  // namespace

std::vector<int> templateArgWidths(std::span<const uint8_t> tpl) {
  std::vector<int> out;
  size_t p = 0;
  while (p < tpl.size()) {
    if (tpl[p] != PCT) {
      p++;
      continue;
    }
    const auto spec = parseSpec(tpl, p + 1);
    if (!spec) {
      p++;
      continue;
    }
    p = spec->next;
    const char conv = spec->conv;
    if (conv == '%') continue;
    if (conv == 'c') {
      out.push_back(1);
      continue;
    }
    if (conv == 's' || conv == 'r') {
      out.push_back(2);
      continue;
    }
    if (isFloatConv(conv)) {
      out.push_back(spec->length == Length::L || spec->length == Length::LL ? 8 : 4);
      continue;
    }
    out.push_back(intBytes(spec->length));
  }
  return out;
}

std::vector<uint8_t> formatTemplate(std::span<const uint8_t> tpl, std::span<const uint8_t> params,
                                    const PrintfReader& readByte) {
  std::vector<uint8_t> out;
  size_t cursor = 0;
  size_t p = 0;
  auto ram = [&](uint32_t o) -> uint8_t { return readByte ? readByte(o) : 0; };
  while (p < tpl.size()) {
    const uint8_t c = tpl[p];
    if (c != PCT) {
      out.push_back(c);
      p++;
      continue;
    }
    const auto spec = parseSpec(tpl, p + 1);
    if (!spec) {
      // Not a format we know. Emit the percent as literal text.
      out.push_back(PCT);
      p++;
      continue;
    }
    p = spec->next;
    const char conv = spec->conv;
    if (conv == '%') {
      out.push_back(PCT);
      continue;
    }
    if (conv == 'c') {
      const auto b = take(params, cursor, 1);
      if (!b) pushAscii(out, NO_PARAM);
      else pushAscii(out, pad("", "", std::string(1, static_cast<char>((*b)[0])), *spec, false));
      continue;
    }
    if (conv == 's' || conv == 'r') {
      const auto ptr = take(params, cursor, 2);
      if (!ptr) {
        pushAscii(out, NO_PARAM);
        continue;
      }
      const uint32_t addr = (static_cast<uint32_t>((*ptr)[0]) << 8) | (*ptr)[1];
      if (conv == 'r') {
        // Raw sized string: exactly `width` bytes from the address, any zero
        // byte included. The width is the count, not a field to pad.
        for (int i = 0; i < spec->width; i++) out.push_back(ram(addr + static_cast<uint32_t>(i)));
        continue;
      }
      // Zero-terminated string from RAM.
      std::string str;
      for (uint32_t i = 0; i < 4096; i++) {
        const uint8_t b = ram(addr + i);
        if (b == 0) break;
        str += static_cast<char>(b);
      }
      if (spec->precision && str.size() > static_cast<size_t>(*spec->precision)) {
        str.resize(static_cast<size_t>(*spec->precision));
      }
      pushAscii(out, pad("", "", str, *spec, false));
      continue;
    }
    if (isFloatConv(conv)) {
      const size_t n = spec->length == Length::L || spec->length == Length::LL ? 8 : 4;
      const auto b = take(params, cursor, n);
      if (!b) pushAscii(out, NO_PARAM);
      else pushAscii(out, formatFloat(*spec, *b));
      continue;
    }
    // An integer conversion.
    const size_t n = static_cast<size_t>(intBytes(spec->length));
    const auto b = take(params, cursor, n);
    if (!b) pushAscii(out, NO_PARAM);
    else pushAscii(out, formatInt(*spec, *b));
  }
  return out;
}

}  // namespace sc8
