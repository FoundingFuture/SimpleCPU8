# sc8_project: a directory of C, assembly, BASIC and assets into one ROM.
# simplecpu-make and the IDE's Build button share it.
add_library(sc8_project STATIC project.cpp)
target_include_directories(sc8_project PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(sc8_project PUBLIC sc8::cc sc8::assets PRIVATE sc8_warnings)
add_library(sc8::project ALIAS sc8_project)

# A BASIC project puts its slots on the interpreter, which exists only
# when the tools that build it do.
if(SC8_BUILD_TOOLS)
  target_link_libraries(sc8_project PUBLIC sc8::basic)
  target_compile_definitions(sc8_project PRIVATE SC8_HAVE_BASIC=1)
endif()
