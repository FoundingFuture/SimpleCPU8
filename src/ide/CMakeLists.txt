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
  highlight.cpp
  basic_assist.cpp
  settings.cpp
  run_panes.cpp
  micro_panes.cpp
  manual.cpp
  guide.cpp
  datapath.cpp
  sprite_editor.cpp
  sprite_pane.cpp
  "${SC8_GUIDES_H}")

# The window's own full screen is a Cocoa call on macOS. The one
# Objective-C++ file compiles as C++ told to read Objective-C++, rather
# than through CMake's OBJCXX language. Enabling that language for the
# whole tree also took over GLFW's Objective-C files inside raylib, which
# then failed to build as C++.
if(APPLE)
  target_sources(simplecpu-ide PRIVATE native_window.mm)
  set_source_files_properties(native_window.mm PROPERTIES LANGUAGE CXX COMPILE_OPTIONS "-xobjective-c++")
  target_link_libraries(simplecpu-ide PRIVATE "-framework Cocoa")
else()
  target_sources(simplecpu-ide PRIVATE native_window.cpp)
endif()

# On Windows the IDE is a GUI program, so the Start Menu opens it without a
# console window. main() stays the entry point: MSVC is told so, and MinGW
# finds it on its own. The resource gives the executable its icon.
if(WIN32)
  set(SC8_ICON_ICO "${CMAKE_SOURCE_DIR}/packaging/icon/simplecpu-8.ico")
  configure_file("${CMAKE_SOURCE_DIR}/packaging/windows/simplecpu-ide.rc.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/simplecpu-ide.rc" @ONLY)
  target_sources(simplecpu-ide PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/simplecpu-ide.rc")
  set_target_properties(simplecpu-ide PROPERTIES WIN32_EXECUTABLE TRUE)
  if(MSVC)
    target_link_options(simplecpu-ide PRIVATE /ENTRY:mainCRTStartup)
  endif()
endif()

target_include_directories(simplecpu-ide PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(simplecpu-ide PRIVATE sc8::vm sc8::basic sc8::cc sc8::project rlimgui imgui sc8_warnings)
