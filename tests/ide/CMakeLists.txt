# The IDE's tests. settings.cpp uses the standard library alone, so it is
# compiled in here and both presets run these.

file(GLOB SC8_IDE_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
sc8_add_test(sc8_ide_tests ${SC8_IDE_TESTS} "${CMAKE_SOURCE_DIR}/src/ide/settings.cpp")
target_include_directories(sc8_ide_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(sc8_ide_tests PRIVATE SC8_PACKAGING_DIR="${CMAKE_SOURCE_DIR}/packaging"
  SC8_LICENSE_FILE="${CMAKE_SOURCE_DIR}/LICENSE")
# The fetched libraries' licence files, which packaging/licenses copies.
# A library this preset does not fetch has an empty path.
set(SC8_LICENSE_PATHS)
foreach(lib raylib miniaudio imgui doctest stb)
  set(file "")
  if(${lib}_SOURCE_DIR)
    foreach(name LICENSE LICENSE.txt)
      if(EXISTS "${${lib}_SOURCE_DIR}/${name}")
        set(file "${${lib}_SOURCE_DIR}/${name}")
      endif()
    endforeach()
  endif()
  string(TOUPPER "${lib}" upper)
  list(APPEND SC8_LICENSE_PATHS "SC8_LICENSE_${upper}=\"${file}\"")
endforeach()
target_compile_definitions(sc8_ide_tests PRIVATE ${SC8_LICENSE_PATHS})
