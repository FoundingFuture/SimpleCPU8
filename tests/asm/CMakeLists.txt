sc8_add_test(sc8_asm_tests
  expr_test.cpp
  asm_test.cpp
  golden_test.cpp
  format_test.cpp)
target_link_libraries(sc8_asm_tests PRIVATE sc8::asm)
# The golden byte test reads the browser project's demo sources and the
# fixtures from the tree. The layout test reads the examples.
target_compile_definitions(sc8_asm_tests PRIVATE
  SC8_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples"
  SC8_GOLDEN_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden-src"
  SC8_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden-bytes")
