# The IDE's tests. settings.cpp, view.cpp and indent.cpp use the standard
# library alone, so they are compiled in here and both presets run these.

file(GLOB SC8_IDE_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
sc8_add_test(sc8_ide_tests ${SC8_IDE_TESTS} "${CMAKE_SOURCE_DIR}/src/ide/settings.cpp"
  "${CMAKE_SOURCE_DIR}/src/ide/view.cpp" "${CMAKE_SOURCE_DIR}/src/ide/indent.cpp")
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

# The editor's indentation keys against a real Dear ImGui text box. A
# context needs no window, so this runs wherever the IDE is built.
if(TARGET imgui)
  sc8_add_test(sc8_ide_keys_tests imgui/indent_keys_test.cpp "${CMAKE_SOURCE_DIR}/src/ide/indent_keys.cpp"
    "${CMAKE_SOURCE_DIR}/src/ide/indent.cpp")
  target_include_directories(sc8_ide_keys_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
  target_link_libraries(sc8_ide_keys_tests PRIVATE imgui)
  if(APPLE)
    # Dear ImGui's clipboard on macOS.
    target_link_libraries(sc8_ide_keys_tests PRIVATE "-framework ApplicationServices")
  endif()
endif()
