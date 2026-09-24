# simplecpu-ide: the editor, assembler, debugger and microcode tools around
# the virtual computer. raylib for the window and the screen, Dear ImGui for
# the panes. The manual and the datapath schema are data modules the panes
# read.

# The four programming guides in docs/guides are compiled in. The Manual
# pane shows them with no file beside the executable. An edited guide is
# picked up by the next build.
set(SC8_GUIDES basic.md c.md assembly.md microcode.md)
set(SC8_GUIDE_PATHS)
foreach(f IN LISTS SC8_GUIDES)
  list(APPEND SC8_GUIDE_PATHS "${CMAKE_SOURCE_DIR}/docs/guides/${f}")
endforeach()
set(SC8_GUIDES_H "${CMAKE_CURRENT_BINARY_DIR}/guides_data.h")
add_custom_command(
  OUTPUT "${SC8_GUIDES_H}"
  COMMAND ${CMAKE_COMMAND} "-DINPUTS=${SC8_GUIDE_PATHS}" "-DOUTPUT=${SC8_GUIDES_H}"
          -DNAME=GUIDES -P "${CMAKE_SOURCE_DIR}/cmake/EmbedTexts.cmake"
  DEPENDS ${SC8_GUIDE_PATHS} "${CMAKE_SOURCE_DIR}/cmake/EmbedTexts.cmake"
  COMMENT "Embedding the guides"
  VERBATIM)

add_executable(simplecpu-ide
  main.cpp
  ide.cpp
  edit_panes.cpp
  project_pane.cpp
  filedialog.cpp
  run_panes.cpp
  micro_panes.cpp
  manual.cpp
  guide.cpp
  datapath.cpp
  "${SC8_GUIDES_H}")

target_include_directories(simplecpu-ide PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(simplecpu-ide PRIVATE sc8::vm sc8::basic sc8::cc sc8::project rlimgui imgui sc8_warnings)
