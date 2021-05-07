/*
 * Fadecandy Firmware
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

#include <stddef.h>
#include <math.h>
#include <algorithm>

#include "led_driver.h"
#include "arm/arm_math.h"
#include "fc_usb.h"
#include "fc_defs.h"
#include "hw/HardwareSerial.h"
#include "hw/pins_arduino.h"
#include "hw/core_pins.h"

// USB data buffers
static fcBuffers buffers;
fcLinearLUT fcBuffers::lutCurrent;

// Double-buffered DMA memory for raw bit planes of output
#define DMAMEM __attribute__ ((section(".dmabuffers"), used))
static DMAMEM uint8_t ledBuffer[2][led::bufferSize(LEDS_PER_STRIP)];

// Reserved RAM area for signalling entry to bootloader
extern uint32_t boot_token;

static void dfu_reboot()
{
    // Reboot to the Fadecandy Bootloader
    boot_token = 0x74624346;

    // Short delay to allow the host to receive the response to DFU_DETACH.
    uint32_t deadline = millis() + 10;
    while (millis() < deadline) {
        watchdog_refresh();
    }

    // Detach from USB, and use the watchdog to time out a 10ms USB disconnect.
    __disable_irq();
    USB0_CONTROL = 0;
    while (1);
}

extern "C" int usb_rx_handler(usb_packet_t *packet)
{
    // USB packet interrupt handler. Invoked by the ISR dispatch code in usb_dev.c
    return buffers.handleUSB(packet);
}

extern "C" int main()
{
    pinMode(LED_BUILTIN, OUTPUT);
    led::init(LEDS_PER_STRIP);

    // Announce firmware version
    serial_begin(BAUD2DIV(115200));
    serial_print("Fadecandy v" DEVICE_VER_STRING "\r\n");

    // Application main loop
    uint8_t* frontBuffer = ledBuffer[0];
    uint8_t* backBuffer = ledBuffer[1];

    while (usb_dfu_state == DFU_appIDLE) {
        watchdog_refresh();

        uint8_t* buffer = backBuffer;
        for (int i = 0; i < LEDS_PER_STRIP; ++i) {
            led::pushPixels<LED_STRIPS>(&buffer, [i](auto strip) [[gnu::always_inline]] {
                const uint8_t* color = buffers.fbNext->pixel(i + LEDS_PER_STRIP * strip);
                return (color[1] << 16) | (color[0] << 8) | color[2]; // GRB order
            });
        }

        // Start sending the next frame over DMA
        while (!led::ready());
        led::write(backBuffer);
        std::swap(frontBuffer, backBuffer);

        // We can switch to the next frame's buffer now.
        buffers.finalizeFrame(false);

        // Performance counter, for monitoring frame rate externally
        perf_frameCounter++;
    }

    // Reboot into DFU bootloader
    dfu_reboot();
}
