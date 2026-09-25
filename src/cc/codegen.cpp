#include "cc/codegen.h"

#include "assets/assets.h"

#include <algorithm>
#include <array>
#include <set>

#include "cc/doubles.h"
#include "cc/layout.h"
#include "cc/lex.h"
#include "cc/parse.h"
#include "cc/peephole.h"
#include "cc/runtime.h"
#include "cc/softmath.h"
#include "devices/constants.h"
#include "devices/gpu.h"

namespace sc8::cc {

namespace {

constexpr const char* PRINTF_ARGS = "__pfargs";

struct Sym {
  std::string name;
  std::string label;
  CType type;
  enum class Where { Global, Frame } where = Where::Global;
  int offset = 0;  // frame offset, for Frame
  int addr = 0;    // RAM address, for Global
};

[[noreturn]] void fail(const Pos& pos, const std::string& msg) { throw CcError(pos.file, pos.line, msg); }

std::string S(int n) { return std::to_string(n); }
std::string S(const char* s) { return std::string(s); }

// The result type of an arithmetic pair, by C's usual promotions cut down to
// the types this subset has.
CType common(const CType& a, const CType& b) {
  if (a.ptr > 0) return a;
  if (b.ptr > 0) return b;
  if (isWide(a) || isWide(b)) return isWide(a) ? a : b;
  if (sizeOf(a) == 2 || sizeOf(b) == 2) {
    return isSigned(a) && isSigned(b) ? T(BaseType::Int) : T(BaseType::UInt);
  }
  // Two bytes still promote to int, which is what C does and what stops
  // 200 + 100 from being 44 in an expression that was written in ints.
  return isSigned(a) && isSigned(b) ? T(BaseType::Int) : T(BaseType::UInt);
}

int constNow(const ExprPtr& e, const Pos& pos) {
  const auto v = foldConst(e);
  if (!v) fail(pos, "a __ROM object's initializer has to be a constant");
  return toInt32(*v) & 0xffff;
}

std::string joinBytes(const std::vector<uint8_t>& bytes, bool terminate) {
  std::string out;
  for (size_t i = 0; i < bytes.size(); i++) {
    if (i) out += ", ";
    out += S(bytes[i]);
  }
  if (terminate) out += bytes.empty() ? "0" : ", 0";
  return out;
}

std::string bytesKey(const std::vector<uint8_t>& bytes) {
  std::string key;
  for (size_t i = 0; i < bytes.size(); i++) {
    if (i) key += ",";
    key += S(bytes[i]);
  }
  return key;
}

std::string pad8(const std::string& name) {
  return std::string(static_cast<size_t>(std::max(1, 8 - static_cast<int>(name.size()) - 1)), ' ');
}

bool isCompare(const std::string& op) {
  return op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=";
}

ExprPtr numOne(const Pos& pos) {
  auto e = std::make_shared<Expr>();
  e->k = ExprKind::Num;
  e->value = 1;
  e->type = T(BaseType::Int);
  e->pos = pos;
  return e;
}

ExprPtr mkBin(const std::string& op, ExprPtr l, ExprPtr r, const Pos& pos) {
  auto e = std::make_shared<Expr>();
  e->k = ExprKind::Bin;
  e->op = op;
  e->l = std::move(l);
  e->r = std::move(r);
  e->pos = pos;
  return e;
}

ExprPtr mkAssign(ExprPtr l, ExprPtr r, const Pos& pos) {
  auto e = std::make_shared<Expr>();
  e->k = ExprKind::Assign;
  e->op = "=";
  e->l = std::move(l);
  e->r = std::move(r);
  e->pos = pos;
  return e;
}

// The printf template's conversions, as the device's formatter reads them.
struct Spec {
  std::string length;
  char conv;
  size_t next;
};

std::optional<Spec> parseSpec(const std::vector<uint8_t>& t, size_t start) {
  size_t p = start;
  auto at = [&]() { return p < t.size() ? static_cast<char>(t[p]) : '\0'; };
  for (;;) {
    const char c = at();
    if (c == '-' || c == '+' || c == ' ' || c == '0' || c == '#') p++;
    else break;
  }
  while (at() >= '0' && at() <= '9') p++;
  if (at() == '.') {
    p++;
    while (at() >= '0' && at() <= '9') p++;
  }
  std::string length;
  if (at() == 'h') {
    p++;
    if (at() == 'h') { length = "hh"; p++; } else length = "h";
  } else if (at() == 'l') {
    p++;
    if (at() == 'l') { length = "ll"; p++; } else length = "l";
  }
  const char conv = at();
  if (conv == '\0') return std::nullopt;
  const std::string_view INT_CONVS = "diuoxXb";
  const std::string_view FLOAT_CONVS = "fFeEgG";
  if (conv != '%' && conv != 'c' && conv != 's' && conv != 'r' &&
      INT_CONVS.find(conv) == std::string_view::npos && FLOAT_CONVS.find(conv) == std::string_view::npos) {
    return std::nullopt;
  }
  return Spec{length, conv, p + 1};
}

int intBytes(const std::string& length) {
  if (length == "hh") return 1;
  if (length == "h") return 2;
  if (length == "l") return 4;
  if (length == "ll") return 8;
  return 2;  // int is 16 bits
}

class Gen {
 public:
  Gen(const Unit& unit, bool softMul, const Profile* profile, int zpReserve, const Assets* assets)
      : unit_(hoistStatics(unit)), softMul_(softMul), profile_(profile), zpReserve_(zpReserve) {
    for (const RomEntry& e : layoutRom(unit.vars, assets)) romPlan_[e.name] = e;
  }

  // The label a function's static local lives under. It is a global with
  // the function's name folded in, so two functions may each have a
  // `static int n` and neither sees the other's. The `__st_` in the middle
  // keeps it clear of the function's own `f__end` label.
  static std::string staticLabel(const std::string& func, const std::string& name) {
    return func + "__st_" + name;
  }

  // A static inside a function is a hidden global: it is laid down in .ram
  // with its initializer, once, and it competes for the zero page like any
  // other global. The unit is copied with those declarations appended, so
  // the allocator sees them without knowing where they came from.
  static Unit hoistStatics(const Unit& unit) {
    Unit out = unit;
    for (const FuncDecl& f : unit.funcs) {
      if (!f.body) continue;
      std::function<void(const StmtPtr&)> walk = [&](const StmtPtr& s) {
        if (!s) return;
        switch (s->k) {
          case StmtKind::Var:
            if (s->decl.storage == Storage::Static) {
              VarDecl v = s->decl;
              v.name = staticLabel(f.name, s->decl.name);
              for (const VarDecl& seen : out.vars) {
                if (seen.name == v.name) fail(v.pos, "static " + s->decl.name + " is declared twice in " + f.name);
              }
              v.storage = Storage::Auto;
              out.vars.push_back(v);
            }
            break;
          case StmtKind::Block: for (const StmtPtr& x : s->body) walk(x); break;
          case StmtKind::If: walk(s->t); walk(s->f); break;
          case StmtKind::While: case StmtKind::Do: walk(s->loopBody); break;
          case StmtKind::For: walk(s->init); walk(s->loopBody); break;
          case StmtKind::Switch: for (const SwitchCase& c : s->cases) for (const StmtPtr& x : c.body) walk(x); break;
          default: break;
        }
      };
      walk(f.body);
    }
    return out;
  }

  // DESIGN: two passes over the body, and the reason is a chicken and egg.
  // A global's address decides whether it is in the zero page, which decides
  // whether a byte load is one instruction or three. But the addresses start
  // after the compiler's own reservations, and the number of expression
  // temps is not known until the deepest expression has been generated.
  //
  // So the first pass counts the temps and its output is thrown away. The
  // second lays the globals down after the real reservation and generates
  // for real. Nothing in pass one can change the temp count in pass two:
  // choosing the zero page path for a load spends no slot either way.
  Compiled compile() {
    plan();  // once: it checks for a redefinition, which is not idempotent
    body();  // pass one, thrown away, run only to count the temps
    globalBase_ = zpReserve_ + RESERVED_BYTES + 2 * maxSlot_;
    reset();
    std::vector<int> remap;
    const std::vector<std::string> lines = peephole(body(), &remap);
    for (auto& [k, v] : lineOf_) v = remap[static_cast<size_t>(v)];
    return assemble(lines);
  }

 private:
  const Unit unit_;
  bool softMul_;
  const Profile* profile_;
  // Bytes at the start of the zero page the program keeps for itself.
  int zpReserve_;

  std::vector<std::string> out_;
  std::map<std::string, Sym> globals_;
  std::map<std::string, FuncDecl> funcs_;
  std::vector<std::map<std::string, Sym>> scopes_;
  std::map<std::string, std::string> strings_;  // bytes key -> label
  struct StrData { std::string label; std::vector<uint8_t> bytes; };
  std::vector<StrData> stringData_;
  std::set<std::string> runtimeUsed_;
  int maxSlot_ = 0;
  int labelSeq_ = 0;
  std::vector<std::string> breaks_;
  std::vector<std::string> continues_;
  const FuncDecl* fn_ = nullptr;
  int frameSize_ = 0;
  int localBytes_ = 0;
  int argBytes_ = 0;
  std::vector<std::string> ramItems_;
  int ramAddr_ = 0;
  std::vector<ZpEntry> zeroPage_;
  std::map<std::string, int> lineOf_;
  int globalBase_ = 0;
  std::vector<ZpEntry> zpGlobals_;
  std::vector<ZpEntry> allGlobals_;
  std::map<std::string, std::string> placedWhy_;
  std::map<std::string, RomEntry> romSyms_;
  // Declaration order, which a std::map would lose. ROM.h lists it.
  std::vector<std::string> romOrder_;
  std::vector<std::string> romItems_;
  std::map<std::string, RomEntry> romPlan_;
  int saveBytes_ = 0;
  int dSaveBytes_ = 0;
  int dLive_ = 0;
  int frameCursor_ = 0;
  int printfBytes_ = 0;
  bool printfUsed_ = false;
  int maxDouble_ = 0;
  bool doubleUsed_ = false;
  std::map<double, std::string> dlits_;
  struct DLit { std::string label; double value; };
  std::vector<DLit> dlitData_;
  std::vector<StrData> romLiterals_;

  void e(const std::string& line) { out_.push_back(line); }
  void op(const std::string& text) { out_.push_back("        " + text); }
  void lab(const std::string& name) { out_.push_back(name + ":"); }
  std::string uniq(const std::string& tag) { return "__L" + S(labelSeq_++) + "_" + tag; }
  Emit emitter() { return [this](const std::string& l) { e(l); }; }

  // A slot's two halves, spelled for the assembler.
  std::string hi(int slot) { return "[" + tempLabel(slot) + "]"; }
  std::string lo(int slot) { return "[" + tempLabel(slot) + "+1]"; }
  std::string word(int slot) { return "[" + tempLabel(slot) + "]"; }

  void use(int slot) { if (slot + 1 > maxSlot_) maxSlot_ = slot + 1; }

  // ---- the whole unit --------------------------------------------------------

  void reset() {
    romSyms_.clear();
    romOrder_.clear();
    romItems_.clear();
    romLiterals_.clear();
    printfBytes_ = 0;
    printfUsed_ = false;
    doubleUsed_ = false;
    dlits_.clear();
    dlitData_.clear();
    lineOf_.clear();
    ramAddr_ = 0;
    // DESIGN: maxSlot and maxDouble are NOT cleared between the two passes.
    // A frame's temp save area is sized from them at the PROLOGUE, before
    // the body that grows them has been generated. Clearing them gave the
    // first function of pass two a save area of nothing, and its spill then
    // wrote over its own first argument: a program whose first definition
    // made a nested call returned the wrong number and said nothing.
    zpGlobals_.clear();
    allGlobals_.clear();
    placedWhy_.clear();
    globals_.clear();
    strings_.clear();
    stringData_.clear();
    runtimeUsed_.clear();
    labelSeq_ = 0;
    ramItems_.clear();
    zeroPage_.clear();
  }

  std::vector<std::string> body() {
    // The order globals are laid down in IS the zero page allocation: .ram
    // starts at address zero, so whatever is emitted first is in it. The
    // compiler's own reservations come before any of this.
    const int reserved = zpReserve_ + RESERVED_BYTES + 2 * maxSlot_ + (softMul_ ? SOFT_ZP_BYTES : 0);
    for (const Placed& p : allocate(unit_, reserved, profile_)) {
      placedWhy_[p.decl.name] = p.why;
      declareGlobal(p.decl);
    }
    for (const VarDecl& v : unit_.vars) if (v.rom) declareGlobal(v);
    std::vector<std::string> bodyLines;
    std::swap(out_, bodyLines);
    for (const FuncDecl& f : unit_.funcs) if (f.body) genFunc(f);
    std::swap(out_, bodyLines);
    return bodyLines;
  }

  void plan() {
    for (const FuncDecl& f : unit_.funcs) {
      auto prev = funcs_.find(f.name);
      if (prev != funcs_.end() && prev->second.body && f.body) fail(f.pos, f.name + " is defined twice");
      if (prev == funcs_.end() || f.body) funcs_[f.name] = f;
    }
    auto main = funcs_.find("main");
    if (main == funcs_.end() || !main->second.body) {
      fail(Pos{unit_.file, 1}, "a program needs a function called main. Write int main(void) { ... }");
    }
  }

  // Lays the whole file out: entry, code, runtime, then .ram in the order
  // that decides which variables are in the zero page.
  Compiled assemble(const std::vector<std::string>& bodyLines) {
    std::vector<ZpEntry> zpg;
    for (const ZpEntry& z : zpGlobals_) if (z.addr < ZERO_PAGE_SIZE) zpg.push_back(z);
    zpGlobals_ = zpg;
    std::vector<std::string> head;
    head.push_back("; Generated by the SimpleCPU-8 C compiler. Do not edit.");
    head.push_back("; The assembly below is the program. Every block says which C");
    head.push_back("; line it came from, so the two panes read side by side.");
    head.push_back("");
    head.push_back("__start:");
    head.push_back("        LD D2 <- " + S(C_STACK_TOP));
    head.push_back("        LD [" + S(ZP_SP) + "] <- D2");
    head.push_back("        JSR main");
    head.push_back("        HLT");

    // The same entry points either way, so nothing above here knows which
    // runtime it got. That is the point: a student compiles the same a * b
    // both ways and reads the difference.
    const std::vector<std::string> rt = softMul_ ? emitSoftRuntime(runtimeUsed_) : emitRuntime(runtimeUsed_);

    // .ram, in allocation order. The compiler's own reservations, then the
    // program's, which is what puts the first 256 bytes in the zero page.
    std::vector<std::string> ram = {".ram"};
    ramAddr_ = 0;
    auto reserve = [&](const std::string& label, int bytes, const std::string& why) {
      ram.push_back(label + ":" + pad8(label) + "ds " + S(bytes));
      zeroPage_.push_back({label, ramAddr_, bytes, why});
      ramAddr_ += bytes;
    };
    // The system page comes first, so its addresses are the ones the
    // program was written against. The compiler never touches it.
    if (zpReserve_ > 0) reserve("__sys", zpReserve_, "the system page, the program's own");
    reserve(ZP_SP, 2, "the software stack pointer");
    reserve(ZP_RET, 2, "the return value");
    reserve(ZP_CMP, 1, "a word compare's scratch byte");
    reserve(ZP_RA, 2, "runtime operand A");
    reserve(ZP_RB, 2, "runtime operand B");
    if (softMul_ && !runtimeUsed_.empty()) {
      reserve(ZP_RP, 2, "the software runtime's product or remainder");
      reserve(ZP_RQ, 2, "the software runtime's quotient");
      reserve(ZP_RC, 1, "the software runtime's bit counter");
    }
    for (int i = 0; i < maxSlot_; i++) reserve(tempLabel(i), 2, "an expression temp");

    ram.insert(ram.end(), ramItems_.begin(), ramItems_.end());
    for (const StrData& s : stringData_) ram.push_back(s.label + ": db " + joinBytes(s.bytes, true));
    const int blk = softMul_ ? 0 : blockSize(runtimeUsed_);
    if (blk > 0) ram.push_back(S(ACP_BLOCK) + ": ds " + S(blk));

    if (printfUsed_) ram.push_back(S(PRINTF_ARGS) + ": ds " + S(std::max(printfBytes_, 1)));
    if (doubleUsed_) {
      // Three slots past the deepest: a binary operation reads slot n and
      // n + 1 and the device writes its answer to slot n + 2.
      ram.push_back(S(DT) + ": ds " + S((maxDouble_ + 3) * DSIZE));
      ram.push_back(S(DRET) + ": ds " + S(DSIZE));
      for (const DLit& d : dlitData_) {
        ram.push_back(d.label + ": db " + joinBytes(f64Bytes(d.value), false) + "   ; " + numText(d.value));
      }
    }

    // A printf template lives on the cartridge, after the program's own ROM
    // objects, so their addresses do not move when a message is edited.
    std::vector<std::string> romText = romItems_;
    for (const StrData& r : romLiterals_) romText.push_back(r.label + ": db " + joinBytes(r.bytes, true));
    std::vector<std::string> data;
    if (!romText.empty()) {
      data.push_back("");
      data.push_back(".data");
      data.insert(data.end(), romText.begin(), romText.end());
    }

    std::vector<std::string> all = head;
    all.push_back("");
    all.insert(all.end(), bodyLines.begin(), bodyLines.end());
    all.insert(all.end(), rt.begin(), rt.end());
    all.push_back("");
    all.insert(all.end(), ram.begin(), ram.end());
    all.insert(all.end(), data.begin(), data.end());
    all.push_back("");
    std::string text;
    for (size_t i = 0; i < all.size(); i++) {
      if (i) text += "\n";
      text += all[i];
    }
    // The marks were made against the body alone. The head and the blank
    // line after it come first in the finished text, and assembly lines are
    // counted from one.
    const int shift = static_cast<int>(head.size()) + 1 + 1;
    Compiled c;
    c.text = text;
    for (const auto& [k, v] : lineOf_) c.lineOf[k] = v + shift;
    c.zeroPage = zeroPage_;
    c.zeroPage.insert(c.zeroPage.end(), zpGlobals_.begin(), zpGlobals_.end());
    c.globals = allGlobals_;
    for (const std::string& name : romOrder_) c.rom.push_back(romSyms_[name]);
    return c;
  }

  // ---- declarations ----------------------------------------------------------

  // __ROM goes on the cartridge. The CPU cannot read it, the GPU and the
  // audio chip can, and the compiler emits exactly what a hand written
  // program does today: a label in .data with its bytes after it.
  void declareRom(const VarDecl& v) {
    auto it = romPlan_.find(v.name);
    if (it == romPlan_.end()) fail(v.pos, v.name + " is not in the cartridge layout");
    const RomEntry& entry = it->second;
    romSyms_[v.name] = entry;
    romOrder_.push_back(v.name);
    const std::string pad = pad8(v.name);
    if (entry.sameAs) {
      // The bytes are already down under another label, at the same address.
      romItems_.push_back(v.name + ":" + pad + "; the same bytes as " + *entry.sameAs);
      return;
    }
    romItems_.push_back(v.name + ":" + pad + "db " + joinBytes(entry.bytes, false));
  }

  void declareGlobal(const VarDecl& v) {
    if (globals_.count(v.name)) fail(v.pos, v.name + " is declared twice");
    if (v.rom) { declareRom(v); return; }
    if (isWide(v.type) && !isFloat(v.type)) noWide(v.type, v.pos, "variable");
    const int size = sizeOf(v.type);
    Sym sym{v.name, v.name, v.type, Sym::Where::Global, 0, globalBase_ + ramAddr_};
    globals_[v.name] = sym;
    ramItems_.push_back(initLine(v, size));
    ramAddr_ += size;
    auto why = placedWhy_.find(v.name);
    allGlobals_.push_back({v.name, sym.addr, size, why == placedWhy_.end() ? "a global" : why->second});
    if (sym.addr + size <= ZERO_PAGE_SIZE) {
      zpGlobals_.push_back({v.name, sym.addr, size, why == placedWhy_.end() ? "it fitted" : why->second});
    }
  }

  // The .ram line for a global, from its initializer.
  std::string initLine(const VarDecl& v, int size) {
    const std::string pad = pad8(v.name);
    if (!v.init) return v.name + ":" + pad + "ds " + S(size);
    const int width = v.type.arrayLen ? sizeOf(CType{v.type.base, v.type.ptr, std::nullopt}) : size;
    const std::string dir = width == 1 ? "db" : "dw";

    if (v.init->isList) {
      std::vector<std::string> vals;
      for (const ExprPtr& x : v.init->list) vals.push_back(initItem(x, v.pos, width));
      const size_t n = v.type.arrayLen ? static_cast<size_t>(*v.type.arrayLen) : vals.size();
      while (vals.size() < n) vals.push_back("0");
      std::string joined;
      for (size_t i = 0; i < vals.size(); i++) joined += (i ? ", " : "") + vals[i];
      return v.name + ":" + pad + dir + " " + joined;
    }
    if (v.init->one->k == ExprKind::Str) {
      std::vector<uint8_t> bytes = v.init->one->bytes;
      bytes.push_back(0);
      const size_t n = v.type.arrayLen ? static_cast<size_t>(*v.type.arrayLen) : bytes.size();
      while (bytes.size() < n) bytes.push_back(0);
      if (v.type.ptr > 0) {
        // char *s = "hi" points at the bytes rather than holding them.
        return v.name + ":" + pad + "dw " + stringLabel(v.init->one->bytes);
      }
      return v.name + ":" + pad + "db " + joinBytes(bytes, false);
    }
    return v.name + ":" + pad + dir + " " + initItem(v.init->one, v.pos, width);
  }

  // One initializer item, as assembly source. A label keeps its name so the
  // assembler folds the address, which is the only thing that knows it yet.
  // A number for a byte is cut to its byte: -1 in a signed char is 255.
  std::string initItem(const ExprPtr& e, const Pos& pos, int width) {
    if (e->k == ExprKind::Un && e->op == "&" && e->e->k == ExprKind::Id) return e->e->name;
    if (e->k == ExprKind::Id) {
      auto g = globals_.find(e->name);
      if (g != globals_.end() && g->second.type.arrayLen) return e->name;
    }
    if (e->k == ExprKind::Str) return stringLabel(e->bytes);
    return S(constOf(e, pos) & (width == 1 ? 0xff : 0xffff));
  }

  int constOf(const ExprPtr& e, const Pos& pos) {
    const auto v = foldConst(e);
    if (!v) fail(pos, "a global's initializer has to be a constant");
    return toInt32(*v) & 0xffff;
  }

  std::string stringLabel(const std::vector<uint8_t>& bytes) {
    const std::string key = bytesKey(bytes);
    auto hit = strings_.find(key);
    if (hit != strings_.end()) return hit->second;
    const std::string label = "__s" + S(static_cast<int>(stringData_.size()));
    strings_[key] = label;
    stringData_.push_back({label, bytes});
    return label;
  }

  // ---- functions -------------------------------------------------------------

  const Sym* find(const std::string& name) {
    for (size_t i = scopes_.size(); i-- > 0;) {
      auto s = scopes_[i].find(name);
      if (s != scopes_[i].end()) return &s->second;
    }
    auto g = globals_.find(name);
    return g == globals_.end() ? nullptr : &g->second;
  }

  const Sym& lookup(const std::string& name, const Pos& pos) {
    const Sym* s = find(name);
    if (s) return *s;
    if (builtinConstant(name)) fail(pos, name + " is one of the machine's own constants and cannot be assigned");
    fail(pos, name + " is not declared");
  }

  // Every local in a function, laid out before any code is emitted: the
  // frame's shape has to be known at the prologue.
  // DESIGN: every frame carries room to save the expression temps across a
  // call. The temps are zero page, so they are shared by every function, and
  // a callee's own arithmetic lands on the caller's half-finished values.
  // `dbl(3) + dbl(1)` was 4 rather than 8 for exactly that reason: the second
  // call overwrote the first one's answer while working out its own.
  //
  // The frame is where they go, and not a second zero page area, because a
  // function that recurses needs one copy per invocation and a frame is what
  // there is one of per invocation.
  //
  // A call at slot k saves slots 0 to k - 1, which is precisely what is live:
  // the operator that will consume them has not run yet. A call at slot 0,
  // which is a statement call and the common case, saves nothing.
  void layoutFrame(const FuncDecl& f) {
    saveBytes_ = 2 * maxSlot_;
    dSaveBytes_ = maxDouble_ * DSIZE;
    argBytes_ = 0;
    for (const Param& p : f.params) argBytes_ += std::max(sizeOf(p.type), 1);
    int locals = 0;
    std::function<void(const StmtPtr&)> walk = [&](const StmtPtr& s) {
      if (!s) return;
      switch (s->k) {
        case StmtKind::Var:
          // A static local is a global under another label and takes no
          // frame room.
          if (s->decl.storage != Storage::Static) locals += sizeOf(s->decl.type);
          break;
        case StmtKind::Block: for (const StmtPtr& x : s->body) walk(x); break;
        case StmtKind::If: walk(s->t); walk(s->f); break;
        case StmtKind::While: case StmtKind::Do: walk(s->loopBody); break;
        case StmtKind::For: walk(s->init); walk(s->loopBody); break;
        case StmtKind::Switch: for (const SwitchCase& c : s->cases) for (const StmtPtr& x : c.body) walk(x); break;
        default: break;
      }
    };
    walk(f.body);
    localBytes_ = locals;
    frameSize_ = locals + saveBytes_ + dSaveBytes_ + argBytes_;
    if (frameSize_ > MAX_FRAME) {
      fail(f.pos, f.name + " needs a frame of " + S(frameSize_) + " bytes and a frame reaches " + S(MAX_FRAME) +
                      ". A local reaches its frame through [D1+n], and n is one byte. Move the big one to a global.");
    }
  }

  void genFunc(const FuncDecl& f) {
    noWide(f.ret, f.pos, "return type on " + f.name);
    for (const Param& p : f.params) noWide(p.type, f.pos, "parameter " + p.name);
    fn_ = &f;
    layoutFrame(f);
    scopes_.assign(1, {});
    std::map<std::string, Sym>& scope = scopes_[0];
    // Locals occupy the bottom of the frame, arguments the top, because the
    // caller wrote the arguments before the callee knew its own local count.
    frameCursor_ = 0;
    int argOff = localBytes_ + saveBytes_ + dSaveBytes_;
    for (const Param& p : f.params) {
      if (p.name.empty()) continue;
      scope[p.name] = Sym{p.name, p.name, p.type, Sym::Where::Frame, argOff, 0};
      argOff += std::max(sizeOf(p.type), 1);
    }

    e("");
    std::string sig;
    for (size_t i = 0; i < f.params.size(); i++) {
      sig += (i ? ", " : "") + typeName(f.params[i].type) + " " + f.params[i].name;
    }
    e("; " + typeName(f.ret) + " " + f.name + "(" + sig + ")");
    e("; " + f.pos.file + ":" + S(f.pos.line));
    lab(f.name);
    // The frame is the space below __sp. D1 takes its base, and __sp moves
    // down past it, through the address adder.
    const int own = localBytes_ + saveBytes_ + dSaveBytes_;
    op("LD D1 <- [" + S(ZP_SP) + "]");
    if (own > 0) {
      op("LD D1 <- D1-" + S(own));
      op("LD [" + S(ZP_SP) + "] <- D1");
    }

    genStmt(f.body);

    lab(f.name + "__end");
    epilogue();
    fn_ = nullptr;
  }

  // D1 is still the frame base here: every call puts it back. The frame
  // and the arguments the caller wrote below it go in one step. A return
  // writes this in place, which costs two lines and saves a jump.
  void epilogue() {
    if (frameSize_ > 0) {
      op("LD D1 <- D1+" + S(frameSize_));
      op("LD [" + S(ZP_SP) + "] <- D1");
    }
    op("RET");
  }

  // ---- statements ------------------------------------------------------------

  // The FIRST assembly line a C line produced. Keyed by file as well as
  // line: two files both have a line 12 and they are not the same place.
  void mark(const Pos& pos) {
    const std::string key = pos.file + ":" + S(pos.line);
    if (!lineOf_.count(key)) lineOf_[key] = static_cast<int>(out_.size());
  }

  void genStmt(const StmtPtr& sp) {
    const Stmt& s = *sp;
    e(std::string("        ") + STMT_MARK);
    mark(s.pos);
    switch (s.k) {
      case StmtKind::Empty: return;

      case StmtKind::Block: {
        scopes_.emplace_back();
        const int save = frameCursor_;
        for (const StmtPtr& st : s.body) genStmt(st);
        frameCursor_ = save;
        scopes_.pop_back();
        return;
      }

      case StmtKind::Var: {
        const VarDecl& d = s.decl;
        if (d.rom) fail(d.pos, "__ROM is a file scope declaration");
        if (d.zp) {
          fail(d.pos, "__zp on the local " + d.name +
                          " is not allowed. A zero page local is a static by definition. Write static, and know "
                          "that a static local in a function that recurses is a bug rather than a speed-up.");
        }
        if (isWide(d.type) && !isFloat(d.type)) noWide(d.type, d.pos, "local");
        if (d.storage == Storage::Static) {
          // Declared as a hidden global by hoistStatics, initialised from
          // the RAM image once, so nothing runs here. The scope maps the
          // local name onto that global's label.
          auto g = globals_.find(staticLabel(fn_->name, d.name));
          if (g == globals_.end()) fail(d.pos, d.name + " is not declared");
          Sym sym = g->second;
          sym.name = d.name;
          scopes_.back()[d.name] = sym;
          return;
        }
        const int size = sizeOf(d.type);
        Sym sym{d.name, d.name, d.type, Sym::Where::Frame, frameCursor_, 0};
        frameCursor_ += size;
        scopes_.back()[d.name] = sym;
        if (d.init && isFloat(d.type)) {
          genDouble(d.init->one, 0, 0);
          // The frame address is worked out here rather than being a label:
          // a local moves with the frame.
          genAddrOfSym(sym, 1);
          moveDyn(emitter(), Where::zp(tempLabel(1)), Where::at(slotAt(0)));
          return;
        }
        if (d.init) {
          if (d.init->isList) {
            const CType elem{d.type.base, d.type.ptr, std::nullopt};
            const int w = sizeOf(elem);
            for (size_t i = 0; i < d.init->list.size(); i++) {
              genExpr(d.init->list[i], 0);
              storeFrame(sym.offset + static_cast<int>(i) * w, w, 0);
            }
            return;
          }
          if (d.init->one->k == ExprKind::Str && d.type.arrayLen) {
            std::vector<uint8_t> bytes = d.init->one->bytes;
            bytes.push_back(0);
            for (size_t i = 0; i < bytes.size(); i++) {
              op("LD A <- " + S(bytes[i]));
              op("LD [D1+" + S(sym.offset + static_cast<int>(i)) + "] <- A");
            }
            return;
          }
          const CType t = genExpr(d.init->one, 0);
          convert(0, t, d.type);
          storeFrame(sym.offset, size, 0);
        }
        return;
      }

      case StmtKind::Expr: genEffect(s.e); return;

      case StmtKind::Return: {
        if (s.e && isFloat(fn_->ret)) {
          genDouble(s.e, 0, 0);
          moveConst(emitter(), DRET, slotAt(0));
          epilogue();
          return;
        }
        if (s.e) {
          // A byte function answers in the low byte of __ret, which is the
          // byte a caller reads. A word goes through D2.
          if (sizeOf(fn_->ret) == 1 && fn_->ret.ptr == 0 && !romRef(s.e) && !isFloat(typeOf(s.e))) {
            genLow(s.e, 0);
            op("LD A <- " + lo(0));
            op("LD [" + S(ZP_RET) + "+1] <- A");
          } else {
            const CType t = genExpr(s.e, 0);
            convert(0, t, fn_->ret);
            op("LD D2 <- " + word(0));
            op("LD [" + S(ZP_RET) + "] <- D2");
          }
        }
        epilogue();
        return;
      }

      case StmtKind::If: {
        const std::string els = uniq("else");
        const std::string end = uniq("endif");
        branch(s.c, false, els, 0);
        genStmt(s.t);
        if (s.f) {
          op("JMP " + end);
          lab(els);
          genStmt(s.f);
          lab(end);
        } else {
          lab(els);
        }
        return;
      }

      case StmtKind::While: {
        // The test sits at the bottom, so a pass through the loop costs
        // one conditional jump and no JMP. Entry jumps to the test once.
        const std::string top = uniq("while");
        const std::string test = uniq("wtest");
        const std::string end = uniq("wend");
        if (!isTrue(s.c)) op("JMP " + test);
        lab(top);
        breaks_.push_back(end);
        continues_.push_back(test);
        genStmt(s.loopBody);
        breaks_.pop_back();
        continues_.pop_back();
        lab(test);
        branch(s.c, true, top, 0);
        lab(end);
        return;
      }

      case StmtKind::Do: {
        const std::string top = uniq("do");
        const std::string cont = uniq("docont");
        const std::string end = uniq("doend");
        lab(top);
        breaks_.push_back(end);
        continues_.push_back(cont);
        genStmt(s.loopBody);
        breaks_.pop_back();
        continues_.pop_back();
        lab(cont);
        branch(s.c, true, top, 0);
        lab(end);
        return;
      }

      case StmtKind::For: {
        const std::string top = uniq("for");
        const std::string cont = uniq("forstep");
        const std::string end = uniq("forend");
        scopes_.emplace_back();
        const int save = frameCursor_;
        const std::string test = uniq("fortest");
        if (s.init) genStmt(s.init);
        // As in while, the test is at the bottom.
        if (s.c && !isTrue(s.c)) op("JMP " + test);
        lab(top);
        breaks_.push_back(end);
        continues_.push_back(cont);
        genStmt(s.loopBody);
        breaks_.pop_back();
        continues_.pop_back();
        lab(cont);
        if (s.step) genEffect(s.step);
        lab(test);
        if (s.c) branch(s.c, true, top, 0);
        else op("JMP " + top);
        lab(end);
        frameCursor_ = save;
        scopes_.pop_back();
        return;
      }

      case StmtKind::Switch: {
        // A chain of compares. A jump table wants JMP D2 and a table of
        // slots, and that is worth doing once there is something to measure.
        const std::string end = uniq("swend");
        const CType t = genExpr(s.e, 0);
        std::vector<std::string> bodies;
        for (size_t i = 0; i < s.cases.size(); i++) bodies.push_back(uniq("case"));
        std::optional<std::string> dflt;
        for (size_t i = 0; i < s.cases.size(); i++) {
          const SwitchCase& c = s.cases[i];
          if (!c.value) { dflt = bodies[i]; continue; }
          // A byte switch never matches a case its byte cannot hold.
          if (isByte(t) && !fitsByte(t, *c.value)) continue;
          compareSides("==", sideOf(0), immSide(toInt32(*c.value)), isByte(t) ? 1 : 2, false, true, bodies[i]);
        }
        op("JMP " + dflt.value_or(end));
        breaks_.push_back(end);
        for (size_t i = 0; i < s.cases.size(); i++) {
          lab(bodies[i]);
          for (const StmtPtr& st : s.cases[i].body) genStmt(st);
        }
        breaks_.pop_back();
        lab(end);
        return;
      }

      case StmtKind::Break:
        if (breaks_.empty()) fail(s.pos, "break is not inside a loop or a switch");
        op("JMP " + breaks_.back());
        return;

      case StmtKind::Continue:
        if (continues_.empty()) fail(s.pos, "continue is not inside a loop");
        op("JMP " + continues_.back());
        return;

      case StmtKind::Asm:
        // The peephole pass knows nothing of what hand written lines
        // expect, so it stops at them.
        e(std::string("        ") + BARRIER_MARK);
        e("        " + s.text);
        e(std::string("        ") + BARRIER_MARK);
        return;
    }
  }

  // An expression evaluated for what it does, its value unused.
  void genEffect(const ExprPtr& ep) {
    if (isFloat(typeOf(ep))) {
      genDouble(ep, 0, 0);
      return;
    }
    if (ep->k == ExprKind::Assign) {
      genAssign(ep, 0, false);
      return;
    }
    if (ep->k == ExprKind::Post || (ep->k == ExprKind::Un && (ep->op == "++" || ep->op == "--"))) {
      // The old value is not wanted, so i++ is ++i, which is i += 1.
      genAssign(mkAssign(ep->e, mkBin(ep->op == "++" ? "+" : "-", ep->e, numOne(ep->pos), ep->pos), ep->pos), 0, false);
      return;
    }
    genExpr(ep, 0);
  }

  // ---- conditions ------------------------------------------------------------
  //
  // A condition in an if, a loop or a ?: is never turned into a 0 or 1
  // first. branch() jumps on the flags the test itself leaves: a compare is
  // a subtract and a jump on C, Z, or N and V; && and || jump past each
  // other; ! swaps the jump. Every flag has a jump both ways (JZ and JNZ,
  // JC and JNC, JN and JP, JV and JNV), so no test needs a jump over a jump.

  // One side of a comparison: a slot's two bytes, or a constant folded into
  // the instructions as immediates.
  struct Side {
    std::string lo, hi;
    std::optional<int> imm;  // the value, when the side is a constant
  };
  Side sideOf(int slot) { return {lo(slot), hi(slot), std::nullopt}; }
  static Side immSide(int v) {
    v &= 0xffff;
    return {S(v & 0xff), S(v >> 8), v};
  }

  // A value the instructions can name directly, with no evaluation into a
  // temp: a constant, a local at [D1+n], or a global in the zero page. The
  // ALU takes all three as its operand. width is the width the value is
  // used at. A byte used as a word needs its high byte made: 0 for an
  // unsigned one, a sign extension for a signed one, which is code, so a
  // signed byte is a leaf only at width 1. A word used at width 1 is its
  // low byte.
  std::optional<Side> leafSide(const ExprPtr& ep, int width) {
    const Expr& e = *ep;
    if (const auto v = foldConst(ep)) {
      const CType t = typeOf(ep);
      if (isFloat(t) || isWide(t)) return std::nullopt;
      return immSide(toInt32(*v));
    }
    if (e.k != ExprKind::Id) return std::nullopt;
    if (const auto c = constantNamed(e.name)) return immSide(*c);
    const Sym* s = find(e.name);
    if (!s || s->type.arrayLen || romSyms_.count(e.name) || isFloat(s->type) || isWide(s->type)) return std::nullopt;
    const bool frame = s->where == Sym::Where::Frame;
    if (!frame && !inZeroPage(*s)) return std::nullopt;
    auto at = [&](int k) {
      if (frame) return "[D1+" + S(s->offset + k) + "]";
      return k ? "[" + s->label + "+" + S(k) + "]" : "[" + s->label + "]";
    };
    const int size = s->type.ptr > 0 ? 2 : sizeOf(s->type);
    if (size == 2) return Side{at(1), at(0), std::nullopt};
    if (width == 1) return Side{at(0), "", std::nullopt};
    if (isSigned(s->type)) return std::nullopt;
    return Side{at(0), "0", std::nullopt};
  }

  static bool isTrue(const ExprPtr& e) {
    const auto v = foldConst(e);
    return v && *v != 0;
  }

  static bool isByte(const CType& t) { return t.ptr == 0 && !t.arrayLen && sizeOf(t) == 1; }
  static bool fitsByte(const CType& t, double v) { return isSigned(t) ? v >= -128 && v <= 127 : v >= 0 && v <= 255; }

  // Jumps to `label` when the condition's truth is `when` and falls through
  // otherwise. Slots below `slot` are left alone.
  void branch(const ExprPtr& ep, bool when, const std::string& label, int slot) {
    const Expr& e = *ep;
    if (const auto v = foldConst(ep)) {
      if ((*v != 0) == when) op("JMP " + label);
      return;
    }
    if (e.k == ExprKind::Un && e.op == "!") {
      branch(e.e, !when, label, slot);
      return;
    }
    if (e.k == ExprKind::Bin && (e.op == "&&" || e.op == "||")) {
      // && jumps out on the first false side, || on the first true one.
      const bool out = e.op == "||";
      if (when == out) {
        branch(e.l, when, label, slot);
        branch(e.r, when, label, slot);
      } else {
        const std::string skip = uniq(out ? "orskip" : "andskip");
        branch(e.l, out, skip, slot);
        branch(e.r, when, label, slot);
        lab(skip);
      }
      return;
    }
    if (e.k == ExprKind::Bin && isCompare(e.op) && compareBranch(ep, when, label, slot)) return;
    const CType t = genExpr(ep, slot);
    truthy(slot, t);
    op((when ? "JNZ " : "JZ ") + label);
  }

  // Leaves Z set when the value in `slot` is zero. A word is loaded into
  // D2, which sets Z from all sixteen bits, so a pointer's NULL test is one
  // load. D2 is scratch between the compiler's own instructions.
  void truthy(int slot, const CType& t) {
    if (sizeOf(t) == 1 && t.ptr == 0) {
      op("LD A <- " + lo(slot));
    } else {
      op("LD D2 <- " + word(slot));
    }
  }

  // A comparison as a branch. Both sides are compared at one width: a byte
  // when both are bytes of one signedness, or one is a byte and the other a
  // constant that fits it, since C's promotion to int changes no order
  // there. A constant side is an immediate. False when the operands are
  // not the CPU's to compare (a double, a long).
  bool compareBranch(const ExprPtr& ep, bool when, const std::string& label, int slot) {
    const Expr& e = *ep;
    const CType lt0 = typeOf(e.l);
    const CType rt0 = typeOf(e.r);
    if (isFloat(lt0) || isFloat(rt0) || isWide(lt0) || isWide(rt0)) return false;
    const CType rtype = common(lt0, rt0);
    int width = sizeOf(rtype);
    bool sgn = isSigned(rtype) && rtype.ptr == 0;
    const auto lc = foldConst(e.l);
    const auto rc = foldConst(e.r);
    if (isByte(lt0) && isByte(rt0) && isSigned(lt0) == isSigned(rt0)) {
      width = 1;
      sgn = isSigned(lt0);
    } else if (isByte(lt0) && rc && fitsByte(lt0, *rc)) {
      width = 1;
      sgn = isSigned(lt0);
    } else if (isByte(rt0) && lc && fitsByte(rt0, *lc)) {
      width = 1;
      sgn = isSigned(rt0);
    }
    auto side = [&](const ExprPtr& x, int at) {
      if (auto leaf = leafSide(x, width)) return *leaf;
      const CType got = genExpr(x, at);
      if (width == 2) convert(at, got, rtype);
      return sideOf(at);
    };
    // A leaf takes no slot. When both sides need evaluating, the right one
    // goes first, so the left is the value A still holds for the compare.
    const bool leftLeaf = leafSide(e.l, width).has_value();
    const bool rightLeaf = leafSide(e.r, width).has_value();
    Side a;
    Side b;
    if (!leftLeaf && !rightLeaf) {
      b = side(e.r, slot);
      a = side(e.l, slot + 1);
    } else {
      a = side(e.l, slot);
      b = side(e.r, slot);
    }
    compareSides(e.op, a, b, width, sgn, when, label);
    return true;
  }

  static std::string mirror(const std::string& cop) {
    if (cop == "<") return ">";
    if (cop == ">") return "<";
    if (cop == "<=") return ">=";
    if (cop == ">=") return "<=";
    return cop;
  }

  // The jump for a compare of two sides already in place. A constant goes
  // to the right, and <= k and > k become < k+1 and >= k+1, so the
  // variable is always the one in A and CMP leaves it there for the next
  // compare of the same value.
  void compareSides(const std::string& cop0, const Side& a0, const Side& b0, int width, bool sgn, bool when,
                    const std::string& label) {
    std::string cop = cop0;
    Side a = a0;
    Side b = b0;
    if (a.imm && !b.imm) {
      std::swap(a, b);
      cop = mirror(cop);
    }
    if (b.imm && cop != "==" && cop != "!=") {
      int k = *b.imm & (width == 1 ? 0xff : 0xffff);
      if (sgn) k = width == 1 ? static_cast<int8_t>(k) : static_cast<int16_t>(k);
      const int top = width == 1 ? (sgn ? 127 : 255) : (sgn ? 32767 : 65535);
      if ((cop == "<=" || cop == ">") && k < top) {
        cop = cop == "<=" ? "<" : ">=";
        k++;
        b = immSide(k);
      }
      if (k == 0 && (cop == "<" || cop == ">=")) {
        if (!sgn) {
          // Unsigned, nothing is below zero.
          if ((cop == ">=") == when) op("JMP " + label);
          return;
        }
        // Signed against zero is the sign bit, which the load sets as N.
        op("LD A <- " + (width == 2 ? a.hi : a.lo));
        op(((cop == "<") == when ? "JN " : "JP ") + label);
        return;
      }
      if (!sgn && k == 1 && (cop == "<" || cop == ">=")) {
        // Unsigned below one is zero.
        compareSides(cop == "<" ? "==" : "!=", a, immSide(0), width, sgn, when, label);
        return;
      }
    }

    if (cop == "==" || cop == "!=") {
      const bool onEqual = (cop == "==") == when;
      const std::string jeq = onEqual ? "JZ " : "JNZ ";
      if (b.imm && (*b.imm & (width == 1 ? 0xff : 0xffff)) == 0 && !a.imm) {
        // Against zero the load is the test.
        if (width == 1) op("LD A <- " + a.lo);
        else op("LD D2 <- " + a.hi);
        op(jeq + label);
        return;
      }
      op("LD A <- " + a.lo);
      op("CMP A, " + b.lo);
      if (width == 1) {
        op(jeq + label);
      } else if (onEqual) {
        // Equal needs both bytes, so a low byte that differs is done.
        const std::string skip = uniq("ne");
        op("JNZ " + skip);
        op("LD A <- " + a.hi);
        op("CMP A, " + b.hi);
        op("JZ " + label);
        lab(skip);
      } else {
        op("JNZ " + label);
        op("LD A <- " + a.hi);
        op("CMP A, " + b.hi);
        op("JNZ " + label);
      }
      return;
    }

    // The other four are all "x < y" with the sides swapped or the answer
    // inverted, so one subtract shape serves them.
    // a <  b : less(a, b)
    // a >  b : less(b, a)
    // a >= b : not less(a, b)
    // a <= b : not less(b, a)
    const bool swap = cop == ">" || cop == "<=";
    const bool invert = cop == ">=" || cop == "<=";
    const Side& x = swap ? b : a;
    const Side& y = swap ? a : b;
    op("LD A <- " + x.lo);
    if (width == 1) {
      op("CMP A, " + y.lo);
    } else {
      // A word compare chains the borrow, which only SUB and SBC do. The
      // load between them leaves C alone.
      op("SUB A <- " + y.lo);
      op("LD A <- " + x.hi);
      op("SBC A <- " + y.hi);
    }
    const bool onLess = when != invert;
    if (!sgn) {
      // C is the borrow, so it is set exactly when x is below y.
      op((onLess ? "JC " : "JNC ") + label);
      return;
    }
    // Signed less is N xor V: with no overflow N is the sign of the
    // difference, with overflow it is the opposite.
    const std::string ovf = uniq("ovf");
    const std::string skip = uniq("cmpskip");
    op("JV " + ovf);
    op((onLess ? "JN " : "JP ") + label);
    op("JMP " + skip);
    lab(ovf);
    op((onLess ? "JP " : "JN ") + label);
    lab(skip);
  }

  // A condition as a value, 0 or 1 in `slot`.
  CType boolValue(const ExprPtr& ep, int slot) {
    const std::string yes = uniq("true");
    const std::string done = uniq("tdone");
    branch(ep, true, yes, slot);
    op("LD A <- 0");
    op("JMP " + done);
    lab(yes);
    op("LD A <- 1");
    lab(done);
    op("LD " + lo(slot) + " <- A");
    op("LD A <- 0");
    op("LD " + hi(slot) + " <- A");
    return T(BaseType::Int);
  }

  // ---- expressions -----------------------------------------------------------

  void loadConst(int slot, double v, const CType& t) {
    use(slot);
    if (sizeOf(t) == 1 && t.ptr == 0) {
      op("LD A <- " + S(toInt32(v) & 0xff));
      op("LD " + lo(slot) + " <- A");
      return;
    }
    op("LD D2 <- " + S(toInt32(v) & 0xffff));
    op("LD " + word(slot) + " <- D2");
  }

  // A word goes through D2, which moves both bytes in one instruction.
  void storeFrame(int offset, int size, int slot) {
    if (size == 1) {
      op("LD A <- " + lo(slot));
      op("LD [D1+" + S(offset) + "] <- A");
      return;
    }
    op("LD D2 <- " + word(slot));
    op("LD [D1+" + S(offset) + "] <- D2");
  }

  void loadFrame(int offset, int size, int slot) {
    use(slot);
    if (size == 1) {
      op("LD A <- [D1+" + S(offset) + "]");
      op("LD " + lo(slot) + " <- A");
      return;
    }
    op("LD D2 <- [D1+" + S(offset) + "]");
    op("LD " + word(slot) + " <- D2");
  }

  bool inZeroPage(const Sym& sym) { return sym.addr + sizeOf(sym.type) <= ZERO_PAGE_SIZE; }

  void loadGlobal(const Sym& sym, int slot) {
    use(slot);
    const int size = sizeOf(sym.type);
    if (size == 2) {
      op("LD D2 <- [" + sym.label + "]");
      op("LD " + word(slot) + " <- D2");
      return;
    }
    if (inZeroPage(sym)) {
      op("LD A <- [" + sym.label + "]");
    } else {
      op("LD D2 <- " + sym.label);
      op("LD A <- [D2]");
    }
    op("LD " + lo(slot) + " <- A");
  }

  void storeGlobal(const Sym& sym, int slot) {
    const int size = sizeOf(sym.type);
    if (size == 2) {
      op("LD D2 <- " + word(slot));
      op("LD [" + sym.label + "] <- D2");
      return;
    }
    op("LD A <- " + lo(slot));
    if (inZeroPage(sym)) {
      op("LD [" + sym.label + "] <- A");
    } else {
      op("LD D2 <- " + sym.label);
      op("LD [D2] <- A");
    }
  }

  // Widen or narrow a value already in a slot.
  void convert(int slot, const CType& from, const CType& to) {
    const int fs = from.ptr > 0 ? 2 : sizeOf(from);
    const int ts = to.ptr > 0 ? 2 : sizeOf(to);
    if (fs == ts) return;
    if (ts == 1) return;  // narrowing keeps the low byte, which is already there
    // Widening a byte. Sign extend a signed one, zero the high half otherwise.
    if (isSigned(from)) {
      const std::string neg = uniq("sx");
      const std::string done = uniq("sxd");
      op("LD A <- " + lo(slot));
      op("JN " + neg);
      op("LD A <- 0");
      op("JMP " + done);
      lab(neg);
      op("LD A <- $FF");
      lab(done);
    } else {
      op("LD A <- 0");
    }
    op("LD " + hi(slot) + " <- A");
  }

  CType typeOf(const ExprPtr& ep) {
    const Expr& e = *ep;
    switch (e.k) {
      case ExprKind::Num: return e.type;
      case ExprKind::Str: return T(BaseType::Char, 1);
      case ExprKind::Id: {
        const auto c = constantNamed(e.name);
        if (c) return *c > 255 ? T(BaseType::UInt) : T(BaseType::UChar);
        const Sym& s = lookup(e.name, e.pos);
        if (s.type.arrayLen) return CType{s.type.base, s.type.ptr + 1, std::nullopt};
        return s.type;
      }
      case ExprKind::Call: {
        if (e.fn->k != ExprKind::Id) fail(e.pos, "only a plain function name can be called yet");
        auto f = funcs_.find(e.fn->name);
        if (f == funcs_.end()) {
          if (e.fn->name == "in") return T(BaseType::UChar);
          if (e.fn->name == "out") return T(BaseType::Void);
          if (e.fn->name == "gpu_printf") return T(BaseType::Void);
          fail(e.pos, e.fn->name + " is not declared");
        }
        return f->second.ret;
      }
      case ExprKind::Un:
        if (e.op == "&") {
          const CType t = typeOf(e.e);
          return CType{t.base, t.ptr + 1, std::nullopt};
        }
        if (e.op == "*") {
          const CType t = typeOf(e.e);
          if (t.ptr == 0) fail(e.pos, "cannot take * of " + typeName(t));
          return CType{t.base, t.ptr - 1, std::nullopt};
        }
        if (e.op == "!") return T(BaseType::Int);
        return typeOf(e.e);
      case ExprKind::Post: return typeOf(e.e);
      case ExprKind::Bin: {
        if (isCompare(e.op) || e.op == "&&" || e.op == "||") return T(BaseType::Int);
        if (e.op == "<<" || e.op == ">>") return common(typeOf(e.l), T(BaseType::Int));
        return common(typeOf(e.l), typeOf(e.r));
      }
      case ExprKind::Assign: return typeOf(e.l);
      case ExprKind::Index: {
        const CType a = typeOf(e.a);
        if (a.ptr == 0) fail(e.pos, typeName(a) + " cannot be indexed");
        return CType{a.base, a.ptr - 1, std::nullopt};
      }
      case ExprKind::Cond: return common(typeOf(e.t), typeOf(e.f));
      case ExprKind::Cast: return e.type;
      case ExprKind::Sizeof: return T(BaseType::UInt);
      case ExprKind::Comma: return typeOf(e.r);
    }
    return T(BaseType::Int);
  }

  // A name the program did not declare may be one the MACHINE declares. A
  // variable of the same name wins, so a program is never broken by the
  // machine gaining a command.
  std::optional<int> constantNamed(const std::string& name) {
    if (find(name)) return std::nullopt;
    return builtinConstant(name);
  }

  // A coprocessor type reaching a place that cannot handle it. Refusing is
  // the only honest answer: the CPU can move those bytes and do nothing
  // else with them, so narrowing one quietly would compute a wrong number
  // and say nothing.
  void noWide(const CType& t, const Pos& pos, const std::string& what) {
    if (!isWide(t) || isFloat(t)) return;
    fail(pos, typeName(t) + " " + what + " is not in this build yet. " + typeName(t) +
                  " is a coprocessor type: the CPU can move its bytes and do nothing else with them, so the "
                  "compiler refuses rather than quietly using the low two.");
  }

  CType genExpr(const ExprPtr& ep, int slot) {
    const Expr& e = *ep;
    use(slot);
    // A comparison between doubles has an INT result, so it belongs here
    // rather than in the double path.
    if (e.k == ExprKind::Bin && isCompare(e.op)) {
      if (isFloat(typeOf(e.l)) || isFloat(typeOf(e.r))) return genDoubleCompare(ep, slot);
    }
    if (e.k == ExprKind::Cast && !isWide(e.type) && isFloat(typeOf(e.e))) {
      narrowFromDouble(e.e, slot);
      convert(slot, T(BaseType::Int), e.type);
      return e.type;
    }
    switch (e.k) {
      case ExprKind::Num: {
        const CType t = typeOf(ep);
        noWide(t, e.pos, "literal");
        loadConst(slot, e.value, t);
        return t;
      }

      case ExprKind::Str: {
        const std::string label = stringLabel(e.bytes);
        op("LD D2 <- " + label);
        op("LD " + word(slot) + " <- D2");
        return T(BaseType::Char, 1);
      }

      case ExprKind::Id: {
        if (romSyms_.count(e.name)) {
          fail(e.pos, e.name + " is __ROM. The CPU cannot read the cartridge, so its address and size are all it "
                          "gets: rom_copy(dst, " + e.name + ", ROM_" + e.name +
                          "_SIZE) brings the bytes into RAM, and ROM_BANK, ROM_HI and ROM_LO split the address "
                          "for a device port.");
        }
        const auto c = constantNamed(e.name);
        if (c) {
          const CType t = *c > 255 ? T(BaseType::UInt) : T(BaseType::UChar);
          loadConst(slot, *c, t);
          return t;
        }
        const Sym& s = lookup(e.name, e.pos);
        if (s.type.arrayLen) {
          // An array decays to the address of its first element.
          genAddrOfSym(s, slot);
          return CType{s.type.base, s.type.ptr + 1, std::nullopt};
        }
        if (s.where == Sym::Where::Frame) loadFrame(s.offset, sizeOf(s.type), slot);
        else loadGlobal(s, slot);
        return s.type;
      }

      case ExprKind::Cast: {
        noWide(e.type, e.pos, "cast");
        const CType from = genExpr(e.e, slot);
        convert(slot, from, e.type);
        return e.type;
      }

      case ExprKind::Sizeof: {
        const CType t = e.ofType ? *e.ofType : typeOf(e.e);
        loadConst(slot, sizeOf(t), T(BaseType::UInt));
        return T(BaseType::UInt);
      }

      case ExprKind::Comma:
        genExpr(e.l, slot);
        return genExpr(e.r, slot);

      case ExprKind::Assign: return genAssign(ep, slot);
      case ExprKind::Bin: return genBinary(ep, slot);
      case ExprKind::Un: return genUnary(ep, slot);
      case ExprKind::Post: return genPost(ep, slot);
      case ExprKind::Index: {
        if (e.a->k == ExprKind::Id && romSyms_.count(e.a->name)) {
          fail(e.pos, e.a->name + " is __ROM and cannot be read from C. rom_copy(dst, " + e.a->name + ", ROM_" +
                          e.a->name + "_SIZE) copies it into RAM, and then dst[...] reads it.");
        }
        const CType t = typeOf(ep);
        loadPlace(placeOf(ep, slot, sizeOf(t) == 1), sizeOf(t), slot);
        return t;
      }
      case ExprKind::Call: return genCall(ep, slot);

      case ExprKind::Cond: {
        const std::string f = uniq("cf");
        const std::string end = uniq("ce");
        const CType t = typeOf(ep);
        branch(e.c, false, f, slot);
        const CType tt = genExpr(e.t, slot);
        convert(slot, tt, t);
        op("JMP " + end);
        lab(f);
        const CType ft = genExpr(e.f, slot);
        convert(slot, ft, t);
        lab(end);
        return t;
      }
    }
    return T(BaseType::Int);
  }

  // ---- lvalues ---------------------------------------------------------------

  void genAddrOfSym(const Sym& s, int slot) {
    use(slot);
    if (s.where == Sym::Where::Global) {
      op("LD D2 <- " + s.label);
      op("LD " + word(slot) + " <- D2");
      return;
    }
    // A frame address is D1 plus the offset, which the address adder makes.
    op("LD D2 <- D1+" + S(s.offset));
    op("LD " + word(slot) + " <- D2");
  }

  // ---- addresses in D2 --------------------------------------------------------
  //
  // An element or a pointer's target is reached through D2. The address is
  // built there with the address adder rather than in a temp: D2 plus an
  // offset, D2 plus A, and a displacement left for the load or store to
  // add, [D2+n].

  // A pointer or array value that loads into D2 in one instruction and
  // leaves A alone: an array, a pointer variable, a string. The line, or
  // empty.
  std::string baseLoad(const ExprPtr& ep) {
    const Expr& e = *ep;
    if (e.k == ExprKind::Str) return "LD D2 <- " + stringLabel(e.bytes);
    if (e.k != ExprKind::Id || constantNamed(e.name)) return "";
    const Sym* s = find(e.name);
    if (!s || romSyms_.count(e.name)) return "";
    if (s->type.arrayLen) {
      if (s->where == Sym::Where::Frame) return "LD D2 <- D1+" + S(s->offset);
      return "LD D2 <- " + s->label;
    }
    if (s->type.ptr == 0 && sizeOf(s->type) != 2) return "";
    if (s->where == Sym::Where::Frame) return "LD D2 <- [D1+" + S(s->offset) + "]";
    return "LD D2 <- [" + s->label + "]";
  }

  // Where an element or a target is: D2 plus disp, or with viaA the byte
  // at D2+A, which only a byte load can use.
  struct Place {
    int disp = 0;
    bool viaA = false;
  };

  // The address of a[i] or *p into D2. byteLoad says the caller will load
  // one byte and can take [D2+A]. Slots from slot up are free to use.
  Place placeOf(const ExprPtr& ep, int slot, bool byteLoad) {
    const Expr& e = *ep;
    if (e.k == ExprKind::Un && e.op == "*") {
      // *(p + k) is p's target k elements on.
      const Expr& in = *e.e;
      if (in.k == ExprKind::Bin && (in.op == "+" || in.op == "-") && typeOf(in.l).ptr > 0 && typeOf(in.r).ptr == 0) {
        if (const auto k = foldConst(in.r)) {
          const CType pt = typeOf(in.l);
          const int w = sizeOf(CType{pt.base, pt.ptr - 1, std::nullopt});
          return constPlace(in.l, (in.op == "+" ? 1 : -1) * toInt32(*k) * w, w, slot);
        }
      }
      const CType pt = typeOf(e.e);
      return constPlace(e.e, 0, sizeOf(CType{pt.base, pt.ptr - 1, std::nullopt}), slot);
    }
    // a[i]
    const CType at = typeOf(e.a);
    if (at.ptr == 0) fail(e.pos, typeName(at) + " cannot be indexed");
    const int w = sizeOf(CType{at.base, at.ptr - 1, std::nullopt});
    if (const auto k = foldConst(e.i)) return constPlace(e.a, toInt32(*k) * w, w, slot);
    const CType it = typeOf(e.i);
    if (isByte(it) && !isSigned(it) && w <= 2) {
      // An unsigned byte index goes in A, and D2 steps by it once per byte
      // of the element. The base loads last, since loading it leaves A.
      const std::string base = baseLoad(e.a);
      if (base.empty()) genExpr(e.a, slot);
      const std::optional<Side> leaf = leafSide(e.i, 1);
      if (!leaf) genLow(e.i, slot + 1);
      op("LD A <- " + (leaf ? leaf->lo : lo(slot + 1)));
      op(base.empty() ? "LD D2 <- " + word(slot) : base);
      if (w == 1 && byteLoad) return {0, true};
      for (int k = 0; k < w; k++) op("LD D2 <- D2+A");
      return {};
    }
    // A word index: the element's address as a 16 bit sum, through A.
    genAddress(ep, slot);
    op("LD D2 <- " + word(slot));
    return {};
  }

  // base + a constant byte offset into D2. An offset that fits the
  // displacement stays for the access to add.
  Place constPlace(const ExprPtr& baseExpr, int offset, int width, int slot) {
    const std::string base = baseLoad(baseExpr);
    if (base.empty()) {
      genExpr(baseExpr, slot);
      op("LD D2 <- " + word(slot));
    } else {
      op(base);
    }
    if (offset >= 0 && offset + width - 1 <= 255) return {offset, false};
    op("LD D2 <- D2+" + S(offset & 0xffff));
    return {};
  }

  // Loads the element at place into slot, as size bytes.
  void loadPlace(const Place& p, int size, int slot) {
    use(slot);
    if (p.viaA) {
      op("LD A <- [D2+A]");
      op("LD " + lo(slot) + " <- A");
      return;
    }
    auto at = [&](int k) { return p.disp + k == 0 ? std::string("[D2]") : "[D2+" + S(p.disp + k) + "]"; };
    if (size == 1) {
      op("LD A <- " + at(0));
      op("LD " + lo(slot) + " <- A");
      return;
    }
    // Big-endian: the high byte first.
    op("LD A <- " + at(0));
    op("LD " + hi(slot) + " <- A");
    op("LD A <- " + at(1));
    op("LD " + lo(slot) + " <- A");
  }

  void storePlace(const Place& p, int size, const Side& v) {
    auto at = [&](int k) { return p.disp + k == 0 ? std::string("[D2]") : "[D2+" + S(p.disp + k) + "]"; };
    if (size == 1) {
      op("LD A <- " + v.lo);
      op("LD " + at(0) + " <- A");
      return;
    }
    op("LD A <- " + v.hi);
    op("LD " + at(0) + " <- A");
    op("LD A <- " + v.lo);
    op("LD " + at(1) + " <- A");
  }

  // The ADDRESS of an lvalue, into slot.
  CType genAddress(const ExprPtr& ep, int slot) {
    const Expr& e = *ep;
    switch (e.k) {
      case ExprKind::Id: {
        const Sym& s = lookup(e.name, e.pos);
        genAddrOfSym(s, slot);
        const CType inner = s.type.arrayLen ? CType{s.type.base, s.type.ptr, std::nullopt} : s.type;
        return CType{inner.base, inner.ptr + 1, std::nullopt};
      }
      case ExprKind::Un:
        if (e.op == "*") return genExpr(e.e, slot);
        break;
      case ExprKind::Index: {
        // A constant or byte index: the address adder, through placeOf.
        const CType idx = typeOf(e.i);
        if (foldConst(e.i) || (isByte(idx) && !isSigned(idx))) {
          const CType pt = typeOf(e.a);
          const CType elem{pt.base, pt.ptr - 1, std::nullopt};
          if (foldConst(e.i) || sizeOf(elem) <= 2) {
            const Place p = placeOf(ep, slot, false);
            if (p.disp != 0) op("LD D2 <- D2+" + S(p.disp));
            op("LD " + word(slot) + " <- D2");
            return CType{elem.base, elem.ptr + 1, std::nullopt};
          }
        }
        // base + index * elementSize, as 16 bit arithmetic. For bytes the
        // base and the index can be named in the instructions: a pointer
        // variable, a global array's address as an immediate, an index
        // variable.
        {
          const CType pt = typeOf(e.a);
          if (pt.ptr > 0 && sizeOf(CType{pt.base, pt.ptr - 1, std::nullopt}) == 1 && !romRef(e.a)) {
            std::optional<Side> base = leafSide(e.a, 2);
            if (!base && e.a->k == ExprKind::Id) {
              const Sym* sym = find(e.a->name);
              if (sym && sym->type.arrayLen && sym->where == Sym::Where::Global) {
                base = Side{sym->label + " & $FF", sym->label + " >> 8", std::nullopt};
              }
            }
            const std::optional<Side> index = leafSide(e.i, 2);
            if (base || index) {
              Side b = base ? *base : Side{};
              Side i = index ? *index : Side{};
              if (!base) {
                genExpr(e.a, slot);
                b = sideOf(slot);
              }
              if (!index) {
                const CType it = genExpr(e.i, base ? slot : slot + 1);
                convert(base ? slot : slot + 1, it, T(BaseType::UInt));
                i = sideOf(base ? slot : slot + 1);
              }
              use(slot);
              op("LD A <- " + b.lo);
              op("ADD A <- " + i.lo);
              op("LD " + lo(slot) + " <- A");
              op("LD A <- " + b.hi);
              op("ADC A <- " + i.hi);
              op("LD " + hi(slot) + " <- A");
              return CType{pt.base, pt.ptr, std::nullopt};
            }
          }
        }
        const CType at = genExpr(e.a, slot);
        if (at.ptr == 0) fail(e.pos, typeName(at) + " cannot be indexed");
        const CType elem{at.base, at.ptr - 1, std::nullopt};
        const int w = sizeOf(elem);
        const CType it = genExpr(e.i, slot + 1);
        convert(slot + 1, it, T(BaseType::UInt));
        if (w != 1) scaleBy(slot + 1, w);
        addWord(slot, slot + 1);
        return CType{elem.base, elem.ptr + 1, std::nullopt};
      }
      default: break;
    }
    fail(e.pos, "this is not something that has an address");
  }

  void scaleBy(int slot, int w) {
    if (w == 2) { addWordSelf(slot); return; }
    if (w == 4) { addWordSelf(slot); addWordSelf(slot); return; }
    if (w == 8) { addWordSelf(slot); addWordSelf(slot); addWordSelf(slot); return; }
    // Anything else is a real multiply.
    loadConst(slot + 1, w, T(BaseType::UInt));
    callRuntime("__mul16", slot, slot + 1);
  }

  void addWordSelf(int slot) {
    op("LD A <- " + lo(slot));
    op("ADD A <- " + lo(slot));
    op("LD " + lo(slot) + " <- A");
    op("LD A <- " + hi(slot));
    op("ADC A <- " + hi(slot));
    op("LD " + hi(slot) + " <- A");
  }

  void addWord(int a, int b) {
    op("LD A <- " + lo(a));
    op("ADD A <- " + lo(b));
    op("LD " + lo(a) + " <- A");
    op("LD A <- " + hi(a));
    op("ADC A <- " + hi(b));
    op("LD " + hi(a) + " <- A");
  }

  void loadThrough(int slot, int size) {
    op("LD D2 <- " + word(slot));
    if (size == 1) {
      op("LD A <- [D2]");
      op("LD " + lo(slot) + " <- A");
      return;
    }
    op("LD A <- [D2]+");
    op("LD " + hi(slot) + " <- A");
    op("LD A <- [D2]");
    op("LD " + lo(slot) + " <- A");
  }

  void storeThrough(int addrSlot, int valSlot, int size) {
    op("LD D2 <- " + word(addrSlot));
    if (size == 1) {
      op("LD A <- " + lo(valSlot));
      op("LD [D2] <- A");
      return;
    }
    op("LD A <- " + hi(valSlot));
    op("LD [D2]+ <- A");
    op("LD A <- " + lo(valSlot));
    op("LD [D2] <- A");
  }

  // want says the assignment's value is used, so it has to be left in
  // slot. A statement's assignment leaves it out.
  CType genAssign(const ExprPtr& ep, int slot, bool want = true) {
    const Expr& e = *ep;
    const CType target = typeOf(e.l);
    if (e.op != "=") {
      const std::string op2 = e.op.substr(0, e.op.size() - 1);
      return genAssign(mkAssign(e.l, mkBin(op2, e.l, e.r, e.pos), e.pos), slot, want);
    }
    // A byte target keeps only the low byte, so only the low byte is
    // worked out.
    const bool low = sizeOf(target) == 1 && target.ptr == 0 && !romRef(e.r) && !isFloat(typeOf(e.r));
    // Simple names take the short path: no address to compute.
    if (e.l->k == ExprKind::Id) {
      const Sym& s = lookup(e.l->name, e.l->pos);
      if (s.type.arrayLen) fail(e.pos, s.name + " is an array and cannot be assigned");
      const bool frame = s.where == Sym::Where::Frame;
      auto destAt = [&](int k) {
        if (frame) return "[D1+" + S(s.offset + k) + "]";
        return k ? "[" + s.label + "+" + S(k) + "]" : "[" + s.label + "]";
      };
      const int size = target.ptr > 0 ? 2 : sizeOf(target);
      if (!isFloat(target) && !isWide(target) && !romRef(e.r)) {
        // A word plus a constant is one instruction of the address adder.
        if (size == 2) {
          if (const auto plan = leaPlan(e.r)) {
            leaAdd(plan->first, plan->second, slot);
            op("LD " + destAt(0) + " <- D2");
            if (want) op("LD " + word(slot) + " <- D2");
            return target;
          }
        }
        // The arithmetic writes the variable itself, byte by byte.
        const Expr& r = *e.r;
        if (!want && (frame || inZeroPage(s)) && r.k == ExprKind::Bin &&
            (r.op == "+" || r.op == "-" || r.op == "&" || r.op == "|" || r.op == "^")) {
          const CType lt0 = typeOf(r.l);
          const CType rt0 = typeOf(r.r);
          if (lt0.ptr == 0 && rt0.ptr == 0 && !isFloat(lt0) && !isFloat(rt0) && !isWide(lt0) && !isWide(rt0)) {
            const Side dest = size == 1 ? Side{destAt(0), "", std::nullopt} : Side{destAt(1), destAt(0), std::nullopt};
            aluBinary(e.r, slot, size == 1 ? T(BaseType::UChar) : common(lt0, rt0), size, dest);
            return target;
          }
        }
      }
      if (low) {
        genLow(e.r, slot);
      } else {
        const CType vt = genExpr(e.r, slot);
        convert(slot, vt, target);
      }
      if (s.where == Sym::Where::Frame) storeFrame(s.offset, sizeOf(s.type), slot);
      else storeGlobal(s, slot);
      return target;
    }
    // Through an address: a[i] = v or *p = v. A leaf value is read after
    // the address is in D2, and the address is built first. Anything else
    // is worked out first and kept in slot while the address is built.
    const int size = sizeOf(target);
    if (e.l->k == ExprKind::Index || (e.l->k == ExprKind::Un && e.l->op == "*")) {
      const std::optional<Side> leaf =
          !isFloat(target) && !isWide(target) && !romRef(e.r) ? leafSide(e.r, size) : std::nullopt;
      if (leaf) {
        const Place p = placeOf(e.l, slot, false);
        storePlace(p, size, *leaf);
        // The value of the assignment, for a caller that wants it.
        op("LD A <- " + leaf->lo);
        op("LD " + lo(slot) + " <- A");
        if (size == 2) {
          op("LD A <- " + leaf->hi);
          op("LD " + hi(slot) + " <- A");
        }
        return target;
      }
      if (low) {
        genLow(e.r, slot);
      } else {
        const CType vt = genExpr(e.r, slot);
        convert(slot, vt, target);
      }
      const Place p = placeOf(e.l, slot + 1, false);
      storePlace(p, size, sideOf(slot));
      return target;
    }
    if (low) {
      genLow(e.r, slot);
    } else {
      const CType vt = genExpr(e.r, slot);
      convert(slot, vt, target);
    }
    genAddress(e.l, slot + 1);
    storeThrough(slot + 1, slot, sizeOf(target));
    return target;
  }

  CType genUnary(const ExprPtr& ep, int slot) {
    const Expr& e = *ep;
    if (e.op == "+") return genExpr(e.e, slot);

    if (e.op == "-") {
      const CType t = genExpr(e.e, slot);
      const CType r = common(t, T(BaseType::Int));
      convert(slot, t, r);
      // 0 - x, in two bytes.
      op("LD A <- 0");
      op("SUB A <- " + lo(slot));
      op("LD " + lo(slot) + " <- A");
      if (sizeOf(r) == 2) {
        op("LD A <- 0");
        op("SBC A <- " + hi(slot));
        op("LD " + hi(slot) + " <- A");
      }
      return r;
    }

    if (e.op == "~") {
      const CType t = genExpr(e.e, slot);
      const CType r = common(t, T(BaseType::Int));
      convert(slot, t, r);
      op("LD A <- " + lo(slot));
      op("XOR A <- $FF");
      op("LD " + lo(slot) + " <- A");
      if (sizeOf(r) == 2) {
        op("LD A <- " + hi(slot));
        op("XOR A <- $FF");
        op("LD " + hi(slot) + " <- A");
      }
      return r;
    }

    if (e.op == "!") return boolValue(ep, slot);

    if (e.op == "&") return genAddress(e.e, slot);

    if (e.op == "*") {
      const CType pt = typeOf(e.e);
      if (pt.ptr == 0) fail(e.pos, "cannot take * of " + typeName(pt));
      const CType t{pt.base, pt.ptr - 1, std::nullopt};
      loadPlace(placeOf(ep, slot, sizeOf(t) == 1), sizeOf(t), slot);
      return t;
    }

    // ++ and --
    auto a = std::make_shared<Expr>();
    a->k = ExprKind::Assign;
    a->op = e.op == "++" ? "+=" : "-=";
    a->l = e.e;
    a->r = numOne(e.pos);
    a->pos = e.pos;
    return genAssign(a, slot);
  }

  CType genPost(const ExprPtr& ep, int slot) {
    const Expr& e = *ep;
    // The old value is the expression's value, so it is kept before the step.
    const CType t = genExpr(e.e, slot);
    auto a = std::make_shared<Expr>();
    a->k = ExprKind::Assign;
    a->op = e.op == "++" ? "+=" : "-=";
    a->l = e.e;
    a->r = numOne(e.pos);
    a->pos = e.pos;
    genAssign(a, slot + 1);
    return t;
  }

  void callRuntime(const std::string& name, int a, int b) {
    runtimeUsed_.insert(name);
    op("LD A <- " + hi(a));
    op("LD [" + S(ZP_RA) + "] <- A");
    op("LD A <- " + lo(a));
    op("LD [" + S(ZP_RA) + "+1] <- A");
    op("LD A <- " + hi(b));
    op("LD [" + S(ZP_RB) + "] <- A");
    op("LD A <- " + lo(b));
    op("LD [" + S(ZP_RB) + "+1] <- A");
    op("JSR " + name);
    op("LD A <- [" + S(ZP_RA) + "]");
    op("LD " + hi(a) + " <- A");
    op("LD A <- [" + S(ZP_RA) + "+1]");
    op("LD " + lo(a) + " <- A");
  }

  CType genBinary(const ExprPtr& ep, int slot) {
    const Expr& e = *ep;
    const std::string& bop = e.op;

    if (bop == "&&" || bop == "||") return boolValue(ep, slot);

    const CType lt0 = typeOf(e.l);
    const CType rt0 = typeOf(e.r);

    if (const auto plan = leaPlan(ep)) {
      leaAdd(plan->first, plan->second, slot);
      op("LD " + word(slot) + " <- D2");
      use(slot);
      return lt0.ptr > 0 ? lt0 : common(lt0, rt0);
    }

    // Pointer arithmetic: p + n scales n by what p points at.
    if ((bop == "+" || bop == "-") && lt0.ptr > 0 && rt0.ptr == 0) {
      const CType elem{lt0.base, lt0.ptr - 1, std::nullopt};
      const int w = sizeOf(elem);
      genExpr(e.l, slot);
      const CType it = genExpr(e.r, slot + 1);
      convert(slot + 1, it, T(BaseType::UInt));
      if (w != 1) scaleBy(slot + 1, w);
      if (bop == "+") addWord(slot, slot + 1);
      else subWord(slot, slot + 1);
      return lt0;
    }

    const CType rtype = common(lt0, rt0);
    const bool compare = isCompare(bop);
    const int width = sizeOf(rtype);
    if (compare && !isWide(rtype)) return boolValue(ep, slot);

    if (bop == "+" || bop == "-" || bop == "&" || bop == "|" || bop == "^") {
      aluBinary(ep, slot, rtype, width);
      return rtype;
    }

    const CType lt = genExpr(e.l, slot);
    convert(slot, lt, rtype);
    if (bop == ">>" && !isSigned(rtype) && width == 2) {
      // An unsigned shift by eight is the high byte moved down, four
      // instructions where the coprocessor costs fifty. Every port write of
      // a 16 bit value does it, so the library wrappers ride on this.
      const auto n = foldConst(e.r);
      if (n && *n == 8) {
        op("LD A <- " + hi(slot));
        op("LD " + lo(slot) + " <- A");
        op("LD A <- 0");
        op("LD " + hi(slot) + " <- A");
        return rtype;
      }
    }
    const CType rt = genExpr(e.r, slot + 1);
    convert(slot + 1, rt, rtype);

    if (compare) {
      compareInto(slot, bop, slot, slot + 1, rtype);
      return T(BaseType::Int);
    }

    if (bop == "+") { if (width == 1) byteOp(slot, slot + 1, "ADD"); else addWord(slot, slot + 1); }
    else if (bop == "-") { if (width == 1) byteOp(slot, slot + 1, "SUB"); else subWord(slot, slot + 1); }
    else if (bop == "&") bitOp(slot, slot + 1, "AND", width);
    else if (bop == "|") bitOp(slot, slot + 1, "OR", width);
    else if (bop == "^") bitOp(slot, slot + 1, "XOR", width);
    else if (bop == "*") callRuntime("__mul16", slot, slot + 1);
    else if (bop == "/") callRuntime(isSigned(rtype) ? "__sdiv16" : "__udiv16", slot, slot + 1);
    else if (bop == "%") callRuntime(isSigned(rtype) ? "__srem16" : "__urem16", slot, slot + 1);
    else if (bop == "<<") genShiftLeft(slot, slot + 1, ep);
    else if (bop == ">>") callRuntime(isSigned(rtype) ? "__sshr16" : "__ushr16", slot, slot + 1);
    else fail(e.pos, bop + " is not in the subset");
    return rtype;
  }

  // +, -, &, | and ^ at a width, into slot. A side that is a leaf is named
  // in the instruction, not copied to a temp first. For the commutative
  // four, a leaf on the left swaps over, so only one side is ever
  // evaluated when one is a leaf.
  void aluBinary(const ExprPtr& ep, int slot, const CType& rtype, int width,
                 const std::optional<Side>& dest = std::nullopt) {
    const Expr& e = *ep;
    const std::string& bop = e.op;
    auto evaluate = [&](const ExprPtr& x, int at) {
      if (width == 1) {
        genLow(x, at);
      } else {
        const CType got = genExpr(x, at);
        convert(at, got, rtype);
      }
      return sideOf(at);
    };
    std::optional<Side> ls = leafSide(e.l, width);
    std::optional<Side> rs = leafSide(e.r, width);
    Side a;
    Side b;
    if (rs) {
      a = ls ? *ls : evaluate(e.l, slot);
      b = *rs;
    } else if (ls && bop != "-") {
      a = evaluate(e.r, slot);
      b = *ls;
    } else {
      // The right side first, so the left one is the last thing stored and
      // the load that follows is a reload the peephole pass removes.
      b = evaluate(e.r, slot);
      a = evaluate(e.l, slot + 1);
    }
    use(slot);
    const std::string outLo = dest ? dest->lo : lo(slot);
    const std::string outHi = dest ? dest->hi : hi(slot);
    const std::string m1 = bop == "+" ? "ADD" : bop == "-" ? "SUB" : bop == "&" ? "AND" : bop == "|" ? "OR" : "XOR";
    const std::string m2 = bop == "+" ? "ADC" : bop == "-" ? "SBC" : m1;
    op("LD A <- " + a.lo);
    if (!(b.imm && (*b.imm & 0xff) == 0 && m1 != "AND")) op(m1 + " A <- " + b.lo);
    else if (m1 == "ADD" || m1 == "SUB") op(m1 + " A <- 0");
    op("LD " + outLo + " <- A");
    if (width == 1) return;
    const bool hiZero = b.hi == "0";
    if (hiZero && m1 == "AND") {
      op("LD A <- 0");
    } else {
      op("LD A <- " + a.hi);
      if (!(hiZero && (m1 == "OR" || m1 == "XOR"))) op(m2 + " A <- " + b.hi);
    }
    op("LD " + outHi + " <- A");
  }

  // A word plus or minus a constant: the word and the constant, the
  // constant already scaled for a pointer. Nothing when e is not that.
  std::optional<std::pair<ExprPtr, int>> leaPlan(const ExprPtr& ep) {
    const Expr& e = *ep;
    if (e.k != ExprKind::Bin || (e.op != "+" && e.op != "-")) return std::nullopt;
    const auto k = foldConst(e.r);
    if (!k || romRef(e.l)) return std::nullopt;
    const CType lt = typeOf(e.l);
    if (isFloat(lt) || isWide(lt) || isFloat(typeOf(e.r))) return std::nullopt;
    int scale = 1;
    if (lt.ptr > 0) scale = sizeOf(CType{lt.base, lt.ptr - 1, std::nullopt});
    else if (sizeOf(lt) != 2) return std::nullopt;
    const int v = toInt32(*k) * scale * (e.op == "-" ? -1 : 1);
    return std::make_pair(e.l, v);
  }

  // base + k into D2.
  void leaAdd(const ExprPtr& base, int k, int slot) {
    const std::string load = baseLoad(base);
    if (load.empty()) {
      genExpr(base, slot);
      op("LD D2 <- " + word(slot));
    } else {
      op(load);
    }
    if ((k & 0xffff) != 0) op("LD D2 <- D2+" + S(k & 0xffff));
  }

  // A __ROM name, or one indexed. typeOf cannot answer for it, and genExpr
  // refuses it with the advice.
  bool romRef(const ExprPtr& ep) const {
    const ExprPtr& named = ep->k == ExprKind::Index ? ep->a : ep;
    return named->k == ExprKind::Id && romSyms_.count(named->name) > 0;
  }

  // The low byte of a value, into lo(slot), for a place that keeps one
  // byte: a byte variable, a byte argument. Arithmetic modulo 256 needs
  // only the low bytes of +, -, &, |, ^ and ~, so those run at width 1.
  // Anything else is evaluated whole and its low byte taken.
  void genLow(const ExprPtr& ep, int slot) {
    const Expr& e = *ep;
    use(slot);
    // A __ROM name has no type to ask for. genExpr says what to do instead.
    if (romRef(ep)) {
      genExpr(ep, slot);
      return;
    }
    const CType t = typeOf(ep);
    if (!isFloat(t) && !isWide(t)) {
      if (auto leaf = leafSide(ep, 1)) {
        op("LD A <- " + leaf->lo);
        op("LD " + lo(slot) + " <- A");
        return;
      }
      if (e.k == ExprKind::Bin && (e.op == "+" || e.op == "-" || e.op == "&" || e.op == "|" || e.op == "^")) {
        const CType lt0 = typeOf(e.l);
        const CType rt0 = typeOf(e.r);
        if (lt0.ptr == 0 && rt0.ptr == 0 && !isFloat(lt0) && !isFloat(rt0) && !isWide(lt0) && !isWide(rt0)) {
          aluBinary(ep, slot, T(BaseType::UChar), 1);
          return;
        }
      }
      if (e.k == ExprKind::Un && e.op == "~" && !isFloat(typeOf(e.e))) {
        genLow(e.e, slot);
        op("LD A <- " + lo(slot));
        op("XOR A <- $FF");
        op("LD " + lo(slot) + " <- A");
        return;
      }
      if (e.k == ExprKind::Cast && !isFloat(typeOf(e.e)) && !isWide(typeOf(e.e)) && e.type.ptr == 0 &&
          !isFloat(e.type)) {
        genLow(e.e, slot);
        return;
      }
    }
    genExpr(ep, slot);
  }

  // A shift left by a small constant is adds, which the CPU does in one or
  // two instructions. Anything else goes to the coprocessor.
  void genShiftLeft(int slot, int rslot, const ExprPtr& e) {
    const auto n = foldConst(e->r);
    if (n && *n >= 0 && *n <= 4) {
      for (int i = 0; i < static_cast<int>(*n); i++) addWordSelf(slot);
      return;
    }
    callRuntime("__shl16", slot, rslot);
  }

  void byteOp(int a, int b, const std::string& mn) {
    op("LD A <- " + lo(a));
    op(mn + " A <- " + lo(b));
    op("LD " + lo(a) + " <- A");
  }

  void bitOp(int a, int b, const std::string& mn, int width) {
    byteOp(a, b, mn);
    if (width == 2) {
      op("LD A <- " + hi(a));
      op(mn + " A <- " + hi(b));
      op("LD " + hi(a) + " <- A");
    }
  }

  void subWord(int a, int b) {
    op("LD A <- " + lo(a));
    op("SUB A <- " + lo(b));
    op("LD " + lo(a) + " <- A");
    op("LD A <- " + hi(a));
    op("SBC A <- " + hi(b));
    op("LD " + hi(a) + " <- A");
  }

  // The machine has no CMP, so a comparison is a subtract and a jump on
  // its flags, and the value is 0 or 1 by which way the jump went.
  void compareInto(int dst, const std::string& cop, int a, int b, const CType& t) {
    use(dst);
    const std::string yes = uniq("cmpy");
    const std::string done = uniq("cmpd");
    compareSides(cop, sideOf(a), sideOf(b), sizeOf(t), isSigned(t) && t.ptr == 0, true, yes);
    op("LD A <- 0");
    op("JMP " + done);
    lab(yes);
    op("LD A <- 1");
    lab(done);
    op("LD " + lo(dst) + " <- A");
    op("LD A <- 0");
    op("LD " + hi(dst) + " <- A");
  }

  // ---- calls -----------------------------------------------------------------

  CType genCall(const ExprPtr& ep, int slot) {
    const Expr& e = *ep;
    if (e.fn->k != ExprKind::Id) fail(e.pos, "only a plain function name can be called yet");
    const std::string& name = e.fn->name;

    if (name == "out" || name == "in") return genPortIntrinsic(name, ep, slot);
    if (name == "gpu_printf") return genPrintf(ep, slot);

    auto fit = funcs_.find(name);
    if (fit == funcs_.end()) fail(e.pos, name + " is not declared");
    const FuncDecl& f = fit->second;
    if (!f.variadic && e.args.size() != f.params.size()) {
      fail(e.pos, name + " takes " + S(static_cast<int>(f.params.size())) + " argument" +
                      (f.params.size() == 1 ? "" : "s") + " and " + S(static_cast<int>(e.args.size())) +
                      (e.args.size() == 1 ? " was" : " were") + " given");
    }

    // Every argument is evaluated BEFORE the stack moves, because an argument
    // may itself contain a call and that call would move it again. A double
    // is eight bytes and lives in RAM, so it goes to a double temp of its own
    // and is copied into the frame after.
    std::vector<int> offsets;
    int bytes = 0;
    int dslot = 0;
    std::vector<std::optional<int>> dOf;
    for (size_t i = 0; i < e.args.size(); i++) {
      const CType want = i < f.params.size() ? f.params[i].type : T(BaseType::Int);
      const int si = slot + static_cast<int>(i);
      if (isFloat(want)) {
        genDouble(e.args[i], dslot, si);
        dOf.push_back(dslot);
        dslot += 3;  // a double operation needs the two slots after its own
      } else {
        const CType t = genExpr(e.args[i], si);
        convert(si, t, want);
        dOf.push_back(std::nullopt);
      }
      offsets.push_back(bytes);
      bytes += std::max(sizeOf(want), 1);
    }
    use(slot + std::max(static_cast<int>(e.args.size()), 1));

    for (int i = 0; i < slot; i++) spill(i, true);
    for (int i = 0; i < dLive_; i++) spillDouble(i, true);
    if (bytes > 0) {
      // __sp is D1 between calls, so the arguments' space starts below it.
      op("LD D2 <- D1-" + S(bytes));
      op("LD [" + S(ZP_SP) + "] <- D2");
      for (size_t i = 0; i < e.args.size(); i++) {
        const CType pt = i < f.params.size() ? f.params[i].type : T(BaseType::Int);
        const int size = std::max(sizeOf(pt), 1);
        const int at = offsets[i];
        const int si = slot + static_cast<int>(i);
        if (dOf[i]) {
          // D2 holds the new frame base, so the destination is worked out
          // from it. Adding the offset costs less than reloading a pointer.
          op("LD " + word(slot) + " <- D2");
          if (at != 0) {
            op("LD A <- " + lo(slot));
            op("ADD A <- " + S(at));
            op("LD " + lo(slot) + " <- A");
            op("LD A <- " + hi(slot));
            op("ADC A <- 0");
            op("LD " + hi(slot) + " <- A");
          }
          moveDyn(emitter(), Where::zp(tempLabel(slot)), Where::at(slotAt(*dOf[i])));
          op("LD D2 <- [" + S(ZP_SP) + "]");
          continue;
        }
        if (size == 1) {
          op("LD A <- " + lo(si));
          op("LD [D2+" + S(at) + "] <- A");
        } else {
          op("LD A <- " + hi(si));
          op("LD [D2+" + S(at) + "] <- A");
          op("LD A <- " + lo(si));
          op("LD [D2+" + S(at + 1) + "] <- A");
        }
      }
    }
    op("JSR " + name);
    op("LD D1 <- [" + S(ZP_SP) + "]");
    for (int i = 0; i < slot; i++) spill(i, false);
    for (int i = 0; i < dLive_; i++) spillDouble(i, false);
    if (sizeOf(f.ret) == 1 && f.ret.ptr == 0) {
      op("LD A <- [" + S(ZP_RET) + "+1]");
      op("LD " + lo(slot) + " <- A");
    } else if (sizeOf(f.ret) > 0) {
      op("LD D2 <- [" + S(ZP_RET) + "]");
      op("LD " + word(slot) + " <- D2");
    }
    return f.ret;
  }

  // One double temp to or from the frame. Eight bytes, and the frame's
  // address is not a constant, so it is worked out and the move goes through
  // the pointer. A recursive function needs one copy per invocation, which
  // is what a frame is.
  void spillDouble(int i, bool out) {
    const int at = localBytes_ + saveBytes_ + i * DSIZE;
    const int scratch = maxSlot_ + 1;
    use(scratch);
    op("LD " + word(scratch) + " <- D1");
    if (at != 0) {
      op("LD A <- " + lo(scratch));
      op("ADD A <- " + S(at & 0xff));
      op("LD " + lo(scratch) + " <- A");
      op("LD A <- " + hi(scratch));
      op("ADC A <- " + S((at >> 8) & 0xff));
      op("LD " + hi(scratch) + " <- A");
    }
    if (out) moveDyn(emitter(), Where::zp(tempLabel(scratch)), Where::at(slotAt(i)));
    else moveDyn(emitter(), Where::at(slotAt(i)), Where::zp(tempLabel(scratch)));
  }

  // One temp slot to or from its place in the frame's save area.
  void spill(int i, bool out) {
    const int at = localBytes_ + i * 2;
    if (out) {
      op("LD A <- " + hi(i));
      op("LD [D1+" + S(at) + "] <- A");
      op("LD A <- " + lo(i));
      op("LD [D1+" + S(at + 1) + "] <- A");
    } else {
      op("LD A <- [D1+" + S(at) + "]");
      op("LD " + hi(i) + " <- A");
      op("LD A <- [D1+" + S(at + 1) + "]");
      op("LD " + lo(i) + " <- A");
    }
  }

  // gpu_printf. The compiler does the work the design gives it: the template
  // goes to ROM, because CMD_PRINTF reads it from the cartridge and a
  // template is ROM by hardware definition, and the arguments are marshalled
  // into a RAM block, big-endian and sized by the FORMAT. The widths come
  // from the device's own formatter, so the block written is the block read.
  CType genPrintf(const ExprPtr& ep, int slot) {
    const Expr& e = *ep;
    if (e.args.empty()) fail(e.pos, "gpu_printf needs a format: gpu_printf(\"score %u\", n)");
    const ExprPtr& fmt = e.args[0];
    std::optional<std::vector<int>> widths;
    // Which conversions read a real number. An int under one of those is
    // widened to a double, the way C's promotion of a vararg would.
    std::vector<bool> real;
    std::string label;

    if (fmt->k == ExprKind::Str) {
      // A string literal handed to gpu_printf goes to the cartridge on its
      // own, which is what makes the common call read like C. Every float
      // conversion in it is widened to the l length first: a double is the
      // only real number this C has, so %f and %lf both take one, and the
      // device reads four bytes for %f. Widening the template keeps the
      // device's reading and C's meaning in step.
      const std::vector<uint8_t> tmpl = widenFloatSpecs(fmt->bytes);
      label = romLiteral(tmpl);
      widths = templateArgWidths(tmpl);
      real = templateFloatConvs(tmpl);
      const int given = static_cast<int>(e.args.size()) - 1;
      const int n = static_cast<int>(widths->size());
      if (n != given) {
        fail(e.pos, "this format takes " + S(n) + " argument" + (n == 1 ? "" : "s") + " and " + S(given) +
                        (given == 1 ? " was" : " were") +
                        " given. On this machine an int is 16 bits, so %d reads two bytes, %hhu one, %lu four "
                        "and %llu eight.");
      }
    } else if (fmt->k == ExprKind::Id && romSyms_.count(fmt->name)) {
      label = fmt->name;
    } else {
      fail(e.pos, "gpu_printf's format has to be a string literal or a __ROM object. CMD_PRINTF reads it from the "
                  "cartridge, which the CPU cannot write.");
    }

    // Widths from the format when it is known, and from the argument types
    // otherwise. The two agree in every program that is right. A double is
    // eight bytes either way, so a __ROM template prints one with %lf.
    std::vector<int> sizes;
    if (widths) sizes = *widths;
    else for (size_t i = 1; i < e.args.size(); i++) {
      const CType t = typeOf(e.args[i]);
      sizes.push_back(isFloat(t) ? DSIZE : std::min(sizeOf(t), 2));
      real.push_back(isFloat(t));
    }
    int at = 0;
    for (size_t i = 1; i < e.args.size(); i++) {
      const int w = i - 1 < sizes.size() ? sizes[i - 1] : 2;
      const bool isReal = i - 1 < real.size() && real[i - 1];
      if (w == DSIZE && (isReal || isFloat(typeOf(e.args[i])))) {
        // The eight IEEE bytes, big-endian, straight from the double slot
        // the expression left them in. An int under %lf is widened first.
        genDouble(e.args[i], 0, slot);
        moveConst(emitter(), S(PRINTF_ARGS) + " + " + S(at), slotAt(0));
        at += w;
        continue;
      }
      const CType t = genExpr(e.args[i], slot);
      // A value wider than the machine's int is zero filled to what the
      // format reads, high bytes first, which is how the device reads it.
      // Through D2: direct byte addressing is zero page only, and the
      // argument block sits after every global.
      if (w > 2) {
        op("LD D2 <- " + S(PRINTF_ARGS) + " + " + S(at));
        op("LD A <- 0");
        for (int k = 0; k < w - 2; k++) op("LD [D2]+ <- A");
      }
      const int lowAt = at + std::max(w - 2, 0);
      if (w == 1) {
        op("LD D2 <- " + S(PRINTF_ARGS) + " + " + S(at));
        op("LD A <- " + lo(slot));
        op("LD [D2] <- A");
      } else {
        convert(slot, t, T(BaseType::UInt));
        op("LD D2 <- " + S(PRINTF_ARGS) + " + " + S(lowAt));
        op("LD A <- " + hi(slot));
        op("LD [D2]+ <- A");
        op("LD A <- " + lo(slot));
        op("LD [D2] <- A");
      }
      at += w;
    }
    // At least one byte, even for a format with no conversions: the command
    // is pointed at the block whether or not it will read any of it.
    printfUsed_ = true;
    if (at > printfBytes_) printfBytes_ = at;

    op("OUT GPU_CART_BANK, (" + label + ") >> 16");
    op("OUT GPU_CART_HI, ((" + label + ") >> 8) & 255");
    op("OUT GPU_CART_LO, (" + label + ") & 255");
    op("OUT GPU_TEXT_ARG_HI, " + S(PRINTF_ARGS) + " >> 8");
    op("OUT GPU_TEXT_ARG_LO, " + S(PRINTF_ARGS) + " & 255");
    op("OUT GPU_CMD, CMD_PRINTF");
    return T(BaseType::Void);
  }

  // A string literal that has to live on the cartridge rather than in RAM.
  std::string romLiteral(const std::vector<uint8_t>& bytes) {
    const std::string key = "rom:" + bytesKey(bytes);
    auto hit = strings_.find(key);
    if (hit != strings_.end()) return hit->second;
    const std::string label = "__rs" + S(static_cast<int>(romLiterals_.size()));
    strings_[key] = label;
    romLiterals_.push_back({label, bytes});
    return label;
  }

  // ---- doubles ---------------------------------------------------------------
  //
  // A double expression leaves its value in double slot `d`, which is eight
  // bytes of RAM at __dt + d * 8. The slots are consecutive on purpose: the
  // coprocessor's block is A, then B, then the result, so the left operand
  // in slot d and the right in slot d + 1 ARE the device's A and B with no
  // copying, and the answer lands in slot d + 2.

  void useD(int d) {
    doubleUsed_ = true;
    if (d > maxDouble_) maxDouble_ = d;
  }

  std::string dlit(double v) {
    auto hit = dlits_.find(v);
    if (hit != dlits_.end()) return hit->second;
    const std::string label = "__df" + S(static_cast<int>(dlitData_.size()));
    dlits_[v] = label;
    dlitData_.push_back({label, v});
    return label;
  }

  // The address of a double lvalue, either a constant or a frame address put
  // into a slot pair at run time.
  Where dAddr(const ExprPtr& e, int slot) {
    use(slot);
    if (e->k == ExprKind::Id) {
      const Sym* s = find(e->name);
      if (s && s->where == Sym::Where::Global) return Where::at(s->label);
      if (s && s->where == Sym::Where::Frame) {
        genAddrOfSym(*s, slot);
        return Where::zp(tempLabel(slot));
      }
    }
    genAddress(e, slot);
    return Where::zp(tempLabel(slot));
  }

  // Generate a double expression into double slot d.
  void genDouble(const ExprPtr& e, int d, int slot) {
    useD(d + 2);
    const int savedLive = dLive_;
    dLive_ = d;
    genDoubleInner(e, d, slot);
    dLive_ = savedLive;
  }

  void genDoubleInner(const ExprPtr& ep, int d, int slot) {
    const Expr& e = *ep;
    switch (e.k) {
      case ExprKind::Num:
        moveConst(emitter(), slotAt(d), dlit(e.value));
        return;

      case ExprKind::Id:
      case ExprKind::Index: {
        // A named double, or one in an array: read from where it lives.
        const CType t = typeOf(ep);
        if (!isFloat(t)) { widenToDouble(ep, d, slot); return; }
        moveDyn(emitter(), Where::at(slotAt(d)), dAddr(ep, slot));
        return;
      }

      case ExprKind::Cast:
        if (isFloat(e.type)) { genDouble(e.e, d, slot); return; }
        break;

      case ExprKind::Un:
        if (e.op == "*" && isFloat(typeOf(ep))) {
          moveDyn(emitter(), Where::at(slotAt(d)), dAddr(ep, slot));
          return;
        }
        if (e.op == "+") { genDouble(e.e, d, slot); return; }
        if (e.op == "-") {
          genDouble(e.e, d, slot);
          acpRun(emitter(), slotAt(d), "ACP_NEG", F64);
          // NEG is unary, so the answer is at A + 8, which is slot d + 1.
          moveConst(emitter(), slotAt(d), slotAt(d + 1));
          return;
        }
        break;

      case ExprKind::Bin: {
        const std::string cmd = doubleOp(e.op);
        if (cmd.empty()) {
          fail(e.pos, e.op + " has no meaning on a double. The coprocessor does add, subtract, multiply and divide "
                          "on one, and the bitwise operators and the remainder are integer only.");
        }
        genDouble(e.l, d, slot);
        genDouble(e.r, d + 1, slot);
        acpRun(emitter(), slotAt(d), cmd, F64);
        moveConst(emitter(), slotAt(d), slotAt(d + 2));
        return;
      }

      case ExprKind::Call: {
        const CType t = typeOf(ep);
        if (!isFloat(t)) { widenToDouble(ep, d, slot); return; }
        genCall(ep, slot);
        moveConst(emitter(), slotAt(d), DRET);
        return;
      }

      case ExprKind::Assign:
        genDoubleAssign(ep, d, slot);
        return;

      case ExprKind::Cond: {
        const std::string f = uniq("dcf");
        const std::string end = uniq("dce");
        branch(e.c, false, f, slot);
        genDouble(e.t, d, slot);
        op("JMP " + end);
        lab(f);
        genDouble(e.f, d, slot);
        lab(end);
        return;
      }

      case ExprKind::Comma:
        genExpr(e.l, slot);
        genDouble(e.r, d, slot);
        return;

      default:
        break;
    }
    // Anything else with a narrow type is widened; anything else at all is
    // a shape this build does not do.
    const CType t = typeOf(ep);
    if (!isFloat(t)) { widenToDouble(ep, d, slot); return; }
    fail(e.pos, "this is not something the compiler can turn into a double yet");
  }

  // An int expression, widened. The coprocessor converts, so the eight bytes
  // go in as a signed 64 bit integer first.
  void widenToDouble(const ExprPtr& e, int d, int slot) {
    useD(d + 2);
    const CType t = genExpr(e, slot);
    convert(slot, t, T(BaseType::Int));
    // Sign fill the top six bytes, then the value's own two.
    const bool sgn = isSigned(t) || t.ptr == 0;
    op("LD D2 <- " + slotAt(d));
    if (sgn) {
      const std::string neg = uniq("dsx");
      const std::string done = uniq("dsxd");
      op("LD A <- " + hi(slot));
      op("JN " + neg);
      op("LD A <- 0");
      op("JMP " + done);
      lab(neg);
      op("LD A <- $FF");
      lab(done);
    } else {
      op("LD A <- 0");
    }
    for (int i = 0; i < 6; i++) op("LD [D2]+ <- A");
    op("LD A <- " + hi(slot));
    op("LD [D2]+ <- A");
    op("LD A <- " + lo(slot));
    op("LD [D2] <- A");
    // CVT is unary: the answer lands at A + 8, which is slot d + 1.
    acpRun(emitter(), slotAt(d), "ACP_CVT", I64, F64);
    moveConst(emitter(), slotAt(d), slotAt(d + 1));
  }

  // A double, narrowed to an int in `slot`. Truncates toward zero, which is
  // what a C cast does.
  void narrowFromDouble(const ExprPtr& e, int slot) {
    genDouble(e, 0, slot);
    useD(2);
    acpRun(emitter(), slotAt(0), "ACP_CVT", F64, I64);
    // The low two bytes of a big-endian 64 bit integer are its last two.
    op("LD D2 <- " + slotAt(1) + " + 6");
    op("LD A <- [D2]+");
    op("LD " + hi(slot) + " <- A");
    op("LD A <- [D2]");
    op("LD " + lo(slot) + " <- A");
  }

  // a <op> b on two doubles, leaving 0 or 1 in `slot`. ACP_CMP hands back
  // minus one, zero or one, and asking for an integer result makes it an
  // integer the CPU can already compare.
  CType genDoubleCompare(const ExprPtr& ep, int slot) {
    const Expr& e = *ep;
    genDouble(e.l, 0, slot);
    genDouble(e.r, 1, slot);
    useD(2);
    acpRun(emitter(), slotAt(0), "ACP_CMP", F64, I64);
    op("LD D2 <- " + slotAt(2) + " + 6");
    op("LD A <- [D2]+");
    op("LD " + hi(slot) + " <- A");
    op("LD A <- [D2]");
    op("LD " + lo(slot) + " <- A");
    loadConst(slot + 1, 0, T(BaseType::Int));
    compareInto(slot, e.op, slot, slot + 1, T(BaseType::Int));
    return T(BaseType::Int);
  }

  void genDoubleAssign(const ExprPtr& ep, int d, int slot) {
    const Expr& e = *ep;
    if (e.op != "=") {
      const std::string op2 = e.op.substr(0, e.op.size() - 1);
      genDoubleAssign(mkAssign(e.l, mkBin(op2, e.l, e.r, e.pos), e.pos), d, slot);
      return;
    }
    genDouble(e.r, d, slot);
    moveDyn(emitter(), dAddr(e.l, slot + 1), Where::at(slotAt(d)));
  }

  CType genPortIntrinsic(const std::string& name, const ExprPtr& ep, int slot) {
    const Expr& e = *ep;
    if (name == "out") {
      if (e.args.size() != 2) fail(e.pos, "out takes a port and a value: out(port, value)");
      const std::string port = portOf(e.args[0], e.pos);
      genExpr(e.args[1], slot);
      op("LD A <- " + lo(slot));
      op("OUT " + port + " <- A");
      return T(BaseType::Void);
    }
    if (e.args.size() != 1) fail(e.pos, "in takes a port: in(port)");
    const std::string port = portOf(e.args[0], e.pos);
    op("IN " + port + " -> A");
    op("LD " + lo(slot) + " <- A");
    op("LD A <- 0");
    op("LD " + hi(slot) + " <- A");
    return T(BaseType::UChar);
  }

  // A port has to be known at assembly time, and the assembler's own built-in
  // names are C constants here, so GPU_CMD reaches the assembler unchanged.
  std::string portOf(const ExprPtr& e, const Pos& pos) {
    if (e->k == ExprKind::Id && constantNamed(e->name)) return e->name;
    const auto v = foldConst(e);
    if (!v) {
      fail(pos, "a port has to be a name or a constant the compiler can work out. The assembler's port names are "
                "constants here too, so out(GPU_CMD, 1) works.");
    }
    return numText(*v);
  }
};

}  // namespace

namespace {

// The name of a blob that could not be read, and the form that asked for it.
[[noreturn]] void noAsset(const VarDecl& v, const AssetInit& a, const char* what) {
  fail(v.pos, a.name + ": cannot read the " + what + " for " + assetFormName(a.form) + " (is it beside the source?)");
}

// The bytes an asset initializer puts on the cartridge. Each form emits the
// blob the device command reads: CMD_BLIT for __image, CMD_SPRITE_DEF for
// __sprite, CMD_LOAD_PALETTE for __palette, CMD_DEF_SAMPLE for __sample.
// __file is the bytes as they are.
// A picture drawn in any colours comes out in the machine's palette. An
// image whose colours all exist there arrives already indexed to it. Any
// other keeps its own palette, which .image in assembly loads on request.
// C has no such step, so each pixel goes to the nearest machine colour and
// index 0, the transparent one, stays 0.
//
// A black pixel is a drawn pixel, so it must not land on index 0. It goes
// to the palette's other black entry when it has one, and to index 1 when
// it does not. The default 3-3-2 palette has black at 0 only, so a black
// pixel comes out as index 1, dark blue. `blackDrawn` says whether any did.
ImageAsset onMachinePalette(ImageAsset img, bool* blackDrawn = nullptr) {
  const std::vector<uint8_t> machine = machinePalette();
  if (img.palette.size() != 768 || img.palette == machine) return img;
  uint8_t black = 1;
  for (size_t i = 1; i < 256; i++) {
    if (machine[i * 3] == 0 && machine[i * 3 + 1] == 0 && machine[i * 3 + 2] == 0) {
      black = static_cast<uint8_t>(i);
      break;
    }
  }
  std::array<uint8_t, 256> map{};
  std::array<bool, 256> wasBlack{};
  for (size_t i = 1; i < 256; i++) {
    map[i] = nearestIndex(machine, img.palette[i * 3], img.palette[i * 3 + 1], img.palette[i * 3 + 2]);
    if (map[i] == 0) { map[i] = black; wasBlack[i] = true; }
  }
  for (uint8_t& p : img.pixels) {
    if (wasBlack[p] && blackDrawn) *blackDrawn = true;
    p = map[p];
  }
  img.palette = machine;
  return img;
}

// The remark a sprite with black pixels earns, so the dark blue background
// on the screen has an explanation in the build's notes.
constexpr const char* SPRITE_BLACK_NOTE =
    "black pixels in a sprite are drawn as index 1 and look dark blue; use a transparent background for the "
    "parts that should not draw";

std::vector<uint8_t> assetBytes(const VarDecl& v, const AssetInit& a, const Assets* assets) {
  const std::string& name = a.name;
  const char* what = a.form == AssetForm::File ? "file" : a.form == AssetForm::Sample ? "sample" : "image";
  if (!assets) noAsset(v, a, what);
  // The map first, then the loader, the way the assembler's asset() resolves
  // a directive's name.
  auto find = [&](const auto& map, const auto& loader) {
    auto it = map.find(name);
    if (it != map.end()) return it->second;
    if (loader) {
      if (auto got = loader(name)) return *got;
    }
    noAsset(v, a, what);
  };
  auto size = [](int w, int h) { return std::to_string(w) + " x " + std::to_string(h); };

  std::vector<uint8_t> out;
  switch (a.form) {
    case AssetForm::File: return find(assets->files, assets->loadFile);
    case AssetForm::Sample: return find(assets->samples, assets->loadSample);
    case AssetForm::Palette: {
      const ImageAsset img = find(assets->images, assets->loadImage);
      if (img.palette.size() != 768) fail(v.pos, name + ": the image carries no 256 entry palette for __palette");
      out.push_back(0);  // 0 means 256 entries, as a byte cannot say 256
      out.insert(out.end(), img.palette.begin(), img.palette.end());
      return out;
    }
    case AssetForm::Image: {
      const ImageAsset img = onMachinePalette(find(assets->images, assets->loadImage));
      if (img.width < 1 || img.height < 1 || img.width > 256 || img.height > 256) {
        fail(v.pos, name + ": " + size(img.width, img.height) + " does not fit CMD_BLIT, which draws at most 256 "
                                                                    "pixels a side");
      }
      out.push_back(static_cast<uint8_t>(img.width & 0xff));  // 256 is written as 0
      out.push_back(static_cast<uint8_t>(img.height & 0xff));
      out.insert(out.end(), img.pixels.begin(), img.pixels.end());
      return out;
    }
    case AssetForm::Sprite: {
      if (a.frames < 1) fail(v.pos, name + ": __sprite needs at least one frame");
      if (a.frames > gpu::SPRITE_FRAMES_MAX) {
        fail(v.pos, name + ": __sprite takes at most " + std::to_string(gpu::SPRITE_FRAMES_MAX) + " frames");
      }
      bool blackDrawn = false;
      const ImageAsset img = onMachinePalette(find(assets->images, assets->loadImage), &blackDrawn);
      if (blackDrawn && assets->note) assets->note(name, SPRITE_BLACK_NOTE);
      if (img.width < 1 || img.height < 1 || img.width % a.frames != 0) {
        fail(v.pos, name + ": " + std::to_string(img.width) + " pixels wide does not divide into " +
                        std::to_string(a.frames) + " frames for __sprite");
      }
      const int fw = img.width / a.frames;
      const int fh = img.height;
      if (fw > gpu::SPRITE_MAX || fh > gpu::SPRITE_MAX) {
        fail(v.pos, name + ": a frame is " + size(fw, fh) + " and a sprite is at most " +
                        std::to_string(gpu::SPRITE_MAX) + " pixels a side");
      }
      out.push_back(static_cast<uint8_t>(a.frames));
      out.push_back(static_cast<uint8_t>(fw));
      out.push_back(static_cast<uint8_t>(fh));
      // The strip is one row-major image. Each frame comes out as its own
      // row-major block, which is how CMD_SPRITE_DEF reads them.
      for (int f = 0; f < a.frames; f++) {
        for (int y = 0; y < fh; y++) {
          const size_t row = static_cast<size_t>(y * img.width + f * fw);
          out.insert(out.end(), img.pixels.begin() + static_cast<long>(row),
                     img.pixels.begin() + static_cast<long>(row + static_cast<size_t>(fw)));
        }
      }
      return out;
    }
  }
  return out;
}

}  // namespace

std::vector<RomEntry> layoutRom(const std::vector<VarDecl>& vars, const Assets* assets) {
  std::vector<RomEntry> out;
  std::map<std::string, std::string> seen;  // byte key -> first label
  std::map<std::string, int> at;
  int addr = 0;
  for (const VarDecl& v : vars) {
    if (!v.rom) continue;
    if (!v.init) {
      fail(v.pos, v.name + " is __ROM and needs a constant initializer. The cartridge is built at assembly time "
                          "and there is nothing else it could be built from.");
    }
    const std::vector<uint8_t> bytes = romBytesOf(v, assets);
    const std::string key = bytesKey(bytes);
    auto first = seen.find(key);
    RomEntry entry{v.name, first != seen.end() ? at[first->second] : addr, static_cast<int>(bytes.size()),
                   v.pos.file, v.pos.line, std::nullopt, bytes};
    if (first != seen.end()) {
      entry.sameAs = first->second;
    } else {
      seen[key] = v.name;
      addr += static_cast<int>(bytes.size());
    }
    at[v.name] = entry.addr;
    out.push_back(entry);
  }
  return out;
}

std::vector<uint8_t> romBytesOf(const VarDecl& v, const Assets* assets) {
  if (v.init->asset) return assetBytes(v, *v.init->asset, assets);
  const CType elemType{v.type.base, v.type.ptr, std::nullopt};
  if (elemType.ptr == 0 && elemType.base != BaseType::Char && elemType.base != BaseType::UChar &&
      elemType.base != BaseType::Int && elemType.base != BaseType::UInt) {
    fail(v.pos, v.name + " is __ROM and holds " + typeName(elemType) +
                    ". The cartridge holds bytes and words: declare it char, unsigned char, int or unsigned int.");
  }
  const int elem = sizeOf(elemType);
  auto push = [&](std::vector<uint8_t>& out, int n) {
    if (elem == 1) {
      out.push_back(static_cast<uint8_t>(n & 0xff));
    } else {
      out.push_back(static_cast<uint8_t>((n >> 8) & 0xff));
      out.push_back(static_cast<uint8_t>(n & 0xff));
    }
  };
  std::vector<uint8_t> out;
  if (v.init->isList) {
    for (const ExprPtr& x : v.init->list) push(out, constNow(x, v.pos));
    const size_t want = static_cast<size_t>((v.type.arrayLen ? *v.type.arrayLen : static_cast<int>(v.init->list.size())) * elem);
    while (out.size() < want) out.push_back(0);
    return out;
  }
  if (v.init->one->k == ExprKind::Str) {
    std::vector<uint8_t> b = v.init->one->bytes;
    b.push_back(0);
    const size_t want = v.type.arrayLen ? static_cast<size_t>(*v.type.arrayLen) : b.size();
    while (b.size() < want) b.push_back(0);
    return b;
  }
  push(out, constNow(v.init->one, v.pos));
  return out;
}

std::vector<bool> templateFloatConvs(const std::vector<uint8_t>& tmpl) {
  std::vector<bool> out;
  size_t p = 0;
  while (p < tmpl.size()) {
    if (tmpl[p] != '%') { p++; continue; }
    const auto spec = parseSpec(tmpl, p + 1);
    if (!spec) { p++; continue; }
    p = spec->next;
    if (spec->conv == '%') continue;
    out.push_back(std::string_view("fFeEgG").find(spec->conv) != std::string_view::npos);
  }
  return out;
}

std::vector<uint8_t> widenFloatSpecs(const std::vector<uint8_t>& tmpl) {
  std::vector<uint8_t> out;
  size_t p = 0;
  while (p < tmpl.size()) {
    if (tmpl[p] != '%') { out.push_back(tmpl[p++]); continue; }
    const auto spec = parseSpec(tmpl, p + 1);
    if (!spec) { out.push_back(tmpl[p++]); continue; }
    const bool isFloatConv = std::string_view("fFeEgG").find(spec->conv) != std::string_view::npos;
    // spec->next is one past the conversion letter, so the letter is the
    // byte before it. An l goes in front of it when the spec has no length.
    const size_t convAt = spec->next - 1;
    out.insert(out.end(), tmpl.begin() + static_cast<long>(p), tmpl.begin() + static_cast<long>(convAt));
    if (isFloatConv && spec->length.empty()) out.push_back('l');
    out.push_back(tmpl[convAt]);
    p = spec->next;
  }
  return out;
}

std::vector<int> templateArgWidths(const std::vector<uint8_t>& tmpl) {
  std::vector<int> out;
  size_t p = 0;
  while (p < tmpl.size()) {
    if (tmpl[p] != '%') { p++; continue; }
    const auto spec = parseSpec(tmpl, p + 1);
    if (!spec) { p++; continue; }
    p = spec->next;
    const char conv = spec->conv;
    if (conv == '%') continue;
    if (conv == 'c') { out.push_back(1); continue; }
    if (conv == 's' || conv == 'r') { out.push_back(2); continue; }
    if (std::string_view("fFeEgG").find(conv) != std::string_view::npos) {
      out.push_back(spec->length == "l" || spec->length == "ll" ? 8 : 4);
      continue;
    }
    out.push_back(intBytes(spec->length));
  }
  return out;
}

Compiled compileUnit(const std::string& src, const std::string& file) {
  const Unit unit = parse(src, file);
  return Gen(unit, false, nullptr, 0, nullptr).compile();
}

Compiled compileUnitTree(const Unit& unit, bool softMul, const Profile* profile, int zpReserve,
                         const Assets* assets) {
  return Gen(unit, softMul, profile, zpReserve, assets).compile();
}

}  // namespace sc8::cc
