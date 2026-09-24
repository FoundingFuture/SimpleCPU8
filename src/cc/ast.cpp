#include "cc/ast.h"

namespace sc8::cc {

int sizeOf(const CType& t) {
  if (t.arrayLen) return *t.arrayLen * sizeOf(CType{t.base, t.ptr, std::nullopt});
  if (t.ptr > 0) return 2;
  switch (t.base) {
    case BaseType::Void: return 0;
    case BaseType::Char: case BaseType::UChar: return 1;
    case BaseType::Int: case BaseType::UInt: return 2;
    case BaseType::Long: case BaseType::ULong: case BaseType::Double: return 8;
    case BaseType::RomT: return 3;
  }
  return 0;
}

bool isSigned(const CType& t) {
  return t.ptr == 0 && (t.base == BaseType::Char || t.base == BaseType::Int || t.base == BaseType::Long);
}

bool isWide(const CType& t) {
  return t.ptr == 0 && (t.base == BaseType::Long || t.base == BaseType::ULong || t.base == BaseType::Double);
}

bool isFloat(const CType& t) { return t.ptr == 0 && t.base == BaseType::Double; }

std::string baseName(BaseType b) {
  switch (b) {
    case BaseType::Void: return "void";
    case BaseType::Char: return "char";
    case BaseType::UChar: return "uchar";
    case BaseType::Int: return "int";
    case BaseType::UInt: return "uint";
    case BaseType::Long: return "long";
    case BaseType::ULong: return "ulong";
    case BaseType::Double: return "double";
    case BaseType::RomT: return "rom_t";
  }
  return "?";
}

std::string assetFormName(AssetForm f) {
  switch (f) {
    case AssetForm::Image: return "__image";
    case AssetForm::Sprite: return "__sprite";
    case AssetForm::Palette: return "__palette";
    case AssetForm::Sample: return "__sample";
    case AssetForm::File: return "__file";
  }
  return "?";
}

std::string typeName(const CType& t) {
  std::string s = baseName(t.base) + std::string(static_cast<size_t>(t.ptr), '*');
  if (t.arrayLen) s += "[" + std::to_string(*t.arrayLen) + "]";
  return s;
}

}  // namespace sc8::cc
