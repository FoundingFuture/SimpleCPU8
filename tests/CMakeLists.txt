# One doctest executable per library, registered with CTest.

add_library(sc8_test_main OBJECT main.cpp)
target_link_libraries(sc8_test_main PUBLIC doctest)

function(sc8_add_test name)
  add_executable(${name} ${ARGN} $<TARGET_OBJECTS:sc8_test_main>)
  target_link_libraries(${name} PRIVATE doctest sc8_warnings)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_subdirectory(core)
add_subdirectory(asm)
add_subdirectory(assets)
add_subdirectory(devices)
add_subdirectory(cc)
# sc8_vm links raylib, which the headless preset leaves out.
if(SC8_BUILD_IDE)
  add_subdirectory(vm)
endif()
