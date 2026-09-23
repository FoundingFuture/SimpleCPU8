# Warning flags shared by every target of this project. Third party code
# fetched by Dependencies.cmake does not get them.

add_library(sc8_warnings INTERFACE)

if(MSVC)
  target_compile_options(sc8_warnings INTERFACE /W4 /permissive- /utf-8)
else()
  target_compile_options(sc8_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wno-unused-parameter -Wno-missing-field-initializers)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # A GCC 13 false positive on vector::push_back of a two byte struct.
    target_compile_options(sc8_warnings INTERFACE -Wno-stringop-overflow)
  endif()
endif()
