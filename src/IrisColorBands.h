#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace iris {

// Shared by DSP and the EQ display. Each crossover spans one octave in log
// frequency; its centre is the displayed split frequency. With ordered splits,
// these nonnegative weights form a continuous partition of unity. They are
// spectral weights, not three time-domain filters with additional phase/latency.
inline std::array<float, 3> colorBandWeights(float hz, float splitLow, float splitHigh) noexcept {
    splitLow = std::isfinite(splitLow) ? std::max(1.0f, splitLow) : 700.0f;
    splitHigh = std::isfinite(splitHigh) ? std::max(splitLow, splitHigh) : std::max(splitLow, 3000.0f);
    if (!(hz > 0)) return {1.0f, 0.0f, 0.0f};
    const auto transition = [hz](float split) noexcept {
        const float t = std::max(0.0f, std::min(1.0f, std::log2(hz / split) + .5f));
        return t * t * (3.0f - 2.0f * t);
    };
    const float lowToMid = transition(splitLow), midToHigh = transition(splitHigh);
    return {1.0f - lowToMid, lowToMid - midToHigh, midToHigh};
}

} // namespace iris
