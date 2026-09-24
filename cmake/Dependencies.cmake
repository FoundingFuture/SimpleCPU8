# Third party code, fetched at configure time with FetchContent and pinned to
# a tag or a commit. Nothing here needs a package manager on the machine.
#
# raylib brings its own CMake project. The others ship no usable CMake
# target, so this file defines one for each from the fetched sources.

include(FetchContent)

# CMake 4 refuses projects that ask for a minimum below 3.5. raylib's bundled
# glfw and older tags of other projects still do, so give them a floor.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

set(FETCHCONTENT_QUIET OFF)

# doctest: one header, used by tests/.
if(SC8_BUILD_TESTS)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.12
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake_disabled)
  FetchContent_MakeAvailable(doctest)
  add_library(doctest INTERFACE)
  target_include_directories(doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}/doctest")
endif()

# stb_image: one header, decodes the image formats .image accepts. The
# implementation is compiled once in src/assets/stb_impl.cpp. Pinned to a
# commit because the repository carries no release tags.
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
  SOURCE_SUBDIR cmake_disabled)
FetchContent_MakeAvailable(stb)
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")

# miniaudio: one header. The assembler's .sample decodes through it, so the
# headless build needs it too. The implementation is compiled once in
# src/assets/miniaudio_impl.cpp, with device IO for src/vm/audio.cpp.
FetchContent_Declare(miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG 0.11.25
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR cmake_disabled)
FetchContent_MakeAvailable(miniaudio)
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio SYSTEM INTERFACE "${miniaudio_SOURCE_DIR}")

if(SC8_BUILD_IDE)
  # raylib: window, input, 2D drawing. The examples and games are skipped.
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
  # Audio goes through miniaudio directly, so raylib's own audio module is
  # left out to avoid two copies of it.
  set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)
  # The IDE never reads an image from the clipboard. With the option on,
  # rcore.c prints a pragma message on Linux because JPG decoding is off.
  set(SUPPORT_CLIPBOARD_IMAGE OFF CACHE BOOL "" FORCE)
  # Nothing here records or replays input events. The module's reader
  # ignores what fgets returns, and glibc marks that result as required.
  set(SUPPORT_AUTOMATION_EVENTS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(raylib)

  # With CUSTOMIZE_BUILD, raylib 5.5 turns every #define in config.h into a
  # -D flag. MAX_MESH_VERTEX_BUFFERS is defined in both branches of an #ifdef
  # there, so each raylib unit saw it twice and warned. Drop the branch for
  # GPU skinning, which is off, and keep the 7 that config.h resolves to.
  get_target_property(raylibDefines raylib COMPILE_DEFINITIONS)
  list(REMOVE_ITEM raylibDefines "MAX_MESH_VERTEX_BUFFERS=9")
  set_target_properties(raylib PROPERTIES COMPILE_DEFINITIONS "${raylibDefines}")

  # Dear ImGui, docking branch, so the IDE panes can be rearranged.
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b-docking
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake_disabled)
  FetchContent_MakeAvailable(imgui)
  add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp")
  target_include_directories(imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}")

  # rlImGui: the raylib backend for Dear ImGui. Pinned to a commit because the
  # repository carries no release tags.
  FetchContent_Declare(rlimgui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG 1550009359ad975927f7f0e4a3f47e3f27123ea9
    SOURCE_SUBDIR cmake_disabled)
  FetchContent_MakeAvailable(rlimgui)
  add_library(rlimgui STATIC "${rlimgui_SOURCE_DIR}/rlImGui.cpp")
  target_include_directories(rlimgui SYSTEM PUBLIC "${rlimgui_SOURCE_DIR}")
  target_link_libraries(rlimgui PUBLIC imgui raylib)
endif()
