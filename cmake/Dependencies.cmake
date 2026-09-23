# Third party code, fetched at configure time with FetchContent and pinned to
# a tag or a commit. Nothing here needs a package manager on the machine.
#
# raylib brings its own CMake project. The other four ship no usable CMake
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

if(SC8_BUILD_IDE)
  # raylib: window, input, 2D drawing. The examples and games are skipped.
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
  # Audio goes through miniaudio directly, so raylib's own audio module is
  # left out to avoid two copies of it.
  set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(raylib)

  # miniaudio: one header. The implementation is compiled once in
  # src/ide/audio.cpp.
  FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.25
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake_disabled)
  FetchContent_MakeAvailable(miniaudio)
  add_library(miniaudio INTERFACE)
  target_include_directories(miniaudio SYSTEM INTERFACE "${miniaudio_SOURCE_DIR}")

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
