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

// Pushes data into a DMA buffer for one pixel from each of up to 8 strips.
// |N| is the number of strips.
// |Sampler| is a lambda function to retrieve a pixel from each strip
//      signature: size_t Sampler(unsigned n)
template <size_t N, typename Sampler>
inline void pushPixels(uint8_t** buffer, Sampler sampler) {
    uint32_t o0, o1, o2, o3, o4, o5;

    // Use the BFI (bit field insert) instruction to efficiently remap bits for DMA.
    // It seems that we can't rely on newer versions of GCC (e.g. 10.2.1) to optimize bit-field
    // expressions using BFI and prefers instead to emit sequences of ANDs and ORs so we need
    // to use assembly to get the desired instructions.
    #define LED_BFI(R, V, B, W) asm ("bfi %0, %1, %2, %3" : "+r" (R) : "r" (V), "M" (B), "M" (W))
    #define LED_LSR_BFI4(X, R, B) \
        if constexpr (X == 0) R = p >> (B + 3); else LED_BFI(R, p >> (B + 3), X, 1); \
        LED_BFI(R, p >> (B + 2), 8 + X, 1); \
        LED_BFI(R, p >> (B + 1), 16 + X, 1); \
        LED_BFI(R, p >> B, 24 + X, 1);
    #define LED_SWIZZLE(X) if constexpr (X < N) { \
        uint32_t p = sampler(X); \
        LED_LSR_BFI4(X, o5, 0); \
        LED_LSR_BFI4(X, o4, 4); \
        LED_LSR_BFI4(X, o3, 8); \
        LED_LSR_BFI4(X, o2, 12); \
        LED_LSR_BFI4(X, o1, 16); \
        LED_LSR_BFI4(X, o0, 20); \
    }
    LED_SWIZZLE(0)
    LED_SWIZZLE(1)
    LED_SWIZZLE(2)
    LED_SWIZZLE(3)
    LED_SWIZZLE(4)
    LED_SWIZZLE(5)
    LED_SWIZZLE(6)
    LED_SWIZZLE(7)
    #undef LED_SWIZZLE
    #undef LED_LSR_BFI4
    #undef LED_BFI

    uint32_t*& out = *reinterpret_cast<uint32_t**>(buffer);
    *(out++) = o0;
    *(out++) = o1;
    *(out++) = o2;
    *(out++) = o3;
    *(out++) = o4;
    *(out++) = o5;
}
} // namespace led
