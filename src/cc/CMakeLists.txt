# The C compiler: a defined subset of C, compiled to assembly text the
# assembler then takes. The port from the TypeScript compiler is pending.
# See docs/c-compiler-design.md in the SimpleCPU repository for the design.

add_library(sc8_cc STATIC
  cc.cpp)

target_include_directories(sc8_cc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(sc8_cc PUBLIC sc8::asm PRIVATE sc8_warnings)
add_library(sc8::cc ALIAS sc8_cc)
