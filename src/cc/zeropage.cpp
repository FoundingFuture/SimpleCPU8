#include "cc/zeropage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "cc/layout.h"
#include "cc/lex.h"
#include "cc/parse.h"

namespace sc8::cc {

namespace {

// A mention inside a loop counts for more, for each loop it is inside. A
// variable touched once in a loop beats one touched five times in setup, and
// being in a loop at all is the answer that matters.
constexpr double LOOP_WEIGHT = 10;

// One long loop must not swamp the program, so a literal bound is read and
// then capped. 30000 iterations and 200 rank the same.
constexpr double BOUND_CAP = 100;

// The weight of one mention at this loop depth, given the bounds of the
// loops it sits inside.
double weightFor(const std::vector<double>& bounds) {
  double n = 1;
  for (double b : bounds) n *= b;
  return n;
}

// A for loop's trip count when the compiler can read it, and the flat loop
// weight when it cannot.
double tripCount(const Stmt& s) {
  const ExprPtr& c = s.c;
  if (!c || c->k != ExprKind::Bin) return LOOP_WEIGHT;
  if (c->op != "<" && c->op != "<=" && c->op != ">" && c->op != ">=" && c->op != "!=") return LOOP_WEIGHT;
  const auto to = foldConst(c->r);
  if (!to) return LOOP_WEIGHT;
  std::optional<double> from = 0;
  if (s.init && s.init->k == StmtKind::Var) {
    const auto& init = s.init->decl.init;
    if (!init) from = 0;
    else if (init->isList) from = std::nullopt;
    else from = foldConst(init->one);
  } else if (s.init && s.init->k == StmtKind::Expr && s.init->e->k == ExprKind::Assign) {
    from = foldConst(s.init->e->r);
  }
  const double span = std::fabs(*to - from.value_or(0));
  if (!std::isfinite(span) || span <= 0) return LOOP_WEIGHT;
  return std::min(std::max(span, LOOP_WEIGHT), BOUND_CAP);
}

struct Scorer {
  std::map<std::string, double> score;

  void bump(const std::string& name, double by) { score[name] += by; }

  void inExpr(const ExprPtr& e, const std::vector<double>& bounds) {
    if (!e) return;
    const double w = weightFor(bounds);
    switch (e->k) {
      case ExprKind::Id: bump(e->name, w); return;
      case ExprKind::Call:
        for (const ExprPtr& a : e->args) inExpr(a, bounds);
        inExpr(e->fn, bounds);
        return;
      case ExprKind::Bin: case ExprKind::Assign: case ExprKind::Comma:
        inExpr(e->l, bounds); inExpr(e->r, bounds); return;
      case ExprKind::Un: case ExprKind::Post: case ExprKind::Cast: inExpr(e->e, bounds); return;
      case ExprKind::Index: inExpr(e->a, bounds); inExpr(e->i, bounds); return;
      case ExprKind::Cond: inExpr(e->c, bounds); inExpr(e->t, bounds); inExpr(e->f, bounds); return;
      case ExprKind::Sizeof: inExpr(e->e, bounds); return;
      default: return;
    }
  }

  void inInit(const std::optional<Initializer>& init, const std::vector<double>& bounds) {
    if (!init) return;
    if (init->isList) for (const ExprPtr& x : init->list) inExpr(x, bounds);
    else inExpr(init->one, bounds);
  }

  void inStmt(const StmtPtr& s, const std::vector<double>& bounds) {
    if (!s) return;
    switch (s->k) {
      case StmtKind::Block: for (const StmtPtr& x : s->body) inStmt(x, bounds); return;
      case StmtKind::Expr: inExpr(s->e, bounds); return;
      case StmtKind::Return: inExpr(s->e, bounds); return;
      case StmtKind::Var: inInit(s->decl.init, bounds); return;
      case StmtKind::If:
        inExpr(s->c, bounds); inStmt(s->t, bounds); inStmt(s->f, bounds); return;
      case StmtKind::While: case StmtKind::Do: {
        std::vector<double> inner = bounds;
        inner.push_back(LOOP_WEIGHT);
        inExpr(s->c, inner);
        inStmt(s->loopBody, inner);
        return;
      }
      case StmtKind::For: {
        inStmt(s->init, bounds);
        std::vector<double> inner = bounds;
        inner.push_back(tripCount(*s));
        inExpr(s->c, inner);
        inExpr(s->step, inner);
        inStmt(s->loopBody, inner);
        return;
      }
      case StmtKind::Switch:
        inExpr(s->e, bounds);
        for (const SwitchCase& c : s->cases) for (const StmtPtr& x : c.body) inStmt(x, bounds);
        return;
      default: return;
    }
  }
};

std::string fixed1(double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.1f", v);
  return buf;
}

}  // namespace

std::map<std::string, double> scoreNames(const Unit& unit) {
  Scorer sc;
  for (const FuncDecl& f : unit.funcs) sc.inStmt(f.body, {});
  return sc.score;
}

std::vector<Placed> allocate(const Unit& unit, int reserved, const Profile* measured) {
  const std::map<std::string, double> score = scoreNames(unit);
  std::vector<const VarDecl*> globals;
  for (const VarDecl& v : unit.vars) if (!v.rom) globals.push_back(&v);

  std::vector<const VarDecl*> pinned, rest;
  for (const VarDecl* v : globals) (v->zp ? pinned : rest).push_back(v);

  int want = 0;
  for (const VarDecl* v : pinned) want += sizeOf(v->type);
  const int free = ZERO_PAGE_SIZE - reserved;
  if (want > free) {
    const VarDecl& first = *pinned[0];
    throw CcError(first.pos.file, first.pos.line,
                  "the __zp variables need " + std::to_string(want) + " bytes and " + std::to_string(free) +
                      " are free. The compiler's own reservations took " + std::to_string(reserved) +
                      ": the stack pointer, the return value, and one temp slot per level of the deepest "
                      "expression in the program. Drop a __zp, or make one smaller.");
  }

  // Everything else by weighted count, and declaration order breaks a tie so
  // the same program always lays out the same way.
  // DESIGN: the ranking is per BYTE, not per variable.
  //
  // The zero page is 256 bytes and a variable takes as many of them as its
  // type says. An 80 byte line buffer touched 33 times and a one byte
  // counter touched 200 times are not comparable by their totals: the buffer
  // asks for eighty times the room for a sixth of the work. So what is
  // compared is the work per byte it costs, which is what makes the page go
  // to the variables that get the most out of it.
  std::map<const VarDecl*, size_t> order;
  for (size_t i = 0; i < globals.size(); i++) order[globals[i]] = i;
  auto scoreOf = [&](const VarDecl& v) {
    auto it = score.find(v.name);
    return it == score.end() ? 0.0 : it->second;
  };
  auto measuredOf = [&](const VarDecl& v) -> std::optional<double> {
    if (!measured) return std::nullopt;
    auto it = measured->find(v.name);
    if (it == measured->end()) return std::nullopt;
    return it->second;
  };
  auto total = [&](const VarDecl& v) { return measuredOf(v).value_or(scoreOf(v)); };
  auto rank = [&](const VarDecl& v) { return total(v) / std::max(sizeOf(v.type), 1); };
  std::vector<const VarDecl*> sorted = rest;
  std::stable_sort(sorted.begin(), sorted.end(), [&](const VarDecl* a, const VarDecl* b) {
    const double d = rank(*b) - rank(*a);
    return d != 0 ? d < 0 : order[a] < order[b];
  });

  std::vector<Placed> out;
  for (const VarDecl* v : pinned) out.push_back({*v, scoreOf(*v), "asked for with __zp"});

  // Greedy, and a variable that does not FIT in what is left is passed over
  // rather than pushing everything after it out. A 400 byte array would
  // otherwise take the whole page and give it to nobody.
  int at = reserved + want;
  std::vector<Placed> late;
  for (const VarDecl* v : sorted) {
    const int size = sizeOf(v->type);
    const double s = scoreOf(*v);
    const auto m = measuredOf(*v);
    const std::string per = size > 1 ? ", " + fixed1(total(*v) / size) + " a byte" : "";
    const std::string why = m ? "measured " + numText(*m) + " accesses" + per
                           : s > 0 ? "scored " + numText(s) + per
                                   : "it fitted";
    if (at + size <= ZERO_PAGE_SIZE) {
      out.push_back({*v, m.value_or(s), why});
      at += size;
    } else {
      late.push_back({*v, m.value_or(s), "no room in the zero page"});
    }
  }
  out.insert(out.end(), late.begin(), late.end());
  return out;
}

}  // namespace sc8::cc
