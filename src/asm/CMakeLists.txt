add_library(sc8_asm STATIC
  expr.cpp
  asm.cpp)

target_include_directories(sc8_asm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(sc8_asm PUBLIC sc8::core sc8::devices PRIVATE sc8_warnings)
add_library(sc8::asm ALIAS sc8_asm)
