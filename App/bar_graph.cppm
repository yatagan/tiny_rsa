// Bar-graph dynamics for an 8-row LED matrix: instant attack, linear decay
// and a falling peak-hold dot per column. Integer math only (no FPU).
//
// Feed update() one 0..255 target level per column once per frame; it
// returns column bitmaps ready for max7219::Max7219::set_columns().
export module bar_graph;
import std;

namespace bar_graph {

constexpr std::uint8_t kDecayPerFrame    = 6;   // level units (0..255) per frame
constexpr std::uint8_t kPeakHoldFrames   = 12;
constexpr std::uint8_t kPeakFallPerFrame = 3;

// 0..255 level -> 0..8 bar height.
constexpr std::uint8_t height_from_level(std::uint8_t level) {
    return static_cast<std::uint8_t>((level + 16) / 32);
}

// Bar of height h: the bottom h rows lit (bit r = row r, r = 0 top).
constexpr std::uint8_t column_mask(std::uint8_t h) {
    return static_cast<std::uint8_t>(0xFFu << (8 - h));
}

}  // namespace bar_graph

export namespace bar_graph {

template <std::size_t N>
class BarGraph {
public:
    constexpr BarGraph() = default;

    std::array<std::uint8_t, N> update(std::span<const std::uint8_t, N> targets) {
        std::array<std::uint8_t, N> cols{};
        for (std::size_t x = 0; x < N; ++x) {
            std::uint8_t& lvl = level_[x];
            if (targets[x] > lvl) {
                lvl = targets[x];  // instant attack
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
        return cols;
    }

private:
    std::array<std::uint8_t, N> level_{};
    std::array<std::uint8_t, N> peak_{};
    std::array<std::uint8_t, N> hold_{};
};

}  // namespace bar_graph
