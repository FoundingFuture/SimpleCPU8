file(GLOB SC8_DEVICE_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
sc8_add_test(sc8_device_tests ${SC8_DEVICE_TESTS})
target_link_libraries(sc8_device_tests PRIVATE sc8::asm)
