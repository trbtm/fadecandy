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

// Pushes data into a buffer for one pixel from each of up to 8 strips.
// |N| is the number of strips.
// |Sampler| is a lambda function to retrieve a pixel from each strip
//      signature: size_t Sampler(unsigned n)
template <size_t N, typename Sampler>
inline void pushPixels(uint8_t** buffer, Sampler sampler) {
    // Six output words
    union {
        uint32_t word;
        struct {
            uint32_t p0a:1, p1a:1, p2a:1, p3a:1, p4a:1, p5a:1, p6a:1, p7a:1,
                        p0b:1, p1b:1, p2b:1, p3b:1, p4b:1, p5b:1, p6b:1, p7b:1,
                        p0c:1, p1c:1, p2c:1, p3c:1, p4c:1, p5c:1, p6c:1, p7c:1,
                        p0d:1, p1d:1, p2d:1, p3d:1, p4d:1, p5d:1, p6d:1, p7d:1;
        };
    } o0, o1, o2, o3, o4, o5;

    /*
     * Remap bits.
     *
     * This generates compact and efficient code using the BFI instruction.
     */
    #define SWIZZLE(X) if constexpr (X < N) { \
        uint32_t p = sampler(X); \
        o5.p ## X ## d = p; \
        o5.p ## X ## c = p >> 1; \
        o5.p ## X ## b = p >> 2; \
        o5.p ## X ## a = p >> 3; \
        o4.p ## X ## d = p >> 4; \
        o4.p ## X ## c = p >> 5; \
        o4.p ## X ## b = p >> 6; \
        o4.p ## X ## a = p >> 7; \
        o3.p ## X ## d = p >> 8; \
        o3.p ## X ## c = p >> 9; \
        o3.p ## X ## b = p >> 10; \
        o3.p ## X ## a = p >> 11; \
        o2.p ## X ## d = p >> 12; \
        o2.p ## X ## c = p >> 13; \
        o2.p ## X ## b = p >> 14; \
        o2.p ## X ## a = p >> 15; \
        o1.p ## X ## d = p >> 16; \
        o1.p ## X ## c = p >> 17; \
        o1.p ## X ## b = p >> 18; \
        o1.p ## X ## a = p >> 19; \
        o0.p ## X ## d = p >> 20; \
        o0.p ## X ## c = p >> 21; \
        o0.p ## X ## b = p >> 22; \
        o0.p ## X ## a = p >> 23; \
    }
    SWIZZLE(0)
    SWIZZLE(1)
    SWIZZLE(2)
    SWIZZLE(3)
    SWIZZLE(4)
    SWIZZLE(5)
    SWIZZLE(6)
    SWIZZLE(7)
    #undef SWIZZLE

    uint32_t** out = reinterpret_cast<uint32_t**>(buffer);
    *(*out++) = o0.word;
    *(*out++) = o1.word;
    *(*out++) = o2.word;
    *(*out++) = o3.word;
    *(*out++) = o4.word;
    *(*out++) = o5.word;
}
}
