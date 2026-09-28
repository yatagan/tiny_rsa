// Real-time audio spectrum analyzer: MAX9814 -> ADC -> q15 FFT -> 32 bars on
// the MAX7219 32x8 matrix.
//
// Every 32 ms the ADC delivers a block of 256 samples at 8 kHz. The block is
// DC-removed, Hann-windowed and transformed; FFT bins 2..127 (62.5 Hz ..
// 3.97 kHz, 31.25 Hz apart) are summed into 32 log-spaced bands, converted to
// dB and drawn with the shared bar-graph dynamics (~31 fps).
//
// Call rta::init() once after HAL_Init(), then rta::step() from the main
// loop. step() is non-blocking; it returns immediately unless a new block is
// ready, and processing a block takes a few ms of the 32 ms budget.
export module rta;
import std;
import audio_adc;
import fft_q15;
import max7219;
import bar_graph;

namespace {

using fft_q15::kN;
static_assert(audio_adc::kBlock == kN);

constexpr std::size_t kBands = max7219::kWidth;

// Level mapping. Each LED row spans kDbPerRow; a band power of kTopQ3 (in
// log2_q3 units, see below) fills the column. Tune these on the real device
// together with the MAX9814 GAIN pin. The default top corresponds to about
// -3 dB below a sine at the MAX9814's AGC-regulated 1.4 Vpp.
constexpr std::uint32_t kDbPerRow = 5;
constexpr std::int32_t  kTopQ3    = 180;

// Synthetic input for checking the FFT and display without a microphone:
// replaces the ADC data with a cosine at kTestToneBin * 31.25 Hz.
constexpr bool        kTestTone    = false;
constexpr std::size_t kTestToneBin = 32;  // 1 kHz

// log2(v) with 3 fractional bits (1 unit = 1/8 octave of power = ~0.376 dB).
// 0 for v == 0, at most 247 for v < 2^31.
constexpr std::uint8_t log2_q3(std::uint32_t v) {
    if (v == 0) {
        return 0;
    }
    const int msb  = 31 - std::countl_zero(v);
    const auto frac = msb >= 3 ? (v >> (msb - 3)) & 7u : (v << (3 - msb)) & 7u;
    return static_cast<std::uint8_t>(msb * 8 + static_cast<int>(frac));
}

static_assert(log2_q3(1) == 0);
static_assert(log2_q3(256) == 64);
static_assert(log2_q3(384) == 68);  // 1.5 * 2^8 -> 8.5 octaves

// Displayed range in log2_q3 units: 3.0103 dB per octave of power.
constexpr std::int32_t kRangeQ3 = static_cast<std::int32_t>(8 * kDbPerRow * 8 * 10000 / 30103);

// Band power -> 0..255 bar level.
constexpr std::uint8_t level_from_power(std::uint32_t p) {
    const std::int32_t above_floor = log2_q3(p) - (kTopQ3 - kRangeQ3);
    return static_cast<std::uint8_t>(std::clamp(above_floor * 256 / kRangeQ3, 0, 255));
}

// Band b covers FFT bins [kEdges[b], kEdges[b + 1]). Edges are spaced by a
// constant ratio (64^(1/32) ~ 1.139) where the bin resolution allows it; the
// lowest bands would be narrower than one bin, so each gets at least one.
constexpr std::size_t kLowBin  = 2;       // 62.5 Hz: bin 0 is DC, bin 1 holds window leakage from it
constexpr std::size_t kHighBin = kN / 2;  // exclusive: Nyquist

constexpr auto kEdges = [] {
    // r^kBands = kHighBin / kLowBin, solved by bisection (no constexpr pow).
    const double target = static_cast<double>(kHighBin) / kLowBin;
    double lo = 1.0;
    double hi = 2.0;
    for (int it = 0; it < 60; ++it) {
        const double mid = (lo + hi) / 2;
        double p = 1.0;
        for (std::size_t i = 0; i < kBands; ++i) {
            p *= mid;
        }
        (p < target ? lo : hi) = mid;
    }

    std::array<std::size_t, kBands + 1> e{};
    e[0]     = kLowBin;
    double f = kLowBin;
    for (std::size_t b = 1; b <= kBands; ++b) {
        f *= lo;
        e[b] = std::max(e[b - 1] + 1, static_cast<std::size_t>(f + 0.5));
    }
    e[kBands] = kHighBin;
    return e;
}();

static_assert(kEdges[kBands - 1] < kEdges[kBands], "every band needs at least one bin");

class Rta {
public:
    constexpr Rta() = default;

    void init() {
        display_.init(4);
        adc_.init();
    }

    void step() {
        const auto block = adc_.ready_block();
        if (!block) {
            return;
        }
        load(*block);
        fft_q15::fft(re_, im_);

        // Sum of power over a band: by Parseval the one-sided total is at
        // most 32767^2 < 2^30, so the uint32 sum cannot overflow.
        std::array<std::uint8_t, kBands> targets{};
        for (std::size_t b = 0; b < kBands; ++b) {
            std::uint32_t sum = 0;
            for (std::size_t k = kEdges[b]; k < kEdges[b + 1]; ++k) {
                sum += fft_q15::power(re_[k], im_[k]);
            }
            targets[b] = level_from_power(sum);
        }

        display_.set_columns(bars_.update(targets));
        display_.flush();
    }

private:
    // ADC counts -> DC-free, windowed q15 in re_, zero im_.
    void load(audio_adc::Block block) {
        std::uint32_t acc = 0;
        for (const std::uint16_t s : block) {
            acc += s;
        }
        const auto mean = static_cast<std::int32_t>(acc / kN);

        for (std::size_t n = 0; n < kN; ++n) {
            std::int32_t x;
            if constexpr (kTestTone) {
                x = fft_q15::cos_q15(n * kTestToneBin) / 4;
            } else {
                // 12-bit -> q15 (x16), saturating: the bias is not mid-scale.
                x = std::clamp((static_cast<std::int32_t>(block[n]) - mean) * 16, -32767, 32767);
            }
            re_[n] = static_cast<std::int16_t>((x * fft_q15::hann_q15(n)) >> 15);
            im_[n] = 0;
        }
    }

    max7219::Max7219               display_{};
    bar_graph::BarGraph<kBands>    bars_{};
    audio_adc::AudioAdc            adc_{};
    std::array<std::int16_t, kN>   re_{};
    std::array<std::int16_t, kN>   im_{};
};

constinit Rta rta_instance{};

}  // namespace

export namespace rta {

void init() { rta_instance.init(); }
void step() { rta_instance.step(); }

}  // namespace rta
