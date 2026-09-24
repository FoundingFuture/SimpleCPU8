# simplecpu-ide: the editor, assembler, debugger and microcode tools around
# the virtual computer. raylib for the window and the screen, Dear ImGui for
# the panes. The manual and the datapath schema are data modules the panes
# read.

add_executable(simplecpu-ide
  main.cpp
  ide.cpp
  edit_panes.cpp
  run_panes.cpp
  micro_panes.cpp
  manual.cpp
  datapath.cpp)

target_link_libraries(simplecpu-ide PRIVATE sc8::vm sc8::basic sc8::cc rlimgui imgui sc8_warnings)
