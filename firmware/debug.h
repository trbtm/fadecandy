/*
 * Debugging routines.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void crash(const char* reason) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
