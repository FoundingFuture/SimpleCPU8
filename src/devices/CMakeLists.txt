# The peripherals on the IO bus, each in its own pair of files, plus the
# port, command and constant tables every device publishes. The assembler
# and the C compiler read those tables through constants.cpp.
#
# Sources are globbed so a new device is one pair of files and nothing
# else. CONFIGURE_DEPENDS makes CMake notice a new file on the next build.

file(GLOB SC8_DEVICE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(sc8_devices STATIC ${SC8_DEVICE_SOURCES})

target_include_directories(sc8_devices PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(sc8_devices PUBLIC sc8::core PRIVATE sc8_warnings)
add_library(sc8::devices ALIAS sc8_devices)
