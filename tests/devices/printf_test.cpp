#include <doctest.h>

#include <cstring>
#include <numeric>
#include <string>
#include <vector>

#include "devices/printf.h"

using namespace sc8;

namespace {

using Bytes = std::vector<uint8_t>;

Bytes t(const std::string& s) { return Bytes(s.begin(), s.end()); }
std::string str(const Bytes& out) { return std::string(out.begin(), out.end()); }
std::string fmt(const std::string& tpl, const Bytes& params, const PrintfReader& env = {}) {
  return str(formatTemplate(t(tpl), params, env));
}
// A RAM reader for %s and %r: the bytes live at address 0 and up.
PrintfReader ramEnv(Bytes bytes) {
  return [bytes](uint32_t o) -> uint8_t { return o < bytes.size() ? bytes[o] : 0; };
}
Bytes withNul(const std::string& s) {
  Bytes b = t(s);
  b.push_back(0);
  return b;
}

// A float32 value as its four big-endian bytes.
Bytes floatBytes32(float v) {
  uint32_t bits;
  std::memcpy(&bits, &v, sizeof bits);
  return {static_cast<uint8_t>(bits >> 24), static_cast<uint8_t>(bits >> 16), static_cast<uint8_t>(bits >> 8),
          static_cast<uint8_t>(bits)};
}

// A double as its eight big-endian bytes.
Bytes floatBytes64(double v) {
  uint64_t bits;
  std::memcpy(&bits, &v, sizeof bits);
  Bytes out;
  for (int i = 7; i >= 0; i--) out.push_back(static_cast<uint8_t>(bits >> (i * 8)));
  return out;
}

const std::string NO = std::string(NO_PARAM);

}  // namespace

TEST_SUITE("printf, C grammar") {
  TEST_CASE("passes plain text and newlines through") { CHECK_EQ(fmt("hi\nthere", {}), "hi\nthere"); }

  TEST_CASE("reads an unsigned byte with the hh length") { CHECK_EQ(fmt("%hhu", {200}), "200"); }

  TEST_CASE("reads the same byte as signed") {
    CHECK_EQ(fmt("%hhd", {200}), "-56");
    CHECK_EQ(fmt("%hhd", {0x80}), "-128");
  }

  TEST_CASE("treats a bare int as 16 bits, big-endian") {
    CHECK_EQ(fmt("%u", {0x12, 0x34}), "4660");
    CHECK_EQ(fmt("%d", {0xff, 0xff}), "-1");
    CHECK_EQ(fmt("%u", {0xff, 0xff}), "65535");
  }

  TEST_CASE("reads long and long long") {
    CHECK_EQ(fmt("%lu", {0xff, 0xff, 0xff, 0xff}), "4294967295");
    CHECK_EQ(fmt("%ld", {0xff, 0xff, 0xff, 0xff}), "-1");
    CHECK_EQ(fmt("%llu", {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}), "18446744073709551615");
    CHECK_EQ(fmt("%lld", {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe}), "-2");
  }

  TEST_CASE("formats hex, octal, and binary") {
    CHECK_EQ(fmt("%x", {0x12, 0x34}), "1234");
    CHECK_EQ(fmt("%X", {0x00, 0xab}), "AB");
    CHECK_EQ(fmt("%hho", {8}), "10");
    CHECK_EQ(fmt("%hhb", {200}), "11001000");
  }

  TEST_CASE("honors the alternate-form flag") {
    CHECK_EQ(fmt("%#x", {0x12, 0x34}), "0x1234");
    CHECK_EQ(fmt("%#hho", {8}), "010");
    CHECK_EQ(fmt("%#hhb", {5}), "0b101");
    CHECK_EQ(fmt("%#x", {0, 0}), "0");  // no prefix on zero
  }

  TEST_CASE("applies width, precision, and the minus and zero flags to ints") {
    CHECK_EQ(fmt("%5hhu", {42}), "   42");
    CHECK_EQ(fmt("%-5hhu|", {42}), "42   |");
    CHECK_EQ(fmt("%05hhu", {42}), "00042");
    CHECK_EQ(fmt("%.4hhu", {42}), "0042");
  }

  TEST_CASE("shows a sign with the plus and space flags") {
    CHECK_EQ(fmt("%+hhd", {5}), "+5");
    CHECK_EQ(fmt("% hhd", {5}), " 5");
    CHECK_EQ(fmt("%+hhd", {200}), "-56");
  }

  TEST_CASE("prints a char and a literal percent") { CHECK_EQ(fmt("%c%%", {65}), "A%"); }

  TEST_CASE("formats a single-precision float with default and set precision") {
    CHECK_EQ(fmt("%f", {0x3f, 0xc0, 0x00, 0x00}), "1.500000");
    CHECK_EQ(fmt("%.2f", {0x3f, 0xc0, 0x00, 0x00}), "1.50");
    CHECK_EQ(fmt("%03.4f", {0x3f, 0xc0, 0x00, 0x00}), "1.5000");
  }

  TEST_CASE("pads a float to a field width") {
    // 3.14159 as float32, two decimals, width 8.
    const Bytes b = floatBytes32(3.14159f);
    CHECK_EQ(fmt("%8.2f", b), "    3.14");
    CHECK_EQ(fmt("%+.2f", b), "+3.14");
  }

  TEST_CASE("formats a double with the l length") { CHECK_EQ(fmt("%.1lf", {0x3f, 0xf8, 0, 0, 0, 0, 0, 0}), "1.5"); }

  TEST_CASE("formats scientific and general floats") {
    CHECK_EQ(fmt("%e", {0x3f, 0xc0, 0x00, 0x00}), "1.500000e+00");
    CHECK_EQ(fmt("%g", {0x3f, 0xc0, 0x00, 0x00}), "1.5");
  }

  TEST_CASE("follows toPrecision for the general form, not C's %g") {
    // JavaScript keeps the fixed form down to 1e-6 where C switches at
    // 1e-4, and pads a one digit exponent to two.
    CHECK_EQ(fmt("%g", floatBytes32(0.00001f)), "0.00001");
    CHECK_EQ(fmt("%g", floatBytes32(1234567.0f)), "1.23457e+06");
    CHECK_EQ(fmt("%G", floatBytes32(1234567.0f)), "1.23457E+06");
    CHECK_EQ(fmt("%#g", floatBytes32(1.5f)), "1.50000");
    CHECK_EQ(fmt("%.3g", floatBytes32(100.0f)), "100");
  }

  TEST_CASE("rounds an exact half up in magnitude, as toFixed did") {
    // C's printf would give 0.12 and 2 here.
    CHECK_EQ(fmt("%.2f", floatBytes32(0.125f)), "0.13");
    CHECK_EQ(fmt("%.2f", floatBytes32(-0.125f)), "-0.13");
    CHECK_EQ(fmt("%.0f", floatBytes32(2.5f)), "3");
    CHECK_EQ(fmt("%.0f", floatBytes32(-2.5f)), "-3");
    CHECK_EQ(fmt("%.0e", floatBytes32(2.5f)), "3e+00");
    CHECK_EQ(fmt("%.1g", floatBytes32(2.5f)), "3");
  }

  // Expected strings come from Node 22 running the browser's formatting:
  // toFixed, toExponential with a two digit exponent, and toPrecision with
  // its zeros trimmed. They round the exact binary value, so 0.135 gives 0.14
  // and 1.005 gives 1.00, and they take a true half upward.
  TEST_CASE("matches JavaScript's toFixed, toExponential and toPrecision") {
    struct Case {
      double value;
      const char* spec;
      const char* expected;
    };
    const Case doubles[] = {
        {0.125, "%.2lf", "0.13"},         {-0.125, "%.2lf", "-0.13"},     {0.135, "%.2lf", "0.14"},
        {2.5, "%.0lf", "3"},              {-2.5, "%.0lf", "-3"},          {3.5, "%.0lf", "4"},
        {0.0005, "%.3lf", "0.001"},       {0.0005, "%.4lf", "0.0005"},    {1.005, "%.2lf", "1.00"},
        {1.5, "%.0lf", "2"},              {0.5, "%.0lf", "1"},            {123.456, "%.0lf", "123"},
        {123.456, "%.1lf", "123.5"},      {123.456, "%.2lf", "123.46"},   {123.456, "%.3lf", "123.456"},
        {123.456, "%.5lf", "123.45600"},  {9.995, "%.2lf", "9.99"},       {9.5, "%.0lf", "10"},
        {99.5, "%.0lf", "100"},           {0.045, "%.2lf", "0.04"},       {1e-7, "%.6lf", "0.000000"},
        {0.125, "%.1le", "1.3e-01"},      {1.25, "%.1le", "1.3e+00"},     {2.5, "%.0le", "3e+00"},
        {9.5, "%.0le", "1e+01"},          {123.456, "%.2le", "1.23e+02"}, {123.456, "%le", "1.234560e+02"},
        {0.0005, "%.2le", "5.00e-04"},    {1.005, "%.2le", "1.00e+00"},   {0, "%.2le", "0.00e+00"},
        {0, "%.2lf", "0.00"},             {0.125, "%.2lg", "0.13"},       {2.5, "%.1lg", "3"},
        {123.456, "%.4lg", "123.5"},      {123.456, "%lg", "123.456"},    {123.456, "%.2lg", "1.2e+02"},
        {0.0005, "%.1lg", "0.0005"},      {1.005, "%.3lg", "1"},          {9.5, "%.1lg", "1e+01"},
        {99.5, "%.2lg", "1e+02"},         {0.5, "%.0lg", "0.5"},          {1234567.5, "%.7lg", "1234568"},
    };
    for (const Case& c : doubles) {
      CAPTURE(c.value);
      CAPTURE(c.spec);
      CHECK_EQ(fmt(c.spec, floatBytes64(c.value)), c.expected);
    }
    // The same through a float32, which is what Math.fround gave Node.
    const Case singles[] = {
        {0.125, "%.2f", "0.13"},     {2.5, "%.0f", "3"},           {0.135, "%.2f", "0.14"},
        {1.005, "%.2f", "1.00"},     {123.456, "%.2f", "123.46"},  {123.456, "%.4f", "123.4560"},
    };
    for (const Case& c : singles) {
      CAPTURE(c.value);
      CAPTURE(c.spec);
      CHECK_EQ(fmt(c.spec, floatBytes32(static_cast<float>(c.value))), c.expected);
    }
  }

  TEST_CASE("prints infinities and not a number") {
    CHECK_EQ(fmt("%f", {0x7f, 0x80, 0, 0}), "inf");
    CHECK_EQ(fmt("%F", {0xff, 0x80, 0, 0}), "-INF");
    CHECK_EQ(fmt("%5f", {0x7f, 0xc0, 0, 0}), "  nan");
  }

  TEST_CASE("reads a zero-terminated string from RAM, with precision and width") {
    const auto env = ramEnv(withNul("hello"));
    CHECK_EQ(fmt("%s", {0, 0}, env), "hello");
    CHECK_EQ(fmt("%.3s", {0, 0}, env), "hel");
    CHECK_EQ(fmt("%-8s|", {0, 0}, env), "hello   |");
  }

  TEST_CASE("prints exactly N bytes with %r, ignoring any terminator") {
    const auto env = ramEnv(withNul("WORLD!"));
    CHECK_EQ(fmt("%5r", {0, 0}, env), "WORLD");
    // A zero inside the run is still emitted as a byte.
    CHECK(formatTemplate(t("%3r"), Bytes{0, 0}, ramEnv({72, 0, 74})) == Bytes{72, 0, 74});
    // Past the end reads as zero bytes, still exactly N.
    CHECK(formatTemplate(t("%4r"), Bytes{0, 0}, ramEnv({65, 66})) == Bytes{65, 66, 0, 0});
  }

  TEST_CASE("prints nothing for %r with no width but still consumes the pointer") {
    CHECK_EQ(fmt("[%r][%hhu]", {0, 0, 9}, ramEnv(t("abc"))), "[][9]");
  }

  TEST_CASE("shows the no-parameter text when the queue runs short") {
    CHECK_EQ(fmt("%d", {0x12}), NO);
    CHECK_EQ(fmt("a %hhu b %hhu", {5}), "a 5 b " + NO);
  }

  TEST_CASE("ignores extra parameters") { CHECK_EQ(fmt("%hhu", {7, 8, 9}), "7"); }

  TEST_CASE("handles a full template") { CHECK_EQ(fmt("x=%hhu y=%hd\n", {5, 0xff, 0xf6}), "x=5 y=-10\n"); }

  TEST_CASE("leaves an unknown conversion as a literal percent") { CHECK_EQ(fmt("%z", {}), "%z"); }

  TEST_CASE("shows one byte several ways to make the point") {
    CHECK_EQ(fmt("%hhu %hhd %#hhx %hhb", {200, 200, 200, 200}), "200 -56 0xc8 11001000");
  }
}

// How many bytes each conversion reads, taken from the formatter itself. The
// C compiler marshals gpu_printf's arguments into RAM, and it has to lay them
// out exactly as the device will read them. Two lists would drift.
TEST_SUITE("templateArgWidths") {
  auto widths = [](const std::string& s) { return templateArgWidths(t(s)); };
  using W = std::vector<int>;

  TEST_CASE("counts nothing in a template with no conversions") { CHECK(widths("hello") == W{}); }

  TEST_CASE("gives an int two bytes, because an int is 16 bits here") { CHECK(widths("%d") == W{2}); }

  TEST_CASE("reads the length modifier") { CHECK(widths("%hhu %hu %u %lu %llu") == W{1, 2, 2, 4, 8}); }

  TEST_CASE("gives a char one byte and a pointer two") { CHECK(widths("%c %s %r") == W{1, 2, 2}); }

  TEST_CASE("gives a float four bytes and eight with the l length") { CHECK(widths("%f %lf") == W{4, 8}); }

  TEST_CASE("counts several in order") { CHECK(widths("x=%hhu y=%d name=%s") == W{1, 2, 2}); }

  TEST_CASE("takes no argument for a doubled percent") { CHECK(widths("100%% done") == W{}); }

  TEST_CASE("ignores a percent that starts no conversion, like the formatter does") {
    CHECK(widths("50%, and %d") == W{2});
  }

  // "50% off" is not a stray percent: "% o" is a space flagged octal
  // conversion, in C and here. So it really does take an argument, and a
  // program that means a literal percent has to write %%.
  TEST_CASE("takes an argument for % o, which is octal with a space flag") { CHECK(widths("50% off") == W{2}); }

  TEST_CASE("agrees with what the formatter actually consumes") {
    // The formatter is handed exactly the bytes the widths predict, and every
    // conversion has to find its parameter rather than report one missing.
    const std::string tpl = "%hhu %d %lu %c";
    const W w = widths(tpl);
    const int total = std::accumulate(w.begin(), w.end(), 0);
    Bytes params;
    for (int i = 0; i < total; i++) params.push_back(static_cast<uint8_t>(i + 1));
    const std::string out = fmt(tpl, params);
    CHECK_EQ(out.find("no parameter"), std::string::npos);
  }
}
