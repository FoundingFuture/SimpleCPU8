// The one translation unit that compiles miniaudio. The decoder serves
// .sample here and the device serves src/vm/audio.cpp, and both share the
// converters underneath, so one implementation has to carry both.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#include "miniaudio.h"
