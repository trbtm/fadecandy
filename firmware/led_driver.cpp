/*
 * Simplified LED driver for SK6812 pixels based on OctoWS2811.
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

#include "led_driver.h"

#include "time.h"
#include "hw/core_pins.h"

namespace led {

struct Timings {
    // LED strip frequency in Hz.
    uint32_t frequency;
    // Reset interval in microseconds.
    uint32_t resetInterval;
    // On-time percentage for 0s and 1s, as a fraction of 255.
    uint32_t t0h, t1h;
};

// OctoWS2811 defaults.
//
// Waveform timing: these set the high time for a 0 and 1 bit, as a fraction of
// the total 800 kHz or 400 kHz clock cycle.  The scale is 0 to 255.  The Worldsemi
// datasheet seems T1H should be 600 ns of a 1250 ns cycle, or 48%.  That may
// erroneous information?  Other sources reason the chip actually samples the
// line close to the center of each bit time, so T1H should be 80% if TOH is 20%.
// The chips appear to work based on a simple one-shot delay triggered by the
// rising edge.  At least 1 chip tested retransmits 0 as a 330 ns pulse (26%) and
// a 1 as a 660 ns pulse (53%).  Perhaps it's actually sampling near 500 ns?
// There doesn't seem to be any advantage to making T1H less, as long as there
// is sufficient low time before the end of the cycle, so the next rising edge
// can be detected.  T0H has been lengthened slightly, because the pulse can
// narrow if the DMA controller has extra latency during bus arbitration.  If you
// have an insight about tuning these parameters AND you have actually tested on
// real LED strips, please contact paul@pjrc.com.  Please do not email based only
// on reading the datasheets and purely theoretical analysis.
constexpr Timings octo { 800000, 300, 60, 176 };

// SK6812 seems more stable with these timings (less random flashing) but it
// seems to depend on how the board is initialized.  There might be some
// non-determinism involved in the DMA timings across boots.
//
// Data sheet suggestions:
// The data transmission time (TH+TL=1.25µs±600ns):
//     T0H 0 code, high level time 0.3µs ±0.15µs
//     T1H 1 code, high level time 0.6µs ±0.15µs
//     T0L 0 code, low level time 0.9µs ±0.15µs
//     T1L 1 code, low level time 0.6µs ±0.15µs
//     Trst Reset code，low level time 80µs
constexpr Timings normal { 800000, 300, 56, 172 };

// Experimental!
// constexpr Timings aggressive { 900000, 300, 44, 150 }; // good
// constexpr Timings aggressive { 1000000, 300, 44, 150 }; // still good
// constexpr Timings aggressive { 1000000, 300, 40, 140 }; // still good!
// constexpr Timings aggressive { 1100000, 300, 40, 140 }; // too fast, flickers, dropouts
// constexpr Timings aggressive { 1000000, 60, 40, 140 }; // still good!
// constexpr Timings aggressive { 1000000, 50, 40, 140 }; // bad, flickers, reset too short
constexpr Timings aggressive { 1000000, 80, 40, 140 }; // still good! safe

constexpr Timings timings = aggressive;

const uint8_t ONES = 0xFF;

volatile bool writeInProgress = false;
volatile uint64_t writeFinishedAt = 0;

void init(size_t ledsPerStrip) {
    const size_t bufsize = bufferSize(ledsPerStrip);

    // configure the 8 output pins
    GPIOD_PCOR = 0xFF;
    pinMode(2, OUTPUT);     // strip #1
    pinMode(14, OUTPUT);    // strip #2
    pinMode(7, OUTPUT);     // strip #3
    pinMode(8, OUTPUT);     // strip #4
    pinMode(6, OUTPUT);     // strip #5
    pinMode(20, OUTPUT);    // strip #6
    pinMode(21, OUTPUT);    // strip #7
    pinMode(5, OUTPUT);     // strip #8

    // create the two waveforms for WS2811 low and high bits
    analogWriteFrequency(3, timings.frequency);
    analogWriteFrequency(4, timings.frequency);
    analogWrite(3, timings.t0h);
    analogWrite(4, timings.t1h);

    // pin 16 triggers DMA(port B) on rising edge (configure for pin 3's waveform)
    CORE_PIN16_CONFIG = PORT_PCR_IRQC(1) | PORT_PCR_MUX(3);
    pinMode(3, INPUT_PULLUP); // pin 3 no longer needed

    // pin 15 triggers DMA(port C) on falling edge of low duty waveform
    // pin 15 and 16 must be connected by the user: 16 is output, 15 is input
    pinMode(15, INPUT);
    CORE_PIN15_CONFIG = PORT_PCR_IRQC(2) | PORT_PCR_MUX(1);

    // pin 4 triggers DMA(port A) on falling edge of high duty waveform
    CORE_PIN4_CONFIG = PORT_PCR_IRQC(2) | PORT_PCR_MUX(3);

    // enable clocks to the DMA controller and DMAMUX
    SIM_SCGC7 |= SIM_SCGC7_DMA;
    SIM_SCGC6 |= SIM_SCGC6_DMAMUX;
    DMA_CR = 0;
    DMA_ERQ = 0;

    // DMA channel #1 sets WS2811 high at the beginning of each cycle
    DMA_TCD1_SADDR = &ONES;
    DMA_TCD1_SOFF = 0;
    DMA_TCD1_ATTR = DMA_TCD_ATTR_SSIZE(0) | DMA_TCD_ATTR_DSIZE(0);
    DMA_TCD1_NBYTES_MLNO = 1;
    DMA_TCD1_SLAST = 0;
    DMA_TCD1_DADDR = &GPIOD_PSOR;
    DMA_TCD1_DOFF = 0;
    DMA_TCD1_CITER_ELINKNO = bufsize;
    DMA_TCD1_DLASTSGA = 0;
    DMA_TCD1_CSR = DMA_TCD_CSR_DREQ;
    DMA_TCD1_BITER_ELINKNO = bufsize;

    // DMA channel #2 writes the pixel data at 20% of the cycle
    DMA_TCD2_SOFF = 1;
    DMA_TCD2_ATTR = DMA_TCD_ATTR_SSIZE(0) | DMA_TCD_ATTR_DSIZE(0);
    DMA_TCD2_NBYTES_MLNO = 1;
    DMA_TCD2_SLAST = -bufsize;
    DMA_TCD2_DADDR = &GPIOD_PDOR;
    DMA_TCD2_DOFF = 0;
    DMA_TCD2_CITER_ELINKNO = bufsize;
    DMA_TCD2_DLASTSGA = 0;
    DMA_TCD2_CSR = DMA_TCD_CSR_DREQ;
    DMA_TCD2_BITER_ELINKNO = bufsize;

    // DMA channel #3 clear all the pins low at 48% of the cycle
    DMA_TCD3_SADDR = &ONES;
    DMA_TCD3_SOFF = 0;
    DMA_TCD3_ATTR = DMA_TCD_ATTR_SSIZE(0) | DMA_TCD_ATTR_DSIZE(0);
    DMA_TCD3_NBYTES_MLNO = 1;
    DMA_TCD3_SLAST = 0;
    DMA_TCD3_DADDR = &GPIOD_PCOR;
    DMA_TCD3_DOFF = 0;
    DMA_TCD3_CITER_ELINKNO = bufsize;
    DMA_TCD3_DLASTSGA = 0;
    DMA_TCD3_CSR = DMA_TCD_CSR_DREQ | DMA_TCD_CSR_INTMAJOR;
    DMA_TCD3_BITER_ELINKNO = bufsize;

    // route the edge detect interrupts to trigger the 3 channels
    DMAMUX0_CHCFG1 = 0;
    DMAMUX0_CHCFG1 = DMAMUX_SOURCE_PORTB | DMAMUX_ENABLE;
    DMAMUX0_CHCFG2 = 0;
    DMAMUX0_CHCFG2 = DMAMUX_SOURCE_PORTC | DMAMUX_ENABLE;
    DMAMUX0_CHCFG3 = 0;
    DMAMUX0_CHCFG3 = DMAMUX_SOURCE_PORTA | DMAMUX_ENABLE;

    // enable a done interrupts when channel #3 completes
    NVIC_ENABLE_IRQ(IRQ_DMA_CH3);
    //pinMode(1, OUTPUT); // testing: oscilloscope trigger
}

void write(const uint8_t* buffer) {
    // wait for any prior DMA operation
    while (writeInProgress);

    // wait for LED reset
    uint64_t now;
    do {
        now = micros64();
        // for some weird reason, the compiler may optimize this code in a way that exits
        // exit early unless we rule out now <= writeFinishedAt even though I verified that
        // the clock is monotonic
    } while (now <= writeFinishedAt || now - writeFinishedAt < timings.resetInterval);

    DMA_TCD2_SADDR = buffer;

    // ok to start, but we must be very careful to begin
    // without any prior 3 x 800kHz DMA requests pending
    uint32_t sc = FTM1_SC;
    uint32_t cv = FTM1_C1V;
    __disable_irq();
    // CAUTION: this code is timing critical.  Any editing should be
    // tested by verifying the oscilloscope trigger pulse at the end
    // always occurs while both waveforms are still low.  Simply
    // counting CPU cycles does not take into account other complex
    // factors, like flash cache misses and bus arbitration from USB
    // or other DMA.  Testing should be done with the oscilloscope
    // display set at infinite persistence and a variety of other I/O
    // performed to create realistic bus usage.  Even then, you really
    // should not mess with this timing critical code!
    writeInProgress = true;
    while (FTM1_CNT <= cv) ; 
    while (FTM1_CNT > cv) ; // wait for beginning of an 800 kHz cycle
    while (FTM1_CNT < cv) ;
    FTM1_SC = sc & 0xE7;    // stop FTM1 timer (hopefully before it rolls over)
    //digitalWriteFast(1, HIGH); // oscilloscope trigger
    PORTB_ISFR = (1<<0);    // clear any prior rising edge
    PORTC_ISFR = (1<<0);    // clear any prior low duty falling edge
    PORTA_ISFR = (1<<13);   // clear any prior high duty falling edge
    DMA_ERQ = 0x0E;     // enable all 3 DMA channels
    FTM1_SC = sc;       // restart FTM1 timer
    //digitalWriteFast(1, LOW);
    __enable_irq();
}

} // namespace led

extern "C" void dma_ch3_isr() {
    DMA_CINT = 3;
    led::writeFinishedAt = micros64();
    led::writeInProgress = false;
}
