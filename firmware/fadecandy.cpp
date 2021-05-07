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

#include "config.h"
#include "arm/arm_math.h"
#include "hw/HardwareSerial.h"
#include "hw/pins_arduino.h"
#include "hw/core_pins.h"
#include "hw/usb_dev.h"
#include "led_driver.h"

namespace app {

// A reference to a pixel.
class PixelRef {
    const uint8_t* data_;

public:
    PixelRef(const uint8_t* data) : data_(data) {}

    inline uint8_t r() const { return data_[0]; }
    inline uint8_t g() const { return data_[1]; }
    inline uint8_t b() const { return data_[2]; }
};

// A zero-copy frame buffer holds references to received USB packets.
class FrameBuffer {
    usb_packet_t* packets_[PACKETS_PER_FRAME];

public:
    // Allocate packets. They'll have zero'ed contents initially.
    FrameBuffer() {
        for (size_t i = 0; i < PACKETS_PER_FRAME; ++i) {
            usb_packet_t* p = packets_[i] = usb_malloc();
            for (size_t j = 0; j < sizeof(p->buf); ++j)
                p->buf[j] = 0;
        }
    }

    void storePacket(size_t index, usb_packet_t* packet) {
        if (index < PACKETS_PER_FRAME)
            std::swap(packet, packets_[index]);
        usb_free(packet);
    }

    [[gnu::always_inline]] PixelRef operator[](size_t index) const {
        return PixelRef(&packets_[index / PIXELS_PER_PACKET]->buf[2 + (index % PIXELS_PER_PACKET) * 3]);
    }
};

// Holds a pair of buffers for double-buffering.
template <typename T>
class SwapChain {
    T* front_;
    T* back_;

public:
    SwapChain(T* front, T* back) : front_(front), back_(back) {}

    const T& front() const { return *front_; }
    T& back() { return *back_; }

    void flip() { std::swap(front_, back_); }
};

// Double-buffered frame buffers for incoming video.
FrameBuffer frameBuffers[2];
SwapChain<FrameBuffer> frameSwapChain(&frameBuffers[0], &frameBuffers[1]);

// Double-buffered DMA memory for raw bit planes of output
using OutputBuffer = uint8_t[led::bufferSize(LEDS_PER_STRIP)];
#define DMAMEM __attribute__ ((section(".dmabuffers"), used))
DMAMEM OutputBuffer outputBuffers[2];
SwapChain<OutputBuffer> outputSwapChain(&outputBuffers[0], &outputBuffers[1]);

// Configuration flags remotely set by the host.
volatile uint8_t configFlags = 0;

// Set to true if any USB packets were handled since the last loop iteration.
// Used to show activity on the bus.
volatile bool handledUsbPacketsSinceLastLoop = false;

// Set to true if there's a new frame buffer pending swap.
volatile bool receivedNewFrameBufferSinceLastLoop = false;

// USB protocol definitions
constexpr uint8_t PACKET_TYPE_MASK = 0xc0;
constexpr uint8_t PACKET_TYPE_FRAMEBUFFER = 0x00;
constexpr uint8_t PACKET_TYPE_CONFIG = 0x80;
// constexpr uint8_t PACKET_TYPE_LUT = 0x40;
constexpr uint8_t PACKET_FLAG_FINAL = 0x20;

// Configuration flags set by the host.
// constexpr uint8_t CFLAG_NO_DITHERING      = (1 << 0);
// constexpr uint8_t CFLAG_NO_INTERPOLATION  = (1 << 1);
constexpr uint8_t CFLAG_NO_ACTIVITY_LED   = (1 << 2);
constexpr uint8_t CFLAG_LED_CONTROL       = (1 << 3);

// Called from an interrupt context so we need to take care with synchronization.
// Must either take ownership of the packet or free it.
// Unrecognized packets are ignored to support protocol expansion.
bool handleUsbRxIrq(usb_packet_t* packet) {
    uint8_t control = packet->buf[0];
    switch (control & PACKET_TYPE_MASK) {
        case PACKET_TYPE_FRAMEBUFFER:
            // Framebuffer updates are synchronized; don't accept any packets until
            // a new frame is swapped in.
            if (receivedNewFrameBufferSinceLastLoop) return false;

            frameSwapChain.back().storePacket(packet->buf[1], packet);

            if (control & PACKET_FLAG_FINAL) {
                receivedNewFrameBufferSinceLastLoop = true;
            }
            break;

        case PACKET_TYPE_CONFIG:
            // Config changes take effect immediately.
            configFlags = packet->buf[1];
            usb_free(packet);
            break;

        default:
            usb_free(packet);
            break;
    }

    handledUsbPacketsSinceLastLoop = true;
    return true;
}

void setup() {
    // Announce firmware version
    serial_begin(BAUD2DIV(115200));
    serial_print("Fadecandy v" DEVICE_VER_STRING "\r\n");

    // Configure peripherals.
    pinMode(LED_BUILTIN, OUTPUT);
    led::init(LEDS_PER_STRIP);
}

void loop() {
    // Render the next output buffer.
    uint8_t* out = outputSwapChain.back();
    for (int i = 0; i < LEDS_PER_STRIP; ++i) {
        led::pushPixels<LED_STRIPS>(&out, [i](auto strip) [[gnu::always_inline]] {
            const PixelRef pixel = frameSwapChain.front()[i + LEDS_PER_STRIP * strip];
            return (pixel.g() << 16) | (pixel.r() << 8) | pixel.b(); // GRB order
        });
    }

    // Start sending the next output buffer to the LEDs using DMA.
    while (!led::ready());
    led::write(outputSwapChain.back());
    outputSwapChain.flip();

    // Synchronize with the interrupt handler and flip buffers if a new frame was received.
    if (receivedNewFrameBufferSinceLastLoop) {
        frameSwapChain.flip();
        receivedNewFrameBufferSinceLastLoop = false;
        perf_receivedKeyframeCounter++;
        usb_rx_resume();
    }

    // Update the activity LED activity.
    const uint8_t currentFlags = configFlags;
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
}

} // namespace app

// USB packet interrupt handler. Invoked by the ISR dispatch code in usb_dev.c
extern "C" int usb_rx_handler(usb_packet_t *packet) {
    return app::handleUsbRxIrq(packet);
}

// Reserved RAM area for signalling entry to bootloader
extern uint32_t boot_token;

extern "C" int main() {
    // Run application until asked to reboot into the bootloader
    app::setup();
    while (usb_dfu_state == DFU_appIDLE) {
        watchdog_refresh();
        app::loop();
    }

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
