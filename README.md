# TinyRTA

A real-time audio spectrum analyzer (RTA) built on the **STM32G030F6P6** — an 8 KB RAM, 32 KB flash, Cortex-M0+ microcontroller with no FPU. TinyRTA samples ambient sound through a microphone front-end, runs a fixed-point FFT, and drives a small OLED display with a 32-bar spectrum visualizer, all within the constraints of one of the smallest chips in the STM32 lineup.

## Features

- Real-time audio sampling via ADC + DMA (ping-pong buffering)
- 128-point fixed-point (q15) FFT using CMSIS-DSP
- Log-spaced grouping of 65 raw FFT bins into 32 display buckets
- Hann windowing for reduced spectral leakage
- SSD1306 128x64 I2C/SPI OLED output
- Runs comfortably within ~1.5 KB RAM and well under 32 KB flash

## Hardware

| Component | Notes |
|---|---|
| MCU | STM32G030F6P6 (TSSOP20, 64 MHz Cortex-M0+) |
| Microphone | Electret mic + preamp (e.g. MAX9814 / MAX4466) |
| Display | SSD1306 128x64 OLED (I2C or SPI) |
| Power | 3.3V supply |

> No onboard mic preamp — an external analog front-end is required to bring the mic signal into the ADC's input range.

## Signal Chain

```
Mic + Preamp → ADC (DMA, timer-triggered, 8 kHz) → Hann Window
            → arm_rfft_q15 (128-pt) → arm_cmplx_mag_q15 (65 bins)
            → Log-grouping → 32 buckets → SSD1306 bar display
```

- **Sample rate:** 8 kHz (Nyquist = 4 kHz, sufficient for a visual spectrum display)
- **FFT size:** 128 points (65 usable bins), grouped logarithmically into 32 buckets
- **Frame rate:** ~10–20 fps depending on display refresh overhead

## Memory Budget

| Buffer | Size | Location |
|---|---|---|
| ADC sample buffer (128 x q15) | 256 B | RAM |
| Hann window table | 256 B | Flash |
| RFFT output (complex) | 260 B | RAM |
| Magnitude buffer (65 bins) | 130 B | RAM |
| Display bucket array (32) | ~64 B | RAM |
| CMSIS-DSP RFFT tables | ~1–1.5 KB | Mostly Flash |
| Display framebuffer (128x64 mono) | 1 KB | RAM |

Total RAM usage: roughly 2.5–3 KB, leaving headroom in the 8 KB budget for stack and application state.

## Status

🚧 Early development — signal chain designed, implementation in progress.

## Roadmap

- [ ] ADC + DMA acquisition pipeline
- [ ] CMSIS-DSP q15 RFFT integration (trimmed for flash size)
- [ ] Log-bucket grouping algorithm
- [ ] SSD1306 driver + bar rendering
- [ ] Amplitude log-compression / auto-gain for varying input levels
- [ ] Optional: adjustable sample rate / FFT size via config

## License

TBD

## Contributing

Issues and pull requests welcome once the initial pipeline lands.
