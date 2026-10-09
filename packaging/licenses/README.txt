simplecpu-8.txt  SimpleCPU-8 itself, under the MIT licence.

SimpleCPU-8 is built with these libraries, at the versions pinned in
cmake/Dependencies.cmake. Each file below holds the library's own
licence text.

raylib.txt     raylib: the window, the input and the drawing.
miniaudio.txt  miniaudio: the sound device and the sample decoder.
imgui.txt      Dear ImGui: the IDE's panes.
stb.txt        stb_image and stb_image_write: the pictures, and the
               zlib codec that compresses a ROM file on disk.
doctest.txt    doctest: the test framework. It runs the tests and is
               in none of the programs.

Two more libraries are compiled in. Both are under the zlib licence,
which asks for no notice in a binary, so their texts are not here:

rlImGui        the raylib backend for Dear ImGui, in the IDE.
GLFW           the window and input layer inside raylib.
