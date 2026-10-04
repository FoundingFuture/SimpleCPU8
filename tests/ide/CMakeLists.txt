# The IDE's tests. settings.cpp uses the standard library alone, so it is
# compiled in here and both presets run these.

file(GLOB SC8_IDE_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
sc8_add_test(sc8_ide_tests ${SC8_IDE_TESTS} "${CMAKE_SOURCE_DIR}/src/ide/settings.cpp")
target_include_directories(sc8_ide_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(sc8_ide_tests PRIVATE SC8_PACKAGING_DIR="${CMAKE_SOURCE_DIR}/packaging")
