// Sample usage of the max7219 module: an animated 32-column bar graph that
// previews the eventual RTA spectrum display. Integer math only (no FPU).
//
// Call max7219_demo::init() once after HAL_Init(), then max7219_demo::step()
// from the main loop. step() is non-blocking and renders a frame every ~33 ms.
module;
#include "stm32g0xx_hal.h"
export module max7219_demo;
import std;
import max7219;

namespace {

constexpr std::uint32_t kFramePeriodMs    = 33;  // ~30 fps
constexpr std::uint8_t  kDecayPerFrame    = 6;   // level units (0..255) per frame
constexpr std::uint8_t  kPeakHoldFrames   = 12;
constexpr std::uint8_t  kPeakFallPerFrame = 3;

// 0..255 triangle wave over an 8-bit phase.
constexpr std::uint8_t tri(std::uint8_t phase) {
    return static_cast<std::uint8_t>(phase < 128 ? phase * 2 : (255 - phase) * 2);
}

// 0..255 level -> 0..8 bar height.
constexpr std::uint8_t height_from_level(std::uint8_t level) {
    return static_cast<std::uint8_t>((level + 16) / 32);
}

// Bar of height h: the bottom h rows lit (bit r = row r, r = 0 top).
constexpr std::uint8_t column_mask(std::uint8_t h) {
    return static_cast<std::uint8_t>(0xFFu << (8 - h));
}

class BarGraphDemo {
public:
    constexpr BarGraphDemo() = default;

    void init() {
        display_.init(4);
        last_ms_ = HAL_GetTick();
    }

    void step() {
        const std::uint32_t now = HAL_GetTick();
        if (now - last_ms_ < kFramePeriodMs) {
            return;
        }
        last_ms_ = now;
        ++t_;

        std::array<std::uint8_t, max7219::kWidth> cols{};
        for (std::size_t x = 0; x < max7219::kWidth; ++x) {
            // Two triangle "hills" drifting in opposite directions plus a
            // little LFSR jitter, so the bars look like a live spectrum.
            const auto p1 = static_cast<std::uint8_t>(x * 16 + t_ * 2);
            const auto p2 = static_cast<std::uint8_t>(x * 9 + 64 - t_ * 3);
            auto target = static_cast<std::uint16_t>((tri(p1) + tri(p2)) / 2);        // 0..255
            target = static_cast<std::uint16_t>(target * 3 / 4 + (lfsr_next() & 0x3F));  // 0..254

            std::uint8_t& lvl = level_[x];
            if (target > lvl) {
                lvl = static_cast<std::uint8_t>(target);  // instant attack
            } else {
                lvl = static_cast<std::uint8_t>(lvl > kDecayPerFrame ? lvl - kDecayPerFrame : 0);
            }

            std::uint8_t& pk = peak_[x];
            if (lvl >= pk) {
                pk = lvl;
                hold_[x] = kPeakHoldFrames;
            } else if (hold_[x] > 0) {
                --hold_[x];
            } else {
                pk = static_cast<std::uint8_t>(pk > kPeakFallPerFrame ? pk - kPeakFallPerFrame : 0);
            }

            const std::uint8_t h  = height_from_level(lvl);
            const std::uint8_t ph = height_from_level(pk);
            std::uint8_t mask = column_mask(h);
            if (ph > h) {
                mask |= static_cast<std::uint8_t>(1u << (8 - ph));  // peak-hold dot
            }
            cols[x] = mask;
        }

        display_.set_columns(cols);
        display_.flush();
    }

private:
    // 16-bit Galois LFSR, taps 0xB400 (maximal length).
    std::uint16_t lfsr_next() {
        const bool lsb = lfsr_ & 1u;
        lfsr_ = static_cast<std::uint16_t>(lfsr_ >> 1);
        if (lsb) {
            lfsr_ ^= 0xB400u;
        }
        return lfsr_;
    }

    max7219::Max7219 display_{};
    std::array<std::uint8_t, max7219::kWidth> level_{};
    std::array<std::uint8_t, max7219::kWidth> peak_{};
    std::array<std::uint8_t, max7219::kWidth> hold_{};
    std::uint16_t lfsr_    = 0xACE1u;  // any non-zero seed
    std::uint16_t t_       = 0;
    std::uint32_t last_ms_ = 0;
};

constinit BarGraphDemo demo{};

}  // namespace

export namespace max7219_demo {

void init() { demo.init(); }
void step() { demo.step(); }

}  // namespace max7219_demo
