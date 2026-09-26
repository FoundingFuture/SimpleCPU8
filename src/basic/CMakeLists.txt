# The BASIC interpreter: C for the machine, compiled at build time by the
# tools built in this same tree.
#
#   simplecpu-cc   the nine C files -> basic.asm and ROM.h
#   simplecpu-asm  basic.asm -> basic.rom, optimal microcode, title BASIC
#   EmbedBinary    basic.rom -> basic_rom_data.h
#
# sc8_basic wraps the embedded bytes as sc8::basicRom(), so the virtual
# computer boots BASIC with no file beside the executable.

set(SC8_BASIC_SOURCES main.c term.c lex.c expr.c strings.c edit.c run.c bang.c store.c)
set(SC8_BASIC_HEADERS basic.h)

set(SC8_BASIC_ASM "${CMAKE_CURRENT_BINARY_DIR}/basic.asm" CACHE INTERNAL "the assembly simplecpu-cc writes for BASIC")
set(SC8_BASIC_ROM_H "${CMAKE_CURRENT_BINARY_DIR}/ROM.h")
set(SC8_BASIC_ROM "${CMAKE_CURRENT_BINARY_DIR}/basic.rom" CACHE INTERNAL "the BASIC interpreter ROM")
set(SC8_BASIC_DATA_H "${CMAKE_CURRENT_BINARY_DIR}/basic_rom_data.h")

# The compiler resolves #include "basic.h" by the name it was given on the
# command line. So the files are passed by bare name from this directory.
# The first file holds main, as the browser project passed them. bang.c and
# store.c are new here: the bang statement and the storage driver.
add_custom_command(
  OUTPUT "${SC8_BASIC_ASM}" "${SC8_BASIC_ROM_H}"
  COMMAND $<TARGET_FILE:simplecpu-cc> ${SC8_BASIC_SOURCES} ${SC8_BASIC_HEADERS}
          -o "${SC8_BASIC_ASM}" --rom-header "${SC8_BASIC_ROM_H}" -zp-reserve 32
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  DEPENDS simplecpu-cc ${SC8_BASIC_SOURCES} ${SC8_BASIC_HEADERS}
  COMMENT "Compiling BASIC")

add_custom_command(
  OUTPUT "${SC8_BASIC_ROM}"
  COMMAND $<TARGET_FILE:simplecpu-asm> "${SC8_BASIC_ASM}" -o "${SC8_BASIC_ROM}"
          --microcode optimal --title BASIC
  DEPENDS simplecpu-asm "${SC8_BASIC_ASM}"
  COMMENT "Burning basic.rom")

add_custom_command(
  OUTPUT "${SC8_BASIC_DATA_H}"
  COMMAND ${CMAKE_COMMAND} -DINPUT="${SC8_BASIC_ROM}" -DOUTPUT="${SC8_BASIC_DATA_H}"
          -DNAME=BASIC_ROM -P "${CMAKE_SOURCE_DIR}/cmake/EmbedBinary.cmake"
  DEPENDS "${SC8_BASIC_ROM}" "${CMAKE_SOURCE_DIR}/cmake/EmbedBinary.cmake"
  COMMENT "Embedding basic.rom")

# The assembly is embedded as well, so a project that adds a driver to
# BASIC can assemble the interpreter and the driver into one ROM.
set(SC8_BASIC_ASM_H "${CMAKE_CURRENT_BINARY_DIR}/basic_asm_data.h")
add_custom_command(
  OUTPUT "${SC8_BASIC_ASM_H}"
  COMMAND ${CMAKE_COMMAND} -DINPUT="${SC8_BASIC_ASM}" -DOUTPUT="${SC8_BASIC_ASM_H}"
          -DNAME=BASIC_ASM -P "${CMAKE_SOURCE_DIR}/cmake/EmbedText.cmake"
  DEPENDS "${SC8_BASIC_ASM}" "${CMAKE_SOURCE_DIR}/cmake/EmbedText.cmake"
  COMMENT "Embedding basic.asm")

# And the C sources themselves, so a project that holds BASIC and C
# compiles the interpreter and the user's files as one program.
set(SC8_BASIC_SRC_H "${CMAKE_CURRENT_BINARY_DIR}/basic_src_data.h")
set(SC8_BASIC_SRC_PATHS)
foreach(f IN LISTS SC8_BASIC_SOURCES SC8_BASIC_HEADERS)
  list(APPEND SC8_BASIC_SRC_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
endforeach()
add_custom_command(
  OUTPUT "${SC8_BASIC_SRC_H}"
  COMMAND ${CMAKE_COMMAND} "-DINPUTS=${SC8_BASIC_SRC_PATHS}" "-DOUTPUT=${SC8_BASIC_SRC_H}"
          -DNAME=BASIC_SRC -P "${CMAKE_SOURCE_DIR}/cmake/EmbedTexts.cmake"
  DEPENDS ${SC8_BASIC_SRC_PATHS} "${CMAKE_SOURCE_DIR}/cmake/EmbedTexts.cmake"
  COMMENT "Embedding the BASIC sources"
  VERBATIM)

add_library(sc8_basic STATIC basic_rom.cpp program.cpp lines.cpp "${SC8_BASIC_DATA_H}" "${SC8_BASIC_ASM_H}"
            "${SC8_BASIC_SRC_H}")
target_include_directories(sc8_basic
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.."
  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(sc8_basic PRIVATE sc8_warnings)
add_library(sc8::basic ALIAS sc8_basic)
