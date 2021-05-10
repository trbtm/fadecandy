/*
 * Debugging routines.
 */

#include "debug.h"

#include "hw/HardwareSerial.h"

void crash(const char* reason) {
    // Wait for the watchdog to expire.
    serial_urgent(reason);
    serial_urgent(" -- CRASH\r\n");
    while (1) serial_urgent("."); // wait for watchdog timer
    __builtin_unreachable();
}
