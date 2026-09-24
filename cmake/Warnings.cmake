# Warning flags shared by every target of this project. Third party code
# fetched by Dependencies.cmake does not get them, and the two files that
# compile third party single header libraries (stb_image, miniaudio) turn
# them off for themselves.
#
# Every warning is an error in a Release build. SC8_WERROR=OFF relaxes
# that for a compiler this tree has not met yet.

option(SC8_WERROR "Treat warnings as errors" ON)

add_library(sc8_warnings INTERFACE)

if(MSVC)
  target_compile_options(sc8_warnings INTERFACE /W4 /permissive- /utf-8)
  if(SC8_WERROR)
    target_compile_options(sc8_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(sc8_warnings INTERFACE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wconversion -Wsign-conversion -Wdouble-promotion
    -Wold-style-cast -Wcast-align -Wcast-qual
    -Wnon-virtual-dtor -Woverloaded-virtual
    -Wformat=2 -Wimplicit-fallthrough -Wextra-semi
    -Wunused -Wunused-parameter -Wmissing-field-initializers)
  if(SC8_WERROR)
    target_compile_options(sc8_warnings INTERFACE -Werror)
  endif()
endif()
