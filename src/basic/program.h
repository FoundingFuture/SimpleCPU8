// The interpreter's stored program, as text and as the bytes in its
// memory. The IDE's editor and the machine hold the same program in these
// two forms, and this is the bridge between them.
//
// A stored line is two bytes of line number, high first, one byte of
// record length, the text, and a zero. A line number of zero ends the
// program, so an empty program is three bytes. edit.c is the C side of the
// same format. The system page says where the program sits: SYS_PROG holds
// its address and SYS_PROG_LEN its length. docs/basic-system-page.md.
#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace sc8::basic {

// The largest program the interpreter keeps, PROGMAX in basic.h.
constexpr size_t PROGRAM_MAX = 6144;

// System page addresses the bridge reads, mirrored from basic.h.
constexpr uint16_t SYS_PROG = 0x08;
constexpr uint16_t SYS_PROG_LEN = 0x0A;
constexpr uint16_t SYS_RUNNING = 0x15;

// Text to stored bytes. Lines are sorted by number, a later line with the
// same number replaces an earlier one, a numbered line with no text
// deletes, and a line with no number is skipped, the way typing them in
// would go. Leading spaces after the number are dropped, as ed_store
// receives them. Text past 250 characters is cut. A program that does not
// fit is cut at the last line that does.
std::vector<uint8_t> encodeProgram(const std::string& text);

// Stored bytes back to text, one line per row, as LIST prints them.
std::string decodeProgram(std::span<const uint8_t> bytes);

// The stored lines by number.
std::map<int, std::string> programLines(std::span<const uint8_t> bytes);

// A three way merge by line number. base is the program both sides last
// agreed on, mine and theirs what each made of it since. A line only one
// side changed takes that side's version. A line both changed takes mine.
std::vector<uint8_t> mergePrograms(std::span<const uint8_t> base, std::span<const uint8_t> mine,
                                   std::span<const uint8_t> theirs);

// The document's text brought in line with a stored program, changing as
// little as it can. A numbered line the program holds unchanged keeps its
// own spelling and place. A changed line is rewritten as LIST prints it.
// A line the program lacks goes. A new line goes before the first line
// numbered above it. Blank lines and lines without a number stay.
std::string patchText(const std::string& text, std::span<const uint8_t> program);

}  // namespace sc8::basic
