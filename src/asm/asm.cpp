#include "asm/asm.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <numbers>
#include <set>
#include <unordered_map>

#include "asm/expr.h"
#include "core/machine.h"
#include "devices/constants.h"
#include "devices/gpu.h"

namespace sc8 {

namespace {

// The largest length a low byte and a high byte can carry between them.
constexpr int64_t MAX_BLOB_SIZE = 0xffff;

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

std::string upper(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

std::string lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string stripSpaces(std::string_view s) {
  std::string out;
  for (char c : s) {
    if (!std::isspace(static_cast<unsigned char>(c))) out += c;
  }
  return out;
}

bool isIdent(std::string_view s) {
  if (s.empty()) return false;
  if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
  for (char c : s) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
  }
  return true;
}

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

using Extra = std::function<std::optional<int64_t>(std::string_view)>;

// A built-in name or a literal. The old tool let a port name resolve only
// as a port and a command name only as an OUT value. Every registry name
// now resolves wherever a number does, so LD A <- CMD_CLEAR works. `extra`
// is the program's own .equ names, which come first.
std::optional<int64_t> parseNumber(std::string_view s, const Extra& extra = nullptr) {
  if (extra) {
    if (auto v = extra(s)) return v;
  }
  if (auto v = builtinConstant(s)) return *v;
  return parseLiteral(s);
}

// A number or an expression over built-in constants, for the places that
// run in pass one: a port, a count and a .addr. A label cannot be folded
// here.
std::optional<int64_t> constantValue(std::string_view s, const Extra& extra = nullptr) {
  if (auto n = parseNumber(s, extra)) return n;
  if (!looksLikeExpression(s)) return std::nullopt;
  ExprResult r = evalExpr(s, [&](std::string_view name) { return parseNumber(name, extra); });
  if (r.ok) return r.value;
  return std::nullopt;
}

// Port operands take any name and any expression a number does, so
// ACP_ADDR_HI + 1 works the way BLOCK + 8 does.
std::optional<int64_t> parsePort(std::string_view s, const Extra& extra = nullptr) {
  return constantValue(s, extra);
}

// OUT values take a name or a literal here. An expression waits for pass
// two, where a label can join in.
std::optional<int64_t> parseOutValue(std::string_view s, const Extra& extra = nullptr) {
  return parseNumber(s, extra);
}

std::optional<double> parseFloatLit(std::string_view s) {
  // ^[0-9]*\.[0-9]+$
  size_t dot = s.find('.');
  if (dot == std::string_view::npos || dot + 1 >= s.size()) return std::nullopt;
  for (size_t i = 0; i < s.size(); i++) {
    if (i == dot) continue;
    if (!std::isdigit(static_cast<unsigned char>(s[i]))) return std::nullopt;
  }
  return std::stod(std::string(s));
}

// Split a data line on commas, but keep commas inside a quoted string. A
// backslash escapes the next character, so an escaped quote stays in.
std::vector<std::string> splitDataItems(std::string_view line) {
  std::vector<std::string> toks;
  std::string cur;
  bool inStr = false;
  for (size_t i = 0; i < line.size(); i++) {
    const char ch = line[i];
    if (inStr) {
      cur += ch;
      if (ch == '\\' && i + 1 < line.size()) cur += line[++i];
      else if (ch == '"') inStr = false;
    } else if (ch == '"') {
      inStr = true;
      cur += ch;
    } else if (ch == ',') {
      toks.push_back(cur);
      cur.clear();
    } else {
      cur += ch;
    }
  }
  toks.push_back(cur);
  return toks;
}

// Decode a quoted string token to its bytes. Escapes: \n \t \r \0 \\ \"
// and \xHH. An unknown escape keeps the character.
std::optional<std::vector<uint8_t>> decodeString(std::string_view tok) {
  if (tok.size() < 2 || tok.front() != '"' || tok.back() != '"') return std::nullopt;
  std::string_view body = tok.substr(1, tok.size() - 2);
  std::vector<uint8_t> out;
  for (size_t i = 0; i < body.size(); i++) {
    const char ch = body[i];
    if (ch != '\\') {
      out.push_back(static_cast<uint8_t>(ch));
      continue;
    }
    if (++i >= body.size()) break;
    const char e = body[i];
    switch (e) {
      case 'n': out.push_back(10); break;
      case 't': out.push_back(9); break;
      case 'r': out.push_back(13); break;
      case '0': out.push_back(0); break;
      case '\\': out.push_back(92); break;
      case '"': out.push_back(34); break;
      case 'x': {
        auto v = parseLiteral("$" + std::string(body.substr(i + 1, 2)));
        out.push_back(static_cast<uint8_t>(v.value_or(0) & 0xff));
        i += 2;
        break;
      }
      default: out.push_back(static_cast<uint8_t>(e)); break;
    }
  }
  return out;
}

// FNV-1a, for cartridge blob dedup.
uint32_t hashBytes(const std::vector<uint8_t>& bytes) {
  uint32_t h = 0x811c9dc5u;
  for (uint8_t b : bytes) {
    h ^= b;
    h *= 0x01000193u;
  }
  return h;
}

// Every field has a default, so a site names only the fields its shape uses.
struct Operand {
  enum class T { Reg, IndA, IndD, IdxD, DispD, Stack, Mem, Val, Round, Sum };
  T t{};
  std::string r{};   // Reg: A, D1, D2
  int x = 0;         // IndD, IdxD, DispD: 1 or 2
  bool inc = false;  // IndA, IndD
  bool push = false; // Stack: [SP]- pushes, [SP]+ pops
  std::string expr{}; // DispD, Mem, Val
  std::string was{};  // Round: the operand as typed
  bool plusA = false; // Sum: D1+A rather than D1+n
};

// The six addressing shapes, spelled with one bracket pair. A dereference
// is a bracket group standing alone as the whole operand. A call has an
// identifier in front of the bracket, so get_lowbyte(x) matches none of
// these and falls through to the value branch. Order matters: [D1+A] is
// tried before the displacement shape, which would otherwise read A as a
// label named A.
std::optional<Operand> parseDeref(const std::string& s, char open, char close) {
  if (s.size() < 3 || s.front() != open) return std::nullopt;
  const char last = s.back();
  const bool suffixed = last == '+' || last == '-';
  const std::string body = suffixed ? s.substr(0, s.size() - 1) : s;
  if (body.size() < 3 || body.back() != close) return std::nullopt;
  const std::string inner = body.substr(1, body.size() - 2);
  const bool dReg = inner == "D1" || inner == "D2";
  // D3 has only the displacement form, so [D3] is [D3+0].
  if (inner == "D3" && !suffixed) return Operand{.t = Operand::T::DispD, .x = 3, .expr = "0"};
  if (suffixed) {
    if (last == '+' && inner == "A") return Operand{.t = Operand::T::IndA, .inc = true};
    if (last == '+' && dReg) return Operand{.t = Operand::T::IndD, .x = inner[1] - '0', .inc = true};
    if (inner == "SP") return Operand{.t = Operand::T::Stack, .push = last == '-'};
    return std::nullopt;
  }
  if (inner == "A") return Operand{.t = Operand::T::IndA};
  if (dReg) return Operand{.t = Operand::T::IndD, .x = inner[1] - '0'};
  if (inner == "D1+A" || inner == "D2+A" || inner == "D3+A") return Operand{.t = Operand::T::IdxD, .x = inner[1] - '0'};
  if ((startsWith(inner, "D1+") || startsWith(inner, "D2+") || startsWith(inner, "D3+")) && inner.size() > 3) {
    return Operand{.t = Operand::T::DispD, .x = inner[1] - '0', .expr = inner.substr(3)};
  }
  if (inner.empty()) return std::nullopt;
  return Operand{.t = Operand::T::Mem, .expr = inner};
}

// The same operand written the way it has to be written now. The suffix
// travels outside the bracket, so ($1C)+ is named as [$1C]+.
std::string squareSpelling(const std::string& s) {
  if (s.size() < 2 || s.front() != '(') return s;
  std::string body = s;
  std::string suffix;
  if (body.back() == '+' || body.back() == '-') {
    suffix = body.back();
    body.pop_back();
  }
  if (body.size() < 2 || body.back() != ')') return s;
  return "[" + body.substr(1, body.size() - 2) + "]" + suffix;
}

std::optional<Operand> parseOperand(std::string_view raw) {
  const std::string s = stripSpaces(raw);
  if (s == "A" || s == "D1" || s == "D2" || s == "D3") return Operand{.t = Operand::T::Reg, .r = s};
  if (auto d = parseDeref(s, '[', ']')) return d;
  // A register plus an offset with no brackets is the sum itself, the
  // address arithmetic: D1+8, D1-8, D2+A.
  if (s.size() > 3 && (startsWith(s, "D1") || startsWith(s, "D2") || startsWith(s, "D3")) && (s[2] == '+' || s[2] == '-')) {
    const std::string off = s.substr(3);
    if (s[2] == '+' && off == "A") return Operand{.t = Operand::T::Sum, .x = s[1] - '0', .plusA = true};
    return Operand{.t = Operand::T::Sum, .x = s[1] - '0', .expr = s[2] == '+' ? off : "0-(" + off + ")"};
  }
  // Round brackets did this job until every program was rewritten. They are
  // matched here only to be refused with the spelling that works. A group
  // holding an operator is arithmetic, and (1 + 2) has one reading.
  if (auto r = parseDeref(s, '(', ')')) {
    if (r->t == Operand::T::Mem && looksLikeExpression(r->expr)) {
      return Operand{.t = Operand::T::Val, .expr = r->expr};
    }
    return Operand{.t = Operand::T::Round, .was = s};
  }
  if (s.empty()) return std::nullopt;
  return Operand{.t = Operand::T::Val, .expr = s.front() == '&' ? s.substr(1) : s};
}

enum class ExprKind { Imm8, Addr8, Disp8, Imm16, Off16, Addr16, Target, Out8 };

struct PendingInstr {
  int line;
  uint32_t slot;
  std::string opName;
  std::optional<std::string> expr;
  ExprKind exprKind = ExprKind::Imm8;
  int64_t literal = 0;  // pre-resolved operand (ports, immediates already merged)
};

struct RamItem {
  int line;
  int width;  // 1 or 2
  std::string expr;
  // ds. The image is laid down by walking these in order, so reserved space
  // is an item of its own rather than a jump in the offset.
  std::optional<int64_t> skip;
};

struct CartItem {
  int line;
  int width;
  std::string expr;
  size_t at;
};

struct Placed {
  size_t at;
  size_t size;
};

const std::unordered_map<std::string_view, bool> BRANCHES = {
    {"JMP", true}, {"JZ", true}, {"JC", true}, {"JN", true}, {"JV", true}, {"JNZ", true}, {"JNC", true}, {"JP", true}, {"JNV", true}, {"JSR", true}};

bool isTestMnemonic(std::string_view m) { return m == "CMP" || m == "TST"; }

bool isShiftMnemonic(std::string_view m) {
  return m == "SHL" || m == "SHR" || m == "ROL" || m == "ROR" || m == "ASR";
}

bool isAluMnemonic(std::string_view m) {
  return m == "ADD" || m == "SUB" || m == "ADC" || m == "SBC" || m == "AND" || m == "OR" ||
         m == "XOR";
}

// The directives an error message offers, built from the list.
std::string directiveList() {
  std::string out;
  const size_t n = std::size(DATA_DIRECTIVES);
  for (size_t i = 0; i < n; i++) {
    if (i > 0) out += (i + 1 == n) ? " or " : ", ";
    out += ".";
    out += DATA_DIRECTIVES[i];
  }
  return out;
}

// `.file('name')` or `.file("name")`. The delimiters match each other, so a
// name may hold the other quote.
struct Directive {
  std::string kind, name;
  std::string frames;  // .sprite's optional count, an expression, or empty
};

std::optional<Directive> parseDataDirective(std::string_view line) {
  if (line.empty() || line[0] != '.') return std::nullopt;
  for (std::string_view d : DATA_DIRECTIVES) {
    if (!startsWith(line.substr(1), d)) continue;
    std::string_view rest = trim(line.substr(1 + d.size()));
    if (rest.size() < 4 || rest.front() != '(' || rest.back() != ')') continue;
    std::string_view arg = trim(rest.substr(1, rest.size() - 2));
    // .sprite may name its frame count after the file name.
    std::string frames;
    if (d == "sprite" && arg.size() >= 2 && (arg.front() == '\'' || arg.front() == '"')) {
      const size_t close = arg.find(arg.front(), 1);
      if (close != std::string_view::npos && close + 1 < arg.size()) {
        std::string_view after = trim(arg.substr(close + 1));
        if (after.empty() || after.front() != ',') continue;
        frames = std::string(trim(after.substr(1)));
        if (frames.empty()) continue;
        arg = arg.substr(0, close + 1);
      }
    }
    if (arg.size() < 2) continue;
    const char q = arg.front();
    if ((q != '\'' && q != '"') || arg.back() != q) continue;
    std::string_view name = arg.substr(1, arg.size() - 2);
    if (name.empty() || name.find(q) != std::string_view::npos) continue;
    return Directive{std::string(d), std::string(name), frames};
  }
  return std::nullopt;
}

// `get_x(inner)`: the macro name and its argument.
std::optional<std::pair<std::string, std::string>> parseMacro(std::string_view expr) {
  if (!startsWith(expr, "get_")) return std::nullopt;
  size_t open = expr.find('(');
  if (open == std::string_view::npos || expr.back() != ')' || open + 1 >= expr.size() - 1) {
    return std::nullopt;
  }
  return std::pair{std::string(expr.substr(4, open - 4)), std::string(expr.substr(open + 1, expr.size() - open - 2))};
}

class Assembler {
 public:
  Assembler(std::string_view source, const Assets* assets) : source_(source), assets_(assets) {
    out_.ram.assign(RAM_SIZE, 0);
  }

  Assembled run() {
    passOne();
    passTwo();
    return std::move(out_);
  }

 private:
  using T = Operand::T;

  void err(int line, std::string message) { out_.errors.push_back({line, std::move(message)}); }

  // The program's own names from .equ. They resolve wherever a number does,
  // in both passes, and a .equ folds in pass one so a port or a count can
  // use one.
  std::map<std::string, int64_t> equs_;
  Extra equ() {
    return [this](std::string_view name) -> std::optional<int64_t> {
      auto it = equs_.find(std::string(name));
      if (it == equs_.end()) return std::nullopt;
      return it->second;
    };
  }
  std::optional<int64_t> parseNumber(std::string_view s) { return sc8::parseNumber(s, equ()); }
  std::optional<int64_t> constantValue(std::string_view s) { return sc8::constantValue(s, equ()); }
  std::optional<int64_t> parsePort(std::string_view s) { return sc8::parsePort(s, equ()); }
  std::optional<int64_t> parseOutValue(std::string_view s) { return sc8::parseOutValue(s, equ()); }

  // .equ NAME, expr names a number. The expression folds here, like a
  // count does, so the name is good from the next line on in either pass.
  void defineEqu(int line, std::string_view rest) {
    const size_t comma = rest.find(',');
    if (comma == std::string_view::npos) return err(line, ".equ needs: .equ NAME, value");
    const std::string name(trim(rest.substr(0, comma)));
    const std::string_view text = trim(rest.substr(comma + 1));
    if (!isIdent(name)) return err(line, ".equ needs a name: .equ NAME, value");
    if (equs_.count(name) || out_.labels.count(name)) return err(line, "duplicate label: " + name);
    if (builtinConstant(name)) return err(line, name + " is one of the machine's own constants");
    auto v = constantValue(text);
    if (!v) return err(line, ".equ needs a value the assembler can work out here, and not a label");
    equs_[name] = *v;
  }

  void defineLabel(int line, const std::string& name, Label::Kind kind, int64_t value) {
    if (out_.labels.count(name) || equs_.count(name)) err(line, "duplicate label: " + name);
    else out_.labels[name] = Label{kind, static_cast<int>(value)};
  }

  // A count that has to be known in pass one, because every address after
  // it depends on it. So a label in one is refused rather than deferred.
  std::optional<int64_t> foldNow(int line, std::string_view text, std::string_view what) {
    auto n = constantValue(text);
    if (!n) {
      err(line, std::string(what) + " needs a count the assembler can work out here, and not a label");
      return std::nullopt;
    }
    if (*n < 0) {
      err(line, std::string(what) + " cannot reserve " + std::to_string(*n) + " bytes");
      return std::nullopt;
    }
    return n;
  }

  void roundErr(int line, const Operand& op) {
    err(line, "round brackets no longer dereference. Use " + squareSpelling(op.was) + " rather than " + op.was + ".");
  }

  size_t placeBlob(int line, const std::vector<uint8_t>& bytes) {
    const uint32_t h = hashBytes(bytes);
    auto& bucket = blobIndex_[h];
    for (const auto& prev : bucket) {
      if (out_.cart.size() >= prev.first + bytes.size() &&
          std::equal(bytes.begin(), bytes.end(), out_.cart.begin() + static_cast<long>(prev.first)) &&
          prev.second == bytes.size()) {
        return prev.first;  // dedup: reuse the earlier copy
      }
    }
    if (cartOffset_ + bytes.size() > static_cast<size_t>(CART_DATA_SIZE)) {
      err(line, ".data exceeds the 1MB cartridge");
      return cartOffset_;
    }
    const size_t at = cartOffset_;
    out_.cart.resize(at + bytes.size());
    std::copy(bytes.begin(), bytes.end(), out_.cart.begin() + static_cast<long>(at));
    cartOffset_ += bytes.size();
    bucket.push_back({at, bytes.size()});
    return at;
  }

  template <class Map, class Loader>
  const typename Map::mapped_type* asset(Map& map, const Loader& loader, const std::string& name) {
    auto it = map.find(name);
    if (it != map.end()) return &it->second;
    if (loader) {
      if (auto loaded = loader(name)) return &(map[name] = std::move(*loaded));
    }
    return nullptr;
  }

  // Place a directive's blob and return its cartridge offset plus its
  // length. A deduplicated blob returns an earlier offset and grows the
  // cartridge by nothing, so the length must come back from here.
  std::optional<Placed> dataDirective(int line, const std::string& kind, const std::string& name,
                                      const std::string& framesExpr = "") {
    if (kind == "file" || kind == "sample" || kind == "font") {
      const std::vector<uint8_t>* bytes = nullptr;
      if (kind == "file") bytes = asset(files_, assets_ ? assets_->loadFile : nullptr, name);
      else if (kind == "font") bytes = asset(fonts_, assets_ ? assets_->loadFont : nullptr, name);
      else bytes = asset(samples_, assets_ ? assets_->loadSample : nullptr, name);
      if (!bytes) {
        err(line, "unknown " + kind + ": " + name + " (add it to the project assets)");
        return std::nullopt;
      }
      return Placed{placeBlob(line, *bytes), bytes->size()};
    }
    const ImageAsset* img = asset(images_, assets_ ? assets_->loadImage : nullptr, name);
    if (!img) {
      err(line, "unknown image: " + name + " (add it to the project assets)");
      return std::nullopt;
    }
    if (kind == "sprite") {
      std::vector<uint8_t> blob;
      if (!spriteBlob(line, *img, name, framesExpr, blob)) return std::nullopt;
      return Placed{placeBlob(line, blob), blob.size()};
    }
    const std::vector<uint8_t>& bytes = kind == "image" ? img->pixels : img->palette;
    return Placed{placeBlob(line, bytes), bytes.size()};
  }

  // The CMD_SPRITE_DEF blob of a strip, the frames side by side in the
  // picture. The same rules and messages as C's __sprite.
  bool spriteBlob(int line, const ImageAsset& img, const std::string& name, const std::string& framesExpr,
                  std::vector<uint8_t>& out) {
    int frames = img.frames > 0 ? img.frames : 1;
    if (!framesExpr.empty()) {
      auto n = constantValue(framesExpr);
      if (!n) {
        err(line, name + ": .sprite needs a frame count the assembler can work out here, and not a label");
        return false;
      }
      if (*n < 1) {
        err(line, name + ": .sprite needs at least one frame");
        return false;
      }
      frames = static_cast<int>(std::min<int64_t>(*n, 1 << 20));
    }
    if (frames > gpu::SPRITE_FRAMES_MAX) {
      err(line, name + ": .sprite takes at most " + std::to_string(gpu::SPRITE_FRAMES_MAX) + " frames");
      return false;
    }
    if (img.width < 1 || img.height < 1 || img.width % frames != 0) {
      err(line, name + ": " + std::to_string(img.width) + " pixels wide does not divide into " +
                    std::to_string(frames) + " frames for .sprite");
      return false;
    }
    const int fw = img.width / frames, fh = img.height;
    if (fw > gpu::SPRITE_MAX || fh > gpu::SPRITE_MAX) {
      err(line, name + ": a frame is " + std::to_string(fw) + " x " + std::to_string(fh) +
                    " and a sprite is at most " + std::to_string(gpu::SPRITE_MAX) + " pixels a side");
      return false;
    }
    out.push_back(static_cast<uint8_t>(frames));
    out.push_back(static_cast<uint8_t>(fw));
    out.push_back(static_cast<uint8_t>(fh));
    for (int f = 0; f < frames; f++) {
      for (int y = 0; y < fh; y++) {
        const auto row = static_cast<size_t>(y * img.width + f * fw);
        out.insert(out.end(), img.pixels.begin() + static_cast<long>(row),
                   img.pixels.begin() + static_cast<long>(row + static_cast<size_t>(fw)));
      }
    }
    return true;
  }

  void passOne() {
    if (assets_) {
      files_ = assets_->files;
      images_ = assets_->images;
      samples_ = assets_->samples;
      fonts_ = assets_->fonts;
    }
    enum class Section { Code, Ram, Data } section = Section::Code;
    int ramWidth = 1;
    int cartWidth = 1;

    int lineNo = 0;
    size_t pos = 0;
    while (pos <= source_.size()) {
      size_t nl = source_.find('\n', pos);
      std::string_view raw = source_.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
      pos = nl == std::string_view::npos ? source_.size() + 1 : nl + 1;
      lineNo++;

      size_t sc = raw.find(';');
      if (sc != std::string_view::npos) raw = raw.substr(0, sc);
      std::string line(trim(raw));
      if (line.empty()) continue;

      // The three sections may each open more than once, so a second source
      // appended to a first can add code, RAM and data of its own. Offsets
      // carry on where the section left off.
      if (line == ".code") {
        section = Section::Code;
        continue;
      }
      if (line == ".ram") {
        section = Section::Ram;
        continue;
      }
      if (line == ".data") {
        section = Section::Data;
        continue;
      }

      // Leading label. In .ram and .data the definition waits: a .addr line
      // binds its label to a chosen address, and a .data directive binds to
      // wherever the blob landed.
      std::optional<std::string> dataLabel;
      size_t colon = line.find(':');
      if (colon != std::string::npos && isIdent(std::string_view(line).substr(0, colon))) {
        std::string name = line.substr(0, colon);
        if (section == Section::Code) defineLabel(lineNo, name, Label::Kind::Code, static_cast<int64_t>(slot_));
        else dataLabel = name;
        line = std::string(trim(std::string_view(line).substr(colon + 1)));
        if (line.empty()) {
          if (dataLabel) {
            defineLabel(lineNo, *dataLabel, section == Section::Ram ? Label::Kind::Ram : Label::Kind::Data,
                        section == Section::Ram ? ramOffset_ : static_cast<int64_t>(cartOffset_));
            if (section == Section::Ram) out_.ramLineAddr[lineNo] = static_cast<int>(ramOffset_);
          }
          continue;
        }
      }

      if (section == Section::Ram) {
        // label: .addr($10) names a RAM address without emitting bytes.
        if (startsWith(line, ".addr(") && line.back() == ')') {
          std::string_view arg = trim(std::string_view(line).substr(6, line.size() - 7));
          auto v = constantValue(arg);
          if (!v || *v >= RAM_SIZE) err(lineNo, ".addr takes a RAM address 0.." + std::to_string(RAM_SIZE - 1));
          if (dataLabel) defineLabel(lineNo, *dataLabel, Label::Kind::Ram, v.value_or(0));
          else err(lineNo, ".addr needs a label in front");
          out_.ramLineAddr[lineNo] = static_cast<int>(v.value_or(0));
          continue;
        }
        if (dataLabel) defineLabel(lineNo, *dataLabel, Label::Kind::Ram, ramOffset_);
        out_.ramLineAddr[lineNo] = static_cast<int>(ramOffset_);
        // ds reserves n zero bytes. The count folds in pass one, like a
        // port and a .addr do.
        if (startsWith(line, "ds ") || startsWith(line, "ds\t")) {
          auto n = foldNow(lineNo, trim(std::string_view(line).substr(3)), "ds");
          if (n && *n > 0) {
            ramItems_.push_back({lineNo, 1, "0", *n});
            ramOffset_ += *n;
          }
          continue;
        }
        for (std::string item : splitDataItems(line)) {
          item = std::string(trim(item));
          if (item.empty()) continue;
          if (!dataWidth(item, ramWidth)) continue;
          if (auto str = decodeString(item)) {
            for (uint8_t byte : *str) {
              ramItems_.push_back({lineNo, 1, std::to_string(byte), std::nullopt});
              ramOffset_ += 1;
            }
            continue;
          }
          ramItems_.push_back({lineNo, ramWidth, item.front() == '&' ? item.substr(1) : item, std::nullopt});
          ramOffset_ += ramWidth;
        }
        continue;
      }

      if (section == Section::Data) {
        if (auto dir = parseDataDirective(line)) {
          auto placed = dataDirective(lineNo, dir->kind, dir->name, dir->frames);
          if (dataLabel) {
            defineLabel(lineNo, *dataLabel, Label::Kind::Data,
                        static_cast<int64_t>(placed ? placed->at : cartOffset_));
            // Only on a successful placement. A failed directive has no
            // length to give.
            if (placed) {
              blobSizes_[*dataLabel] = placed->size;
              out_.assets.push_back({dir->kind, *dataLabel, static_cast<uint32_t>(placed->at),
                                     static_cast<uint32_t>(placed->size)});
              out_.placedFiles.push_back({dir->kind, dir->name, static_cast<uint32_t>(placed->at),
                                          static_cast<uint32_t>(placed->size)});
            }
          }
          continue;
        }
        if (dataLabel) defineLabel(lineNo, *dataLabel, Label::Kind::Data, static_cast<int64_t>(cartOffset_));
        if (startsWith(line, "ds ") || startsWith(line, "ds\t")) {
          auto n = foldNow(lineNo, trim(std::string_view(line).substr(3)), "ds");
          if (n) {
            if (cartOffset_ + static_cast<size_t>(*n) > static_cast<size_t>(CART_DATA_SIZE)) {
              err(lineNo, ".data exceeds the 1MB cartridge");
            } else {
              cartOffset_ += static_cast<size_t>(*n);
              out_.cart.resize(cartOffset_);
            }
          }
          continue;
        }
        for (std::string item : splitDataItems(line)) {
          item = std::string(trim(item));
          if (item.empty()) continue;
          if (!dataWidth(item, cartWidth)) continue;
          if (auto str = decodeString(item)) {
            for (uint8_t byte : *str) {
              if (cartOffset_ + 1 > static_cast<size_t>(CART_DATA_SIZE)) {
                err(lineNo, ".data exceeds the 1MB cartridge");
                break;
              }
              cartItems_.push_back({lineNo, 1, std::to_string(byte), cartOffset_});
              cartOffset_ += 1;
            }
            continue;
          }
          if (cartOffset_ + static_cast<size_t>(cartWidth) > static_cast<size_t>(CART_DATA_SIZE)) {
            err(lineNo, ".data exceeds the 1MB cartridge");
            break;
          }
          cartItems_.push_back({lineNo, cartWidth, item.front() == '&' ? item.substr(1) : item, cartOffset_});
          cartOffset_ += static_cast<size_t>(cartWidth);
        }
        out_.cart.resize(cartOffset_);
        continue;
      }

      // .org places what follows at an instruction slot, so a driver can
      // sit at a known address a program reaches by JSR through a vector.
      // The count folds in pass one, like a port and a .addr do.
      if (startsWith(line, ".equ ") || startsWith(line, ".equ\t")) {
        defineEqu(lineNo, trim(std::string_view(line).substr(5)));
        continue;
      }

      if (startsWith(line, ".org ") || startsWith(line, ".org\t")) {
        auto n = foldNow(lineNo, trim(std::string_view(line).substr(5)), ".org");
        if (n) {
          if (*n > 0xffff) err(lineNo, ".org takes an instruction slot 0..65535");
          else slot_ = static_cast<uint32_t>(*n);
        }
        continue;
      }

      parseInstruction(lineNo, line);
    }
  }

  // db and dw switch the current width. Returns false when the item was
  // the keyword alone.
  static bool dataWidth(std::string& item, int& width) {
    std::string head = lower(item.substr(0, 2));
    if ((head == "db" || head == "dw") &&
        (item.size() == 2 || !std::isalnum(static_cast<unsigned char>(item[2])))) {
      width = head == "db" ? 1 : 2;
      item = std::string(trim(std::string_view(item).substr(2)));
      return !item.empty();
    }
    return true;
  }

  void pushInstr(PendingInstr p) {
    p.slot = slot_;
    if (slots_.count(slot_)) err(p.line, "instruction slot " + std::to_string(slot_) + " is already used");
    slots_.insert(slot_);
    out_.lineToInstr[p.line] = static_cast<int>(slot_);
    if (slot_ > 0xffff) err(p.line, "the program runs past slot 65535");
    pending_.push_back(std::move(p));
    slot_++;
  }

  void pushLiteral(int line, std::string opName, int64_t literal = 0) {
    pushInstr({line, 0, std::move(opName), std::nullopt, ExprKind::Imm8, literal});
  }

  void pushExpr(int line, std::string opName, std::string expr, ExprKind kind, int64_t literal = 0) {
    pushInstr({line, 0, std::move(opName), std::move(expr), kind, literal});
  }

  struct Arrow {
    std::string dst, src;
  };

  // Normalize arrows: "LD X <- Y" keeps dst left, "LD Y -> X" swaps.
  static std::optional<Arrow> splitArrow(std::string_view rest) {
    size_t p = rest.find("<-");
    if (p != std::string_view::npos) {
      return Arrow{std::string(trim(rest.substr(0, p))), std::string(trim(rest.substr(p + 2)))};
    }
    p = rest.find("->");
    if (p != std::string_view::npos) {
      return Arrow{std::string(trim(rest.substr(p + 2))), std::string(trim(rest.substr(0, p)))};
    }
    return std::nullopt;
  }

  void parseInstruction(int lineNo, const std::string& line) {
    size_t ws = 0;
    while (ws < line.size() && !std::isspace(static_cast<unsigned char>(line[ws]))) ws++;
    const std::string mnem = upper(std::string_view(line).substr(0, ws));
    const std::string rest(trim(std::string_view(line).substr(ws)));
    const std::string restUpper = upper(rest);

    if (mnem == "NOP" || mnem == "HLT" || mnem == "RET") {
      if (!rest.empty()) return err(lineNo, mnem + " takes no operand");
      return pushLiteral(lineNo, mnem);
    }

    if (BRANCHES.count(mnem)) {
      if (rest.empty()) return err(lineNo, mnem + " needs a target");
      // JMP D1 and JSR D2: the address is in the register, so there is no
      // operand to fold. Only JMP and JSR take the indirect form.
      if (mnem == "JMP" || mnem == "JSR") {
        if (restUpper == "D1" || restUpper == "D2") return pushLiteral(lineNo, mnem + " " + restUpper);
        // Square brackets are contents-of. [D1] is the BYTE at that
        // address, which is not a 16 bit target, so it is refused by name.
        const std::string bare = stripSpaces(restUpper);
        if (bare == "[D1]" || bare == "[D2]") {
          return err(lineNo, mnem + " takes the register itself. Use " + mnem + " " + bare.substr(1, 2) + ", not " + rest);
        }
        if (restUpper == "A" || restUpper == "B" || restUpper == "SP" || restUpper == "PC") {
          return err(lineNo, mnem + " through a register works on D1 and D2 only");
        }
      }
      std::string target = rest.front() == '&' ? rest.substr(1) : rest;
      return pushExpr(lineNo, mnem, target, ExprKind::Target);
    }

    if (mnem == "PUSHB" || mnem == "POPB") {
      if (restUpper != "A") return err(lineNo, mnem + " works on A");
      return pushLiteral(lineNo, mnem + " A");
    }
    if (mnem == "PUSHW" || mnem == "POPW") {
      if (restUpper != "D1" && restUpper != "D2") return err(lineNo, mnem + " works on D1 or D2");
      return pushLiteral(lineNo, mnem + " " + restUpper);
    }
    // PUSH and POP pick the byte or word opcode from the register.
    if (mnem == "PUSH" || mnem == "POP") {
      const std::string byte = mnem == "PUSH" ? "PUSHB A" : "POPB A";
      const std::string word = mnem == "PUSH" ? "PUSHW" : "POPW";
      if (restUpper == "A") return pushLiteral(lineNo, byte);
      if (restUpper == "D1" || restUpper == "D2") return pushLiteral(lineNo, word + " " + restUpper);
      return err(lineNo, mnem + " works on A, D1, or D2");
    }

    if (mnem == "INC" || mnem == "DEC") {
      if (restUpper == "A") {
        // One-to-one aliases: INC A = ADD A <- 1, DEC A = SUB A <- 1.
        return pushLiteral(lineNo, mnem == "INC" ? "ADD A <- imm8" : "SUB A <- imm8", 1);
      }
      if (mnem == "INC" && (restUpper == "D1" || restUpper == "D2")) {
        return pushLiteral(lineNo, "INC " + restUpper);
      }
      if (restUpper == "D3") {
        return err(lineNo, mnem + " D3 does not exist; step it with LD D3 <- D3" + (mnem == "INC" ? "+1" : "-1"));
      }
      return err(lineNo, mnem + " " + rest + " is not an instruction");
    }

    if (mnem == "OUT") {
      // The arrow form is OUTA in LD's clothing: OUT A -> GPU_COLOR and
      // OUT GPU_COLOR <- A both say "send the accumulator to the port".
      if (auto arrow = splitArrow(rest)) {
        if (upper(arrow->src) != "A") return err(lineNo, "OUT with an arrow sends A: OUT A -> port");
        auto port = parsePort(arrow->dst);
        if (!port || *port > 0xff || *port < 0) return err(lineNo, "bad port number");
        return pushLiteral(lineNo, "OUTA", *port << 8);
      }
      size_t comma = rest.find(',');
      if (comma == std::string::npos || rest.find(',', comma + 1) != std::string::npos) {
        return err(lineNo, "OUT needs: OUT port, value");
      }
      auto port = parsePort(trim(std::string_view(rest).substr(0, comma)));
      if (!port || *port > 0xff || *port < 0) return err(lineNo, "bad port number");
      const std::string valText(trim(std::string_view(rest).substr(comma + 1)));
      auto val = parseOutValue(valText);
      if (!val) {
        // A label or get_*byte() expression: resolved in pass 2.
        return pushExpr(lineNo, "OUT", stripSpaces(valText), ExprKind::Out8, *port << 8);
      }
      if (*val > 0xff) return err(lineNo, "bad OUT value");
      return pushLiteral(lineNo, "OUT", (*port << 8) | (*val & 0xff));
    }
    if (mnem == "IN" || mnem == "INB" || mnem == "INW") {
      // One mnemonic reads a port. The target picks the width: A is a byte
      // (INB), D1 or D2 is a word (INW), read high byte first.
      std::string target, portText;
      if (auto arrow = splitArrow(rest)) {
        const std::string d = upper(arrow->dst);
        if (d == "A" || d == "D1" || d == "D2") {
          target = d;
          portText = arrow->src;
        } else {
          target = upper(arrow->src);
          portText = arrow->dst;
        }
      } else {
        target = mnem == "INW" ? "D2" : "A";
        portText = rest;
      }
      if (mnem == "INB" && target != "A") return err(lineNo, "INB reads a byte into A");
      if (mnem == "INW" && target == "A") return err(lineNo, "INW reads a word into D1 or D2");
      if (target != "A" && target != "D1" && target != "D2") return err(lineNo, "IN reads into A, D1, or D2");
      auto port = parsePort(trim(portText));
      if (!port || *port > 0xff || *port < 0) return err(lineNo, "bad port number");
      return pushLiteral(lineNo, target == "A" ? "INB" : "INW " + target, *port << 8);
    }
    if (mnem == "OUTA") {
      auto port = parsePort(rest);
      if (!port || *port > 0xff || *port < 0) return err(lineNo, "bad port number");
      return pushLiteral(lineNo, "OUTA", *port << 8);
    }

    if (isAluMnemonic(mnem)) {
      auto arrow = splitArrow(rest);
      if (!arrow || upper(arrow->dst) != "A") return err(lineNo, mnem + " writes A: " + mnem + " A <- source");
      auto src = parseOperand(arrow->src);
      if (src && src->t == T::Round) return roundErr(lineNo, *src);
      if (src && src->t == T::Val) return pushExpr(lineNo, mnem + " A <- imm8", src->expr, ExprKind::Imm8);
      if (src && src->t == T::Mem) return pushExpr(lineNo, mnem + " A <- [addr8]", src->expr, ExprKind::Addr8);
      if (src && src->t == T::DispD) {
        return pushExpr(lineNo, mnem + " A <- [D" + std::to_string(src->x) + "+n]", src->expr, ExprKind::Disp8);
      }
      return err(lineNo, mnem + " takes an immediate, a zero page address, or [D1+n], [D2+n] or [D3+n]");
    }

    // TST D1, TST D2 and TST D3: the address add with nothing added, which
    // sets Z from the whole pointer. The NULL test for a register.
    if (mnem == "TST" && (restUpper == "D1" || restUpper == "D2" || restUpper == "D3")) {
      return pushExpr(lineNo, "LD " + restUpper + " <- " + restUpper + "+n", std::string("0"), ExprKind::Off16);
    }

    if (isTestMnemonic(mnem)) {
      // CMP and TST write no register, so they take no arrow: CMP A, 5.
      const std::string what = mnem == "CMP" ? "subtracts" : "ANDs";
      if (splitArrow(rest)) return err(lineNo, mnem + " " + what + " and keeps A, so it takes no arrow: " + mnem + " A, source");
      const size_t comma = rest.find(',');
      if (comma == std::string::npos || upper(trim(std::string_view(rest).substr(0, comma))) != "A") {
        return err(lineNo, mnem + " compares A with a source: " + mnem + " A, source");
      }
      auto src = parseOperand(trim(std::string_view(rest).substr(comma + 1)));
      if (src && src->t == T::Round) return roundErr(lineNo, *src);
      if (src && src->t == T::Val) return pushExpr(lineNo, mnem + " A, imm8", src->expr, ExprKind::Imm8);
      if (src && src->t == T::Mem) return pushExpr(lineNo, mnem + " A, [addr8]", src->expr, ExprKind::Addr8);
      if (src && src->t == T::DispD) {
        return pushExpr(lineNo, mnem + " A, [D" + std::to_string(src->x) + "+n]", src->expr, ExprKind::Disp8);
      }
      return err(lineNo, mnem + " takes an immediate, a zero page address, or [D1+n], [D2+n] or [D3+n]");
    }

    if (isShiftMnemonic(mnem)) {
      if (restUpper != "A") return err(lineNo, mnem + " shifts A: " + mnem + " A");
      return pushLiteral(lineNo, mnem + " A");
    }

    if (mnem == "LD") {
      auto arrow = splitArrow(rest);
      if (!arrow) return err(lineNo, "LD needs an arrow: LD dst <- src");
      auto dst = parseOperand(arrow->dst);
      auto src = parseOperand(arrow->src);
      if (!dst || !src) return err(lineNo, "unreadable operand");
      if (dst->t == T::Round) return roundErr(lineNo, *dst);
      if (src->t == T::Round) return roundErr(lineNo, *src);
      return ldInstruction(lineNo, *dst, *src);
    }

    err(lineNo, "unknown mnemonic: " + mnem);
  }

  static constexpr const char* D3_INDEX =
      "[D3+A] does not exist; point D2 at the array with LD D2 <- D3+n, then use [D2+A]";

  void ldInstruction(int lineNo, const Operand& dst, const Operand& src) {
    auto push = [&](const std::string& opName, std::optional<std::string> expr = std::nullopt,
                    ExprKind kind = ExprKind::Imm8) {
      if (!opByName(opName)) return err(lineNo, "internal: " + opName + " missing from ISA");
      if (expr) pushExpr(lineNo, opName, *expr, kind);
      else pushLiteral(lineNo, opName);
    };
    const std::string plus = "+";

    // Stack sugar: a store to [SP]- is a push, a load from [SP]+ is a pop.
    if (dst.t == T::Stack) {
      if (!dst.push) return err(lineNo, "a store to the stack uses [SP]-: LD [SP]- <- A");
      if (src.t != T::Reg || src.r == "D3") return err(lineNo, "the stack takes a register: LD [SP]- <- A, D1, or D2");
      return push(src.r == "A" ? "PUSHB A" : "PUSHW " + src.r);
    }
    if (src.t == T::Stack) {
      if (src.push) return err(lineNo, "a load from the stack uses [SP]+: LD A <- [SP]+");
      if (dst.t != T::Reg || dst.r == "D3") return err(lineNo, "the stack fills a register: LD A, D1, or D2 <- [SP]+");
      return push(dst.r == "A" ? "POPB A" : "POPW " + dst.r);
    }

    if (dst.t == T::Reg && dst.r == "A") {
      switch (src.t) {
        case T::Val: return push("LD A <- imm8", src.expr, ExprKind::Imm8);
        case T::Mem: return push("LD A <- [addr8]", src.expr, ExprKind::Addr8);
        case T::IndA:
          if (src.inc) return err(lineNo, "LD [A]+ -> A overwrites the register being stepped");
          return push("LD A <- [A]");
        case T::IndD: return push("LD A <- [D" + std::to_string(src.x) + "]" + (src.inc ? plus : ""));
        case T::DispD: return push("LD A <- [D" + std::to_string(src.x) + "+n]", src.expr, ExprKind::Disp8);
        case T::IdxD:
          if (src.x == 3) return err(lineNo, D3_INDEX);
          return push("LD A <- [D" + std::to_string(src.x) + "+A]");
        case T::Reg: return err(lineNo, "register to register moves do not exist");
        case T::Sum: return err(lineNo, "A is a byte and cannot hold an address sum; load it into D1 or D2");
        default: break;
      }
    }

    if (dst.t == T::Reg && dst.r == "D3") {
      switch (src.t) {
        case T::Mem: return push("LD D3 <- [addr16]", src.expr, ExprKind::Addr16);
        case T::Sum:
          if (src.plusA) return err(lineNo, "D3 takes a constant offset only: LD D3 <- D" + std::to_string(src.x) + "+n");
          return push("LD D3 <- D" + std::to_string(src.x) + "+n", src.expr, ExprKind::Off16);
        case T::Reg:
          if (src.r == "A") return err(lineNo, "a D register takes a byte from A only through RAM: store A, then load the word");
          return push("LD D3 <- " + src.r + "+n", std::string("0"), ExprKind::Off16);
        case T::Val: return err(lineNo, "LD D3 takes no constant: LD D1 <- value, then LD D3 <- D1");
        default: return err(lineNo, "D3 loads from an address, or from D1, D2 or D3 plus an offset");
      }
    }

    if (dst.t == T::Reg) {
      const int x = dst.r == "D1" ? 1 : 2;
      const std::string X = "D" + std::to_string(x);
      const std::string S = "D" + std::to_string(src.x);
      switch (src.t) {
        case T::Val: return push("LD " + X + " <- imm16", src.expr, ExprKind::Imm16);
        case T::Mem: return push("LD " + X + " <- [addr16]", src.expr, ExprKind::Addr16);
        case T::IndA: return push("LD " + X + " <- [A]" + (src.inc ? plus : ""));
        case T::IndD:
          if (src.x == x) return err(lineNo, "LD " + X + " <- [" + X + "] corrupts its own address; use the other D register");
          return push("LD " + X + " <- [" + S + "]" + (src.inc ? plus : ""));
        case T::DispD:
          if (src.x == x) return err(lineNo, "LD " + X + " <- [" + X + "+n] corrupts its own address; use the other D register");
          return push("LD " + X + " <- [" + S + "+n]", src.expr, ExprKind::Disp8);
        case T::IdxD:
          if (src.x == 3) return err(lineNo, D3_INDEX);
          if (src.x == x) return err(lineNo, "LD " + X + " <- [" + X + "+A] corrupts its own address; use the other D register");
          return push("LD " + X + " <- [" + S + "+A]");
        case T::Sum:
          if (src.plusA && src.x == 3) return err(lineNo, "D3 takes a constant offset only: LD " + X + " <- D3+n");
          if (src.plusA) return push("LD " + X + " <- " + S + "+A");
          return push("LD " + X + " <- " + S + "+n", src.expr, ExprKind::Off16);
        case T::Reg:
          // A copy is the sum with nothing added. Onto itself it changes
          // only Z, which is the NULL test TST D1 spells.
          if (src.r == "D1" || src.r == "D2" || src.r == "D3") {
            return push("LD " + X + " <- " + src.r + "+n", std::string("0"), ExprKind::Off16);
          }
          return err(lineNo, "a D register takes a byte from A only through RAM: store A, then load the word");
        default: break;
      }
    }

    if (dst.t == T::Mem) {
      if (src.t == T::Reg && src.r == "A") return push("LD [addr8] <- A", dst.expr, ExprKind::Addr8);
      if (src.t == T::Reg) return push("LD [addr16] <- " + src.r, dst.expr, ExprKind::Addr16);
      return err(lineNo, "stores take a register source");
    }

    const std::string X = "D" + std::to_string(dst.x);
    if (dst.t == T::IndD) {
      if (src.t == T::Reg && src.r == "A") return push("LD [" + X + "]" + (dst.inc ? plus : "") + " <- A");
      if (src.t == T::Reg && src.r == "D3") return err(lineNo, "D3 is stored only at an address: LD [addr16] <- D3");
      if (src.t == T::Reg) {
        const int y = src.r == "D1" ? 1 : 2;
        if (dst.inc) return err(lineNo, "word stores take no +");
        if (y == dst.x) return err(lineNo, "LD [" + X + "] <- " + src.r + " is not in the instruction set; use the other D register");
        return push("LD [" + X + "] <- " + src.r);
      }
      return err(lineNo, "stores take a register source");
    }

    if (dst.t == T::DispD) {
      if (src.t == T::Reg && src.r == "A") return push("LD [" + X + "+n] <- A", dst.expr, ExprKind::Disp8);
      if (src.t == T::Reg && src.r == "D3") return err(lineNo, "D3 is stored only at an address: LD [addr16] <- D3");
      if (src.t == T::Reg) {
        const int y = src.r == "D1" ? 1 : 2;
        if (y == dst.x) return err(lineNo, "LD [" + X + "+n] <- " + src.r + " is not in the instruction set; use the other D register");
        return push("LD [" + X + "+n] <- " + src.r, dst.expr, ExprKind::Disp8);
      }
      return err(lineNo, "stores take a register source");
    }

    if (dst.t == T::IdxD && dst.x == 3) return err(lineNo, D3_INDEX);
    if (dst.t == T::IdxD) return err(lineNo, "indexed stores do not exist: A is the index and the only byte source");
    if (dst.t == T::IndA) return err(lineNo, "stores through [A] do not exist; use a D register");
    err(lineNo, "unsupported LD shape");
  }

  // Float data values, for db and dw only. A bare float is unsigned
  // normalized: 0..1 scales to the full width, saturating at the top.
  // sin(t) and cos(t) take TURNS and encode centered, 128 in the middle, so
  // the bytes feed straight into DRAW_PATH.
  std::optional<int64_t> resolveFloatData(int line, const std::string& expr, int width) {
    const std::string low = lower(expr);
    if ((startsWith(low, "sin(") || startsWith(low, "cos(")) && expr.back() == ')') {
      const std::string argText = expr.substr(4, expr.size() - 5);
      std::optional<double> arg = parseFloatLit(argText);
      if (!arg) {
        if (auto n = parseNumber(argText)) arg = static_cast<double>(*n);
      }
      if (!arg) {
        err(line, "bad " + low.substr(0, 3) + " argument: " + argText + " (turns, e.g. sin(0.25))");
        return 0;
      }
      const double v = startsWith(low, "sin") ? std::sin(2 * std::numbers::pi * *arg)
                                              : std::cos(2 * std::numbers::pi * *arg);
      return width == 1 ? 128 + std::llround(127 * v) : 32768 + std::llround(32767 * v);
    }
    auto f = parseFloatLit(expr);
    if (!f) return std::nullopt;
    if (*f < 0 || *f > 1) {
      err(line, "float data values are 0..1 (got " + expr + ")");
      return 0;
    }
    const double scale = width == 1 ? 256 : 65536;
    return std::min<int64_t>(std::llround(*f * scale), static_cast<int64_t>(scale) - 1);
  }

  // Pass 2: resolve expressions. The get_*byte macros split a cartridge
  // address (or any value) into the bytes the GPU ports want.
  std::optional<int64_t> resolve(int line, const std::string& expr) {
    if (auto macro = parseMacro(expr)) {
      const std::string& kind = macro->first;
      const std::string& inner = macro->second;
      if (kind == "sizelo" || kind == "sizehi") {
        const std::string name(trim(inner));
        auto it = blobSizes_.find(name);
        if (it == blobSizes_.end()) {
          err(line, "get_" + kind + " needs a label on a " + directiveList() + " line, not " + name);
          return std::nullopt;
        }
        // Two bytes carry at most 65535, and a 256x256 image is 65536.
        // Reporting 0 for it would be a lie, so the macros refuse the blob.
        if (static_cast<int64_t>(it->second) > MAX_BLOB_SIZE) {
          err(line, "get_" + kind + ": " + name + " is " + std::to_string(it->second) +
                        " bytes, past the " + std::to_string(MAX_BLOB_SIZE) + " byte ceiling the size macros report");
          return std::nullopt;
        }
        const int64_t size = static_cast<int64_t>(it->second);
        return kind == "sizelo" ? size & 0xff : (size >> 8) & 0xff;
      }
      if (kind == "lowbyte" || kind == "highbyte" || kind == "bankbyte") {
        auto v = resolve(line, inner);
        if (!v) return std::nullopt;
        if (kind == "lowbyte") return *v & 0xff;
        if (kind == "highbyte") return (*v >> 8) & 0xff;
        return (*v >> 16) & 0x0f;
      }
    }
    if (auto n = parseNumber(expr)) return n;
    auto label = out_.labels.find(expr);
    if (label != out_.labels.end()) return label->second.value;
    // Only something carrying an operator reaches the evaluator, so a
    // plain typo still gets this file's own message.
    if (looksLikeExpression(expr)) {
      ExprResult folded = evalExpr(expr, [&](std::string_view name) -> std::optional<int64_t> {
        if (auto known = parseNumber(name)) return known;
        auto it = out_.labels.find(std::string(name));
        if (it == out_.labels.end()) return std::nullopt;
        return it->second.value;
      });
      if (folded.ok) return folded.value;
      if (folded.kind != ExprFailure::Syntax) {
        err(line, folded.message);
        return std::nullopt;
      }
    }
    err(line, "undefined label: " + expr);
    return std::nullopt;
  }

  void passTwo() {
    uint32_t top = 0;
    for (const PendingInstr& p : pending_) top = std::max(top, p.slot + 1);
    out_.program.assign(std::min<size_t>(top, 65536), UNLOADED_SLOT);
    out_.instrToLine.assign(out_.program.size(), 0);
    for (const PendingInstr& p : pending_) {
      if (p.slot >= out_.program.size()) continue;
      const OpDef* def = opByName(p.opName);
      Instr& target = out_.program[p.slot];
      out_.instrToLine[p.slot] = p.line;
      int64_t operand = p.literal;
      if (p.expr) {
        auto v = resolve(p.line, *p.expr);
        if (!v) {
          target = {def->op, 0};
          continue;
        }
        switch (p.exprKind) {
          case ExprKind::Imm8:
          case ExprKind::Disp8:
            if (*v > 0xff) err(p.line, "value " + std::to_string(*v) + " does not fit in a byte");
            operand = *v & 0xff;
            break;
          case ExprKind::Addr8: {
            auto label = out_.labels.find(*p.expr);
            if (label != out_.labels.end() && label->second.kind != Label::Kind::Ram) {
              err(p.line, *p.expr + " is not a RAM label");
            }
            if (*v >= ZERO_PAGE) {
              err(p.line, "byte direct addressing is zero page only (address " + std::to_string(*v) + "); use a D register");
            }
            operand = *v & 0xff;
            break;
          }
          case ExprKind::Imm16:
            if (*v > 0xffff) err(p.line, "value " + std::to_string(*v) + " does not fit in 16 bits");
            operand = *v & 0xffff;
            break;
          case ExprKind::Off16:
            if (*v > 0xffff || *v < -0xffff) err(p.line, "offset " + std::to_string(*v) + " does not fit in 16 bits");
            operand = *v & 0xffff;
            break;
          case ExprKind::Addr16:
            if (*v >= RAM_SIZE) err(p.line, "address " + std::to_string(*v) + " is outside data RAM");
            operand = *v & 0xffff;
            break;
          case ExprKind::Target: {
            auto label = out_.labels.find(*p.expr);
            if (label != out_.labels.end() && label->second.kind != Label::Kind::Code) {
              err(p.line, *p.expr + " is not a code label");
            }
            operand = *v & 0xffff;
            break;
          }
          case ExprKind::Out8:
            if (*v > 0xff) err(p.line, "OUT value " + std::to_string(*v) + " does not fit in a byte");
            operand = p.literal | (*v & 0xff);
            break;
        }
      }
      target = {def->op, static_cast<uint16_t>(operand & 0xffff)};
    }

    // RAM image.
    size_t offset = 0;
    for (const RamItem& item : ramItems_) {
      if (item.skip) {
        if (offset + static_cast<size_t>(*item.skip) > static_cast<size_t>(RAM_SIZE)) {
          err(item.line, ".ram exceeds " + std::to_string(RAM_SIZE) + " bytes");
          break;
        }
        offset += static_cast<size_t>(*item.skip);
        continue;
      }
      int64_t v = dataValue(item.line, item.expr, item.width);
      if (offset + static_cast<size_t>(item.width) > static_cast<size_t>(RAM_SIZE)) {
        err(item.line, ".ram exceeds " + std::to_string(RAM_SIZE) + " bytes");
        break;
      }
      if (item.width == 1) {
        if (v > 0xff) err(item.line, "db value " + std::to_string(v) + " does not fit in a byte");
        out_.ram[offset++] = static_cast<uint8_t>(v & 0xff);
      } else {
        out_.ram[offset++] = static_cast<uint8_t>((v >> 8) & 0xff);
        out_.ram[offset++] = static_cast<uint8_t>(v & 0xff);
      }
    }
    out_.ramLength = offset;

    // Cartridge values from db and dw in .data.
    for (const CartItem& item : cartItems_) {
      int64_t v = dataValue(item.line, item.expr, item.width);
      if (item.width == 1) {
        if (v > 0xff) err(item.line, "db value " + std::to_string(v) + " does not fit in a byte");
        out_.cart[item.at] = static_cast<uint8_t>(v & 0xff);
      } else {
        out_.cart[item.at] = static_cast<uint8_t>((v >> 8) & 0xff);
        out_.cart[item.at + 1] = static_cast<uint8_t>(v & 0xff);
      }
    }
  }

  int64_t dataValue(int line, const std::string& expr, int width) {
    if (auto f = resolveFloatData(line, expr, width)) return *f;
    return resolve(line, expr).value_or(0);
  }

  std::string_view source_;
  const Assets* assets_;
  Assembled out_;
  std::vector<PendingInstr> pending_;
  uint32_t slot_ = 0;
  std::set<uint32_t> slots_;
  std::vector<RamItem> ramItems_;
  std::vector<CartItem> cartItems_;
  int64_t ramOffset_ = 0;
  size_t cartOffset_ = 0;
  std::unordered_map<uint32_t, std::vector<std::pair<size_t, size_t>>> blobIndex_;
  // Byte length of the blob each asset-directive label names, for the size
  // macros.
  std::map<std::string, size_t> blobSizes_;
  std::map<std::string, std::vector<uint8_t>> files_, samples_, fonts_;
  std::map<std::string, ImageAsset> images_;
};

}  // namespace

Cartridge Assembled::cartridge() const {
  Cartridge c;
  c.program = program;
  c.ram.assign(ram.begin(), ram.begin() + static_cast<long>(ramLength));
  c.data = cart;
  c.assets = assets;
  return c;
}

Assembled assemble(std::string_view source, const Assets* assets) {
  return Assembler(source, assets).run();
}

}  // namespace sc8
