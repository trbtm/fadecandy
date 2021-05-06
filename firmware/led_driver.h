/*
 * Simplified LED driver for SK6812 pixels based on OctoWS2811.
 *
 * Clients are responsible for managing buffers and waiting for the LEDs
 * to be ready to receive new data.
 */

/*  OctoWS2811 - High Performance WS2811 LED Display Library
    http://www.pjrc.com/teensy/td_libs_OctoWS2811.html
    Copyright (c) 2013 Paul Stoffregen, PJRC.COM, LLC

    Zero-copy variant (OctoWS2811z) hacked up by Micah Elizabeth Scott.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

namespace led {

// Computes the size of buffer required for a set of 8 LED strips of given length.
constexpr size_t bufferSize(size_t ledsPerStrip) { return ledsPerStrip * 24; }

// Initialize the GPIOs and DMA for LED output.
void init(size_t ledsPerStrip);

// Returns true if all prior writes have finished and the LEDs are ready to receive
// new data taking into account the LED protocol's timing requirements.
bool ready();

// Returns true if all prior writes have finished.
bool writeFinished();

// Writes a buffer of encoded LED data to the DMA engine.
// This operation completes asynchronously: the client must not modify the contents of
// the buffer again until |writeFinished()| returns true.
// Assumes the LEDs are ready to receive more data.
void write(const uint8_t* buffer);
}
