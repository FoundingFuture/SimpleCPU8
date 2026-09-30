# The virtual computer: the machine with its devices on the bus, the
# display that scales the GPU's 256x256 frame to the window, keyboard
# input and audio output. The simplecpu executable is this library with a
# command line. The IDE builds on the same library.

add_library(sc8_vm STATIC
  computer.cpp
  display.cpp
  audio.cpp
  alert_tone.cpp
  keys.cpp)

target_include_directories(sc8_vm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(sc8_vm
  PUBLIC sc8::asm sc8::assets raylib miniaudio
  PRIVATE sc8_warnings)
add_library(sc8::vm ALIAS sc8_vm)

# The CRT shader travels inside the binary, so a ROM and one executable are
# all a player needs.
set(SC8_SHADER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/shaders/crt.fs")
set(SC8_SHADER_HDR "${CMAKE_CURRENT_BINARY_DIR}/crt_shader.h")
add_custom_command(
  OUTPUT "${SC8_SHADER_HDR}"
  COMMAND ${CMAKE_COMMAND} -DINPUT="${SC8_SHADER_SRC}" -DOUTPUT="${SC8_SHADER_HDR}"
          -DNAME=CRT_SHADER_FS -P "${CMAKE_SOURCE_DIR}/cmake/EmbedText.cmake"
  DEPENDS "${SC8_SHADER_SRC}" "${CMAKE_SOURCE_DIR}/cmake/EmbedText.cmake"
  COMMENT "Embedding crt.fs")
target_sources(sc8_vm PRIVATE "${SC8_SHADER_HDR}")
target_include_directories(sc8_vm PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(simplecpu main.cpp)
target_link_libraries(simplecpu PRIVATE sc8::vm sc8_warnings)

# The BASIC ROM is built by the tools, so --basic exists only when they do.
if(TARGET sc8_basic)
  target_link_libraries(simplecpu PRIVATE sc8::basic)
  target_compile_definitions(simplecpu PRIVATE SC8_HAVE_BASIC=1)
endif()
