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
#include <stdint.h>
#include <math.h>

#include <algorithm>
#include <new>
#include <type_traits>
#include <utility>

#include "arm/arm_math.h"
#include "config.h"
#include "debug.h"
#include "hw/mk20dx128.h"
#include "hw/HardwareSerial.h"
#include "hw/pins_arduino.h"
#include "hw/core_pins.h"
#include "hw/usb_dev.h"
#include "led_driver.h"
#include "render.h"
#include "time.h"

void operator delete(void*, size_t) {}

namespace app {

// Double-buffered DMA memory for raw bit planes of output
using OutputBuffer = uint8_t[led::bufferSize(CONFIG_MAX_LEDS_PER_STRIP)];
__attribute__ ((section(".dmabuffers"), used))
OutputBuffer outputBuffers[2];
uint8_t* frontOutputBuffer = outputBuffers[0];
uint8_t* backOutputBuffer = outputBuffers[1];

// The renderer for presenting incoming frames.
FLEXRAM_DATA std::aligned_union_t<0, render::RendererHolder> rendererHolderStorage;
render::RendererHolder* rendererHolder;

// Configuration flags remotely set by the host.
volatile uint8_t configFlags = 0;

// Set to true if any USB packets were handled since the last loop iteration.
// Used to show activity on the bus.
volatile bool handledUsbPacketsSinceLastLoop = false;

// Set to true if there's a new frame buffer pending swap.
volatile bool receivedNewFrameBufferSinceLastLoop = false;

// USB protocol definitions
constexpr uint8_t PACKET_HEADER_CONTROL_MASK = 0x80;
constexpr uint8_t PACKET_HEADER_INDEX_MASK = 0x7f;
constexpr uint8_t PACKET_CONTROL_CONFIG = PACKET_HEADER_CONTROL_MASK | 0x00;

// Configuration flags set by the host.
constexpr uint8_t CFLAG_NO_DITHERING      = (1 << 0);
// constexpr uint8_t CFLAG_NO_INTERPOLATION  = (1 << 1);
constexpr uint8_t CFLAG_NO_ACTIVITY_LED   = (1 << 2);
constexpr uint8_t CFLAG_LED_CONTROL       = (1 << 3);

// Called from an interrupt context so we need to take care with synchronization.
// Must either take ownership of the packet or free it.
// Unrecognized packets are ignored to support protocol expansion.
bool handleUsbRxIrq(usb_packet_t* packet, size_t len) {
    const uint8_t header = packet->buf[0];
    if (header & PACKET_HEADER_CONTROL_MASK) {
        // Handle control requests.
        switch (header) {
            case PACKET_CONTROL_CONFIG:
                configFlags = packet->buf[1];
                break;
        }
        usb_free(packet);
    } else {
        // Handle frame buffer image data.
        const uint8_t index = header & PACKET_HEADER_INDEX_MASK;

        // Framebuffer updates are synchronized; don't accept any packets until
        // a new frame is swapped in.
        if (receivedNewFrameBufferSinceLastLoop) return false;

        // Take ownership of the packet.
        if (rendererHolder->get()->storeFramePacket(index, packet, len)) {
            receivedNewFrameBufferSinceLastLoop = true;
        }
    }

    handledUsbPacketsSinceLastLoop = true;
    return true;
}

void configure(render::RendererId id, render::RendererOptions options) {
    if (rendererHolder->init(id, std::move(options))) {
        led::init(options.ledsPerStrip);
    } else {
        serial_print("config failed!\r\n");
    }
}

void setup() {
    // Announce firmware version
    serial_begin(BAUD2DIV(115200));
    serial_print("Fadecandy v" CONFIG_DEVICE_VER_STRING "\r\n");

    // Configure peripherals.
    pinMode(LED_BUILTIN, OUTPUT);

    rendererHolder = new (&rendererHolderStorage) render::RendererHolder();
    configure(
            render::RendererId{render::ColorFormat::R8G8B8, render::DitherMode::TEMPORAL, render::InterpolateMode::LINEAR},
            render::RendererOptions{6, 120, 3});
}

uint64_t measureStart = 0;
uint32_t measureFrames = 0;

void loop() {
    const uint8_t currentFlags = configFlags;

    // Render the next output buffer and write it out using DMA.
    rendererHolder->get()->render(backOutputBuffer);
    std::swap(frontOutputBuffer, backOutputBuffer);
    led::write(frontOutputBuffer);

    // Synchronize with the interrupt handler and flip buffers if a new frame was received.
    if (receivedNewFrameBufferSinceLastLoop) {
        rendererHolder->get()->advanceFrame();
        receivedNewFrameBufferSinceLastLoop = false;
        perf_receivedKeyframeCounter++;
        usb_rx_resume();
    }

    // Update the activity LED activity.
    if (currentFlags & CFLAG_NO_ACTIVITY_LED) {
        // LED under manual control
        digitalWriteFast(LED_BUILTIN, currentFlags & CFLAG_LED_CONTROL);
    } else {
        // Use the built-in LED as a USB activity indicator.
        digitalWriteFast(LED_BUILTIN, handledUsbPacketsSinceLastLoop);
        handledUsbPacketsSinceLastLoop = false;
    }

    // Performance counter, for monitoring frame rate externally
    perf_frameCounter++;

    uint64_t now = micros64();
    measureFrames++;
    if (now - measureStart > 10000000) {
        serial_pdec32(measureFrames);
        serial_print(" per 10 sec\r\n");
        measureStart = now;
        measureFrames = 0;
    }
}

} // namespace app

// USB packet interrupt handler. Invoked by the ISR dispatch code in usb_dev.c
extern "C" int usb_rx_handler(usb_packet_t *packet, size_t len) {
    return app::handleUsbRxIrq(packet, len);
}

// Reserved RAM area for signalling entry to bootloader
extern uint32_t boot_token;

extern "C" int main() {
    initSysticks();

    // Run application until asked to reboot into the bootloader
    app::setup();
    while (usb_dfu_state == DFU_appIDLE) {
        watchdog_refresh();
        app::loop();
    }

    // Reboot to the Fadecandy Bootloader
    boot_token = 0x74624346;

    // Short delay to allow the host to receive the response to DFU_DETACH.
    uint64_t deadline = millis64() + 10;
    while (millis64() < deadline) {
        watchdog_refresh();
    }

    // Detach from USB, and use the watchdog to time out a 10ms USB disconnect.
    __disable_irq();
    USB0_CONTROL = 0;
    crash("DFU entry");
}
