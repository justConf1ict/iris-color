#pragma once

#include <array>
#include <cstdint>
#include <memory>

namespace iris {

inline constexpr int kVisualBins = 192;
inline constexpr float kVisualMinHz = 20.0f;
inline constexpr float kVisualMaxHz = 20000.0f;
struct VisualMove {
    float sourceHz = 0, targetHz = 0, levelDb = -120;
};
struct VisualFrame {
    // Log-spaced centres from kVisualMinHz through kVisualMaxHz inclusive.
    // dryDb is aligned original input; wetDb is post Morph/Gate/Color EQ/Width/band
    // replacement, before the additive time-domain Material Body layer,
    // final Color mix, output gain and soft limiter.
    std::array<float, kVisualBins> dryDb{}, wetDb{}, gateGain{};
    float sampleRate = 48000;
    uint64_t frameCounter = 0;
    uint16_t activeMask = 0, moveCount = 0;
    std::array<VisualMove, 32> moves{};
    float attackTailGain = 1, diveSemitones = 0, tamerReductionDb = 0, formantVowel = 0;
    int16_t motionPitchClass = -1;
    bool motionHostSynced = false;
    VisualFrame() noexcept { dryDb.fill(-120); wetDb.fill(-120); gateGain.fill(1); }
};

struct Parameters {
    float color = 160.0f;
    float snap = 100.0f;
    float morph = 30.0f;
    float gateDb = -60.0f;
    float decayMs = 200.0f;
    float lowHz = 180.0f;
    float highHz = 8000.0f;
    float transient = 10.0f;
    float width = 110.0f;
    float outputDb = -6.0f;
    // Amount applies only to the extra resonance above Color 100. EQ shapes
    // the complete wet spectrum after Morph/Gate, before dry band protection.
    float bandColorLow = 100.0f;
    float bandColorMid = 100.0f;
    float bandColorHigh = 100.0f;
    float bandEqLowDb = 0.0f;
    float bandEqMidDb = 0.0f;
    float bandEqHighDb = 0.0f;
    float colorSplitLowHz = 700.0f;
    float colorSplitHighHz = 3000.0f;
    // Creative processors are opt-in. Neutral additions preserve the 0.3
    // arithmetic path, including old saved projects.
    bool attackTailEnabled = false;
    float attackColor = 25.0f;
    float tailColor = 125.0f;
    float colorTransitionMs = 80.0f;
    bool pitchDiveEnabled = false;
    float pitchDiveSemitones = 12.0f;
    float pitchDiveMs = 100.0f;
    bool scaleMotionEnabled = false;
    float scaleMotionDepth = 75.0f;
    float scaleMotionRateHz = 4.0f;
    bool scaleMotionSync = true;
    // Whole, half, quarter, eighth, sixteenth, thirty-second note steps.
    int scaleMotionDivision = 3;
    bool formantEnabled = false;
    float formantMix = 50.0f;
    float formantVowel = 0.0f;
    float formantMotion = 0.0f;
    float formantRateHz = .5f;
    bool peakTamerEnabled = false;
    float peakTamerAmount = 50.0f;
    float peakTamerMaxDb = 6.0f;
    std::array<float, 16> wetEqDb{};
    int root = 0;
    int scale = 0;
    // Absolute pitch classes: bit 0=C, bit 4=E, bit 11=B. Independent of root.
    // An empty custom mask disables tuning and added resonance.
    uint16_t customMask = 0x0fff;
    bool morphEnabled = true;
    bool gateEnabled = true;
    bool midiEnabled = false;
    bool bypass = false;
    bool softLimit = true;
    // 0 preserves the released 0.4 core; 1 selects the new Prism core.
    // State migration, rather than this new-instance default, selects Legacy.
    int engineMode = 1;
    // Optional Prism voicing: a level-balanced tilt inside the selected band.
    // Zero retains the 0.5 arithmetic path. Adapters can choose a nonzero
    // new-instance default, but missing fields in older states must use zero.
    float clarity = 0.0f;
    // Opt-in original modal body. Zero Mix retains the released 0.6 signal
    // arithmetic. Decay is the longest modal T60 before material/HF losses.
    int bodyMaterial = 0; // 0 Glass, 1 Metal, 2 Wood, 3 String.
    float bodyMix = 0.0f;
    float bodyDecayMs = 360.0f;
    float bodyDamping = 45.0f;
    float bodyInharmonicity = 25.0f;
    float bodyStrike = 40.0f;
};

// Owns all working memory. prepare() may allocate; process(), parameter and MIDI
// updates never allocate or lock. Call updates on the processing thread, or while
// processing is stopped. Stereo and in-place buffers are supported.
class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;

    void prepare(double sampleRate, int coreFft = 2048, int spectralFft = 1024);
    void reset() noexcept;
    void setParameters(const Parameters&) noexcept;
    void noteOn(int note) noexcept;
    void noteOff(int note) noexcept;
    void allNotesOff() noexcept;
    // Called on the audio thread at the start of a contiguous process span.
    // Valid PPQ anchors tempo-synced motion, including host loops and seeks.
    // With no valid host position, an internal beat clock continues at tempo.
    void setTransport(double tempo, double quarterNotePosition, bool playing,
                      bool positionValid) noexcept;
    void process(const float* inL, const float* inR, float* outL, float* outR,
                 int n) noexcept;
    int latencySamples() const noexcept;
    double tailSeconds() const noexcept;
    // Call only on the same processing thread as process(), or while stopped.
    // Copies a coherent fixed-size snapshot without allocation or locks. A
    // plugin must provide its own thread-safe mailbox before the UI reads it.
    void copyVisual(VisualFrame&) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace iris
