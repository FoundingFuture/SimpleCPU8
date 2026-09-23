cmake_minimum_required(VERSION 3.24)

project(SimpleCPU8
  VERSION 0.1.0
  DESCRIPTION "SimpleCPU-8: an educational 8 bit CPU with editable microcode"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SC8_BUILD_IDE "Build the sc8 IDE (needs raylib, Dear ImGui and a display)" ON)
option(SC8_BUILD_TOOLS "Build the command line tools" ON)
option(SC8_BUILD_TESTS "Build the doctest suite" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
include(Dependencies)

add_subdirectory(src)

if(SC8_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# The example ROMs need simplecpu-asm, and their tests simplecpu-run.
if(SC8_BUILD_TOOLS)
  add_subdirectory(examples)
endif()
