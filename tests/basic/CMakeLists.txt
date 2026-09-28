# The BASIC interpreter's tests: the ROM booted into a headless machine with
# the GPU in text mode, keys typed through the input device, the screen
# read out of RAM. basic_test.cpp is the browser project's basic.test.ts.
# lines_test.cpp tests the editor's line numbering, with no machine.
sc8_add_test(sc8_basic_tests basic_test.cpp lines_test.cpp)
target_link_libraries(sc8_basic_tests PRIVATE sc8::basic sc8::devices sc8::project)
target_include_directories(sc8_basic_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
