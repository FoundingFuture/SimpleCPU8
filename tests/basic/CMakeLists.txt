# The BASIC interpreter's tests: the ROM booted into a headless machine with
# the GPU in text mode, keys typed through the input device, the screen
# read out of RAM. basic_test.cpp is the browser project's basic.test.ts.
# lines_test.cpp tests the editor's line numbering, with no machine.
sc8_add_test(sc8_basic_tests basic_test.cpp lines_test.cpp check_test.cpp)
target_link_libraries(sc8_basic_tests PRIVATE sc8::basic sc8::devices sc8::project)
target_include_directories(sc8_basic_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")

# basic_profile measures where the interpreter's cycles go, for
# docs/design/basic-speed.md. It is a tool rather than a test, so CTest does
# not run it. It reads the build's basic.asm for the labels.
add_executable(basic_profile "${CMAKE_SOURCE_DIR}/tests/support/basic_profile.cpp")
target_link_libraries(basic_profile PRIVATE sc8::basic sc8::devices sc8::asm sc8_warnings)
target_include_directories(basic_profile PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(basic_profile PRIVATE SC8_BASIC_ASM_PATH="${SC8_BASIC_ASM}")
