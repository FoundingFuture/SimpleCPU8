sc8_add_test(sc8_asm_tests
  expr_test.cpp
  asm_test.cpp)
target_link_libraries(sc8_asm_tests PRIVATE sc8::asm)
