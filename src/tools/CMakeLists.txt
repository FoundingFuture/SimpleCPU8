# Command line front ends. None of these needs a display.
#
#   simplecpu-asm   assemble a source file, burn a ROM
#   simplecpu-run   run a ROM headless and report the machine state
#   simplecpu-cc    compile C to assembly
#   simplecpu-make  build a directory of C, assembly, BASIC and assets into a ROM

add_executable(simplecpu-asm asm_main.cpp)
target_link_libraries(simplecpu-asm PRIVATE sc8::assets sc8_warnings)

add_executable(simplecpu-run run_main.cpp)
target_link_libraries(simplecpu-run PRIVATE sc8::asm sc8_warnings)

add_executable(simplecpu-cc cc_main.cpp)
target_link_libraries(simplecpu-cc PRIVATE sc8::cc sc8::assets sc8_warnings)

# simplecpu-make: a directory is a program. C, assembly, BASIC and assets
# in one folder become one ROM.
add_executable(simplecpu-make make_main.cpp)
target_link_libraries(simplecpu-make PRIVATE sc8::cc sc8::assets sc8_warnings)
