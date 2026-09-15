// MAX7219 4-in-1 LED dot matrix driver (4 cascaded 8x8 blocks = 32x8 pixels).
//
// Transport: bit-banged SPI on GPIOA using the SPI1 AF0 pins, so a later move
// to hardware SPI1 needs no rewiring.
//
//   STM32G030F6P6      MAX7219 module
//   PA7 (pin 14) ----> DIN
//   PA5 (pin 12) ----> CLK
//   PA4 (pin 11) ----> CS / LOAD
//   GND ---------------GND
//   5 V ---------------VCC
//
// The MAX7219 specifies VIH >= 3.5 V at VCC = 5 V, so 3.3 V logic is out of
// spec; it works on most FC-16 clones. If the display is erratic, power the
// module from 3.3 V or add a 74HCT125-class level shifter. Keep leads short.
//
// Board orientation (FC-16 style): hold the board with the input connector on
// the RIGHT. Device 0 is the rightmost 8x8 block, device 3 the leftmost.
// Within a block, register DIGn holds row n (0 = top) and SEG bit 7 is the
// leftmost column, bit 0 the rightmost. Set kRotate180 if the image is upside
// down / mirrored on your board.
//
// Register map (datasheet table 2): 16-bit frames, D15..D12 don't care,
// D11..D8 = address, D7..D0 = data. Data is shifted in on the CLK rising edge
// and latched into all devices on the CS/LOAD rising edge. The frame sent
// first ends up in the last device of the chain.
module;
#include "stm32g0xx_hal.h"
#include "stm32g0xx_ll_gpio.h"
export module max7219;
import std;

export namespace max7219 {

inline constexpr std::size_t kDevices   = 4;
inline constexpr std::size_t kRows      = 8;
inline constexpr std::size_t kWidth     = kDevices * 8;  // logical columns, x = 0 leftmost
inline constexpr bool        kRotate180 = false;

enum class Reg : std::uint8_t {
    NoOp        = 0x00,
    Digit0      = 0x01,  // Digit0..Digit7 = 0x01..0x08, one row each
    DecodeMode  = 0x09,
    Intensity   = 0x0A,
    ScanLimit   = 0x0B,
    Shutdown    = 0x0C,
    DisplayTest = 0x0F,
};

class Max7219 {
public:
    constexpr Max7219() = default;

    // Configures the GPIOs and every MAX7219 register, blanks the display and
    // leaves it in normal operation.
    void init(std::uint8_t intensity = 4);

    // Brightness 0..15, applied to all devices.
    void set_intensity(std::uint8_t level);

    // Zeroes the framebuffer. Call flush() to show it.
    void clear();

    // Replaces the framebuffer. cols[x] bit r set => pixel (x, r) on, with
    // x = 0 the leftmost column and r = 0 the top row.
    void set_columns(std::span<const std::uint8_t, kWidth> cols);

    // Pushes the framebuffer to the chain: 8 rows x 4 devices, one CS
    // transaction per row.
    void flush();

private:
    void write_all(Reg reg, std::uint8_t data);
    static void send_frame(std::uint8_t reg, std::uint8_t data);

    std::array<std::uint8_t, kDevices * kRows> rows_{};  // [device * 8 + row] = SEG bits
};

}  // namespace max7219

namespace max7219 {

namespace {

constexpr std::uint32_t kDin = LL_GPIO_PIN_7;
constexpr std::uint32_t kClk = LL_GPIO_PIN_5;
constexpr std::uint32_t kCs  = LL_GPIO_PIN_4;

inline void cs_low()  { LL_GPIO_ResetOutputPin(GPIOA, kCs); }
inline void cs_high() { LL_GPIO_SetOutputPin(GPIOA, kCs); }

}  // namespace

// One 16-bit frame, MSB first. At 16 MHz every GPIO store takes >= 62.5 ns,
// so the datasheet's 50 ns minimum pulse widths are met without delays.
void Max7219::send_frame(std::uint8_t reg, std::uint8_t data) {
    auto frame = static_cast<std::uint16_t>((reg << 8) | data);
    for (int i = 0; i < 16; ++i) {
        if (frame & 0x8000u) {
            LL_GPIO_SetOutputPin(GPIOA, kDin);
        } else {
            LL_GPIO_ResetOutputPin(GPIOA, kDin);
        }
        LL_GPIO_SetOutputPin(GPIOA, kClk);
        frame = static_cast<std::uint16_t>(frame << 1);
        LL_GPIO_ResetOutputPin(GPIOA, kClk);
    }
}

// Same register/data to all devices in one CS transaction.
void Max7219::write_all(Reg reg, std::uint8_t data) {
    cs_low();
    for (std::size_t i = 0; i < kDevices; ++i) {
        send_frame(static_cast<std::uint8_t>(reg), data);
    }
    cs_high();
}

void Max7219::init(std::uint8_t intensity) {
    __HAL_RCC_GPIOA_CLK_ENABLE();

    // Preload idle levels (CS high, CLK and DIN low) before switching the
    // pins to output so the chain never sees a glitch.
    LL_GPIO_SetOutputPin(GPIOA, kCs);
    LL_GPIO_ResetOutputPin(GPIOA, kClk | kDin);

    GPIO_InitTypeDef gpio{};
    gpio.Pin   = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    // Register contents are undefined at power-up, so write all of them.
    write_all(Reg::DisplayTest, 0);
    write_all(Reg::ScanLimit, 7);   // drive all 8 rows
    write_all(Reg::DecodeMode, 0);  // raw bitmap, no BCD decoding
    set_intensity(intensity);
    clear();
    flush();                        // blank every digit on every device
    write_all(Reg::Shutdown, 1);    // leave shutdown mode
}

void Max7219::set_intensity(std::uint8_t level) {
    write_all(Reg::Intensity, static_cast<std::uint8_t>(level & 0x0F));
}

void Max7219::clear() {
    rows_.fill(0);
}

void Max7219::set_columns(std::span<const std::uint8_t, kWidth> cols) {
    rows_.fill(0);
    for (std::size_t x = 0; x < kWidth; ++x) {
        const std::size_t  px     = kRotate180 ? (kWidth - 1 - x) : x;
        const std::size_t  dev    = (kDevices - 1) - px / 8;  // device 0 = rightmost block
        const auto         colbit = static_cast<std::uint8_t>(0x80u >> (px % 8));
        std::uint8_t bits = cols[x];
        for (std::size_t r = 0; r < kRows && bits != 0; ++r, bits >>= 1) {
            if (bits & 1u) {
                const std::size_t pr = kRotate180 ? (kRows - 1 - r) : r;
                rows_[dev * kRows + pr] |= colbit;
            }
        }
    }
}

void Max7219::flush() {
    for (std::size_t r = 0; r < kRows; ++r) {
        const auto reg = static_cast<std::uint8_t>(static_cast<std::uint8_t>(Reg::Digit0) + r);
        cs_low();
        // The last device in the chain must receive the first frame.
        for (std::size_t dev = kDevices; dev-- > 0;) {
            send_frame(reg, rows_[dev * kRows + r]);
        }
        cs_high();
    }
}

}  // namespace max7219
