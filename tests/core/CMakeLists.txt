sc8_add_test(sc8_core_tests
  conflicts_test.cpp
  microcode_test.cpp
  machine_test.cpp
  golden_test.cpp
  differential_test.cpp)
target_link_libraries(sc8_core_tests PRIVATE sc8::core sc8::asm)
