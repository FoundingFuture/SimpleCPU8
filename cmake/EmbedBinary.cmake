# Turn a binary file into a C++ header holding its bytes as an array plus
# its size. The sibling of EmbedText.cmake for files that are not text.
# Called as a script: cmake -DINPUT=... -DOUTPUT=... -DNAME=... -P EmbedBinary.cmake
#
# The header declares
#
#   static const unsigned char NAME[] = { ... };
#   static const unsigned long NAME_SIZE = ...;

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hexLength)
math(EXPR size "${hexLength} / 2")

# Twelve bytes per line keeps the header readable in a diff and short.
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){12})" "\\1\n  " bytes "${bytes}")

file(WRITE "${OUTPUT}"
  "// Generated from ${INPUT} by cmake/EmbedBinary.cmake. Do not edit.\n"
  "#pragma once\n"
  "static const unsigned char ${NAME}[] = {\n  ${bytes}\n};\n"
  "static const unsigned long ${NAME}_SIZE = ${size};\n")
