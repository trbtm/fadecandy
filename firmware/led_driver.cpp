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

namespace glimmer::led {

const uint8_t ONES = 0xFF;

FLEXRAM_DATA volatile bool writeInProgress = false;
FLEXRAM_DATA volatile uint64_t writeFinishedAt = 0;
FLEXRAM_DATA uint32_t resetInterval;

// When set to 1, sends oscilloscope trigger pulses using the TDO pin.
#define TRACE 0

inline void trace(bool bit) {
#if TRACE
    if (bit) {
        GPIOA_PSOR |= 0x04;
    } else {
        GPIOA_PCOR |= 0x04;
    }
#endif
}

inline void initTrace() {
#if TRACE
    GPIOA_PCOR |= 0x04;
    GPIOA_PDDR |= 0x04;
    PORTA_PCR2 = PORT_PCR_MUX(1);
#endif
}

bool init(size_t ledsPerStrip, const Timings& timings) {
    // Validate parameters.
    if (ledsPerStrip == 0 || !validateTimings(timings)) return false;

    // wait for all prior DMA operations to complete
    while (writeInProgress);

    const size_t bufsize = bufferSize(ledsPerStrip);
    resetInterval = timings.resetInterval;

    initTrace();

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
    //FTM1_CONF = 0; // default configuration
    //FTM1_FMS = 0; // default fault mode status
    FTM1_MODE = FTM_MODE_WPDIS | FTM_MODE_FTMEN; // enable timer
    FTM1_SC = 0; // stop the clock
    // FTM1_CNTIN = 0; // default counter starts at zero
    FTM1_CNT = 0; // reset counter to initial value
    uint32_t mod = (F_BUS + timings.frequency / 2) / timings.frequency;
    FTM1_MOD = mod - 1; // set timer modulus for frequency, rounded up
    FTM1_C0V = (mod * timings.t0h) >> 8; // set low bit phase
    FTM1_C1V = (mod * timings.t1h) >> 8; // set high bit phase
    FTM1_C0SC = 0x69; // start high and become low on match, trigger DMA
    FTM1_C1SC = 0x69; // start high and become low on match, trigger DMA

    // trigger DMA request on rising edge of channel 0 via PORTB (pin 16)
    PORTB_PCR0 = PORT_PCR_IRQC(1) | PORT_PCR_MUX(3);

    // enable clocks to the DMA controller and DMAMUX
    SIM_SCGC7 |= SIM_SCGC7_DMA;
    SIM_SCGC6 |= SIM_SCGC6_DMAMUX;
    DMA_CR = 0;
    DMA_ERQ = 0;
    // DMA_DCHPRI0 = 0; // default priority
    // DMA_DCHPRI1 = 1; // default priority
    // DMA_DCHPRI2 = 2; // default priority
    // DMA_DCHPRI3 = 3; // default priority

    // DMA channel #3 (highest priority) sets WS2811 high at the beginning of each cycle
    DMA_TCD3_SADDR = &ONES;
    DMA_TCD3_SOFF = 0;
    DMA_TCD3_ATTR = DMA_TCD_ATTR_SSIZE(0) | DMA_TCD_ATTR_DSIZE(0);
    DMA_TCD3_NBYTES_MLNO = 1;
    DMA_TCD3_SLAST = 0;
    DMA_TCD3_DADDR = &GPIOD_PSOR;
    DMA_TCD3_DOFF = 0;
    DMA_TCD3_CITER_ELINKNO = bufsize;
    DMA_TCD3_DLASTSGA = 0;
    DMA_TCD3_CSR = DMA_TCD_CSR_DREQ;
    DMA_TCD3_BITER_ELINKNO = bufsize;

    // DMA channel #2 (second priority) writes the pixel data at 23% of the cycle
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

    // DMA channel #1 (third priority) clear all the pins low at 69% of the cycle
    DMA_TCD1_SADDR = &ONES;
    DMA_TCD1_SOFF = 0;
    DMA_TCD1_ATTR = DMA_TCD_ATTR_SSIZE(0) | DMA_TCD_ATTR_DSIZE(0);
    DMA_TCD1_NBYTES_MLNO = 1;
    DMA_TCD1_SLAST = 0;
    DMA_TCD1_DADDR = &GPIOD_PCOR;
    DMA_TCD1_DOFF = 0;
    DMA_TCD1_CITER_ELINKNO = bufsize;
    DMA_TCD1_DLASTSGA = 0;
    DMA_TCD1_CSR = DMA_TCD_CSR_DREQ | DMA_TCD_CSR_INTMAJOR;
    DMA_TCD1_BITER_ELINKNO = bufsize;

    // route the edge detect interrupts to trigger the 3 channels
    DMAMUX0_CHCFG3 = 0;
    DMAMUX0_CHCFG3 = DMAMUX_SOURCE_PORTB | DMAMUX_ENABLE; // trigger on rising edge of channel 0 via PORTB
    DMAMUX0_CHCFG2 = 0;
    DMAMUX0_CHCFG2 = DMAMUX_SOURCE_FTM1_CH0 | DMAMUX_ENABLE; // trigger on falling edge of channel 0
    DMAMUX0_CHCFG1 = 0;
    DMAMUX0_CHCFG1 = DMAMUX_SOURCE_FTM1_CH1 | DMAMUX_ENABLE; // trigger on falling edge of channel 1

    // enable a done interrupts when channel #1 completes
    NVIC_ENABLE_IRQ(IRQ_DMA_CH1);
    FTM1_SC = FTM_SC_CLKS(1) | FTM_SC_PS(0); // start the timer
    return true;
}

void write(const uint8_t* buffer) {
    // Wait for all prior DMA operations to complete.
    while (writeInProgress);

    // Wait for LED reset to complete.
    uint64_t now;
    do {
        now = micros64();
    } while (now < writeFinishedAt + resetInterval);

    // Start the next DMA transfer.
    writeInProgress = true;
    DMA_TCD2_SADDR = buffer;

    trace(true);
    __disable_irq();

    // Reset timer channel 0 interrupt and DMA trigger to prevent premature triggering
    // when DMA requests are re-enabled below.
    FTM1_C0SC = 0x28;

    // Wait for timer channel 1 to elapse twice.
    while (FTM1_C1SC & 0x80) FTM1_C1SC = 0x28;
    while (!(FTM1_C1SC & 0x80));

    // Immediately clear pending timer interrupts and enable DMA triggers.
    // The order of these operations is critical to ensure the correct timing.
    // DMA channel 3 must be re-enabled in the interval between timer channel 1
    // elapsing and the next cycle beginning.
    PORTB_ISFR = 1 << 0; // clear interrupt that will trigger DMA channel 3 on the next cycle
    DMA_ERQ = 0x0e; // enable requests for DMA channel 3, 2, and 1 in that order
    FTM1_C0SC = 0x69; // restore DMA trigger for DMA channel 2
    FTM1_C1SC = 0x69; // restore DMA trigger for DMA channel 1 

    __enable_irq();
    trace(false);
}

} // namespace glimmer::led

extern "C" void dma_ch1_isr() {
    DMA_CINT = 1;
    glimmer::led::writeFinishedAt = micros64();
    glimmer::led::writeInProgress = false;
}
