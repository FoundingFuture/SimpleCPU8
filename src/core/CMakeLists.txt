add_library(sc8_core STATIC
  isa.cpp
  signals.cpp
  conflicts.cpp
  microcode.cpp
  mcparse.cpp
  machine.cpp
  cartridge.cpp)

target_include_directories(sc8_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(sc8_core PRIVATE sc8_warnings)
add_library(sc8::core ALIAS sc8_core)
