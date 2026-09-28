sc8_add_test(sc8_assets_tests
  assets_test.cpp
  font_file_test.cpp
  loaders_test.cpp
  sprite_test.cpp)
target_link_libraries(sc8_assets_tests PRIVATE sc8::assets)
# stb_image_write, for the PNG the loader test decodes.
target_link_libraries(sc8_assets_tests PRIVATE stb)
