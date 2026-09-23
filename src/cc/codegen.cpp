#include "cc/codegen.h"

#include <algorithm>
#include <set>

#include "cc/doubles.h"
#include "cc/layout.h"
#include "cc/lex.h"
#include "cc/parse.h"
#include "cc/runtime.h"
#include "cc/softmath.h"
#include "devices/constants.h"

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
  Gen(const Unit& unit, bool softMul, const Profile* profile)
      : unit_(unit), softMul_(softMul), profile_(profile) {
    for (const RomEntry& e : layoutRom(unit.vars)) romPlan_[e.name] = e;
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
    globalBase_ = RESERVED_BYTES + 2 * maxSlot_;
    reset();
    return assemble(body());
  }

 private:
  const Unit& unit_;
  bool softMul_;
  const Profile* profile_;

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
    const int reserved = RESERVED_BYTES + 2 * maxSlot_ + (softMul_ ? SOFT_ZP_BYTES : 0);
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
    romItems_.push_back(v.name + ":" + pad + "db " + joinBytes(romBytesOf(v), false));
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
      for (const ExprPtr& x : v.init->list) vals.push_back(initItem(x, v.pos));
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
    return v.name + ":" + pad + dir + " " + initItem(v.init->one, v.pos);
  }

  // One initializer item, as assembly source. A label keeps its name so the
  // assembler folds the address, which is the only thing that knows it yet.
  std::string initItem(const ExprPtr& e, const Pos& pos) {
    if (e->k == ExprKind::Un && e->op == "&" && e->e->k == ExprKind::Id) return e->e->name;
    if (e->k == ExprKind::Id) {
      auto g = globals_.find(e->name);
      if (g != globals_.end() && g->second.type.arrayLen) return e->name;
    }
    if (e->k == ExprKind::Str) return stringLabel(e->bytes);
    return S(constOf(e, pos));
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
        case StmtKind::Var: locals += sizeOf(s->decl.type); break;
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
    const int own = localBytes_ + saveBytes_ + dSaveBytes_;
    if (own > 0) addToSp(-own);
    op("LD D1 <- [" + S(ZP_SP) + "]");

    genStmt(f.body);

    lab(f.name + "__end");
    if (frameSize_ > 0) addToSp(frameSize_);
    op("RET");
    fn_ = nullptr;
  }

  // __sp += n, in two bytes. n may be negative.
  void addToSp(int n) {
    const int v = n & 0xffff;
    const int lo8 = v & 0xff;
    const int hi8 = (v >> 8) & 0xff;
    op("LD A <- [" + S(ZP_SP) + "+1]");
    op("ADD A <- " + S(lo8));
    op("LD [" + S(ZP_SP) + "+1] <- A");
    op("LD A <- [" + S(ZP_SP) + "]");
    op("ADC A <- " + S(hi8));
    op("LD [" + S(ZP_SP) + "] <- A");
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

      case StmtKind::Expr:
        if (isFloat(typeOf(s.e))) { genDouble(s.e, 0, 0); return; }
        genExpr(s.e, 0);
        return;

      case StmtKind::Return: {
        if (s.e && isFloat(fn_->ret)) {
          genDouble(s.e, 0, 0);
          moveConst(emitter(), DRET, slotAt(0));
          op("JMP " + fn_->name + "__end");
          return;
        }
        if (s.e) {
          const CType t = genExpr(s.e, 0);
          convert(0, t, fn_->ret);
          op("LD A <- " + hi(0));
          op("LD [" + S(ZP_RET) + "] <- A");
          op("LD A <- " + lo(0));
          op("LD [" + S(ZP_RET) + "+1] <- A");
        }
        op("JMP " + fn_->name + "__end");
        return;
      }

      case StmtKind::If: {
        const std::string els = uniq("else");
        const std::string end = uniq("endif");
        genTest(s.c, els);
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
        const std::string top = uniq("while");
        const std::string end = uniq("wend");
        lab(top);
        genTest(s.c, end);
        breaks_.push_back(end);
        continues_.push_back(top);
        genStmt(s.loopBody);
        breaks_.pop_back();
        continues_.pop_back();
        op("JMP " + top);
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
        genTest(s.c, end);
        op("JMP " + top);
        lab(end);
        return;
      }

      case StmtKind::For: {
        const std::string top = uniq("for");
        const std::string cont = uniq("forstep");
        const std::string end = uniq("forend");
        scopes_.emplace_back();
        const int save = frameCursor_;
        if (s.init) genStmt(s.init);
        lab(top);
        if (s.c) genTest(s.c, end);
        breaks_.push_back(end);
        continues_.push_back(cont);
        genStmt(s.loopBody);
        breaks_.pop_back();
        continues_.pop_back();
        lab(cont);
        if (s.step) genExpr(s.step, 0);
        op("JMP " + top);
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
          loadConst(1, *c.value, t);
          compareInto(2, "==", 0, 1, t);
          op("LD A <- " + lo(2));
          // uniqSkip emits the branch itself. Wrapping it in another JZ once
          // made the skip label jump to itself, which is a very tight loop.
          jumpIfTrue(bodies[i]);
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
        e("        " + s.text);
        return;
    }
  }

  // The machine has JZ and no jump-if-not-zero, so "go there when true" is
  // the inverse branch over a jump. A is already loaded and its flags set.
  void jumpIfTrue(const std::string& target) {
    const std::string skip = uniq("nz");
    op("JZ " + skip);
    op("JMP " + target);
    lab(skip);
  }

  // Evaluate a condition and jump to `falseLabel` when it is zero.
  void genTest(const ExprPtr& e, const std::string& falseLabel) {
    const CType t = genExpr(e, 0);
    truthy(0, t);
    op("JZ " + falseLabel);
  }

  // Leaves Z set when the value in `slot` is zero.
  void truthy(int slot, const CType& t) {
    if (sizeOf(t) == 1 && t.ptr == 0) {
      op("LD A <- " + lo(slot));
    } else {
      op("LD A <- " + lo(slot));
      op("OR A <- " + hi(slot));
    }
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

  void storeFrame(int offset, int size, int slot) {
    if (size == 1) {
      op("LD A <- " + lo(slot));
      op("LD [D1+" + S(offset) + "] <- A");
      return;
    }
    op("LD A <- " + hi(slot));
    op("LD [D1+" + S(offset) + "] <- A");
    op("LD A <- " + lo(slot));
    op("LD [D1+" + S(offset + 1) + "] <- A");
  }

  void loadFrame(int offset, int size, int slot) {
    use(slot);
    if (size == 1) {
      op("LD A <- [D1+" + S(offset) + "]");
      op("LD " + lo(slot) + " <- A");
      return;
    }
    op("LD A <- [D1+" + S(offset) + "]");
    op("LD " + hi(slot) + " <- A");
    op("LD A <- [D1+" + S(offset + 1) + "]");
    op("LD " + lo(slot) + " <- A");
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
        genAddress(ep, slot);
        loadThrough(slot, sizeOf(t));
        return t;
      }
      case ExprKind::Call: return genCall(ep, slot);

      case ExprKind::Cond: {
        const std::string f = uniq("cf");
        const std::string end = uniq("ce");
        const CType t = typeOf(ep);
        genTest(e.c, f);
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
    // A frame address is D1 plus the offset, and D1 can be stored as a word.
    op("LD " + word(slot) + " <- D1");
    if (s.offset == 0) return;
    op("LD A <- " + lo(slot));
    op("ADD A <- " + S(s.offset));
    op("LD " + lo(slot) + " <- A");
    op("LD A <- " + hi(slot));
    op("ADC A <- 0");
    op("LD " + hi(slot) + " <- A");
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
        // base + index * elementSize, as 16 bit arithmetic.
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

  CType genAssign(const ExprPtr& ep, int slot) {
    const Expr& e = *ep;
    const CType target = typeOf(e.l);
    if (e.op != "=") {
      const std::string op2 = e.op.substr(0, e.op.size() - 1);
      return genAssign(mkAssign(e.l, mkBin(op2, e.l, e.r, e.pos), e.pos), slot);
    }
    // Simple names take the short path: no address to compute.
    if (e.l->k == ExprKind::Id) {
      const Sym& s = lookup(e.l->name, e.l->pos);
      if (s.type.arrayLen) fail(e.pos, s.name + " is an array and cannot be assigned");
      const CType vt = genExpr(e.r, slot);
      convert(slot, vt, target);
      if (s.where == Sym::Where::Frame) storeFrame(s.offset, sizeOf(s.type), slot);
      else storeGlobal(s, slot);
      return target;
    }
    const CType vt = genExpr(e.r, slot);
    convert(slot, vt, target);
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

    if (e.op == "!") {
      const CType t = genExpr(e.e, slot);
      const std::string zero = uniq("not1");
      const std::string done = uniq("notd");
      truthy(slot, t);
      op("JZ " + zero);
      op("LD A <- 0");
      op("JMP " + done);
      lab(zero);
      op("LD A <- 1");
      lab(done);
      op("LD " + lo(slot) + " <- A");
      op("LD A <- 0");
      op("LD " + hi(slot) + " <- A");
      return T(BaseType::Int);
    }

    if (e.op == "&") return genAddress(e.e, slot);

    if (e.op == "*") {
      const CType pt = genExpr(e.e, slot);
      if (pt.ptr == 0) fail(e.pos, "cannot take * of " + typeName(pt));
      const CType t{pt.base, pt.ptr - 1, std::nullopt};
      loadThrough(slot, sizeOf(t));
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

    if (bop == "&&" || bop == "||") {
      const std::string shortcut = uniq(bop == "&&" ? "andfalse" : "ortrue");
      const std::string done = uniq("logdone");
      const CType lt = genExpr(e.l, slot);
      truthy(slot, lt);
      if (bop == "&&") {
        op("JZ " + shortcut);
      } else {
        const std::string cont = uniq("orcont");
        op("JZ " + cont);
        op("JMP " + shortcut);
        lab(cont);
      }
      const CType rt = genExpr(e.r, slot);
      truthy(slot, rt);
      const std::string rzero = uniq("rz");
      op("JZ " + rzero);
      op("LD A <- 1");
      op("JMP " + done);
      lab(rzero);
      op("LD A <- 0");
      op("JMP " + done);
      lab(shortcut);
      op(std::string("LD A <- ") + (bop == "&&" ? "0" : "1"));
      lab(done);
      op("LD " + lo(slot) + " <- A");
      op("LD A <- 0");
      op("LD " + hi(slot) + " <- A");
      return T(BaseType::Int);
    }

    const CType lt0 = typeOf(e.l);
    const CType rt0 = typeOf(e.r);

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

    const CType lt = genExpr(e.l, slot);
    convert(slot, lt, rtype);
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

  // The machine has no CMP, so a comparison is a subtract and a flag idiom.
  // Unsigned order is C, the borrow. Signed order is N xor V.
  void compareInto(int dst, const std::string& cop, int a, int b, const CType& t) {
    use(dst);
    const int width = sizeOf(t);
    const bool sgn = isSigned(t) && t.ptr == 0;
    const std::string yes = uniq("cmpy");
    const std::string done = uniq("cmpd");

    // Equality is its own shape: the difference of both bytes, ORed.
    if (cop == "==" || cop == "!=") {
      op("LD A <- " + lo(a));
      op("SUB A <- " + lo(b));
      if (width == 2) {
        op("LD [" + S(ZP_CMP) + "] <- A");
        op("LD A <- " + hi(a));
        op("SUB A <- " + hi(b));
        op("OR A <- [" + S(ZP_CMP) + "]");
      }
      op("JZ " + yes);
      op(std::string("LD A <- ") + (cop == "==" ? "0" : "1"));
      op("JMP " + done);
      lab(yes);
      op(std::string("LD A <- ") + (cop == "==" ? "1" : "0"));
      lab(done);
      op("LD " + lo(dst) + " <- A");
      op("LD A <- 0");
      op("LD " + hi(dst) + " <- A");
      return;
    }

    // The other four are all "a < b" with the sides swapped or the answer
    // inverted, so one subtract shape serves them.
    // a <  b : less(a, b)
    // a >  b : less(b, a)
    // a >= b : not less(a, b)
    // a <= b : not less(b, a)
    // So the sides swap for > and <=, and the answer inverts for >= and <=.
    const bool swap = cop == ">" || cop == "<=";
    const bool invert = cop == ">=" || cop == "<=";
    const int x = swap ? b : a;
    const int y = swap ? a : b;

    op("LD A <- " + lo(x));
    op("SUB A <- " + lo(y));
    if (width == 2) {
      op("LD A <- " + hi(x));
      op("SBC A <- " + hi(y));
    }

    const std::string no = uniq("cmpn");
    if (sgn) {
      // N xor V, and nothing between the subtract and the branches that
      // would disturb a flag. A load would set Z and N, so there is none.
      const std::string vset = uniq("cmpv");
      op("JV " + vset);
      op("JN " + yes);
      op("JMP " + no);
      lab(vset);
      op("JN " + no);
      op("JMP " + yes);
    } else {
      // C is the borrow, so it is set exactly when x is below y.
      op("JC " + yes);
    }
    lab(no);
    op(std::string("LD A <- ") + (invert ? "1" : "0"));
    op("JMP " + done);
    lab(yes);
    op(std::string("LD A <- ") + (invert ? "0" : "1"));
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
    if (bytes > 0) addToSp(-bytes);
    if (bytes > 0) {
      op("LD D2 <- [" + S(ZP_SP) + "]");
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
    if (sizeOf(f.ret) > 0) {
      op("LD A <- [" + S(ZP_RET) + "]");
      op("LD " + hi(slot) + " <- A");
      op("LD A <- [" + S(ZP_RET) + "+1]");
      op("LD " + lo(slot) + " <- A");
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
    std::string label;

    if (fmt->k == ExprKind::Str) {
      // A string literal handed to gpu_printf goes to the cartridge on its
      // own, which is what makes the common call read like C.
      label = romLiteral(fmt->bytes);
      widths = templateArgWidths(fmt->bytes);
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
    // otherwise. The two agree in every program that is right.
    std::vector<int> sizes;
    if (widths) sizes = *widths;
    else for (size_t i = 1; i < e.args.size(); i++) sizes.push_back(std::min(sizeOf(typeOf(e.args[i])), 2));
    int at = 0;
    for (size_t i = 1; i < e.args.size(); i++) {
      const int w = i - 1 < sizes.size() ? sizes[i - 1] : 2;
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

      case ExprKind::Id: {
        const CType t = typeOf(ep);
        if (!isFloat(t)) { widenToDouble(ep, d, slot); return; }
        moveDyn(emitter(), Where::at(slotAt(d)), dAddr(ep, slot));
        return;
      }

      case ExprKind::Cast:
        if (isFloat(e.type)) { genDouble(e.e, d, slot); return; }
        break;

      case ExprKind::Un:
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
        genTest(e.c, f);
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

std::vector<RomEntry> layoutRom(const std::vector<VarDecl>& vars) {
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
    const std::vector<uint8_t> bytes = romBytesOf(v);
    const std::string key = bytesKey(bytes);
    auto first = seen.find(key);
    RomEntry entry{v.name, first != seen.end() ? at[first->second] : addr, static_cast<int>(bytes.size()),
                   v.pos.file, v.pos.line, std::nullopt};
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

std::vector<uint8_t> romBytesOf(const VarDecl& v) {
  const int elem = sizeOf(CType{v.type.base, v.type.ptr, std::nullopt});
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
  return Gen(unit, false, nullptr).compile();
}

Compiled compileUnitTree(const Unit& unit, bool softMul, const Profile* profile) {
  return Gen(unit, softMul, profile).compile();
}

}  // namespace sc8::cc
