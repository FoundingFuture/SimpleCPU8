#include "cc/cc.h"

#include <cctype>
#include <cstdio>
#include <functional>
#include <set>
#include <sstream>

#include "cc/headers.h"
#include "cc/libs.h"
#include "cc/lex.h"
#include "cc/parse.h"

namespace sc8::cc {

namespace {

constexpr const char* LIBC_FILE = "__libc.c";

std::string hex(int v, int width) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%0*X", width, v);
  return buf;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
  for (const std::string& x : v) if (x == s) return true;
  return false;
}

// Everything a statement or an expression names, so reachability can follow.
void namesIn(const ExprPtr& e, std::set<std::string>& out);

void namesInInit(const std::optional<Initializer>& init, std::set<std::string>& out) {
  if (!init) return;
  if (init->isList) for (const ExprPtr& x : init->list) namesIn(x, out);
  else namesIn(init->one, out);
}

void namesIn(const StmtPtr& s, std::set<std::string>& out) {
  if (!s) return;
  switch (s->k) {
    case StmtKind::Block: for (const StmtPtr& x : s->body) namesIn(x, out); break;
    case StmtKind::Expr: namesIn(s->e, out); break;
    case StmtKind::Return: namesIn(s->e, out); break;
    case StmtKind::If: namesIn(s->c, out); namesIn(s->t, out); namesIn(s->f, out); break;
    case StmtKind::While: case StmtKind::Do: namesIn(s->c, out); namesIn(s->loopBody, out); break;
    case StmtKind::For:
      namesIn(s->init, out);
      namesIn(s->c, out);
      namesIn(s->step, out);
      namesIn(s->loopBody, out);
      break;
    case StmtKind::Switch:
      namesIn(s->e, out);
      for (const SwitchCase& c : s->cases) for (const StmtPtr& x : c.body) namesIn(x, out);
      break;
    case StmtKind::Var: namesInInit(s->decl.init, out); break;
    default: break;
  }
}

void namesIn(const ExprPtr& e, std::set<std::string>& out) {
  if (!e) return;
  switch (e->k) {
    case ExprKind::Id: out.insert(e->name); break;
    case ExprKind::Call:
      namesIn(e->fn, out);
      for (const ExprPtr& a : e->args) namesIn(a, out);
      break;
    case ExprKind::Bin: case ExprKind::Assign: case ExprKind::Comma:
      namesIn(e->l, out); namesIn(e->r, out); break;
    case ExprKind::Un: case ExprKind::Post: case ExprKind::Cast: namesIn(e->e, out); break;
    case ExprKind::Index: namesIn(e->a, out); namesIn(e->i, out); break;
    case ExprKind::Cond: namesIn(e->c, out); namesIn(e->t, out); namesIn(e->f, out); break;
    case ExprKind::Sizeof: namesIn(e->e, out); break;
    default: break;
  }
}

// Every word in every inline asm statement in a body. The compiler does not
// read that text, so anything spelled in it is kept.
void asmWords(const StmtPtr& s, std::vector<std::string>& out) {
  if (!s) return;
  if (s->k == StmtKind::Asm) {
    const std::string& t = s->text;
    size_t i = 0;
    while (i < t.size()) {
      const char c = t[i];
      if (c == '_' || std::isalpha(static_cast<unsigned char>(c))) {
        size_t n = 1;
        while (i + n < t.size() && (t[i + n] == '_' || std::isalnum(static_cast<unsigned char>(t[i + n])))) n++;
        out.push_back(t.substr(i, n));
        i += n;
      } else {
        i++;
      }
    }
    return;
  }
  for (const StmtPtr& x : s->body) asmWords(x, out);
  asmWords(s->t, out);
  asmWords(s->f, out);
  asmWords(s->loopBody, out);
  asmWords(s->init, out);
  for (const SwitchCase& c : s->cases) for (const StmtPtr& x : c.body) asmWords(x, out);
}

// A private name, made unique by the file it came from. A static function is
// private by C's own rule, so it needs no second mechanism.
std::string privateName(const std::string& file, const std::string& name) {
  std::string f = file;
  for (char& c : f) if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
  return f + "__" + name;
}

// Rewrite the private names inside one function body.
void rewrite(const ExprPtr& e, const std::map<std::string, std::string>& map);

void rewriteInit(std::optional<Initializer>& init, const std::map<std::string, std::string>& map) {
  if (!init) return;
  if (init->isList) for (const ExprPtr& x : init->list) rewrite(x, map);
  else rewrite(init->one, map);
}

// A private name inside inline assembly, word by word. The compiler does not
// read that text, but the label it names has moved, so the text follows.
void rewriteAsm(std::string& text, const std::map<std::string, std::string>& map) {
  std::string out;
  size_t i = 0;
  while (i < text.size()) {
    const char c = text[i];
    if (c == '_' || std::isalpha(static_cast<unsigned char>(c))) {
      size_t n = 1;
      while (i + n < text.size() && (text[i + n] == '_' || std::isalnum(static_cast<unsigned char>(text[i + n])))) n++;
      const std::string w = text.substr(i, n);
      auto hit = map.find(w);
      out += hit != map.end() ? hit->second : w;
      i += n;
    } else {
      out += c;
      i++;
    }
  }
  text = out;
}

void rewrite(const StmtPtr& s, const std::map<std::string, std::string>& map) {
  if (!s) return;
  if (s->k == StmtKind::Asm) rewriteAsm(s->text, map);
  rewrite(s->e, map);
  rewrite(s->c, map);
  rewrite(s->step, map);
  rewrite(s->t, map);
  rewrite(s->f, map);
  rewrite(s->loopBody, map);
  rewrite(s->init, map);
  for (const StmtPtr& x : s->body) rewrite(x, map);
  for (SwitchCase& c : s->cases) for (const StmtPtr& x : c.body) rewrite(x, map);
  if (s->k == StmtKind::Var) rewriteInit(s->decl.init, map);
}

void rewrite(const ExprPtr& e, const std::map<std::string, std::string>& map) {
  if (!e) return;
  if (e->k == ExprKind::Id) {
    auto hit = map.find(e->name);
    if (hit != map.end()) e->name = hit->second;
  }
  rewrite(e->l, map);
  rewrite(e->r, map);
  rewrite(e->e, map);
  rewrite(e->c, map);
  rewrite(e->t, map);
  rewrite(e->f, map);
  rewrite(e->a, map);
  rewrite(e->i, map);
  rewrite(e->fn, map);
  for (const ExprPtr& x : e->args) rewrite(x, map);
}

using Resolve = std::function<std::optional<std::string>(const std::string&, bool)>;

// A map that keeps insertion order, the way a JavaScript Map does. The order
// functions are merged in is the order they are generated in.
struct FuncTable {
  std::vector<std::string> order;
  std::map<std::string, FuncDecl> byName;

  FuncDecl* get(const std::string& name) {
    auto it = byName.find(name);
    return it == byName.end() ? nullptr : &it->second;
  }
  void set(const std::string& name, FuncDecl f) {
    if (!byName.count(name)) order.push_back(name);
    byName[name] = std::move(f);
  }
};

struct Merged {
  FuncTable funcs;
  std::vector<VarDecl> vars;
  std::vector<std::string> included;
  std::vector<std::string> builds;
};

// Parse every file, make each static name private to its file, and refuse a
// public name two files both define. Run twice: once to find the cartridge
// layout, once for real with ROM.h in scope.
Merged merge(const std::vector<SourceFile>& sources, const Resolve& resolve, const CcOptions& opts) {
  std::vector<Unit> units;
  Merged m;
  for (const SourceFile& f : sources) {
    PpOptions po;
    po.resolve = resolve;
    po.defines = opts.defines;
    Unit u = parse(f.text, f.name, &po);
    for (const std::string& i : u.included) if (!contains(m.included, i)) m.included.push_back(i);
    if (f.name != LIBC_FILE) m.builds.insert(m.builds.end(), u.builds.begin(), u.builds.end());
    units.push_back(std::move(u));
  }
  // A friendly library's unit joins the build when its header was
  // included, by a source or by another library. What nothing reaches is
  // dropped below like any other function.
  for (size_t k = 0; k < m.included.size(); k++) {
    for (const Library& l : libraries()) {
      if (l.header != m.included[k]) continue;
      bool have = false;
      for (const Unit& u : units) have = have || u.file == l.unit;
      if (have) continue;
      PpOptions po;
      po.resolve = resolve;
      po.defines = opts.defines;
      Unit u = parse(l.source, l.unit, &po);
      for (const std::string& i : u.included) if (!contains(m.included, i)) m.included.push_back(i);
      units.push_back(std::move(u));
    }
  }

  std::map<std::string, VarDecl> seenVar;
  std::vector<std::map<std::string, std::string>> renamed(units.size());

  for (size_t ui = 0; ui < units.size(); ui++) {
    const Unit& u = units[ui];
    std::map<std::string, std::string>& map = renamed[ui];
    for (const FuncDecl& f : u.funcs) if (f.isStatic) map[f.name] = privateName(u.file, f.name);
    for (const VarDecl& v : u.vars) {
      if (v.storage == Storage::Static) map[v.name] = privateName(u.file, v.name);
    }
  }
  auto rename = [&](size_t ui, const std::string& name) {
    auto hit = renamed[ui].find(name);
    return hit == renamed[ui].end() ? name : hit->second;
  };

  for (size_t ui = 0; ui < units.size(); ui++) {
    const Unit& u = units[ui];
    for (const FuncDecl& f : u.funcs) {
      if (f.body && f.name == "main" && opts.guestFiles.count(u.file)) {
        throw CcError(f.pos.file, f.pos.line,
                      "main belongs to the interpreter in a project with BASIC. Give this function another "
                      "name and call it from BASIC with CALL.");
      }
      const std::string name = rename(ui, f.name);
      const FuncDecl* prev = m.funcs.get(name);
      if (prev && prev->body && f.body) {
        throw CcError(f.pos.file, f.pos.line,
                      f.name + " is defined in " + prev->pos.file +
                          " as well. Mark one of them static to keep it private to its file.");
      }
      if (prev && !f.body) {
        m.funcs.set(name, *prev);
      } else {
        FuncDecl copy = f;
        copy.name = name;
        m.funcs.set(name, copy);
      }
    }
    for (const VarDecl& v : u.vars) {
      if (v.storage == Storage::Extern && !v.init) continue;
      const std::string name = rename(ui, v.name);
      auto prev = seenVar.find(name);
      if (prev != seenVar.end()) {
        throw CcError(v.pos.file, v.pos.line,
                      v.name + " is declared in " + prev->second.pos.file +
                          " as well. Mark one of them static, or declare it extern here.");
      }
      VarDecl copy = v;
      copy.name = name;
      seenVar[name] = copy;
      m.vars.push_back(copy);
    }
  }

  // Renaming reaches into the bodies too, so a static function's callers in
  // its own file follow it.
  for (size_t ui = 0; ui < units.size(); ui++) {
    const std::map<std::string, std::string>& map = renamed[ui];
    if (map.empty()) continue;
    for (const FuncDecl& f : units[ui].funcs) rewrite(f.body, map);
  }

  return m;
}

}  // namespace

// DESIGN: this is why one build is two phases. A ROM object's address depends
// on sizes and order, and both are known from the declarations alone. So the
// layout is worked out first, ROM.h is written, and only then is the code
// generated with those constants in scope. A program can therefore include
// ROM.h and use ROM_ship_HI for an object declared three lines above it.
std::string romHeaderText(const std::vector<RomEntry>& rom) {
  std::vector<std::string> out = {
      "/* ROM.h. The cartridge as built. Regenerated every compile. Do not edit. */",
      "",
      // NOT __ROM_H__: rom.h, which holds the three address macros, guards
      // itself with that and the two names differ only in case. One would then
      // silently swallow the other on a case insensitive reading.
      "#ifndef __CARTRIDGE_MAP_H__",
      "#define __CARTRIDGE_MAP_H__",
      "",
  };
  if (rom.empty()) out.push_back("/* This program declares nothing __ROM, so the cartridge is empty. */");
  for (const RomEntry& r : rom) {
    const std::string same = r.sameAs ? ", the same bytes as " + *r.sameAs : "";
    out.push_back("/* $" + hex(r.addr, 6) + "  " + r.name + ", declared in " + r.file + " line " +
                  std::to_string(r.line) + same + " */");
    out.push_back("#define ROM_" + r.name + "_SIZE " + std::to_string(r.size));
    out.push_back("#define ROM_" + r.name + "_BANK 0x" + hex((r.addr >> 16) & 0xff, 2));
    out.push_back("#define ROM_" + r.name + "_HI   0x" + hex((r.addr >> 8) & 0xff, 2));
    out.push_back("#define ROM_" + r.name + "_LO   0x" + hex(r.addr & 0xff, 2));
    out.push_back("#define ROM_" + r.name + "      " + std::to_string(r.addr));
    out.push_back("");
  }
  out.push_back("#endif");
  out.push_back("");
  std::string text;
  for (size_t i = 0; i < out.size(); i++) text += (i ? "\n" : "") + out[i];
  return text;
}

Program compileProgram(const std::vector<SourceFile>& files, const CcOptions& opts) {
  if (files.empty()) throw CcError("", 1, "there is nothing to compile");
  std::vector<SourceFile> sources = files;
  if (!opts.noLibc) sources.push_back({LIBC_FILE, LIBC_SOURCE});

  std::map<std::string, std::string> byName = opts.extra;
  for (const SourceFile& f : files) byName[f.name] = f.text;

  // DESIGN: one build, two phases. A ROM object's address depends on sizes
  // and order, and both come from the declarations, so the layout is worked
  // out with no code generated at all. ROM.h is written from it and the code
  // is then generated with those numbers in scope. That is what lets a
  // program include ROM.h and use ROM_ship_HI for an object declared three
  // lines above the include.
  std::string romHeader = romHeaderText({});
  const Resolve resolve = [&](const std::string& name, bool angled) -> std::optional<std::string> {
    const auto& H = headers();
    if (angled) {
      auto h = H.find(name);
      if (h == H.end()) return std::nullopt;
      return h->second;
    }
    if (name == "ROM.h") return romHeader;
    auto b = byName.find(name);
    if (b != byName.end()) return b->second;
    auto h = H.find(name);
    if (h == H.end()) return std::nullopt;
    return h->second;
  };
  romHeader = romHeaderText(layoutRom(merge(sources, resolve, opts).vars, opts.assets));

  // The build line decides which files are compiled and in what order, and
  // carries the machine options. Exactly one file may hold it.
  std::vector<std::pair<std::string, std::vector<std::string>>> carried;
  std::vector<BuildSource> buildSources;
  for (const SourceFile& f : files) {
    const std::vector<std::string> lines = buildLines(f.text);
    if (!lines.empty()) carried.emplace_back(f.name, lines);
    buildSources.push_back({f.name, f.text});
  }
  const BuildPlan plan = planBuild(buildSources, carried);
  std::vector<SourceFile> chosen;
  if (!plan.files.empty()) {
    for (const std::string& n : plan.files) {
      for (const SourceFile& f : files) if (f.name == n) { chosen.push_back(f); break; }
    }
  } else {
    chosen = files;
  }
  std::vector<SourceFile> compileSet = chosen;
  if (!opts.noLibc) compileSet.push_back({LIBC_FILE, LIBC_SOURCE});

  // The cartridge map is in scope without an include: every ROM_ name in
  // ROM.h is a predefined macro on this pass. The include still works and
  // is what a reader opens to see the addresses.
  CcOptions withRom = opts;
  for (size_t at = 0; at < romHeader.size();) {
    size_t nl = romHeader.find('\n', at);
    if (nl == std::string::npos) nl = romHeader.size();
    const std::string line = romHeader.substr(at, nl - at);
    at = nl + 1;
    if (line.rfind("#define ROM_", 0) != 0) continue;
    const size_t sp = line.find(' ', 8);
    if (sp == std::string::npos) continue;
    const std::string name = line.substr(8, sp - 8);
    std::string value = line.substr(sp);
    value.erase(0, value.find_first_not_of(' '));
    if (!withRom.defines.count(name)) withRom.defines[name] = value;
  }
  Merged m = merge(compileSet, resolve, withRom);

  // Reachability from main. Program memory is 64K slots and the whole budget,
  // so a program that never divides must not carry the divide.
  std::set<std::string> reach;
  std::vector<std::string> queue = {"main"};
  // A guest file's public functions are roots too: the host calls them by
  // slot, which the compiler cannot see. Its main would shadow the host's.
  for (const std::string& n : opts.externalCalls) {
    const FuncDecl* f = m.funcs.get(n);
    if (f && f->body && !f->isStatic) {
      queue.push_back(n);
      continue;
    }
    // A static function of that name has a private label nothing outside
    // its file can reach, so the call would fail to assemble.
    for (const std::string& other : m.funcs.order) {
      const FuncDecl* g = m.funcs.get(other);
      if (g->body && g->isStatic && other.size() > n.size() + 2 &&
          other.compare(other.size() - n.size() - 2, std::string::npos, "__" + n) == 0) {
        throw CcError(g->pos.file, g->pos.line,
                      n + " is static, so only its own file can call it. BASIC and assembly call it by name: "
                          "remove static.");
      }
    }
  }
  while (!queue.empty()) {
    const std::string n = queue.back();
    queue.pop_back();
    if (reach.count(n)) continue;
    reach.insert(n);
    const FuncDecl* f = m.funcs.get(n);
    if (!f || !f->body) continue;
    std::set<std::string> names;
    namesIn(f->body, names);
    // A function inline assembly names is reached too: JSR helper in an
    // asm statement is a call the compiler cannot see.
    std::vector<std::string> words;
    asmWords(f->body, words);
    names.insert(words.begin(), words.end());
    for (const std::string& x : names) if (m.funcs.get(x) && !reach.count(x)) queue.push_back(x);
  }
  std::vector<std::string> dropped;
  for (const std::string& n : m.funcs.order) {
    if (!reach.count(n) && m.funcs.get(n)->body) dropped.push_back(n);
  }

  // DESIGN: only the RUNTIME's own globals are dropped, never the program's.
  // The runtime's six byte coprocessor shadow was taking a zero page slot in
  // every program that never touches the coprocessor, and nobody asked for
  // it. A global the PROGRAM declared stays whether or not anything reads
  // it: it is in the RAM image, the watch list can pin it, and deleting
  // somebody's variable because they had not used it yet would be a strange
  // thing for a teaching tool to do.
  std::set<std::string> wanted;
  for (const std::string& n : reach) {
    const FuncDecl* f = m.funcs.get(n);
    if (!f || !f->body) continue;
    namesIn(f->body, wanted);
    // Inline asm is text the compiler does not read, so a name that appears
    // in it is kept. Being wrong the other way would delete something a
    // program uses and the error would come from the assembler.
    std::vector<std::string> words;
    asmWords(f->body, words);
    for (const std::string& w : words) wanted.insert(w);
  }
  // A global's own initializer can name another one.
  bool grew = true;
  while (grew) {
    grew = false;
    for (const VarDecl& v : m.vars) {
      if (!wanted.count(v.name)) continue;
      const size_t before = wanted.size();
      namesInInit(v.init, wanted);
      if (wanted.size() != before) grew = true;
    }
  }
  std::vector<VarDecl> liveVars;
  for (const VarDecl& v : m.vars) {
    if (v.pos.file != LIBC_FILE || wanted.count(v.name)) liveVars.push_back(v);
  }

  Unit merged;
  merged.file = files[0].name;
  for (const std::string& n : m.funcs.order) {
    const FuncDecl& f = *m.funcs.get(n);
    if (!f.body || reach.count(f.name)) merged.funcs.push_back(f);
  }
  merged.vars = liveVars;
  merged.builds = m.builds;
  merged.included = m.included;

  // #pragma heap_stack_size N in any source asks for N bytes, so a project
  // carries its own size into the ROM. The command line's wins.
  int heapStack = opts.heapStackSize;
  if (heapStack == 0) {
    for (const SourceFile& f : chosen) {
      std::istringstream in(f.text);
      std::string line;
      int lineNo = 0;
      while (std::getline(in, line)) {
        lineNo++;
        std::istringstream words(line);
        std::string hash, name;
        long value = 0;
        words >> hash;
        if (hash == "#pragma") words >> name;
        else if (hash == "#") words >> hash >> name;
        if (hash != "#pragma" && hash != "pragma") continue;
        if (name != "heap_stack_size") continue;
        if (!(words >> value) || value < 1 || value > 65535) {
          throw CcError(f.name, lineNo, "#pragma heap_stack_size takes a number of bytes, 1 to 65535");
        }
        heapStack = static_cast<int>(value);
      }
    }
  }
  const Compiled out = compileUnitTree(merged, plan.softMul || opts.defines.count("SOFT_MUL") > 0, opts.profile,
                                       opts.zpReserve, opts.assets, heapStack);
  Program p;
  static_cast<Compiled&>(p) = out;
  for (const SourceFile& f : chosen) p.files.push_back(f.name);
  p.included = m.included;
  p.dropped = dropped;
  p.builds = m.builds;
  p.romHeader = romHeader;
  p.plan = plan;
  return p;
}

Program compileSource(const std::string& src, const CcOptions& opts) {
  return compileProgram({{"main.c", src}}, opts);
}

}  // namespace sc8::cc

namespace sc8 {

CcResult compile(const std::vector<CcInput>& inputs, const cc::CcOptions& opts) {
  CcResult r;
  std::vector<cc::SourceFile> files;
  for (const CcInput& in : inputs) files.push_back({in.path, in.text});
  try {
    const cc::Program p = cc::compileProgram(files, opts);
    r.assembly = p.text;
    r.romHeader = p.romHeader;
  } catch (const cc::CcError& e) {
    r.errors.push_back(e.file + ":" + std::to_string(e.line) + ": " + e.message());
  }
  return r;
}

}  // namespace sc8
