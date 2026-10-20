#pragma once

#include "src/IrisDSP.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace iris::vst3 {

// IDs are a file-format and host-automation contract. Never reorder or reuse them.
enum Param : Steinberg::Vst::ParamID {
    kColor = 0, kSnap, kMorph, kGateDb, kDecayMs, kLowHz, kHighHz,
    kTransient, kWidth, kOutputDb, kRoot, kScale, kMorphEnabled,
    kGateEnabled, kMidiEnabled, kBypass, kSoftLimit, kCustomC,
    // 29/30 were already published as MIDI panic IDs in 0.1/0.2. They are
    // reserved even though new sound parameters follow them numerically.
    kAllNotesOff = 29,
    kAllSoundsOff = 30,
    kColorLow = 31, kColorMid = 32, kColorHigh = 33,
    kColorLowMidHz = 34, kColorMidHighHz = 35,
    kColorEqLowDb = 36, kColorEqMidDb = 37, kColorEqHighDb = 38,
    kAttackTailEnabled = 39,
    kAttackColor = 40,
    kTailColor = 41,
    kColorTransitionMs = 42,
    kPitchDiveEnabled = 43,
    kPitchDiveSemitones = 44,
    kPitchDiveMs = 45,
    kScaleMotionEnabled = 46,
    kScaleMotionDepth = 47,
    kScaleMotionRateHz = 48,
    kScaleMotionSync = 49,
    kScaleMotionDivision = 50,
    kFormantEnabled = 51,
    kFormantMix = 52,
    kFormantVowel = 53,
    kFormantMotion = 54,
    kFormantRateHz = 55,
    kPeakTamerEnabled = 56,
    kPeakTamerAmount = 57,
    kPeakTamerMaxDb = 58,
    kWetEqBand0 = 59, kWetEqBand15 = 74,
    kEngineMode = 75,
    kClarity = 76,
    kBodyMaterial = 77,
    kBodyMix = 78,
    kBodyDecayMs = 79,
    kBodyDamping = 80,
    kBodyInharmonicity = 81,
    kBodyStrike = 82,
    kParameterCount = 83, // ID-indexed storage extent, including two reserved panic IDs
    kTotalParameterCount = 83
};
inline constexpr Steinberg::uint32 kSoundParameterCount = 81;
inline constexpr bool isSoundParameter(Steinberg::Vst::ParamID id) noexcept {
    return id < kParameterCount && id != kAllNotesOff && id != kAllSoundsOff;
}

struct ParameterSpec {
    const char* name;
    const char* units;
    double minimum, maximum, initial;
    int steps;
};

inline constexpr std::array<ParameterSpec, kParameterCount> kSpecs {{
    {"Color", "%", 0, 200, 160, 0},
    {"Snap", "%", 0, 100, 100, 0},
    {"Morph", "%", 0, 100, 30, 0},
    {"Gate threshold", "dB", -96, 0, -60, 0},
    {"Decay", "ms", 25, 1500, 200, 0},
    {"Low frequency", "Hz", 60, 1500, 180, 0},
    {"High frequency", "Hz", 1000, 18000, 8000, 0},
    {"Transient", "%", 0, 100, 10, 0},
    {"Width", "%", 0, 160, 110, 0},
    {"Output", "dB", -24, 12, -6, 0},
    {"Root", "", 0, 11, 0, 11},
    {"Scale", "", 0, 5, 0, 5},
    {"Morph enabled", "", 0, 1, 1, 1},
    {"Gate enabled", "", 0, 1, 1, 1},
    {"MIDI enabled", "", 0, 1, 0, 1},
    {"Bypass", "", 0, 1, 0, 1},
    {"Soft limit", "", 0, 1, 1, 1},
    {"Custom C", "", 0, 1, 1, 1},
    {"Custom C#", "", 0, 1, 1, 1},
    {"Custom D", "", 0, 1, 1, 1},
    {"Custom D#", "", 0, 1, 1, 1},
    {"Custom E", "", 0, 1, 1, 1},
    {"Custom F", "", 0, 1, 1, 1},
    {"Custom F#", "", 0, 1, 1, 1},
    {"Custom G", "", 0, 1, 1, 1},
    {"Custom G#", "", 0, 1, 1, 1},
    {"Custom A", "", 0, 1, 1, 1},
    {"Custom A#", "", 0, 1, 1, 1},
    {"Custom B", "", 0, 1, 1, 1},
    {"MIDI All Notes Off", "", 0, 1, 0, 0}, // reserved; never serialized as sound
    {"MIDI All Sounds Off", "", 0, 1, 0, 0},
    {"Low color", "%", 0, 200, 100, 0},
    {"Mid color", "%", 0, 200, 100, 0},
    {"High color", "%", 0, 200, 100, 0},
    {"Color low-mid split", "Hz", 120, 2000, 700, 0},
    {"Color mid-high split", "Hz", 1000, 12000, 3000, 0},
    {"Color EQ low", "dB", -12, 12, 0, 0},
    {"Color EQ mid", "dB", -12, 12, 0, 0},
    {"Color EQ high", "dB", -12, 12, 0, 0},
    {"Attack Tail enabled", "", 0, 1, 0, 1},
    {"Attack color", "%", 0, 200, 25, 0},
    {"Tail color", "%", 0, 200, 125, 0},
    {"Color transition", "ms", 5, 500, 80, 0},
    {"Pitch Dive enabled", "", 0, 1, 0, 1},
    {"Pitch Dive interval", "st", -24, 24, 12, 0},
    {"Pitch Dive time", "ms", 10, 1000, 100, 0},
    {"Scale Motion enabled", "", 0, 1, 0, 1},
    {"Scale Motion depth", "%", 0, 100, 75, 0},
    {"Scale Motion rate", "Hz", 0.25, 20, 4, 0},
    {"Scale Motion sync", "", 0, 1, 1, 1},
    {"Scale Motion division", "", 0, 5, 3, 5},
    {"Formant enabled", "", 0, 1, 0, 1},
    {"Formant mix", "%", 0, 100, 50, 0},
    {"Formant vowel", "", 0, 4, 0, 0},
    {"Formant motion", "%", 0, 100, 0, 0},
    {"Formant rate", "Hz", 0.05, 8, 0.5, 0},
    {"Peak Tamer enabled", "", 0, 1, 0, 1},
    {"Peak Tamer amount", "%", 0, 100, 50, 0},
    {"Peak Tamer max reduction", "dB", 0, 18, 6, 0},
    {"Wet EQ 80 Hz", "dB", -24, 12, 0, 0},
    {"Wet EQ 114 Hz", "dB", -24, 12, 0, 0},
    {"Wet EQ 162 Hz", "dB", -24, 12, 0, 0},
    {"Wet EQ 231 Hz", "dB", -24, 12, 0, 0},
    {"Wet EQ 329 Hz", "dB", -24, 12, 0, 0},
    {"Wet EQ 468 Hz", "dB", -24, 12, 0, 0},
    {"Wet EQ 666 Hz", "dB", -24, 12, 0, 0},
    {"Wet EQ 948 Hz", "dB", -24, 12, 0, 0},
    {"Wet EQ 1350 Hz", "dB", -24, 12, 0, 0},
    {"Wet EQ 1922 Hz", "dB", -24, 12, 0, 0},
    {"Wet EQ 2736 Hz", "dB", -24, 12, 0, 0},
    {"Wet EQ 3895 Hz", "dB", -24, 12, 0, 0},
    {"Wet EQ 5545 Hz", "dB", -24, 12, 0, 0},
    {"Wet EQ 7894 Hz", "dB", -24, 12, 0, 0},
    {"Wet EQ 11239 Hz", "dB", -24, 12, 0, 0},
    {"Wet EQ 16000 Hz", "dB", -24, 12, 0, 0},
    {"Core", "", 0, 1, 1, 1},
    {"Clarity", "%", 0, 100, 35, 0},
    {"Body material", "", 0, 3, 0, 3},
    {"Body amount", "%", 0, 100, 0, 0},
    {"Body decay", "ms", 40, 1800, 360, 0},
    {"Body damping", "%", 0, 100, 45, 0},
    {"Body inharmonicity", "%", 0, 100, 25, 0},
    {"Body strike", "%", 0, 100, 40, 0},
}};

inline constexpr const char* kNoteNames[] =
    {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
inline constexpr const char* kScaleNames[] =
    {"Minor", "Major", "Minor pentatonic", "Major pentatonic", "Chromatic", "Custom"};
inline constexpr const char* kMotionDivisionNames[] = {"1/1", "1/2", "1/4", "1/8", "1/16", "1/32"};
inline constexpr const char* kEngineModeNames[] = {"Legacy", "Prism"};
inline constexpr const char* kBodyMaterialNames[] = {"Glass", "Metal", "Wood", "String"};
using Values = std::array<double, kParameterCount>;

inline double sanitize(Steinberg::Vst::ParamID id, double value) noexcept {
    if (!isSoundParameter(id)) return 0.0;
    const auto& s = kSpecs[id];
    if (!std::isfinite(value)) value = (s.initial - s.minimum) / (s.maximum - s.minimum);
    value = std::clamp(value, 0.0, 1.0);
    // Match the SDK's StringListParameter / FromNormalized discrete buckets.
    // Host automation may contain values between the canonical list positions.
    if (s.steps > 1) value = std::min<double>(s.steps, std::floor(value * (s.steps + 1))) / s.steps;
    else if (s.steps == 1) value = std::round(value);
    return value;
}
inline double normalized(Steinberg::Vst::ParamID id, double plain) noexcept {
    if (!isSoundParameter(id)) return 0.0;
    const auto& s = kSpecs[id];
    return sanitize(id, (plain - s.minimum) / (s.maximum - s.minimum));
}
inline double plain(Steinberg::Vst::ParamID id, double value) noexcept {
    if (!isSoundParameter(id)) return 0.0;
    const auto& s = kSpecs[id];
    return s.minimum + sanitize(id, value) * (s.maximum - s.minimum);
}
inline int discrete(Steinberg::Vst::ParamID id, double value) noexcept {
    return static_cast<int>(std::lround(plain(id, value)));
}
inline Values defaults() noexcept {
    Values v{};
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = normalized(static_cast<Param>(i), kSpecs[i].initial);
    return v;
}
inline Parameters toDSP(const Values& v) noexcept {
    Parameters p;
    p.color = static_cast<float>(plain(kColor, v[kColor]));
    p.snap = static_cast<float>(plain(kSnap, v[kSnap]));
    p.morph = static_cast<float>(plain(kMorph, v[kMorph]));
    p.gateDb = static_cast<float>(plain(kGateDb, v[kGateDb]));
    p.decayMs = static_cast<float>(plain(kDecayMs, v[kDecayMs]));
    p.lowHz = static_cast<float>(plain(kLowHz, v[kLowHz]));
    p.highHz = static_cast<float>(plain(kHighHz, v[kHighHz]));
    p.transient = static_cast<float>(plain(kTransient, v[kTransient]));
    p.width = static_cast<float>(plain(kWidth, v[kWidth]));
    p.outputDb = static_cast<float>(plain(kOutputDb, v[kOutputDb]));
    p.bandColorLow = static_cast<float>(plain(kColorLow, v[kColorLow]));
    p.bandColorMid = static_cast<float>(plain(kColorMid, v[kColorMid]));
    p.bandColorHigh = static_cast<float>(plain(kColorHigh, v[kColorHigh]));
    p.colorSplitLowHz = static_cast<float>(plain(kColorLowMidHz, v[kColorLowMidHz]));
    p.colorSplitHighHz = static_cast<float>(plain(kColorMidHighHz, v[kColorMidHighHz]));
    p.bandEqLowDb = static_cast<float>(plain(kColorEqLowDb, v[kColorEqLowDb]));
    p.bandEqMidDb = static_cast<float>(plain(kColorEqMidDb, v[kColorEqMidDb]));
    p.bandEqHighDb = static_cast<float>(plain(kColorEqHighDb, v[kColorEqHighDb]));
    p.attackTailEnabled = v[kAttackTailEnabled] >= 0.5;
    p.attackColor = static_cast<float>(plain(kAttackColor, v[kAttackColor]));
    p.tailColor = static_cast<float>(plain(kTailColor, v[kTailColor]));
    p.colorTransitionMs = static_cast<float>(plain(kColorTransitionMs, v[kColorTransitionMs]));
    p.pitchDiveEnabled = v[kPitchDiveEnabled] >= 0.5;
    p.pitchDiveSemitones = static_cast<float>(plain(kPitchDiveSemitones, v[kPitchDiveSemitones]));
    p.pitchDiveMs = static_cast<float>(plain(kPitchDiveMs, v[kPitchDiveMs]));
    p.scaleMotionEnabled = v[kScaleMotionEnabled] >= 0.5;
    p.scaleMotionDepth = static_cast<float>(plain(kScaleMotionDepth, v[kScaleMotionDepth]));
    p.scaleMotionRateHz = static_cast<float>(plain(kScaleMotionRateHz, v[kScaleMotionRateHz]));
    p.scaleMotionSync = v[kScaleMotionSync] >= 0.5;
    p.scaleMotionDivision = discrete(kScaleMotionDivision, v[kScaleMotionDivision]);
    p.formantEnabled = v[kFormantEnabled] >= 0.5;
    p.formantMix = static_cast<float>(plain(kFormantMix, v[kFormantMix]));
    p.formantVowel = static_cast<float>(plain(kFormantVowel, v[kFormantVowel]));
    p.formantMotion = static_cast<float>(plain(kFormantMotion, v[kFormantMotion]));
    p.formantRateHz = static_cast<float>(plain(kFormantRateHz, v[kFormantRateHz]));
    p.peakTamerEnabled = v[kPeakTamerEnabled] >= 0.5;
    p.peakTamerAmount = static_cast<float>(plain(kPeakTamerAmount, v[kPeakTamerAmount]));
    p.peakTamerMaxDb = static_cast<float>(plain(kPeakTamerMaxDb, v[kPeakTamerMaxDb]));
    for (unsigned i = 0; i < 16; ++i) p.wetEqDb[i] = static_cast<float>(plain(kWetEqBand0 + i, v[kWetEqBand0 + i]));
    p.root = discrete(kRoot, v[kRoot]);
    p.scale = discrete(kScale, v[kScale]);
    p.morphEnabled = v[kMorphEnabled] >= 0.5;
    p.gateEnabled = v[kGateEnabled] >= 0.5;
    p.midiEnabled = v[kMidiEnabled] >= 0.5;
    p.bypass = v[kBypass] >= 0.5;
    p.softLimit = v[kSoftLimit] >= 0.5;
    p.engineMode = discrete(kEngineMode, v[kEngineMode]);
    p.clarity = static_cast<float>(plain(kClarity, v[kClarity]));
    p.bodyMaterial = discrete(kBodyMaterial, v[kBodyMaterial]);
    p.bodyMix = static_cast<float>(plain(kBodyMix, v[kBodyMix]));
    p.bodyDecayMs = static_cast<float>(plain(kBodyDecayMs, v[kBodyDecayMs]));
    p.bodyDamping = static_cast<float>(plain(kBodyDamping, v[kBodyDamping]));
    p.bodyInharmonicity = static_cast<float>(plain(kBodyInharmonicity, v[kBodyInharmonicity]));
    p.bodyStrike = static_cast<float>(plain(kBodyStrike, v[kBodyStrike]));
    p.customMask = 0;
    for (unsigned note = 0; note < 12; ++note)
        if (v[kCustomC + note] >= 0.5) p.customMask |= static_cast<std::uint16_t>(1u << note);
    return p;
}
inline Values fromDSP(const Parameters& p) noexcept {
    Values v = defaults();
    const auto set = [&](Param id, double value) { v[id] = normalized(id, value); };
    set(kColor, p.color); set(kSnap, p.snap); set(kMorph, p.morph);
    set(kGateDb, p.gateDb); set(kDecayMs, p.decayMs);
    set(kLowHz, p.lowHz); set(kHighHz, p.highHz);
    set(kTransient, p.transient); set(kWidth, p.width); set(kOutputDb, p.outputDb);
    set(kRoot, p.root); set(kScale, p.scale);
    set(kMorphEnabled, p.morphEnabled); set(kGateEnabled, p.gateEnabled);
    set(kMidiEnabled, p.midiEnabled); set(kBypass, p.bypass); set(kSoftLimit, p.softLimit);
    for (unsigned note = 0; note < 12; ++note)
        set(static_cast<Param>(kCustomC + note), (p.customMask >> note) & 1u);
    set(kColorLow, p.bandColorLow); set(kColorMid, p.bandColorMid); set(kColorHigh, p.bandColorHigh);
    set(kColorLowMidHz, p.colorSplitLowHz); set(kColorMidHighHz, p.colorSplitHighHz);
    set(kColorEqLowDb, p.bandEqLowDb); set(kColorEqMidDb, p.bandEqMidDb); set(kColorEqHighDb, p.bandEqHighDb);
    set(kAttackTailEnabled, p.attackTailEnabled);
    set(kAttackColor, p.attackColor);
    set(kTailColor, p.tailColor);
    set(kColorTransitionMs, p.colorTransitionMs);
    set(kPitchDiveEnabled, p.pitchDiveEnabled);
    set(kPitchDiveSemitones, p.pitchDiveSemitones);
    set(kPitchDiveMs, p.pitchDiveMs);
    set(kScaleMotionEnabled, p.scaleMotionEnabled);
    set(kScaleMotionDepth, p.scaleMotionDepth);
    set(kScaleMotionRateHz, p.scaleMotionRateHz);
    set(kScaleMotionSync, p.scaleMotionSync);
    set(kScaleMotionDivision, p.scaleMotionDivision);
    set(kFormantEnabled, p.formantEnabled);
    set(kFormantMix, p.formantMix);
    set(kFormantVowel, p.formantVowel);
    set(kFormantMotion, p.formantMotion);
    set(kFormantRateHz, p.formantRateHz);
    set(kPeakTamerEnabled, p.peakTamerEnabled);
    set(kPeakTamerAmount, p.peakTamerAmount);
    set(kPeakTamerMaxDb, p.peakTamerMaxDb);
    for (unsigned i = 0; i < 16; ++i) set(static_cast<Param>(kWetEqBand0 + i), p.wetEqDb[i]);
    set(kEngineMode, p.engineMode);
    set(kClarity, p.clarity);
    set(kBodyMaterial, p.bodyMaterial);
    set(kBodyMix, p.bodyMix);
    set(kBodyDecayMs, p.bodyDecayMs);
    set(kBodyDamping, p.bodyDamping);
    set(kBodyInharmonicity, p.bodyInharmonicity);
    set(kBodyStrike, p.bodyStrike);
    return v;
}
inline std::uint16_t activeMask(const Values& v) noexcept {
    const int scale = discrete(kScale, v[kScale]);
    if (scale == 5) return toDSP(v).customMask;
    constexpr std::uint16_t masks[] = {0x5AD, 0xAB5, 0x4A9, 0x295, 0xFFF};
    const unsigned root = static_cast<unsigned>(discrete(kRoot, v[kRoot]));
    const std::uint16_t mask = masks[std::clamp(scale, 0, 4)];
    return static_cast<std::uint16_t>(((mask << root) | (mask >> (12 - root))) & 0xFFF);
}

// Atomic update of a complete decoded snapshot: malformed/truncated streams never partially apply.
bool readState(Steinberg::IBStream* stream, Values& values);
bool writeState(Steinberg::IBStream* stream, const Values& values);

} // namespace iris::vst3
