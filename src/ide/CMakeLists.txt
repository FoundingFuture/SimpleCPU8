# simplecpu-ide: the editor, assembler, debugger and microcode tools around
# the virtual computer. raylib for the window and the screen, Dear ImGui for
# the panes.

add_executable(simplecpu-ide
  main.cpp
  ide.cpp)

target_link_libraries(simplecpu-ide PRIVATE sc8::vm rlimgui imgui sc8_warnings)
