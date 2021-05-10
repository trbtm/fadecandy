/*
 * More accurate time functions that don't rollover.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint64_t millis64(void);
uint64_t micros64(void);

void initSysticks(void);

#ifdef __cplusplus
}
#endif
