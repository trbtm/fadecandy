#ifndef WProgram_h
#define WProgram_h

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "core_pins.h"

// Enforce inlining, so we can take advantage of inter-procedural optimization
#define ALWAYS_INLINE __attribute__ ((always_inline))

#ifdef __cplusplus

#include "pins_arduino.h"

#endif // __cplusplus

#endif // WProgram_h
