# Turn a text file into a C++ header holding it as a string constant.
# Called as a script: cmake -DINPUT=... -DOUTPUT=... -DNAME=... -P EmbedText.cmake
#
# The header declares
#
#   static const char NAME[] = { ..., 0 };
#
# The bytes are spelled out rather than written as a string literal. A
# literal over 65536 characters is beyond what the standard guarantees, and
# Clang warns about one under -Wpedantic. The BASIC assembly is far larger.

file(READ "${INPUT}" hex HEX)

# Character literals rather than 0x.. integers: a byte above 0x7f would
# narrow into a signed char. Twelve per line keeps the header short.
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "'\\\\x\\1'," bytes "${hex}")
string(REGEX REPLACE "(('\\\\x[0-9a-f][0-9a-f]',){12})" "\\1\n  " bytes "${bytes}")

file(WRITE "${OUTPUT}"
  "// Generated from ${INPUT} by cmake/EmbedText.cmake. Do not edit.\n"
  "#pragma once\n"
  "static const char ${NAME}[] = {\n  ${bytes} 0\n};\n")
