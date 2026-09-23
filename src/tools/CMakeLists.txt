# Command line front ends. None of these needs a display.
#
#   simplecpu-asm   assemble a source file, burn a ROM
#   simplecpu-run   run a ROM headless and report the machine state
#   simplecpu-cc    compile C to assembly

add_executable(simplecpu-asm asm_main.cpp)
target_link_libraries(simplecpu-asm PRIVATE sc8::assets sc8_warnings)

add_executable(simplecpu-run run_main.cpp)
target_link_libraries(simplecpu-run PRIVATE sc8::asm sc8_warnings)

add_executable(simplecpu-cc cc_main.cpp)
target_link_libraries(simplecpu-cc PRIVATE sc8::cc sc8_warnings)
