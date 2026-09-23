# The C compiler's tests, one file per test file of the browser project,
# plus the harness that compiles, assembles and runs a snippet.
file(GLOB SC8_CC_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
sc8_add_test(sc8_cc_tests ${SC8_CC_TESTS} harness.cpp)
target_link_libraries(sc8_cc_tests PRIVATE sc8::cc sc8::devices)
target_include_directories(sc8_cc_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
