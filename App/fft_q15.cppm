// 256-point fixed-point (q15) radix-2 FFT for a Cortex-M0+ without FPU.
//
// One compile-time cosine table (one full period, 256 x int16 in flash)
// serves the twiddle factors, the Hann window and test tones. std::cos is not
// constexpr in C++23, so the table is built with a Taylor series evaluated by
// the compiler; no floating point code reaches the target.
//
// fft() is an in-place decimation-in-time transform that halves the data
// after every stage (8 stages -> output = DFT / 256). With |input| <= 1.0 this
// can never overflow, so no saturation checks are needed in the butterflies.
export module fft_q15;
import std;

export namespace fft_q15 {

inline constexpr std::size_t kN = 256;

}  // namespace fft_q15

namespace fft_q15 {

// Module linkage rather than an anonymous namespace: the exported constexpr
// helpers below read kCos, and exported inline code must not expose
// TU-local entities.
constexpr std::size_t kLog2N = 8;
static_assert(std::size_t{1} << kLog2N == kN);

constexpr double kPi = 3.14159265358979323846;

// cos(x) for |x| <= pi, Taylor series to x^28 (error < 1e-14).
constexpr double cos_taylor(double x) {
    const double x2 = x * x;
    double term = 1.0;
    double sum  = 1.0;
    for (int k = 1; k <= 14; ++k) {
        term *= -x2 / ((2 * k - 1) * (2 * k));
        sum += term;
    }
    return sum;
}

constexpr std::int16_t to_q15(double v) {
    const double scaled = v * 32768.0;
    const double r      = scaled >= 0 ? scaled + 0.5 : scaled - 0.5;
    if (r >= 32767.0) {
        return 32767;
    }
    if (r <= -32767.0) {
        return -32767;  // keep the table symmetric
    }
    return static_cast<std::int16_t>(r);
}

// kCos[k] = cos(2 pi k / N) in q15.
constexpr auto kCos = [] {
    std::array<std::int16_t, kN> t{};
    for (std::size_t k = 0; k < kN; ++k) {
        double x = 2.0 * kPi * static_cast<double>(k) / kN;
        if (x > kPi) {
            x -= 2.0 * kPi;
        }
        t[k] = to_q15(cos_taylor(x));
    }
    return t;
}();

static_assert(kCos[0] == 32767);
static_assert(kCos[kN / 4] == 0);
static_assert(kCos[kN / 2] == -32767);
static_assert(kCos[kN / 8] == 23170);  // cos(pi/4) * 32768 = 23170.475

constexpr std::size_t bit_reverse(std::size_t i) {
    std::size_t r = 0;
    for (std::size_t b = 0; b < kLog2N; ++b, i >>= 1) {
        r = (r << 1) | (i & 1u);
    }
    return r;
}

}  // namespace fft_q15

export namespace fft_q15 {

// cos(2 pi k / N) in q15, k taken modulo N.
constexpr std::int16_t cos_q15(std::size_t k) { return kCos[k % kN]; }

// sin(2 pi k / N) in q15, k taken modulo N.
constexpr std::int16_t sin_q15(std::size_t k) { return kCos[(k + 3 * kN / 4) % kN]; }

// Periodic Hann window w[n] = (1 - cos(2 pi n / N)) / 2 in q15, n < N.
constexpr std::int16_t hann_q15(std::size_t n) {
    return static_cast<std::int16_t>((32767 - kCos[n] + 1) >> 1);
}

static_assert(hann_q15(0) == 0);
static_assert(hann_q15(kN / 2) == 32767);

// In-place forward FFT. Output bin k = DFT[k] / N.
void fft(std::span<std::int16_t, kN> re, std::span<std::int16_t, kN> im);

// |X[k]|^2 of an fft() result. Fits in 31 bits.
constexpr std::uint32_t power(std::int16_t re, std::int16_t im) {
    return static_cast<std::uint32_t>(re * re) + static_cast<std::uint32_t>(im * im);
}

}  // namespace fft_q15

namespace fft_q15 {

void fft(std::span<std::int16_t, kN> re, std::span<std::int16_t, kN> im) {
    for (std::size_t i = 0; i < kN; ++i) {
        const std::size_t j = bit_reverse(i);
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }

    for (std::size_t len = 2, step = kN / 2; len <= kN; len <<= 1, step >>= 1) {
        const std::size_t half = len / 2;
        for (std::size_t k = 0; k < half; ++k) {
            // W = exp(-j 2 pi k / len) = cos - j sin
            const std::int32_t wr = cos_q15(k * step);
            const std::int32_t wi = -sin_q15(k * step);
            for (std::size_t a = k; a < kN; a += len) {
                const std::size_t  b  = a + half;
                const std::int32_t br = re[b];
                const std::int32_t bi = im[b];
                const std::int32_t tr = (br * wr - bi * wi) >> 15;
                const std::int32_t ti = (br * wi + bi * wr) >> 15;
                const std::int32_t ar = re[a];
                const std::int32_t ai = im[a];
                re[a] = static_cast<std::int16_t>((ar + tr) >> 1);
                im[a] = static_cast<std::int16_t>((ai + ti) >> 1);
                re[b] = static_cast<std::int16_t>((ar - tr) >> 1);
                im[b] = static_cast<std::int16_t>((ai - ti) >> 1);
            }
        }
    }
}

}  // namespace fft_q15
