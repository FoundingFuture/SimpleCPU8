# Image and audio decoding plus the conversions from the browser version's
# asset pane. stb_image and miniaudio are compiled here, once each, in a
# target of their own so the project's warning flags never reach them.

add_library(sc8_assets_vendor OBJECT
  stb_impl.cpp
  miniaudio_impl.cpp)
target_include_directories(sc8_assets_vendor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
# miniaudio opens the platform's audio backends at run time.
find_package(Threads REQUIRED)
target_link_libraries(sc8_assets_vendor PUBLIC stb miniaudio Threads::Threads ${CMAKE_DL_LIBS})

add_library(sc8_assets STATIC assets.cpp)

target_include_directories(sc8_assets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(sc8_assets
  PUBLIC sc8::asm sc8_assets_vendor
  PRIVATE sc8_warnings)
add_library(sc8::assets ALIAS sc8_assets)
