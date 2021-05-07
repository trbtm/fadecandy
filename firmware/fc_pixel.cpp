/*
 * Fadecandy Firmware: Low-level pixel update code
 * (Included into fadecandy.cpp)
 * 
 * Copyright (c) 2013 Micah Elizabeth Scott
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

static uint32_t FCP_FN(updatePixel)(const uint8_t *pixelNext)
{
    int iR = pixelNext[0] * 0x101;
    int iG = pixelNext[1] * 0x101;
    int iB = pixelNext[2] * 0x101;

    // Pass through our color LUT
    // Result range: [0, 0xFFFF] 
    iR = FCP_FN(lutInterpolate)(buffers.lutCurrent.r, iR);
    iG = FCP_FN(lutInterpolate)(buffers.lutCurrent.g, iG);
    iB = FCP_FN(lutInterpolate)(buffers.lutCurrent.b, iB);

    /*
     * Round to the nearest 8-bit value. Clamping is necessary!
     * This value might be as low as -128 prior to adding 0x80
     * for rounding. After this addition, the result is guaranteed
     * to be >= 0, but it may be over 0xffff.
     *
     * This rules out clamping using the UQADD16 instruction,
     * since the addition itself needs to allow overflow. Instead,
     * we clamp using a separate USAT instruction.
     */

    int r8 = __USAT(iR + 0x80, 16) >> 8;
    int g8 = __USAT(iG + 0x80, 16) >> 8;
    int b8 = __USAT(iB + 0x80, 16) >> 8;

    // Pack the result, in GRB order.
    return (g8 << 16) | (r8 << 8) | b8;
}
