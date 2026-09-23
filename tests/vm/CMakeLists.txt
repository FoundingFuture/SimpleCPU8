# The virtual computer's tests, built by the IDE preset alone because
# sc8_vm links raylib. Nothing here opens a window.

file(GLOB SC8_VM_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
sc8_add_test(sc8_vm_tests ${SC8_VM_TESTS})
target_link_libraries(sc8_vm_tests PRIVATE sc8::vm)

if(TARGET sc8_basic)
  target_link_libraries(sc8_vm_tests PRIVATE sc8::basic)
  target_compile_definitions(sc8_vm_tests PRIVATE SC8_HAVE_BASIC=1)
endif()
