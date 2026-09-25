// The AST for the C subset. See docs/design/c-compiler-design.md.
//
// The TypeScript uses tagged unions. Here one Expr struct and one Stmt
// struct carry every field a kind can have, tagged by an enum, and the
// children are shared pointers: the code generator builds synthetic nodes
// that reuse a subtree, and the driver renames names in place.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sc8::cc {

// Types. `base` is the scalar and `ptr` counts the stars. An array carries
// its length; a pointer to an array is not in the subset.
enum class BaseType { Void, Char, UChar, Int, UInt, Long, ULong, Double, RomT };

struct CType {
  BaseType base = BaseType::Int;
  int ptr = 0;
  // Arrays only in a declaration, never in an expression's type: an array
  // decays to a pointer the moment it is used, the way C does it.
  std::optional<int> arrayLen;
  bool operator==(const CType&) const = default;
};

inline CType T(BaseType base, int ptr = 0) { return CType{base, ptr, std::nullopt}; }

// The size of a type in bytes, and the one place that answer is written.
int sizeOf(const CType& t);

bool isSigned(const CType& t);
// The ACP does these, the CPU cannot touch them.
bool isWide(const CType& t);
bool isFloat(const CType& t);
std::string baseName(BaseType b);
std::string typeName(const CType& t);

struct Pos {
  std::string file;
  int line = 1;
};

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

enum class ExprKind { Num, Str, Id, Call, Un, Post, Bin, Assign, Index, Cond, Cast, Sizeof, Comma };

// Numbers are doubles, as in the TypeScript, so a literal like 3.5 keeps
// its value and the folder behaves like the JavaScript one.
struct Expr {
  ExprKind k = ExprKind::Num;
  Pos pos;
  double value = 0;              // Num
  CType type;                    // Num: the literal's type. Cast: the target
  std::optional<CType> ofType;   // Sizeof: sizeof(type)
  std::vector<uint8_t> bytes;    // Str
  std::string name;              // Id
  std::string op;                // Un, Post, Bin, Assign
  ExprPtr fn;                    // Call
  std::vector<ExprPtr> args;     // Call
  ExprPtr e;                     // Un, Post, Cast, Sizeof
  ExprPtr l, r;                  // Bin, Assign, Comma
  ExprPtr a, i;                  // Index
  ExprPtr c, t, f;               // Cond
};

struct Stmt;
using StmtPtr = std::shared_ptr<Stmt>;

enum class Storage { Auto, Static, Extern };

// An asset initializer: `__image("ship.png")` and its four siblings. The
// bytes come from a file beside the source when the cartridge is laid out,
// so the tree carries the name and the form, never the bytes.
enum class AssetForm { Image, Sprite, Palette, Sample, File };

struct AssetInit {
  AssetForm form = AssetForm::File;
  std::string name;
  // Sprite only: how many frames the strip holds, side by side. Without
  // the count, the PNG says, as the sprite editor saves it. A picture
  // that says nothing is one frame.
  int frames = 1;
  bool framesGiven = false;
};

std::string assetFormName(AssetForm f);

// An initializer is one expression, a brace list, an asset form, or absent.
// A string for a char array is the one expression case with a Str node. An
// asset form leaves `one` empty and `isList` false.
struct Initializer {
  bool isList = false;
  ExprPtr one;
  std::vector<ExprPtr> list;
  std::optional<AssetInit> asset;
};

struct VarDecl {
  std::string name;
  CType type;
  Storage storage = Storage::Auto;
  bool zp = false;
  bool rom = false;
  bool isConst = false;
  std::optional<Initializer> init;
  Pos pos;
};

enum class StmtKind { Block, Expr, Var, If, While, Do, For, Switch, Return, Break, Continue, Asm, Empty };

struct SwitchCase {
  // No value means `default`.
  std::optional<double> value;
  std::vector<StmtPtr> body;
  Pos pos;
};

struct Stmt {
  StmtKind k = StmtKind::Empty;
  Pos pos;
  std::vector<StmtPtr> body;     // Block
  ExprPtr e;                     // Expr, Return (optional), Switch
  VarDecl decl;                  // Var
  ExprPtr c;                     // If, While, Do, For (optional)
  StmtPtr t, f;                  // If
  StmtPtr loopBody;              // While, Do, For
  StmtPtr init;                  // For (optional)
  ExprPtr step;                  // For (optional)
  std::vector<SwitchCase> cases; // Switch
  std::string text;              // Asm
};

struct Param {
  std::string name;
  CType type;
};

struct FuncDecl {
  std::string name;
  CType ret;
  std::vector<Param> params;
  StmtPtr body;  // absent for a prototype
  bool isStatic = false;
  bool isInline = false;
  bool variadic = false;
  Pos pos;
};

struct Unit {
  std::string file;
  std::vector<FuncDecl> funcs;
  std::vector<VarDecl> vars;
  // Build lines found in `// build:` comments, in source order.
  std::vector<std::string> builds;
  // Every header and file the source pulled in, in the order first reached.
  std::vector<std::string> included;
};

}  // namespace sc8::cc
