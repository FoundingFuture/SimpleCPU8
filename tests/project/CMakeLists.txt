# The project library: laying a project out and building it, on disk in a
# temporary folder, for each of the four kinds.
sc8_add_test(sc8_project_tests project_test.cpp)
target_link_libraries(sc8_project_tests PRIVATE sc8::project)
if(TARGET sc8_basic)
  target_compile_definitions(sc8_project_tests PRIVATE SC8_HAVE_BASIC=1)
endif()
