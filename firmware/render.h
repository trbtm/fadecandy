/*
 * Renders colors for display to the LEDs.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#include <algorithm>
#include <type_traits>
#include <utility>

#include "config.h"
#include "arm/core_cmInstr.h"
#include "hw/usb_mem.h"
#include "time.h"

namespace render {

// Color representations.
enum class ColorFormat {
    R8G8B8,     // 24-bit color
    R10G10B10,  // 30-bit color
    R11G11B11,  // 33-bit color, blue LSBs are packed into a separate word
};

// Gets the number of bits per color component of a given color format.
constexpr size_t bitsPerComponent(ColorFormat fmt) {
    return fmt == ColorFormat::R8G8B8 ? 8 :
            fmt == ColorFormat::R10G10B10 ? 10 : 11;
}

// Computes the number of pixels that can be stored in a USB packet, taking
// into account protocol overhead (1 header byte).
constexpr size_t pixelsPerPacket(ColorFormat fmt) {
    return fmt == ColorFormat::R8G8B8 ? 21 : 15;
}

// Computes the number of packets per frame.
constexpr size_t packetsPerFrame(size_t ledStrips, size_t ledsPerStrip, ColorFormat fmt) {
    size_t ppp = pixelsPerPacket(fmt);
    return (ledStrips * ledsPerStrip + ppp - 1) / ppp;
}

// A color with a given number of bits per color component.
template <size_t Nbpc>
struct Color {
    static constexpr size_t bpc = Nbpc;
    unsigned r, g, b;
};

// Reads pixels from frames encoded in a given color format.
template <ColorFormat fmt>
struct FrameAccessor;

template <>
struct FrameAccessor<ColorFormat::R8G8B8> {
    using ColorOut = Color<8>;

    [[gnu::always_inline]] static ColorOut getPixel(const usb_packet_t* const * packets,
            size_t packetIndex, size_t packetOffset) {
        const uint8_t* data = &packets[packetIndex]->buf[1 + packetOffset * 3];
        return ColorOut{ data[0], data[1], data[2] };
    }
};

template <>
struct FrameAccessor<ColorFormat::R10G10B10> {
    using ColorOut = Color<10>;

    [[gnu::always_inline]] static ColorOut getPixel(const usb_packet_t* const * packets,
            size_t packetIndex, size_t packetOffset) {
        const uint32_t* data = reinterpret_cast<const uint32_t*>(&packets[packetIndex]->buf);
        const uint32_t packed = data[packetOffset + 1];
        return ColorOut{
            (packed >> 20) & 0x3ff,
            (packed >> 10) & 0x3ff,
            packed & 0x3ff
        };
    }
};

template <>
struct FrameAccessor<ColorFormat::R11G11B11> {
    using ColorOut = Color<11>;

    [[gnu::always_inline]] static ColorOut getPixel(const usb_packet_t* const * packets,
            size_t packetIndex, size_t packetOffset) {
        const uint32_t* data = reinterpret_cast<const uint32_t*>(&packets[packetIndex]->buf);
        const uint32_t packed = data[packetOffset + 1];
        return ColorOut{
            packed >> 21,
            (packed >> 10) & 0x7ff,
            ((packed & 0x3ff) << 1) | ((data[0] >> packetOffset) & 1),
        };
    }
};

// A frame buffer consisting of USB packets for zero-copy access.
template <ColorFormat fmt, size_t maxPackets>
struct FrameBuffer {
    using ColorOut = typename FrameAccessor<fmt>::ColorOut;

    usb_packet_t* packets[maxPackets];
    uint64_t time = 0;

    [[gnu::always_inline]] ColorOut getPixel(size_t packetIndex, size_t packetOffset) const {
        return FrameAccessor<fmt>::getPixel(packets, packetIndex, packetOffset);
    }

    void alloc(size_t packetsPerFrame) {
        for (size_t j = 0; j < packetsPerFrame; ++j) {
            usb_clear_packet(packets[j] = usb_malloc());
        }
    }

    void free(size_t packetsPerFrame) {
        for (size_t j = 0; j < packetsPerFrame; ++j) {
            usb_free(packets[j]);
        }
    }

    bool storeFramePacket(size_t packetsPerFrame, size_t packetIndex, usb_packet_t* packet, size_t len) {
        // TODO: Validate the packet length too, note that the last packet may be smaller
        if (packetIndex >= packetsPerFrame) {
            usb_free(packet);
            return false;
        }

        std::swap(packet, packets[packetIndex]);
        usb_free(packet);
        if(packetIndex == packetsPerFrame - 1) {
            time = micros64(); // last packet
            return true;
        }
        return false;
    }
};

// Outputs a pixel in GRB format.
// Could be specialized in the future to support additional output formats.
template <typename ColorIn>
class OutputOp {
    template <size_t bpc, size_t shift>
    static inline uint32_t extract8(unsigned x) {
        static_assert(bpc >= 8);
        static_assert(shift == 0 || shift == 8 || shift == 16);
        return (x >> (bpc - 8)) << shift;
    }

public:
    [[gnu::always_inline]] uint32_t operator()(ColorIn color) const {
        return extract8<ColorIn::bpc, 16>(color.g) |
                extract8<ColorIn::bpc, 8>(color.r) |
                extract8<ColorIn::bpc, 0>(color.b);
    }
};

// The type of dither to apply to each pixel.
enum class DitherMode {
    NONE,
    TEMPORAL,
};

// Dithers a pixel (or not) depending on the mode.
template <DitherMode mode, typename ColorIn>
class DitherOp {
public:
    using ColorOut = ColorIn;

    DitherOp(size_t maxDitherBits) {}

    [[gnu::always_inline]] ColorOut operator()(ColorIn color) const { return color; }

    void advancePattern() {}
};

template <typename ColorIn>
class DitherOp<DitherMode::TEMPORAL, ColorIn> {
    static_assert(ColorIn::bpc > 8);

    const unsigned shift_, zeroes_;
    unsigned noise_ = 0;

public:
    using ColorOut = ColorIn;

    DitherOp(size_t maxDitherBits) :
            shift_(32 - std::min(ColorIn::bpc - 8, maxDitherBits)),
            zeroes_(ColorIn::bpc - 8 - std::min(ColorIn::bpc - 8, maxDitherBits)) {}

    [[gnu::always_inline]] ColorOut operator()(ColorIn color) const {
        return ColorOut{ color.r + noise_, color.g + noise_, color.b + noise_ };
    }

    void advancePattern() {
        // Produces a butterfly sequence with a certain number of bits such as:
        // - 0, 1
        // - 0, 2, 1, 3
        // - 0, 4, 2, 6, 1, 5, 3, 7
        noise_ >>= zeroes_;
        noise_ = (__RBIT(__RBIT(noise_ << shift_) + 1) >> shift_);
        noise_ <<= zeroes_;
    }
};

// The type of interpolation to apply between frames.
enum InterpolateMode {
    NONE,
    LINEAR,
};

// Interpolates a pixel (or not) depending on the mode.
template <InterpolateMode mode, typename ColorIn>
class InterpolateOp {
public:
    using ColorOut = ColorIn;
 
    [[gnu::always_inline]] ColorOut operator()(ColorIn front, ColorIn prior) const { return front; }

    void setCoeffs(uint64_t now, uint64_t frontTime, uint64_t priorTime) {}
};

template <typename ColorIn>
class InterpolateOp<InterpolateMode::LINEAR, ColorIn> {
    uint32_t alpha_ = 256, beta_ = 0;

    [[gnu::always_inline]] unsigned lerp(unsigned front, unsigned prior) const {
        return front * alpha_ + prior * beta_;
    }

public:
    using ColorOut = Color<ColorIn::bpc + 8>;

    [[gnu::always_inline]] ColorOut operator()(ColorIn front, ColorIn prior) const {
        return ColorOut{ lerp(front.r, prior.r), lerp(front.g, prior.g), lerp(front.b, prior.b) };
    }

    void setCoeffs(uint64_t now, uint64_t frontTime, uint64_t priorTime) {
        uint64_t period = frontTime - priorTime;
        uint64_t advance = now - frontTime;
        // avoid unnecessary 64-bit multiply and divide
        // this is good enough for 16 seconds of interpolation
        if (advance < 0x1000000ULL && period <= 0x1000000ULL) {
            uint32_t period32 = static_cast<uint32_t>(period);
            uint32_t advance32 = static_cast<uint32_t>(advance);
            if (advance32 < period32) {
                alpha_ = advance32 * 256 / period32;
                beta_ = 256 - alpha_;
                return;
            }
        }
        alpha_ = 256;
        beta_ = 0;
    }
};

// Configuration options for a rendering algorithm.
struct RendererOptions {
    // Number of LED strips.  Between 1 and 8 inclusively.
    const size_t ledStrips;

    // Number of LEDs per strip.
    const size_t ledsPerStrip;

    // Maximum number of color bits to dither.  Typically 0 to 3.
    // This value determines the period at which the temporal dither will repeat itself.
    // For example, a 3-bit dither cycles every 8 frames.  If the refresh rate is too
    // low then a long cycle may seem to flicker and a smaller bit depth should be used.
    //
    // Some values:
    // - 2: 60 Hz cycle for 120 pixels at 800 kHz (very smooth)
    // - 3: 30 Hz cycle for 120 pixels at 800 kHz (flickers)
    //      42 Hz cycle for 120 pixels at 1000 kHz with aggressive timings (somewhat smooth)
    const size_t maxDitherBits;
};

// Renders video frames to an output buffer.
// The default implementation discards all frames and does not render anything.
// Subclasses are template specialized to implement various modes efficiently
// trading increased code size for a reduction in the number of branches.
class Renderer {
public:
    virtual ~Renderer() {}

    // Stores a USB packet containing part of the next frame to be rendered.
    // This function may be called from an interrupt context.
    // Returns true if the frame buffer is ready to be rendered (this is the last packet).
    virtual bool storeFramePacket(size_t packetIndex, usb_packet_t* packet, size_t len) {
        usb_free(packet);
        return false;
    }

    // Flips frame buffers.  Must be called before rendering the new frame.
    virtual void advanceFrame() {}

    // Renders the frame to an output buffer for DMA.
    virtual void render(uint8_t* outputBuffer) {}

    // Returns true if a renderer with the given options can be created,
    // false if the options are invalid or if there is not enough memory.
    static bool canInstantiate(const RendererOptions& options) { return true; }
};

template <ColorFormat fmt, size_t numBuffers, size_t maxPackets>
class BufferedRenderer : public Renderer {
    static_assert(numBuffers > 0);
    static_assert(numBuffers <= config::maxFrameBuffers);

protected:
    using Super = Renderer;
    using FrameBuffer = render::FrameBuffer<fmt, maxPackets>;

    static constexpr size_t pixelsPerPacket_ = pixelsPerPacket(fmt);

    const RendererOptions options_;
    const size_t packetsPerFrame_;

    FrameBuffer buffers_[numBuffers];
    FrameBuffer* backBuffer_;

public:
    BufferedRenderer(RendererOptions options) :
            options_(std::move(options)),
            packetsPerFrame_(packetsPerFrame(options_.ledStrips, options_.ledsPerStrip, fmt)),
            backBuffer_(&buffers_[0]) {
        for (size_t i = 0; i < numBuffers; ++i) {
            buffers_[i].alloc(packetsPerFrame_);
        }
    }

    ~BufferedRenderer() override {
        for (size_t i = 0; i < numBuffers; ++i) {
            buffers_[i].free(packetsPerFrame_);
        }
    }

    bool storeFramePacket(size_t packetIndex, usb_packet_t* packet, size_t len) override {
        return backBuffer_->storeFramePacket(packetsPerFrame_, packetIndex, packet, len);
    }

    static bool canInstantiate(const RendererOptions& options) {
        return Super::canInstantiate(options) &&
                options.ledStrips > 1 && options.ledStrips <= 8 &&
                options.ledsPerStrip > 1 && options.ledsPerStrip < config::maxLedsPerStrip &&
                packetsPerFrame(options.ledStrips, options.ledsPerStrip, fmt) <= maxPackets;
    }
};

template <ColorFormat fmt, DitherMode ditherMode>
class DoubleBufferedRenderer : public BufferedRenderer<fmt, 2, config::maxPacketsPerDoubleBufferedFrame> {
protected:
    using Super = BufferedRenderer<fmt, 2, config::maxPacketsPerDoubleBufferedFrame>;
    using Super::backBuffer_;
    using Super::buffers_;
    using Super::options_;
    using Super::pixelsPerPacket_;
    using DO = DitherOp<ditherMode, typename Super::FrameBuffer::ColorOut>;
    using OO = OutputOp<typename DO::ColorOut>;

    typename Super::FrameBuffer* frontBuffer_;
    DO dither_;
    OO output_;

public:
    DoubleBufferedRenderer(RendererOptions options) :
            Super(std::move(options)),
            frontBuffer_(&buffers_[1]),
            dither_(options.maxDitherBits) { }

    void advanceFrame() override {
        std::swap(frontBuffer_, backBuffer_);
    }

    void render(uint8_t* outputBuffer) override {
        led::updateBuffer(outputBuffer, options_.ledStrips, options_.ledsPerStrip,
                [this](size_t strip, size_t pixel) [[gnu::always_inline]] {
            size_t x = strip * options_.ledsPerStrip + pixel;
            size_t packetIndex = x / pixelsPerPacket_;
            size_t packetOffset = x % pixelsPerPacket_;
            return output_(dither_(frontBuffer_->getPixel(packetIndex, packetOffset)));
        });
        dither_.advancePattern();
    }
};

template <ColorFormat fmt, DitherMode ditherMode, InterpolateMode interpolateMode>
class TripleBufferedRenderer : public BufferedRenderer<fmt, 3, config::maxPacketsPerTripleBufferedFrame> {
protected:
    using Super = BufferedRenderer<fmt, 3, config::maxPacketsPerTripleBufferedFrame>;
    using Super::FrameBuffer;
    using Super::backBuffer_;
    using Super::buffers_;
    using Super::options_;
    using Super::pixelsPerPacket_;
    using IO = InterpolateOp<interpolateMode, typename Super::FrameBuffer::ColorOut>;
    using DO = DitherOp<ditherMode, typename IO::ColorOut>;
    using OO = OutputOp<typename DO::ColorOut>;

    typename Super::FrameBuffer* frontBuffer_;
    typename Super::FrameBuffer* priorBuffer_;
    IO interpolate_;
    DO dither_;
    OO output_;

public:
    TripleBufferedRenderer(RendererOptions options) :
            Super(std::move(options)),
            frontBuffer_(&buffers_[1]),
            priorBuffer_(&buffers_[2]),
            dither_(options.maxDitherBits) {}

    void advanceFrame() override {
        std::swap(frontBuffer_, priorBuffer_);
        std::swap(frontBuffer_, backBuffer_);
    }

    void render(uint8_t* outputBuffer) override {
        interpolate_.setCoeffs(micros64(), frontBuffer_->time, priorBuffer_->time);
        led::updateBuffer(outputBuffer, options_.ledStrips, options_.ledsPerStrip,
                [&](size_t strip, size_t pixel) [[gnu::always_inline]] {
            size_t x = strip * options_.ledsPerStrip + pixel;
            size_t packetIndex = x / pixelsPerPacket_;
            size_t packetOffset = x % pixelsPerPacket_;
            return output_(dither_(interpolate_(
                    frontBuffer_->getPixel(packetIndex, packetOffset),
                    priorBuffer_->getPixel(packetIndex, packetOffset))));
        });
        dither_.advancePattern();
    }
};

// Identifies a particular rendering algorithm.
struct RendererId {
    const ColorFormat fmt;
    const DitherMode ditherMode;
    const InterpolateMode interpolateMode;

    bool operator==(const RendererId& other) const {
        return fmt == other.fmt &&
                ditherMode == other.ditherMode &&
                interpolateMode == other.interpolateMode;
    }
};

// Tag to select a particular rendering algorithm to be compiled.
template <ColorFormat fmt, DitherMode ditherMode, InterpolateMode interpolateMode>
struct RendererTag {
    // In some modes, dithering is a no-op so make it a synonym of the non-dithering mode.
    static constexpr DitherMode effectiveDitherMode =
            fmt == ColorFormat::R8G8B8 && interpolateMode == InterpolateMode::NONE ?
                DitherMode::NONE : ditherMode;

    // Map the tag to a type.
    using Type = std::conditional_t<interpolateMode == InterpolateMode::NONE,
            DoubleBufferedRenderer<fmt, effectiveDitherMode>,
            TripleBufferedRenderer<fmt, effectiveDitherMode, interpolateMode>>;

    static constexpr RendererId id = RendererId{fmt, ditherMode, interpolateMode};

    static Renderer* make(void* mem, RendererOptions options) {
        if (!Renderer::canInstantiate(options)) return nullptr;
        return new (mem) Type(std::move(options));
    }
};

// Holds a renderer that is instantiated and configured at runtime.
class RendererHolder {
    template <typename ...Tags>
    struct RendererTable {
        static constexpr size_t count = sizeof...(Tags);
        static constexpr RendererId ids[] = { Tags::id... };
        typedef Renderer* (*Factory)(void* mem, RendererOptions options);
        static constexpr Factory factories[] = { Tags::make... };
        using Storage = std::aligned_union_t<0, typename Tags::Type...>;
    };
    using Renderers = RendererTable<CONFIG_RENDERERS>;

    Renderer nullRenderer_;
    Renderer* renderer_;    
    Renderers::Storage rendererStorage_;

public:
    RendererHolder() : renderer_(&nullRenderer_) {}
    ~RendererHolder() { clear(); }

    // Gets the current renderer.
    // This may be a null renderer that drops all packets and renders nothing
    // if initialization hasn't happened yet or failed.
    inline Renderer* get() const { return renderer_; }

    // Initializes a renderer with the specified id and options.
    //
    // Returns false if the renderer cannot be initialized such as if it hasn't been
    // compiled into the firmware or if there isn't enough memory for it.
    bool init(RendererId id, RendererOptions options) {
        clear();

        for (size_t i = 0; i < Renderers::count; i++) {
            if (Renderers::ids[i] == id) {
                renderer_ = (Renderers::factories[i])(&rendererStorage_, std::move(options));
                if (renderer_ != nullptr) return true;
                break;
            }
        }

        renderer_ = &nullRenderer_;
        return false;
    }

    void clear() {
        if (renderer_ != &nullRenderer_) {
            renderer_->~Renderer();
            renderer_ = &nullRenderer_;
        }
    }
};

} // namespace render