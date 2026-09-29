# The project library: laying a project out and building it, on disk in a
# temporary folder, for each of the four kinds.
sc8_add_test(sc8_project_tests project_test.cpp)
target_link_libraries(sc8_project_tests PRIVATE sc8::project)
if(TARGET sc8_basic)
  target_compile_definitions(sc8_project_tests PRIVATE SC8_HAVE_BASIC=1)
  # examples/basic/font is built by the simplecpu-make executable and run on the
  # BASIC test harness's machine.
  target_link_libraries(sc8_project_tests PRIVATE sc8::basic sc8::devices)
  target_include_directories(sc8_project_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
  target_compile_definitions(sc8_project_tests PRIVATE
    "SC8_SIMPLECPU_MAKE=\"$<TARGET_FILE:simplecpu-make>\""
    "SC8_EXAMPLES_DIR=\"${CMAKE_SOURCE_DIR}/examples\"")
  add_dependencies(sc8_project_tests simplecpu-make)
endif()
