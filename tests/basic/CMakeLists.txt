# The BASIC interpreter's tests: the ROM booted into a headless machine with
# the GPU in text mode, keys typed through the input device, the screen
# read out of RAM. One file, from the browser project's basic.test.ts.
sc8_add_test(sc8_basic_tests basic_test.cpp)
target_link_libraries(sc8_basic_tests PRIVATE sc8::basic sc8::devices)
