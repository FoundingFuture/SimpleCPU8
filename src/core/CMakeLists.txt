add_library(sc8_core STATIC
  isa.cpp
  signals.cpp
  conflicts.cpp
  microcode.cpp
  mcparse.cpp
  machine.cpp
  cartridge.cpp)

# zlib_impl.cpp compiles stb's zlib code, third party, so it is built
# without the tree's warning flags, the way the assets library treats
# stb_image and miniaudio.
add_library(sc8_core_vendor OBJECT zlib_impl.cpp)
target_include_directories(sc8_core_vendor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(sc8_core_vendor PRIVATE stb)
set_target_properties(sc8_core_vendor PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(sc8_core PRIVATE $<TARGET_OBJECTS:sc8_core_vendor>)
target_include_directories(sc8_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(sc8_core PRIVATE sc8_warnings)
add_library(sc8::core ALIAS sc8_core)
