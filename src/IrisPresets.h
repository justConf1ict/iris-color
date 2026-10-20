#pragma once

#include "IrisDSP.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace iris {

// One registry shared by the native editor and offline renderer. The displayed
// name is derived from the real sound settings, so automation and state restore
// cannot leave a stale label behind. Musical key, gain and the explicitly
// selected core remain session context: the editor shows preset and core names
// separately, and loading a preset never upgrades a Legacy project silently.
inline constexpr int kPresetCount = 14;
inline constexpr const char* kPresetNames[kPresetCount] =
    {"Init", "Glass", "Clean", "Gun", "Water", "Bloom", "Dive", "Ripple", "Vowel", "Silk",
     "Crystal Punch", "Chrome Growl", "Wooden Pluck", "String Halo"};
inline constexpr const char* kPresetKeys[kPresetCount] =
    {"init", "glass", "clean", "gun", "water", "bloom", "dive", "ripple", "vowel", "silk",
     "crystal-punch", "chrome-growl", "wooden-pluck", "string-halo"};

inline const char* presetName(int index) noexcept {
    return index >= 0 && index < kPresetCount ? kPresetNames[index] : "Custom";
}
inline int presetIndex(const char* key) noexcept {
    if (key) for (int i = 0; i < kPresetCount; ++i)
        if (std::strcmp(key, kPresetKeys[i]) == 0) return i;
    return -1;
}
inline Parameters presetParameters(int index) noexcept {
    Parameters p;
    p.clarity = 35.f;
    switch (index) {
        case 1: p.color=150; p.morph=30; p.gateDb=-60; p.decayMs=220; p.transient=10; p.width=115; break;
        case 2: p.color=125; p.morph=55; p.gateDb=-48; p.decayMs=100; p.transient=20; p.width=100; break;
        case 3: p.color=170; p.morph=35; p.gateDb=-48; p.decayMs=120; p.transient=20; p.width=110; break;
        case 4: p.color=185; p.morph=20; p.gateDb=-66; p.decayMs=450; p.transient=5; p.width=135; break;
        case 5: p.color=180; p.morph=20; p.gateDb=-72; p.decayMs=450;
            p.attackTailEnabled=true; p.attackColor=10; p.tailColor=160; p.colorTransitionMs=90; break;
        case 6: p.color=185; p.morph=20; p.gateDb=-66; p.decayMs=220;
            p.pitchDiveEnabled=true; p.pitchDiveSemitones=12; p.pitchDiveMs=110; break;
        case 7: p.color=190; p.morph=20; p.gateDb=-66; p.decayMs=380;
            p.scaleMotionEnabled=true; p.scaleMotionDepth=90; p.scaleMotionSync=true; p.scaleMotionDivision=3; break;
        case 8: p.color=170; p.morph=25; p.gateDb=-66; p.decayMs=240;
            p.formantEnabled=true; p.formantMix=75; p.formantVowel=1.5f;
            p.formantMotion=60; p.formantRateHz=.5f; break;
        case 9: p.color=175; p.morph=25; p.gateDb=-60; p.decayMs=220;
            p.peakTamerEnabled=true; p.peakTamerAmount=70; p.peakTamerMaxDb=8; break;
        case 10: p.color=145; p.morph=32; p.gateDb=-54; p.decayMs=140;
            p.transient=30; p.width=115; p.clarity=58;
            p.bodyMaterial=0; p.bodyMix=35; p.bodyDecayMs=260;
            p.bodyDamping=32; p.bodyInharmonicity=28; p.bodyStrike=80; break;
        case 11: p.color=155; p.morph=25; p.gateDb=-54; p.decayMs=180;
            p.transient=25; p.width=108; p.clarity=48;
            p.bodyMaterial=1; p.bodyMix=32; p.bodyDecayMs=380;
            p.bodyDamping=38; p.bodyInharmonicity=65; p.bodyStrike=55; break;
        case 12: p.color=125; p.morph=40; p.gateDb=-48; p.decayMs=90;
            p.transient=35; p.width=100; p.clarity=48;
            p.bodyMaterial=2; p.bodyMix=42; p.bodyDecayMs=190;
            p.bodyDamping=72; p.bodyInharmonicity=40; p.bodyStrike=90; break;
        case 13: p.color=145; p.morph=24; p.gateDb=-60; p.decayMs=220;
            p.transient=20; p.width=120; p.clarity=45;
            p.bodyMaterial=3; p.bodyMix=30; p.bodyDecayMs=700;
            p.bodyDamping=42; p.bodyInharmonicity=15; p.bodyStrike=25; break;
        default: break;
    }
    return p;
}
inline int matchingPreset(const Parameters& p) noexcept {
    constexpr std::array<float Parameters::*, 36> soundFields {{
        &Parameters::color, &Parameters::snap, &Parameters::morph, &Parameters::gateDb,
        &Parameters::decayMs, &Parameters::lowHz, &Parameters::highHz,
        &Parameters::transient, &Parameters::width,
        &Parameters::bandColorLow, &Parameters::bandColorMid, &Parameters::bandColorHigh,
        &Parameters::colorSplitLowHz, &Parameters::colorSplitHighHz,
        &Parameters::bandEqLowDb, &Parameters::bandEqMidDb, &Parameters::bandEqHighDb,
        &Parameters::attackColor, &Parameters::tailColor, &Parameters::colorTransitionMs,
        &Parameters::pitchDiveSemitones, &Parameters::pitchDiveMs,
        &Parameters::scaleMotionDepth, &Parameters::scaleMotionRateHz,
        &Parameters::formantMix, &Parameters::formantVowel, &Parameters::formantMotion,
        &Parameters::formantRateHz, &Parameters::peakTamerAmount, &Parameters::peakTamerMaxDb,
        &Parameters::clarity, &Parameters::bodyMix, &Parameters::bodyDecayMs,
        &Parameters::bodyDamping, &Parameters::bodyInharmonicity, &Parameters::bodyStrike
    }};
    for (int i = 0; i < kPresetCount; ++i) {
        const Parameters expected = presetParameters(i);
        bool matches = p.morphEnabled == expected.morphEnabled && p.gateEnabled == expected.gateEnabled &&
            p.attackTailEnabled == expected.attackTailEnabled && p.pitchDiveEnabled == expected.pitchDiveEnabled &&
            p.scaleMotionEnabled == expected.scaleMotionEnabled && p.scaleMotionSync == expected.scaleMotionSync &&
            p.scaleMotionDivision == expected.scaleMotionDivision && p.formantEnabled == expected.formantEnabled &&
            p.peakTamerEnabled == expected.peakTamerEnabled && p.bodyMaterial == expected.bodyMaterial;
        for (const auto field : soundFields) {
            const float actual = p.*field, target = expected.*field;
            const float tolerance = std::max(0.0001f, std::abs(target) * 0.000001f);
            if (!std::isfinite(actual) || std::abs(actual - target) > tolerance) { matches = false; break; }
        }
        for (std::size_t band = 0; band < p.wetEqDb.size(); ++band)
            if (!std::isfinite(p.wetEqDb[band]) || std::abs(p.wetEqDb[band] - expected.wetEqDb[band]) > .0001f)
                matches = false;
        if (matches) return i;
    }
    return -1;
}

} // namespace iris
