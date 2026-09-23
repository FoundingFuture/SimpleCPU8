# Turn a text file into a C++ header holding it as a string constant.
# Called as a script: cmake -DINPUT=... -DOUTPUT=... -DNAME=... -P EmbedText.cmake

file(READ "${INPUT}" content)
# A raw string literal needs a delimiter the content cannot contain.
set(delim "sc8embed")
file(WRITE "${OUTPUT}"
  "// Generated from ${INPUT} by cmake/EmbedText.cmake. Do not edit.\n"
  "#pragma once\n"
  "static const char ${NAME}[] = R\"${delim}(${content})${delim}\";\n")
