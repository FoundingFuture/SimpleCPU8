# The C compiler: a defined subset of C, compiled to assembly text the
# assembler then takes. One file pair per module of the TypeScript original.
# See docs/design/c-compiler-design.md for the design.

add_library(sc8_cc STATIC
  ast.cpp
  build.cpp
  cc.cpp
  codegen.cpp
  doubles.cpp
  headers.cpp
  lex.cpp
  parse.cpp
  pp.cpp
  runtime.cpp
  softmath.cpp
  zeropage.cpp)

target_include_directories(sc8_cc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(sc8_cc PUBLIC sc8::asm PRIVATE sc8_warnings)
add_library(sc8::cc ALIAS sc8_cc)
