// The interpreter's stored program, as text and as the bytes in its
// memory. The IDE's editor and the machine hold the same program in these
// two forms, and this is the bridge between them.
//
// The store holds up to FILE_MAX files. A table at its head names them:
// a count, a spare byte, then per file a name of 16 characters and a
// zero and the offset of the file's first record as a word. An offset of
// 0 is a file a line names that is not loaded. Each loaded file has one
// block of records, ended by a record numbered zero.
//
// A stored line is the file's index, two bytes of line number, high
// first, one byte of record length, the text and a zero. The text holds
// each keyword as one byte, each number with its value, and each jump
// target as its file's index and its line. edit.c is the C side of the
// same format. The system page says where the store sits: SYS_PROG holds
// its address and SYS_PROG_LEN its length. docs/basic-system-page.md.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sc8::basic {

// The largest store the interpreter keeps, PROGMAX in basic.h.
constexpr size_t PROGRAM_MAX = 16384;
// The file table and a record, mirrored from basic.h: FILEMAX, FNAME,
// FT_ENTRY, FT_SIZE, R_TEXT and R_END.
constexpr size_t FILE_MAX = 8;
constexpr size_t FILE_NAME = 17;
constexpr size_t FILE_ENTRY = 19;
constexpr size_t TABLE_SIZE = 154;
constexpr size_t RECORD_TEXT = 4;
constexpr size_t END_RECORD = 4;
// A store with one file and no line.
constexpr size_t EMPTY_PROGRAM = TABLE_SIZE + END_RECORD;

// System page addresses the bridge reads, mirrored from basic.h.
constexpr uint16_t SYS_PROG = 0x08;
constexpr uint16_t SYS_PROG_LEN = 0x0A;
// The last error's code, 0 for none, and the line it was on.
constexpr uint16_t SYS_ERR = 0x12;
constexpr uint16_t SYS_ERR_LINE = 0x13;
constexpr uint16_t SYS_RUNNING = 0x15;
// While a program runs: the offset of the line RUN goes to next. RUN moves
// it past a line before the line's statements run, and a jump sets it.
constexpr uint16_t SYS_PC = 0x16;
constexpr uint16_t SYS_COL = 0x05;
constexpr uint16_t SYS_ROW = 0x06;
// Where READ takes its next value, 0 for the first one.
constexpr uint16_t SYS_READ = 0x20;
// 1 while BASIC waits for a line at its prompt, 0 otherwise.
constexpr uint16_t SYS_PROMPT = 0x24;
// The index of the file the running line is in, a word.
constexpr uint16_t SYS_FILE = 0x25;
// The index of the current file, the one a typed line goes into.
constexpr uint16_t SYS_EDIT = 0x27;
// The text grid's columns and rows, read from the GPU after every cell change.
constexpr uint16_t SYS_COLS = 0x22;
constexpr uint16_t SYS_ROWS = 0x23;
// BASIC's text screen, 4 KB up to the top of RAM, room for a 64 by 64 grid.
// The interpreter's heap stack starts here, with --heap-stack-top.
constexpr int SCREEN = 0xF000;
// The page's size, SYS_END in basic.h and the interpreter's -zp-reserve.
constexpr int SYSTEM_PAGE_SIZE = 0x30;

// The interpreter's reserved words in capitals, with the $ of those that
// end in one. They come from basic/keywords.h, which the interpreter
// compiles too. The IDE's highlighter reads the same list.
const std::set<std::string, std::less<>>& basicKeywords();

// The names of a store's files by index, in capitals. A line's jump
// target holds an index into them.
struct FileTable {
  std::vector<std::string> names;
  // A name's index, in any case, or -1.
  int find(std::string_view name) const;
  // The same, with a new entry at the end for a name the table lacks.
  // -1 when the table is full.
  int add(std::string_view name);
};

// The file a document or a path stands for: what follows the last slash,
// up to its first dot, in capitals, 16 characters at most. graphics.bas
// and ../lib/graphics.bas are both the file GRAPHICS.
std::string fileNameOf(std::string_view path);

// True for a name a file may have: letters and digits, 16 at most. A
// SAVE's name becomes a file on the host's disk, so the IDE holds it to
// this before it writes one.
bool isFileName(std::string_view name);

// A line's body, the text after its number, with every reserved word in
// capitals, and a file's name before a target's colon. Strings, the rest
// of a line after REM, INCLUDE, FILE or a bang, names, numbers and
// spacing stay as written. The size never changes.
std::string canonicalLine(std::string_view body);

// A line's body as the interpreter stores it. The walk is canonicalLine's.
// Each word it puts in capitals is one byte, 128 plus the word's place in
// basic/keywords.h. A number is KW_LITERAL, its value high byte first,
// then its digits. The line after GOTO, GOSUB, THEN or RESTORE is
// KW_TARGET, its file's index and its number. A bare number takes `own`.
// NAME:number takes the named file's index, and the name gets an entry
// in `files` when it has none. Numbers and targets take their marker while the line
// stays within 250 bytes. A DATA line stays canonicalLine's text.
// crunch in edit.c is the machine's copy of the rule.
std::string crunchLine(std::string_view body, FileTable& files, int own);
// The same in a store with the one file MAIN.
std::string crunchLine(std::string_view body);

// A stored line's text back to what LIST prints. ed_expand in edit.c is
// the machine's copy. A target in another file than `own` gets that
// file's name and a colon. expandLine(crunchLine(b)) is canonicalLine(b)
// for a line whose targets are written the way LIST writes them.
std::string expandLine(std::string_view stored, const FileTable& files, int own);
std::string expandLine(std::string_view stored);

// The editor's text with canonicalLine applied to the body of every
// numbered row but row `skip`, the one being typed in. The number, the
// spaces around it and rows without a number stay. The size never changes.
std::string canonicalText(std::string_view text, int skip = -1);

// The number of the first line whose body holds a byte of 128 or more
// where the interpreter reads BASIC, or -1. The lexer would take such a
// byte for a keyword. Inside quotes, the rest of a REM line and a bang's
// text it is never read, so it passes there.
int refusedLine(std::string_view text);

// The Messages pane's words for refusedLine, naming the line, or empty.
std::string refusal(std::string_view text);

// Text to one file's records, the way typing the lines in would go,
// without the record that ends the file. Lines are sorted by number. A
// later line with the same number replaces an earlier one. A numbered
// line with no text deletes. A line with no number is skipped. Leading
// spaces after the number are dropped, as ed_store receives them. Text
// past 250 characters is cut. Lines that pass `room` bytes are cut at
// the last line that fits. A line refusedLine names is left out. Each
// stored line is crunchLine of its body.
std::vector<uint8_t> encodeLines(const std::string& text, FileTable& files, int own,
                                 size_t room = PROGRAM_MAX - EMPTY_PROGRAM);

// One file of a store: its name, whether it has a block, and its records
// without the one that ends it.
struct StoredFile {
  std::string name;
  bool loaded = false;
  std::vector<uint8_t> lines;
};

// A whole store, its files by index.
struct Store {
  std::vector<StoredFile> files;
  FileTable table() const;
  // Give every name `names` holds past the store's own an entry that is
  // not loaded, as a line that names a file does.
  void adopt(const FileTable& names);
};

// The bytes at SYS_PROG, SYS_PROG_LEN long, as a store. A store that does
// not read comes back with no file.
Store readStore(std::span<const uint8_t> bytes);
// A store as the bytes the interpreter keeps. The blocks lie in index
// order. The caller checks the size against PROGRAM_MAX.
std::vector<uint8_t> writeStore(const Store& store);

// A store with the one file MAIN holding the text's lines.
std::vector<uint8_t> encodeProgram(const std::string& text);

// True while BASIC waits for a line at its prompt. The program in memory
// is whole then and nothing of BASIC's is half done, so the IDE may read
// it or write a new one. Between two AUTORUN lines it is half stored.
bool atPrompt(std::span<const uint8_t> ram);

// A store into the interpreter's memory, where SYS_PROG says it sits,
// with its length in SYS_PROG_LEN. READ starts again at the first value,
// because its place in the old program means nothing in the new one.
// Nothing is written when the program would run past the end of ram.
void storeProgram(std::span<uint8_t> ram, std::span<const uint8_t> bytes);

// One file's records back to text, one line per row, as LIST prints them.
std::string decodeLines(std::span<const uint8_t> records, const FileTable& files, int own);
// The main file of a store, the same way.
std::string decodeProgram(std::span<const uint8_t> bytes);

// One file's stored lines by number, as LIST prints them.
std::map<int, std::string> linesOf(std::span<const uint8_t> records, const FileTable& files, int own);
// The main file's of a store.
std::map<int, std::string> programLines(std::span<const uint8_t> bytes);

// A three way merge of one file's records by line number. base is what
// both sides last agreed on, mine and theirs what each made of it since.
// A line only one side changed takes that side's version. A line both
// changed takes mine.
std::vector<uint8_t> mergeLines(std::span<const uint8_t> base, std::span<const uint8_t> mine,
                                std::span<const uint8_t> theirs, FileTable& files, int own);
// The same on three stores' main files. The answer is a store with the
// one file MAIN.
std::vector<uint8_t> mergePrograms(std::span<const uint8_t> base, std::span<const uint8_t> mine,
                                   std::span<const uint8_t> theirs);

// The document's text brought in line with a file's stored lines,
// changing as little as it can. A numbered line the file holds unchanged
// keeps its own spelling and place. A line counts as unchanged when it
// would be stored as the stored line is, so `print` in the document
// matches `PRINT` in memory. A changed line is rewritten as LIST prints
// it. A line the file lacks goes. A new line goes before the first line
// numbered above it. Blank lines and lines without a number stay.
std::string patchLines(const std::string& text, std::span<const uint8_t> records, const FileTable& files, int own);
// The same against the main file of a store.
std::string patchText(const std::string& text, std::span<const uint8_t> program);

// One exchange between a document and its file in the store, which the
// IDE makes for every tab while BASIC waits at its prompt. base is the
// file's records both sides agreed on at the last exchange. A side that
// moved carries the other along. Two sides that both moved merge line by
// line, and the document wins a line both changed.
struct Synced {
  std::vector<uint8_t> lines;  // the file's records now, the next base
  std::string text;            // the document's text now
  bool storeChanged = false;   // lines differ from the store's
  bool textChanged = false;    // text differs from the document's
};
Synced syncLines(std::span<const uint8_t> base, const std::string& text, std::span<const uint8_t> stored,
                 FileTable& files, int own);

}  // namespace sc8::basic
