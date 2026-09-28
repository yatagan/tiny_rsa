# TinyRTA

A real-time audio spectrum analyzer (RTA) built on the **STM32G030F6P6** — an 8 KB RAM, 32 KB flash, Cortex-M0+ microcontroller with no FPU. TinyRTA samples ambient sound through a microphone front-end, runs a fixed-point FFT, and drives a 32x8 MAX7219 LED matrix with a 32-bar spectrum visualizer, all within the constraints of one of the smallest chips in the STM32 lineup.

## Features

- Real-time audio sampling via timer-triggered ADC + circular DMA (ping-pong buffering, no interrupts)
- 8x hardware oversampling as a crude anti-alias boxcar filter
- 256-point fixed-point (q15) radix-2 FFT, no CMSIS-DSP; twiddle/window tables built at compile time
- Hann windowing for reduced spectral leakage
- Log-spaced grouping of FFT bins 2..127 into 32 bands, dB scale, peak-hold bars
- MAX7219 4-in-1 32x8 LED matrix output (bit-banged SPI)
- ~8 KB flash, ~2.2 KB RAM for the signal chain

## Hardware

| Component | Notes |
|---|---|
| MCU | STM32G030F6P6 (TSSOP20, 64 MHz Cortex-M0+) |
| Microphone | Electret mic + preamp (e.g. MAX9814 / MAX4466) |
| Display | MAX7219 4-in-1 32x8 LED matrix (FC-16 style) |
| Power | 3.3V supply |

> No onboard mic preamp - an external analog front-end is required to bring the mic signal into the ADC's input range.

### Wiring

| Signal | STM32G030F6P6 | Notes |
|---|---|---|
| MAX9814 OUT | PA0 (pin 7), ADC_IN0 | ~1.25 V DC bias, AGC regulates to ~1.4 Vpp. Optional RC anti-alias (1 kOhm + 47 nF) |
| MAX9814 VDD / GND | 3.3 V / GND | Same rail as the MCU |
| MAX9814 GAIN | - | Unconnected = 60 dB, GND = 50 dB, VDD = 40 dB |
| MAX9814 AR | - | Unconnected = attack/release 1:4000 |
| MAX7219 DIN | PA7 (pin 14) | |
| MAX7219 CLK | PA5 (pin 12) | |
| MAX7219 CS | PA4 (pin 11) | |
| MAX7219 VCC / GND | 5 V / GND | See `App/max7219.cppm` for 3.3 V logic caveats |

### Anti-alias filter (recommended)

The ADC samples at 8 kHz, so it can only represent content below 4 kHz. The
MAX9814 output reaches up to ~20 kHz, and anything above 4 kHz folds back
(aliases) into the displayed range as bars that are not really there. The
on-chip 8x oversampling averages samples over each 125 us period, which only
helps a little: it nulls 8 kHz and 16 kHz but lets 5 kHz through at about
-6 dB.

Add a first-order RC low-pass between MAX9814 OUT and PA0:

```
MAX9814 OUT ---[ 1 kOhm ]---+---> PA0
                            |
                          47 nF
                            |
                           GND
```

The cutoff is fc = 1 / (2 pi R C) ~ 3.4 kHz. It rolls off gently (-6 dB/octave),
so it attenuates the top bars a bit as well; a steeper second stage
(another 1 kOhm + 47 nF) can be added if high-frequency content is strong.

## Signal Chain

```
MAX9814 -> ADC (TIM3 64 kHz trigger, 8x oversampling = 8 kHz, circular DMA)
        -> DC removal -> Hann window -> q15 FFT (256-pt) -> |X|^2
        -> 32 log-spaced band sums -> dB -> peak-hold bars -> MAX7219
```

- **Sample rate:** 8 kHz (Nyquist = 4 kHz, sufficient for a visual spectrum display)
- **FFT size:** 256 points, 31.25 Hz bins; bands span 62.5 Hz .. 4 kHz (the lowest 18 bands are one bin each)
- **Frame rate:** ~31 fps (one spectrum per 256-sample block)
- **Level scale:** 6 dB per LED row, 48 dB total; see [Bring-up and Tuning](#bring-up-and-tuning)

## Bring-up and Tuning

Work through these steps in order. Each one checks one more part of the chain,
so a failure points at a single part.

### 1. Display

Set `APP_DISPLAY_DEMO` to 1 in `Core/Src/main.c` and flash. You should see an
animated fake spectrum. If the image is upside down or mirrored, set
`kRotate180` in `App/max7219.cppm`. Set `APP_DISPLAY_DEMO` back to 0 afterwards.

### 2. FFT and bars, without the microphone

Set `kTestTone = true` in `App/rta.cppm`. This replaces the ADC data with a
synthetic 1 kHz tone at a quarter of full scale. Bar 21 (counting from 0 at
the left) should be lit almost to the top, with the rest dark or nearly dark.
Set `kTestTone` back to `false` afterwards.

### 3. ADC input, in the debugger

Break in `Rta::step()` and inspect the analyzer object
(`(anonymous namespace)::rta_instance`):

- `adc_.buf_` should hold values around 1550 (the MAX9814's 1.25 V bias) in a
  quiet room and swing widely when you clap. Values stuck at 0 or 4095 mean a
  wiring or supply problem.
- `adc_.overruns_` should stay at 0. If it grows, something in the main loop
  is blocking for longer than 32 ms.

### 4. Frequency check

Play pure tones from a phone tone-generator app close to the mic. The peak
should appear at these bars:

| Tone | Bar (0 = left) |
|---|---|
| 100 Hz | 1 |
| 440 Hz | 12 |
| 1 kHz | 21 |
| 3 kHz | 29 |

The bars are 31.25 Hz apart below ~600 Hz (bars 0..17), then get wider on a
log scale up to 4 kHz.

### 5. Levels

Tune the hardware gain first, then the software scale:

- **MAX9814 GAIN pin:** unconnected = 60 dB for room sound and speech, GND =
  50 dB, VDD = 40 dB for loud music or a mic close to a speaker. The chip's
  automatic gain control then keeps the output near 1.4 Vpp for loud input.
- **`kTopQ3`** (`App/rta.cppm`): the band power that fills a column to the top.
  One unit is ~0.38 dB, so +8 means ~3 dB louder input is needed to reach the
  top. Raise it if bars are pinned at the top during normal music; lower it
  if nothing reaches the upper rows.
- **`kDbPerRow`** (`App/rta.cppm`): dB per LED row, 6 by default (48 dB range).
  Use 3..4 for a livelier, jumpier display, 8 for a calmer one that shows
  quiet and loud parts together.

A good setting: in a quiet room only the bottom row flickers occasionally,
and with music playing the bars move through the whole height, touching the
top only on peaks. If room noise lights several rows, raise `kTopQ3` or reduce
the GAIN.

### 6. Look and feel

- **Bar dynamics** (`App/bar_graph.cppm`, shared with the demo):
  `kDecayPerFrame` sets how fast bars fall (larger = faster), and
  `kPeakHoldFrames` / `kPeakFallPerFrame` set how long the peak dot hangs
  and how fast it drops. Frames are ~32 ms.
- **Brightness:** the `display_.init(4)` argument in `App/rta.cppm`, 0..15.

## Memory Budget

| Buffer | Size | Location |
|---|---|---|
| ADC DMA ping-pong buffer (2 x 256 x u16) | 1 KB | RAM |
| FFT work buffers (re + im, 256 x q15 each) | 1 KB | RAM |
| Bar levels / peaks / hold (3 x 32) | 96 B | RAM |
| Display framebuffer (32x8 mono) | 32 B | RAM |
| Cosine table (twiddles, Hann window, test tone) | 512 B | Flash |

Total static RAM (incl. HAL, stack and heap reservations): ~3.7 KB of 8 KB.

## Status

Signal chain implemented; level scaling still needs tuning on hardware (see
[Bring-up and Tuning](#bring-up-and-tuning)).

## Roadmap

- [x] ADC + DMA acquisition pipeline
- [x] q15 FFT
- [x] Log-bucket grouping algorithm
- [x] MAX7219 driver + bar rendering
- [ ] Amplitude log-compression / auto-gain for varying input levels
- [ ] Optional: adjustable sample rate / FFT size via config

## License

TBD

## Contributing

Issues and pull requests welcome once the initial pipeline lands.
