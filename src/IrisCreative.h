#pragma once

#include <array>
#include <algorithm>
#include <cmath>

namespace iris {
inline constexpr int kGraphicEqBands = 16;
inline constexpr float kGraphicEqMinHz = 80.0f;
inline constexpr float kGraphicEqMaxHz = 16000.0f;

// Sixteen log-spaced nodes, linear interpolation in log frequency and dB.
// Frequencies beyond the endpoints retain the corresponding endpoint gain.
inline float graphicEqNodeHz(int index) noexcept {
    index = std::max(0, std::min(index, kGraphicEqBands - 1));
    return kGraphicEqMinHz * std::pow(kGraphicEqMaxHz / kGraphicEqMinHz,
                                     float(index) / (kGraphicEqBands - 1));
}
inline float graphicEqDbAt(float hz, const std::array<float, kGraphicEqBands>& db) noexcept {
    if (!(hz > kGraphicEqMinHz)) return db.front();
    if (hz >= kGraphicEqMaxHz) return db.back();
    const float position = std::log(hz / kGraphicEqMinHz)
        * ((kGraphicEqBands - 1) / std::log(kGraphicEqMaxHz / kGraphicEqMinHz));
    const int left = std::max(0, std::min(int(position), kGraphicEqBands - 2));
    const float mix = position - left;
    return db[left] + mix * (db[left + 1] - db[left]);
}

// Three broad Gaussian formants; vowel 0..4 continuously interpolates A/E/I/O/U.
// The value is a wet-only response in dB, before the user's Mix amount.
inline float formantDbAt(float hz, float vowel) noexcept {
    if (!(hz > 0)) return -6;
    vowel = std::isfinite(vowel) ? std::max(0.0f, std::min(vowel, 4.0f)) : 0;
    static constexpr float centres[5][3] = {
        {800, 1150, 2900}, {400, 1700, 2600}, {300, 2200, 3000},
        {450, 800, 2830}, {325, 700, 2530}};
    static constexpr float widths[3] = {.30f, .24f, .22f};
    static constexpr float boosts[3] = {12, 9, 6};
    const int left = std::min(3, int(vowel));
    const float mix = vowel - left;
    float db = -6;
    for (int i = 0; i < 3; ++i) {
        const float centre = centres[left][i] * std::pow(centres[left + 1][i] / centres[left][i], mix);
        const float distance = std::log2(hz / centre) / widths[i];
        db += boosts[i] * std::exp(-.5f * distance * distance);
    }
    return db;
}
} // namespace iris
