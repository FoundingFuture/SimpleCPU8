# Assembles every example into a ROM under build/roms, and runs each ROM
# headless as a CTest test.
#
# An example is a directory holding <name>.asm and a README.md whose first
# line is "# Title". The title goes into the ROM's META chunk. The ROM
# depends on every asset the source names through .file, .image, .palette
# or .sample. That list is read from the source text at configure time.

set(SC8_ROM_MICROCODE "optimal" CACHE STRING "Microcode set the example ROMs name: naive or optimal")
set(SC8_ROM_DIR "${CMAKE_BINARY_DIR}/roms")
file(MAKE_DIRECTORY "${SC8_ROM_DIR}")

# The demos that run to HLT. Every other demo runs until the instruction
# bound. hanoi and mandel halt in the computer but not here. simplecpu-run
# has no GPU or ACP, so their wait loops read zero forever.
set(SC8_HALTING_DEMOS circle div16 div8 gpu hello list mul16 mul8 text textmode)
set(SC8_TEST_MAX_INSTRUCTIONS 2000000)

file(GLOB SC8_EXAMPLE_DIRS LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
set(SC8_ROMS)
foreach(dir IN LISTS SC8_EXAMPLE_DIRS)
  if(NOT IS_DIRECTORY "${dir}")
    continue()
  endif()
  get_filename_component(name "${dir}" NAME)
  set(src "${dir}/${name}.asm")
  if(NOT EXISTS "${src}")
    continue()
  endif()

  set(title "${name}")
  if(EXISTS "${dir}/README.md")
    file(STRINGS "${dir}/README.md" first LIMIT_COUNT 1)
    string(REGEX REPLACE "^#+ *" "" title "${first}")
  endif()

  # Every .file('x'), .image('x'), .palette('x') and .sample('x') in the source.
  set(assets)
  file(STRINGS "${src}" asset_lines REGEX "\\.(file|image|palette|sample)\\(")
  foreach(line IN LISTS asset_lines)
    string(REGEX MATCHALL "\\.(file|image|palette|sample)\\('([^']*)'\\)" refs "${line}")
    foreach(ref IN LISTS refs)
      string(REGEX REPLACE "^.*\\('([^']*)'\\)$" "\\1" asset "${ref}")
      list(APPEND assets "${dir}/${asset}")
    endforeach()
  endforeach()

  # The title and the asset list are read at configure time. An edit to
  # either file configures again.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${src}" "${dir}/README.md")

  set(rom "${SC8_ROM_DIR}/${name}.rom")
  add_custom_command(
    OUTPUT "${rom}"
    COMMAND simplecpu-asm "${src}" -o "${rom}" --microcode "${SC8_ROM_MICROCODE}" --title "${title}"
    DEPENDS simplecpu-asm "${src}" ${assets}
    COMMENT "Assembling ${name}.rom"
    VERBATIM)
  list(APPEND SC8_ROMS "${rom}")

  if(SC8_BUILD_TESTS)
    add_test(NAME rom_${name}
      COMMAND simplecpu-run "${rom}" --max ${SC8_TEST_MAX_INSTRUCTIONS})
    if(name IN_LIST SC8_HALTING_DEMOS)
      set_tests_properties(rom_${name} PROPERTIES PASS_REGULAR_EXPRESSION "status: halted")
    else()
      set_tests_properties(rom_${name} PROPERTIES PASS_REGULAR_EXPRESSION "status: (halted|running)")
    endif()
  endif()
endforeach()

add_custom_target(roms ALL DEPENDS ${SC8_ROMS})
