#include "IrisDSP.h"
#include "IrisColorBands.h"
#include "IrisCreative.h"
#include "IrisPrism.h"
#include "IrisBody.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace iris {
namespace {
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double twoPi = 2.0 * pi;
using Complex = std::complex<float>;
#if defined(__x86_64__) || defined(_M_X64)
// MXCSR belongs to the host's audio thread. Flush subnormal operands/results
// only inside this callback, then restore every original control/status bit.
class ScopedDenormalsOff {
public:
    ScopedDenormalsOff() noexcept : saved_(_mm_getcsr()) {
        _mm_setcsr(saved_ | 0x8040u); // FTZ (bit 15), DAZ (bit 6).
    }
    ~ScopedDenormalsOff() noexcept { _mm_setcsr(saved_); }
    ScopedDenormalsOff(const ScopedDenormalsOff&) = delete;
    ScopedDenormalsOff& operator=(const ScopedDenormalsOff&) = delete;
private:
    unsigned saved_;
};
#endif
float finiteClamp(float x, float fallback, float lo, float hi) noexcept {
    if (!std::isfinite(x)) x = fallback;
    return std::max(lo, std::min(x, hi));
}
float clean(float x) noexcept { return std::isfinite(x) ? x : 0.0f; }
double wrap(double x) noexcept { return x - twoPi * std::floor((x + pi) / twoPi); }
float approach(float old, float goal, float alpha) noexcept {
    const float v = old + alpha * (goal - old);
    // A float one-pole can otherwise stop a few ulps before a nonzero goal.
    return v == old || std::abs(v - goal) < 1.0e-6f ? goal : v;
}
float kneeLimit(float x) noexcept {
    const float a = std::abs(x);
    if (a <= 0.98f) return x;
    return std::copysign(0.98f + 0.02f * std::tanh((a - 0.98f) * 50.0f), x);
}
Parameters sanitized(Parameters p, double fs) noexcept {
    p.color = finiteClamp(p.color, 160, 0, 200);
    p.snap = finiteClamp(p.snap, 100, 0, 100);
    p.morph = finiteClamp(p.morph, 30, 0, 100);
    p.gateDb = finiteClamp(p.gateDb, -60, -96, 0);
    p.decayMs = finiteClamp(p.decayMs, 200, 25, 1500);
    p.lowHz = finiteClamp(p.lowHz, 180, 60, static_cast<float>(std::min(1500.0, fs * .2)));
    p.highHz = finiteClamp(p.highHz, 8000, p.lowHz + 20, static_cast<float>(std::min(18000.0, fs * .45)));
    p.transient = finiteClamp(p.transient, 10, 0, 100);
    p.width = finiteClamp(p.width, 110, 0, 160);
    p.outputDb = finiteClamp(p.outputDb, -6, -24, 12);
    p.bandColorLow = finiteClamp(p.bandColorLow, 100, 0, 200);
    p.bandColorMid = finiteClamp(p.bandColorMid, 100, 0, 200);
    p.bandColorHigh = finiteClamp(p.bandColorHigh, 100, 0, 200);
    p.bandEqLowDb = finiteClamp(p.bandEqLowDb, 0, -12, 12);
    p.bandEqMidDb = finiteClamp(p.bandEqMidDb, 0, -12, 12);
    p.bandEqHighDb = finiteClamp(p.bandEqHighDb, 0, -12, 12);
    // These are spectral weights, so a split above the audible range is
    // harmless. Keep its requested value at low sample rates; the actual
    // resonators and FFT bins remain strictly inside their Nyquist guards.
    p.colorSplitLowHz = finiteClamp(p.colorSplitLowHz, 700, 120, 2000);
    p.colorSplitHighHz = finiteClamp(p.colorSplitHighHz, 3000, 1000, 12000);
    p.colorSplitHighHz = std::max(p.colorSplitHighHz, p.colorSplitLowHz * 1.25f);
    p.attackColor = finiteClamp(p.attackColor, 25, 0, 200);
    p.tailColor = finiteClamp(p.tailColor, 125, 0, 200);
    p.colorTransitionMs = finiteClamp(p.colorTransitionMs, 80, 5, 500);
    p.pitchDiveSemitones = finiteClamp(p.pitchDiveSemitones, 12, -24, 24);
    p.pitchDiveMs = finiteClamp(p.pitchDiveMs, 100, 10, 1000);
    p.scaleMotionDepth = finiteClamp(p.scaleMotionDepth, 75, 0, 100);
    p.scaleMotionRateHz = finiteClamp(p.scaleMotionRateHz, 4, .25f, 20);
    p.scaleMotionDivision = std::max(0, std::min(p.scaleMotionDivision, 5));
    p.formantMix = finiteClamp(p.formantMix, 50, 0, 100);
    p.formantVowel = finiteClamp(p.formantVowel, 0, 0, 4);
    p.formantMotion = finiteClamp(p.formantMotion, 0, 0, 100);
    p.formantRateHz = finiteClamp(p.formantRateHz, .5f, .05f, 8);
    p.peakTamerAmount = finiteClamp(p.peakTamerAmount, 50, 0, 100);
    p.peakTamerMaxDb = finiteClamp(p.peakTamerMaxDb, 6, 0, 18);
    for (auto& db : p.wetEqDb) db = finiteClamp(db, 0, -24, 12);
    p.root = std::max(0, std::min(p.root, 11));
    p.scale = std::max(0, std::min(p.scale, 5));
    p.customMask &= 0x0fff;
    p.engineMode = std::max(0, std::min(p.engineMode, 1));
    p.clarity = finiteClamp(p.clarity, 0, 0, 100);
    p.bodyMaterial = std::max(0, std::min(p.bodyMaterial, 3));
    p.bodyMix = finiteClamp(p.bodyMix, 0, 0, 100);
    p.bodyDecayMs = finiteClamp(p.bodyDecayMs, 360, 40, 1800);
    p.bodyDamping = finiteClamp(p.bodyDamping, 45, 0, 100);
    p.bodyInharmonicity = finiteClamp(p.bodyInharmonicity, 25, 0, 100);
    p.bodyStrike = finiteClamp(p.bodyStrike, 40, 0, 100);
    return p;
}
struct FFT {
    int size = 0;
    std::vector<unsigned> reversed;
    std::vector<Complex> roots;
    void prepare(int n) {
        size = n; reversed.resize(n); roots.resize(n / 2);
        int bits = 0; for (int q = n; q > 1; q >>= 1) ++bits;
        for (int i = 0; i < n; ++i) {
            unsigned v = static_cast<unsigned>(i), r = 0;
            for (int b = 0; b < bits; ++b) { r = (r << 1) | (v & 1u); v >>= 1; }
            reversed[i] = r;
        }
        for (int k = 0; k < n / 2; ++k)
            roots[k] = Complex(static_cast<float>(std::cos(-twoPi * k / n)), static_cast<float>(std::sin(-twoPi * k / n)));
    }
    void transform(Complex* a, bool inverse) const noexcept {
        for (int i = 0; i < size; ++i)
            if (reversed[i] > static_cast<unsigned>(i)) std::swap(a[i], a[reversed[i]]);
        for (int len = 2; len <= size; len <<= 1) {
            const int half = len / 2, stride = size / len;
            for (int base = 0; base < size; base += len) for (int k = 0; k < half; ++k) {
                Complex w = roots[k * stride];
                if (inverse) w = std::conj(w);
                const Complex u = a[base + k], v = a[base + k + half] * w;
                a[base + k] = u + v; a[base + k + half] = u - v;
            }
        }
        if (inverse) {
            const float scale = 1.0f / size;
            for (int i = 0; i < size; ++i) a[i] *= scale;
        }
    }
};
// Causal streaming WOLA: each frame ending at n is scheduled at n+1.
// Its input began at n-N+1, so exact latency is N. With periodic Hann,
// H=N/4, the sum of squared windows is 3/2.
struct Wola {
    int size = 0, hop = 0, channels = 0, write = 0, read = 0, count = 0;
    FFT fft;
    std::vector<float> window;
    std::array<std::vector<float>, 6> history;
    std::array<std::vector<float>, 2> output;
    std::array<std::vector<Complex>, 6> spectrum;
    void prepare(int n, int numChannels) {
        size = n; hop = n / 4; channels = numChannels; fft.prepare(n); window.resize(n);
        for (int i = 0; i < n; ++i) window[i] = static_cast<float>(.5 - .5 * std::cos(twoPi * i / n));
        for (int c = 0; c < channels; ++c) { history[c].resize(n); spectrum[c].resize(n); }
        for (auto& o : output) o.resize(n * 2);
        reset();
    }
    void reset() noexcept {
        write = read = count = 0;
        for (auto& x : history) std::fill(x.begin(), x.end(), 0.0f);
        for (auto& x : output) std::fill(x.begin(), x.end(), 0.0f);
        for (auto& x : spectrum) std::fill(x.begin(), x.end(), Complex{});
    }
    bool push(const float* input, float* result) noexcept {
        for (int c = 0; c < channels; ++c) history[c][write] = input[c];
        if (++write == size) write = 0;
        for (int c = 0; c < 2; ++c) { result[c] = output[c][read]; output[c][read] = 0; }
        if (++read == size * 2) read = 0;
        if (++count != hop) return false;
        count = 0;
        for (int c = 0; c < channels; ++c) {
            // Zero-phase window order avoids phase cancellation when moving a
            // peak's main lobe by a fractional number of frequency bins.
            for (int i = 0; i < size; ++i)
                spectrum[c][(i + size / 2) % size] = Complex(history[c][(write + i) % size] * window[i], 0);
            fft.transform(spectrum[c].data(), false);
        }
        return true;
    }
    void commit() noexcept {
        for (int c = 0; c < 2; ++c) {
            fft.transform(spectrum[c].data(), true);
            for (int i = 0; i < size; ++i)
                output[c][(read + i) % (size * 2)] += spectrum[c][(i + size / 2) % size].real() * window[i] * (2.0f / 3.0f);
        }
    }
};
struct Delay {
    std::vector<float> data;
    size_t pos = 0;
    void prepare(int n) { data.assign(static_cast<size_t>(std::max(1, n)), 0.0f); pos = 0; }
    void reset() noexcept { std::fill(data.begin(), data.end(), 0.0f); pos = 0; }
    float push(float x) noexcept {
        const float y = data[pos]; data[pos] = x;
        if (++pos == data.size()) pos = 0;
        return y;
    }
};
struct Peak {
    int bin = 0;
    double frequency = 0, target = 0;
    float magnitude = 0;
    int attackFrames = 0;
    std::array<double, 2> phase{};
    std::array<Complex, 2> rotation{};
    bool used = false;
};
struct Resonator {
    double re = 0, im = 0;
    double frequency = 0, movedRe = 0, movedIm = 0, stepRe = 0, stepIm = 0;
    std::array<double, 2> zr{}, zi{};
    float gain = 0, active = 0;
    void clear() noexcept { zr = {}; zi = {}; gain = active = 0; movedRe = re; movedIm = im; stepRe = stepIm = 0; }
};
} // namespace

struct Engine::Impl {
    double fs = 48000;
    int latency = 0;
    bool prepared = false, started = false, havePreviousFrame = false;
    Parameters target{}, current{};
    float smoothing = 0, bypassMix = 0, gain = 1;
    float fastEnvelope = 0, slowEnvelope = 0, onsetEnvelope = 0;
    float fastAttack = 0, fastRelease = 0, slowAttack = 0, slowRelease = 0, onsetRelease = 0;
    float resonanceSmoothing = 0, resonanceInput = 0;
    std::array<float, 3> resonanceCalibration{{.65f, .65f, .65f}};
    std::array<double, 3> resonanceReference{};
    Wola core, spectral;
    PrismCore prism;
    MaterialBody body;
    bool bodyRunning = false;
    float prismMix = 1;
    std::array<Delay, 2> prismOuterDelay;
    VisualFrame visual;
    std::array<Delay, 2> coreDry, finalDry;
    Delay onsetDelay;
    Delay colorEnvelopeDelay;
    float attackTailMix = 0, pitchDiveMix = 0, scaleMotionMix = 0, formantEnableMix = 0, tamerMix = 0;
    float creativeFast = 0, creativeSlow = 0, creativeAttack = 0, creativeRelease = 0;
    float creativeSlowAttack = 0, creativeSlowRelease = 0;
    bool onsetArmed = true;
    double onsetAge = 10, motionStep = 0, formantPhase = 0;
    float colorEnvelope = 1, diveOffset = 0, liveVowel = 0;
    int poleCountdown = 0;
    double transportTempo = 120, transportPpq = 0, transportBeatPhase = 0;
    uint64_t transportSamples = 0;
    bool transportPlaying = true, transportPositionValid = false;
    std::array<float, 12> motionClassGain{};
    int motionClass = -1;
    std::array<std::vector<Complex>, 2> creativeWet;
    std::vector<float> tamerPower, tamerGainDb, tamerRequestedDb;
    std::vector<float> clarityGain, clarityWeight;
    std::vector<float> linkedMagnitude, previousMagnitude, gateGain;
    std::vector<std::array<float, 3>> colorWeights, resonanceWeights;
    std::vector<float> mappedEnergy, collisionGain;
    std::array<std::vector<float>, 2> previousPhase;
    std::array<std::vector<Complex>, 2> mapped;
    std::array<std::vector<Complex>, 2> regionMapped;
    std::vector<Peak> peaks, previousPeaks;
    int peakCount = 0, previousPeakCount = 0;
    std::array<unsigned char, 128> held{};
    std::array<Resonator, 128> resonators;
    float coefficient(double seconds) const noexcept { return static_cast<float>(1.0 - std::exp(-1.0 / (fs * seconds))); }
    uint16_t activeMask() const noexcept {
        unsigned midi = 0;
        if (target.midiEnabled) for (int n = 0; n < 128; ++n) if (held[n]) midi |= 1u << (n % 12);
        if (midi) return static_cast<uint16_t>(midi);
        if (target.scale == 5) return target.customMask;
        static constexpr unsigned masks[] = {0x5adu, 0xab5u, 0x4a9u, 0x295u, 0xfffu};
        const unsigned m = masks[target.scale];
        return static_cast<uint16_t>(((m << target.root) | (m >> (12 - target.root))) & 0xfffu);
    }
    double quantize(double frequency, unsigned mask) const noexcept {
        if (!mask) return frequency;
        const double note = 69.0 + 12.0 * std::log2(std::max(frequency, 1.0) / 440.0);
        // A sparse mask can otherwise choose a note beyond Nyquist (for
        // example 3500 Hz -> C8 at an 8 kHz sample rate). Search only notes
        // that can actually be synthesized, including the FFT edge guard.
        const double highestNote = 69.0 + 12.0 * std::log2(fs * .49 / 440.0);
        const int center = static_cast<int>(std::floor(note));
        int best = center; double distance = 1.0e9;
        for (int n = center - 12; n <= center + 13; ++n) {
            if (n >= highestNote) continue;
            const int pc = (n % 12 + 12) % 12;
            if (!(mask & (1u << pc))) continue;
            const double d = std::abs(note - n);
            if (d < distance) { best = n; distance = d; }
        }
        return 440.0 * std::exp2((best - 69) / 12.0);
    }
    void smoothParameters() noexcept {
        prismMix = approach(prismMix, target.engineMode == 1 ? 1.0f : 0.0f, smoothing);
        current.color = approach(current.color, target.color, smoothing);
        current.snap = approach(current.snap, target.snap, smoothing);
        current.clarity = approach(current.clarity, target.clarity, smoothing);
        current.bodyMaterial = target.bodyMaterial;
        current.bodyMix = approach(current.bodyMix, target.bodyMix, smoothing);
        current.bodyDecayMs = approach(current.bodyDecayMs, target.bodyDecayMs, smoothing);
        current.bodyDamping = approach(current.bodyDamping, target.bodyDamping, smoothing);
        current.bodyInharmonicity = approach(current.bodyInharmonicity, target.bodyInharmonicity, smoothing);
        current.bodyStrike = approach(current.bodyStrike, target.bodyStrike, smoothing);
        current.morph = approach(current.morph, target.morph, smoothing);
        current.gateDb = approach(current.gateDb, target.gateDb, smoothing);
        current.decayMs = approach(current.decayMs, target.decayMs, smoothing);
        current.lowHz = approach(current.lowHz, target.lowHz, smoothing);
        current.highHz = approach(current.highHz, target.highHz, smoothing);
        current.transient = approach(current.transient, target.transient, smoothing);
        current.width = approach(current.width, target.width, smoothing);
        current.outputDb = approach(current.outputDb, target.outputDb, smoothing);
        current.bandColorLow = approach(current.bandColorLow, target.bandColorLow, smoothing);
        current.bandColorMid = approach(current.bandColorMid, target.bandColorMid, smoothing);
        current.bandColorHigh = approach(current.bandColorHigh, target.bandColorHigh, smoothing);
        current.bandEqLowDb = approach(current.bandEqLowDb, target.bandEqLowDb, smoothing);
        current.bandEqMidDb = approach(current.bandEqMidDb, target.bandEqMidDb, smoothing);
        current.bandEqHighDb = approach(current.bandEqHighDb, target.bandEqHighDb, smoothing);
        current.colorSplitLowHz = approach(current.colorSplitLowHz, target.colorSplitLowHz, smoothing);
        current.colorSplitHighHz = approach(current.colorSplitHighHz, target.colorSplitHighHz, smoothing);
        current.attackColor = approach(current.attackColor, target.attackColor, smoothing);
        current.tailColor = approach(current.tailColor, target.tailColor, smoothing);
        current.colorTransitionMs = approach(current.colorTransitionMs, target.colorTransitionMs, smoothing);
        current.pitchDiveSemitones = approach(current.pitchDiveSemitones, target.pitchDiveSemitones, smoothing);
        current.pitchDiveMs = approach(current.pitchDiveMs, target.pitchDiveMs, smoothing);
        current.scaleMotionDepth = approach(current.scaleMotionDepth, target.scaleMotionDepth, smoothing);
        current.scaleMotionRateHz = approach(current.scaleMotionRateHz, target.scaleMotionRateHz, smoothing);
        current.formantMix = approach(current.formantMix, target.formantMix, smoothing);
        current.formantVowel = approach(current.formantVowel, target.formantVowel, smoothing);
        current.formantMotion = approach(current.formantMotion, target.formantMotion, smoothing);
        current.formantRateHz = approach(current.formantRateHz, target.formantRateHz, smoothing);
        current.peakTamerAmount = approach(current.peakTamerAmount, target.peakTamerAmount, smoothing);
        current.peakTamerMaxDb = approach(current.peakTamerMaxDb, target.peakTamerMaxDb, smoothing);
        for (int i = 0; i < kGraphicEqBands; ++i)
            current.wetEqDb[i] = approach(current.wetEqDb[i], target.wetEqDb[i], smoothing);
        attackTailMix = approach(attackTailMix, target.attackTailEnabled ? 1.0f : 0.0f, smoothing);
        pitchDiveMix = approach(pitchDiveMix, target.pitchDiveEnabled ? 1.0f : 0.0f, smoothing);
        scaleMotionMix = approach(scaleMotionMix, target.scaleMotionEnabled ? 1.0f : 0.0f, smoothing);
        formantEnableMix = approach(formantEnableMix, target.formantEnabled ? 1.0f : 0.0f, smoothing);
        tamerMix = approach(tamerMix, target.peakTamerEnabled ? 1.0f : 0.0f, smoothing);
        bypassMix = approach(bypassMix, target.bypass ? 1.0f : 0.0f, smoothing);
        gain = approach(gain, std::pow(10.0f, current.outputDb / 20.0f), smoothing);
    }
    void configureResonators() noexcept {
        const unsigned mask = activeMask();
        const double r = std::exp(-std::log(1000.0) / (fs * current.decayMs * .001));
        resonanceInput = static_cast<float>(1.0 - r);
        for (int n = 0; n < 128; ++n) {
            const double f = 440.0 * std::exp2((n - 69) / 12.0);
            auto& v = resonators[n];
            v.frequency = f;
            v.active = ((mask & (1u << (n % 12))) && f >= current.lowHz && f <= current.highHz && f < fs * .45) ? 1.0f : 0.0f;
            v.re = r * std::cos(twoPi * f / fs); v.im = r * std::sin(twoPi * f / fs);
            if (pitchDiveMix == 0) { v.movedRe = v.re; v.movedIm = v.im; v.stepRe = v.stepIm = 0; }
        }
    }
    void creativeTiming(const float* alignedDry) noexcept {
        // The detector sees exactly the same core delay as the resonators.
        // Hysteresis plus a 50 ms hold-off avoids retriggering each waveform cycle.
        const float level = std::max(std::abs(alignedDry[0]), std::abs(alignedDry[1]));
        creativeFast += (level > creativeFast ? creativeAttack : creativeRelease) * (level - creativeFast);
        creativeSlow += (level > creativeSlow ? creativeSlowAttack : creativeSlowRelease) * (level - creativeSlow);
        if (creativeFast < creativeSlow * 1.12f || creativeFast < 1e-6f) onsetArmed = true;
        if (onsetArmed && onsetAge >= .050 && creativeFast > 1e-5f && creativeFast > creativeSlow * 1.8f) {
            onsetAge = 0; onsetArmed = false; poleCountdown = 0;
        }
        const float position = static_cast<float>(std::min(1.0, onsetAge / (current.colorTransitionMs * .001)));
        const float shape = position * position * (3 - 2 * position);
        const float requested = current.attackColor * .01f
            + shape * ((current.tailColor - current.attackColor) * .01f);
        // Centre-of-window alignment prevents the short spectral stage from
        // applying the envelope from the end of its analysis window.
        colorEnvelope = colorEnvelopeDelay.push(attackTailMix == 0 ? 1.0f : 1 + attackTailMix * (requested - 1));
        const float pitchPosition = static_cast<float>(std::min(1.0, onsetAge / (current.pitchDiveMs * .001)));
        diveOffset = pitchDiveMix == 0 ? 0 : pitchDiveMix * current.pitchDiveSemitones * (1 - pitchPosition) * (1 - pitchPosition);
        onsetAge = std::min(10.0, onsetAge + 1.0 / fs);
        if (pitchDiveMix > 0 && --poleCountdown <= 0) {
            poleCountdown = 32;
            const double ratio = std::exp2(diveOffset / 12.0);
            const double radius = std::exp(-std::log(1000.0) / (fs * current.decayMs * .001));
            for (auto& r : resonators) {
                const double hz = std::min(fs * .45, r.frequency * ratio);
                const double nextRe = radius * std::cos(twoPi * hz / fs);
                const double nextIm = radius * std::sin(twoPi * hz / fs);
                r.stepRe = (nextRe - r.movedRe) / 32;
                r.stepIm = (nextIm - r.movedIm) / 32;
            }
        }
        motionStep += current.scaleMotionRateHz / fs;
        if (motionStep > 1e9) motionStep -= 27720 * std::floor(motionStep / 27720);
        formantPhase += current.formantRateHz / fs;
        formantPhase -= std::floor(formantPhase);
        if (transportPositionValid) {
            if (transportPlaying) ++transportSamples;
        } else if (transportPlaying) {
            transportBeatPhase += transportTempo / (60 * fs);
            if (transportBeatPhase > 1e9) transportBeatPhase -= 110880 * std::floor(transportBeatPhase / 110880);
        }
    }
    void updateMotionFrame() noexcept {
        const unsigned mask = activeMask();
        std::array<int, 12> allowed{}; int count = 0;
        for (int offset = 0; offset < 12; ++offset) {
            const int pc = (target.root + offset) % 12;
            if (mask & (1u << pc)) allowed[count++] = pc;
        }
        // Clock counters already include the current sample. Subtracting N/2
        // (not N/2-1) therefore lands on the periodic Hann window's centre.
        const double alignment = (core.size + spectral.size / 2) / fs;
        double phase = motionStep - alignment * current.scaleMotionRateHz;
        if (target.scaleMotionSync) {
            static constexpr double division[] = {4, 2, 1, .5, .25, .125};
            const double beat = transportPositionValid
                ? transportPpq + (transportPlaying ? transportSamples * transportTempo / (60 * fs) : 0)
                : transportBeatPhase;
            phase = (beat - alignment * transportTempo / 60) / division[target.scaleMotionDivision];
        }
        motionClass = count ? allowed[(static_cast<int>(std::fmod(std::floor(phase), count)) + count) % count] : -1;
        const float alpha = static_cast<float>(1 - std::exp(-spectral.hop / (fs * .012)));
        const float depth = scaleMotionMix * current.scaleMotionDepth * .01f;
        for (int pc = 0; pc < 12; ++pc) {
            const float wanted = pc == motionClass ? 1.0f : 1.0f - depth;
            motionClassGain[pc] = approach(motionClassGain[pc], wanted, alpha);
        }
        liveVowel = std::max(0.0f, std::min(4.0f, current.formantVowel
            + 2 * current.formantMotion * .01f * static_cast<float>(std::sin(twoPi * formantPhase))));
    }
    float motionGain(float hz) const noexcept {
        if (scaleMotionMix == 0 || !(hz > 0)) return 1;
        const double note = 69 + 12 * std::log2(hz / 440.0);
        const int lowNote = static_cast<int>(std::floor(note));
        const int pc = (lowNote % 12 + 12) % 12, next = (pc + 1) % 12;
        const float position = static_cast<float>(note - lowNote);
        // A cosine shoulder joins neighbouring pitch classes without a hard
        // spectral mask. At a resonance centre, that note's gain is exact.
        const float blend = .5f - .5f * static_cast<float>(std::cos(pi * position));
        return motionClassGain[pc] + blend * (motionClassGain[next] - motionClassGain[pc]);
    }
    void renderCore() noexcept {
        const int n = core.size, half = n / 2;
        const double binHz = fs / n;
        auto& xl = core.spectrum[0]; auto& xr = core.spectrum[1];
        float maxMagnitude = 0;
        for (int k = 0; k <= half; ++k) {
            linkedMagnitude[k] = std::sqrt(std::norm(xl[k]) + std::norm(xr[k]));
            maxMagnitude = std::max(maxMagnitude, linkedMagnitude[k]);
        }
        for (int i = 0; i < previousPeakCount; ++i) previousPeaks[i].used = false;
        peakCount = 0;
        const unsigned mask = activeMask();
        if (mask && current.snap > .00001f && maxMagnitude > 1.0e-10f) {
            for (int k = 2; k < half - 1; ++k) {
                if (linkedMagnitude[k] < maxMagnitude * .0001f || linkedMagnitude[k] <= linkedMagnitude[k - 1] || linkedMagnitude[k] < linkedMagnitude[k + 1]) continue;
                const float curvature = 1.0f - (linkedMagnitude[k - 1] + linkedMagnitude[k + 1]) / (2 * linkedMagnitude[k]);
                // A broadband impulse has no sinusoidal main lobe. Treating
                // numerical ripples in its flat spectrum as tunable partials
                // smears the attack over the entire analysis window.
                if (curvature < .08f) continue;
                Peak& p = peaks[peakCount++]; p.bin = k; p.used = false;
                p.magnitude = linkedMagnitude[k];
                const int ref = std::norm(xl[k]) >= std::norm(xr[k]) ? 0 : 1;
                const double phase = std::arg(core.spectrum[ref][k]);
                double frequency;
                const bool localAttack = linkedMagnitude[k] > previousMagnitude[k] * 1.6f;
                if (havePreviousFrame && !localAttack && previousMagnitude[k] > maxMagnitude * .0001f) {
                    const double expected = twoPi * k * core.hop / n;
                    frequency = k * binHz + wrap(phase - previousPhase[ref][k] - expected) * fs / (twoPi * core.hop);
                } else {
                    const double a = std::log(std::max(linkedMagnitude[k - 1], 1.0e-20f));
                    const double b = std::log(std::max(linkedMagnitude[k], 1.0e-20f));
                    const double c = std::log(std::max(linkedMagnitude[k + 1], 1.0e-20f));
                    const double denominator = a - 2 * b + c;
                    const double offset = std::abs(denominator) > 1.0e-12 ? std::max(-.5, std::min(.5, .5 * (a - c) / denominator)) : 0;
                    frequency = (k + offset) * binHz;
                }
                p.frequency = std::max(binHz, std::min(frequency, fs * .49));
                const double tuned = quantize(p.frequency, mask);
                int match = -1;
                double best = std::max(binHz * 3.5, p.frequency * .065);
                for (int j = 0; j < previousPeakCount; ++j) {
                    const double d = std::abs(previousPeaks[j].frequency - p.frequency);
                    if (!previousPeaks[j].used && d < best) { best = d; match = j; }
                }
                p.target = p.frequency * std::pow(tuned / p.frequency, current.snap * .01);
                p.attackFrames = localAttack ? 2 : 0;
                if (p.frequency < current.lowHz || p.frequency > current.highHz) p.target = p.frequency;
                if (match >= 0) {
                    const auto& old = previousPeaks[match];
                    p.attackFrames = std::max(p.attackFrames, old.attackFrames - 1);
                    const int oldNote = static_cast<int>(std::lround(69 + 12 * std::log2(old.target / 440)));
                    const int oldPc = (oldNote % 12 + 12) % 12;
                    if (current.snap > 99.99f && (mask & (1u << oldPc))) {
                        const double oldNoteFrequency = 440.0 * std::exp2((oldNote - 69) / 12.0);
                        const double oldDistance = std::abs(1200.0 * std::log2(p.frequency / oldNoteFrequency));
                        const double newDistance = std::abs(1200.0 * std::log2(p.frequency / tuned));
                        if (oldDistance <= newDistance + 10.0 && oldDistance < 900.0 && oldNoteFrequency < fs * .49 && p.frequency >= current.lowHz && p.frequency <= current.highHz) p.target = oldNoteFrequency;
                    }
                    previousPeaks[match].used = true;
                    if (p.attackFrames > 0) p.target = p.frequency;
                    // Accumulate only the phase caused by frequency movement.
                    // The current input spectrum already carries the source's
                    // attack and phase modulation. Replacing it with an ideal
                    // oscillator phase damaged even already-in-scale attacks.
                    for (int c = 0; c < 2; ++c)
                        p.phase[c] = p.attackFrames > 0 ? 0 : wrap(old.phase[c] + twoPi * (p.target - p.frequency) * core.hop / fs);
                } else { p.phase[0] = p.phase[1] = 0; }
                if (p.attackFrames > 0) p.target = p.frequency;
                for (int c = 0; c < 2; ++c) {
                    const double angle = p.phase[c];
                    p.rotation[c] = Complex(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)));
                }
            }
        }
        // Isolated-tone correction belongs to the new bank only. The existing
        // peak tracker and its Legacy arithmetic remain unchanged.
        double bandPower = 0, dominantPower = 0;
        int dominant = -1;
        for (int k = 1; k < half; ++k)
            if (k * binHz >= current.lowHz && k * binHz <= current.highHz)
                bandPower += double(linkedMagnitude[k]) * linkedMagnitude[k];
        for (int j = 0; j < peakCount; ++j) {
            const auto& partial = peaks[j];
            if (partial.frequency < current.lowHz || partial.frequency > current.highHz) continue;
            double local = 0;
            for (int k = std::max(1, partial.bin - 2); k <= std::min(half - 1, partial.bin + 2); ++k)
                if (k * binHz >= current.lowHz && k * binHz <= current.highHz)
                    local += double(linkedMagnitude[k]) * linkedMagnitude[k];
            if (local > dominantPower) { dominantPower = local; dominant = j; }
        }
        const bool isolated = dominant >= 0 && bandPower > 1e-12
            && dominantPower > bandPower * .985 && peaks[dominant].attackFrames == 0;
        prism.isolatedTone(dominant >= 0 ? peaks[dominant].frequency : 440, isolated);
        if (peakCount > 0) {
            for (auto& a : mapped) std::fill(a.begin(), a.end(), Complex{});
            for (auto& a : regionMapped) std::fill(a.begin(), a.end(), Complex{});
            std::fill(mappedEnergy.begin(), mappedEnergy.end(), 0.0f);
            for (int c = 0; c < 2; ++c) { mapped[c][0] = core.spectrum[c][0]; mapped[c][half] = core.spectrum[c][half]; }
            for (int owner = 0; owner < peakCount; ++owner) {
                const Peak& p = peaks[owner];
                const int begin = owner ? (peaks[owner - 1].bin + p.bin) / 2 + 1 : 1;
                const int end = owner + 1 < peakCount ? (p.bin + peaks[owner + 1].bin) / 2 : half - 1;
                int firstDestination = half, lastDestination = 0;
                double before = 0;
                for (int k = begin; k <= end; ++k) {
                    const double f = k * binHz;
                    if (f < current.lowHz || f > current.highHz) {
                        for (int c = 0; c < 2; ++c) {
                            mapped[c][k] += core.spectrum[c][k];
                            mappedEnergy[k] += std::norm(core.spectrum[c][k]);
                        }
                        continue;
                    }
                    const double position = k + (p.target - p.frequency) / binHz;
                    const int lower = static_cast<int>(std::floor(position));
                    const float fraction = static_cast<float>(position - lower);
                    firstDestination = std::min(firstDestination, std::max(1, lower));
                    lastDestination = std::max(lastDestination, std::min(half - 1, lower + 1));
                    for (int c = 0; c < 2; ++c) {
                        const Complex z = core.spectrum[c][k] * p.rotation[c];
                        before += std::norm(z);
                        if (lower >= 1 && lower < half) regionMapped[c][lower] += z * (1 - fraction);
                        if (lower + 1 >= 1 && lower + 1 < half) regionMapped[c][lower + 1] += z * fraction;
                    }
                }
                double after = 0;
                for (int k = firstDestination; k <= lastDestination; ++k)
                    for (int c = 0; c < 2; ++c) after += std::norm(regionMapped[c][k]);
                // Compensate fractional-bin interpolation per source region.
                // A collision elsewhere must never change this partial's gain.
                const float normalization = before > 1.0e-18 && after > 1.0e-18
                    ? static_cast<float>(std::max(.5, std::min(2.0, std::sqrt(before / after)))) : 1.0f;
                for (int k = firstDestination; k <= lastDestination; ++k) {
                    for (int c = 0; c < 2; ++c) {
                        const Complex z = regionMapped[c][k] * normalization;
                        mapped[c][k] += z;
                        mappedEnergy[k] += std::norm(z);
                        regionMapped[c][k] = Complex{};
                    }
                }
            }
            for (int k = 1; k < half; ++k) {
                double budget = 0, actual = 0;
                for (int j = std::max(1, k - 2); j <= std::min(half - 1, k + 2); ++j) {
                    const float weight = static_cast<float>(3 - std::abs(j - k));
                    budget += mappedEnergy[j] * weight;
                    actual += (std::norm(mapped[0][j]) + std::norm(mapped[1][j])) * weight;
                }
                // Many source regions can land on the same note. Limit only
                // constructive collision energy, using a local five-bin
                // budget. Never boost a destructive cancellation into a peak.
                const float wanted = actual > budget && actual > 1.0e-18
                    ? static_cast<float>(std::sqrt(budget / actual)) : 1.0f;
                collisionGain[k] = approach(collisionGain[k], wanted, wanted < collisionGain[k] ? .8f : .25f);
            }
        } else {
            // No tracked partials means there is no collision to control.
            // Do not carry attenuation across silence into the next note.
            std::fill(collisionGain.begin(), collisionGain.end(), 1.0f);
        }
        // Save phases before replacing spectra or running the inverse FFT.
        for (int c = 0; c < 2; ++c) for (int k = 0; k <= half; ++k) previousPhase[c][k] = std::arg(core.spectrum[c][k]);
        std::copy(linkedMagnitude.begin(), linkedMagnitude.end(), previousMagnitude.begin());
        havePreviousFrame = true;
        if (peakCount > 0) {
            for (int c = 0; c < 2; ++c) {
                for (int k = 0; k <= half; ++k) core.spectrum[c][k] = mapped[c][k] * collisionGain[k];
                core.spectrum[c][0].imag(0); core.spectrum[c][half].imag(0);
                for (int k = 1; k < half; ++k) core.spectrum[c][n-k] = std::conj(core.spectrum[c][k]);
            }
        }
        previousPeakCount = peakCount;
        for (int i = 0; i < peakCount; ++i) previousPeaks[i] = peaks[i];
        configureResonators();
    }
    void renderResonance(const float* samples, float* sum) noexcept {
        sum[0] = sum[1] = 0;
        for (auto& r : resonators) {
            if (pitchDiveMix > 0) { r.movedRe += r.stepRe; r.movedIm += r.stepIm; }
            r.gain = approach(r.gain, r.active, resonanceSmoothing);
            if (r.gain == 0 && r.active == 0) { r.zr = {}; r.zi = {}; continue; }
            for (int c = 0; c < 2; ++c) {
                const double re = pitchDiveMix == 0 ? r.re : r.movedRe;
                const double im = pitchDiveMix == 0 ? r.im : r.movedIm;
                const double zr = re * r.zr[c] - im * r.zi[c] + resonanceInput * samples[c];
                const double zi = im * r.zr[c] + re * r.zi[c];
                r.zr[c] = std::abs(zr) < 1.0e-24 ? 0 : zr; r.zi[c] = std::abs(zi) < 1.0e-24 ? 0 : zi;
                sum[c] += static_cast<float>(2 * zr) * r.gain;
            }
        }
    }
    float bandAmount(double frequency) const noexcept {
        // Bass and air are copied from the aligned dry spectrum. Raised-cosine
        // shoulders avoid a hard-bin crossover.
        const double lowWidth = std::max(40.0, current.lowHz * .35);
        const double highWidth = std::max(200.0, current.highHz * .12);
        double amount = 1;
        if (frequency <= current.lowHz) return 0;
        if (frequency < current.lowHz + lowWidth) amount *= .5 - .5 * std::cos(pi * (frequency - current.lowHz) / lowWidth);
        if (frequency >= current.highHz) return 0;
        if (frequency > current.highHz - highWidth) amount *= .5 - .5 * std::cos(pi * (current.highHz - frequency) / highWidth);
        return static_cast<float>(amount);
    }
    float spectralProtection(double frequency) const noexcept {
        // Prism receives the LR4 middle branch; its low/high guard branches
        // are recombined after this spectral stage at the same fixed delay.
        const float legacy = bandAmount(frequency);
        return prismMix == 0 ? legacy : (prismMix == 1 ? 1.0f : legacy + prismMix * (1 - legacy));
    }
    void tamePeaks() noexcept {
        const int n = spectral.size, half = n / 2;
        std::fill(tamerRequestedDb.begin(), tamerRequestedDb.end(), 0.0f);
        double total = 0; int largest = 0;
        for (int k = 0; k <= half; ++k) {
            tamerPower[k] = std::norm(creativeWet[0][k]) + std::norm(creativeWet[1][k]);
            if (tamerPower[k] > tamerPower[largest]) largest = k;
            total += tamerPower[k];
        }
        double central = 0;
        for (int k = std::max(0, largest - 2); k <= std::min(half, largest + 2); ++k) central += tamerPower[k];
        // Preserve a single coherent musical tone: local spectral prominence
        // alone does not make a sinusoid a bad resonance. Use the core's more
        // accurate phase-based peak estimate to avoid low-bin pitch errors.
        bool singleInKeyTone = false;
        if (central > total * .94 && total > 1e-18) {
            const unsigned mask = activeMask();
            for (int i = 0; i < peakCount; ++i) {
                if (std::abs(peaks[i].target - largest * fs / n) > fs * 2.5 / n) continue;
                const double note = 69 + 12 * std::log2(peaks[i].target / 440.0);
                const int nearest = static_cast<int>(std::lround(note));
                if (std::abs(note - nearest) < .35 && (mask & (1u << ((nearest % 12 + 12) % 12)))) singleInKeyTone = true;
            }
            if (peakCount == 0) {
                // Snap may be zero. Hann log-parabolic estimation is then
                // sufficient for the single-tone exception above about 200 Hz.
                const double a = std::log(std::max(tamerPower[std::max(0, largest - 1)], 1e-30f));
                const double b = std::log(std::max(tamerPower[largest], 1e-30f));
                const double c = std::log(std::max(tamerPower[std::min(half, largest + 1)], 1e-30f));
                const double denominator = a - 2 * b + c;
                const double offset = std::abs(denominator) > 1e-12 ? std::max(-.5, std::min(.5, .5 * (a - c) / denominator)) : 0;
                const double hz = std::max(1.0, (largest + offset) * fs / n);
                const double note = 69 + 12 * std::log2(hz / 440.0);
                const int nearest = static_cast<int>(std::lround(note));
                singleInKeyTone = std::abs(note - nearest) < .35 && (mask & (1u << ((nearest % 12 + 12) % 12)));
            }
        }
        const float maximum = current.peakTamerMaxDb;
        if (!singleInKeyTone && total > 1e-18) for (int k = 2; k < half - 2; ++k) {
            if (tamerPower[k] <= tamerPower[k - 1] || tamerPower[k] < tamerPower[k + 1]) continue;
            if (bandAmount(k * fs / n) == 0) continue;
            const int radius = std::max(7, std::min(24, k / 6));
            double neighbour = 0; int neighbours = 0;
            for (int j = std::max(1, k - radius); j <= std::min(half - 1, k + radius); ++j) {
                if (std::abs(j - k) <= 2) continue;
                neighbour += tamerPower[j]; ++neighbours;
            }
            const double floor = std::max(total * 1e-7, neighbour / std::max(1, neighbours));
            const float prominenceDb = static_cast<float>(10 * std::log10(std::max(double(tamerPower[k]), floor) / floor));
            const float reduction = std::min(maximum, std::max(0.0f, prominenceDb - 10) * current.peakTamerAmount * .01f);
            for (int j = std::max(1, k - 2); j <= std::min(half - 1, k + 2); ++j)
                tamerRequestedDb[j] = std::max(tamerRequestedDb[j], reduction * (std::abs(j - k) == 2 ? .6f : 1.0f));
        }
        visual.tamerReductionDb = 0;
        const float release = static_cast<float>(1 - std::exp(-spectral.hop / (fs * .070)));
        for (int k = 0; k <= half; ++k) {
            // The current Max is a strict reduction cap even while automated.
            tamerGainDb[k] = std::min(maximum, approach(tamerGainDb[k], tamerRequestedDb[k],
                                                       tamerRequestedDb[k] > tamerGainDb[k] ? .75f : release));
            const float reduction = tamerGainDb[k] * tamerMix;
            visual.tamerReductionDb = std::max(visual.tamerReductionDb, reduction);
            const float multiplier = std::pow(10.0f, -.05f * reduction);
            Complex wet[2] = {creativeWet[0][k] * multiplier, creativeWet[1][k] * multiplier};
            const Complex mid = (wet[0] + wet[1]) * .5f;
            const Complex side = (wet[0] - wet[1]) * (current.width * .005f);
            wet[0] = mid + side; wet[1] = mid - side;
            const float amount = spectralProtection(k * fs / n);
            for (int c = 0; c < 2; ++c) {
                const Complex dry = spectral.spectrum[c+2][k];
                spectral.spectrum[c][k] = amount == 1 ? wet[c] : (amount == 0 ? dry : dry + amount * (wet[c] - dry));
            }
        }
    }
    void applyClarity() noexcept {
        const int n=spectral.size, half=n/2;
        const double amount=current.clarity*.01*prismMix;
        double linear=0,quadratic=0,available=0;
        for(int k=0;k<=half;++k) {
            const double hz=k*fs/n;
            const double weight=bandAmount(hz);
            // A broad tilt changes the balance of the existing tuned signal;
            // it neither adds a dry floor nor changes resonator pitch or Q.
            const double tilt=weight>0?std::clamp(3*std::log2(hz/1000.0),-4.5,4.5):0;
            const double gain=std::pow(10.0,.05*amount*tilt);
            clarityGain[k]=static_cast<float>(gain);
            clarityWeight[k]=static_cast<float>(weight);
            const double power=(std::norm(creativeWet[0][k])+std::norm(creativeWet[1][k]))
                * (k==0||k==half?1.0:2.0);
            const double a=1-weight,b=weight*gain;
            linear+=power*a*b;
            quadratic+=power*b*b;
            available+=power*weight*(2-weight);
        }
        // The feathered gain is (1-w)+w*g*s. Solve its power quadratic for s
        // to preserve linked stereo frame energy, including the shoulders.
        // Frequencies outside the selected band keep an exact gain of one.
        const double denominator=linear+std::sqrt(linear*linear+quadratic*available);
        const double level=denominator>1e-30?available/denominator:1;
        for(int k=0;k<=half;++k) {
            const float weight=clarityWeight[k];
            const float multiplier=weight==0?1.0f:static_cast<float>(1-weight+weight*clarityGain[k]*level);
            for(int c=0;c<2;++c)creativeWet[c][k]*=multiplier;
            if(tamerMix!=0)continue; // The tamer applies Width/protection once.
            Complex wet[2]={creativeWet[0][k],creativeWet[1][k]};
            const Complex mid=(wet[0]+wet[1])*.5f;
            const Complex side=(wet[0]-wet[1])*(current.width*.005f);
            wet[0]=mid+side;wet[1]=mid-side;
            const float protection=spectralProtection(k*fs/n);
            for(int c=0;c<2;++c) {
                const Complex dry=spectral.spectrum[c+2][k];
                spectral.spectrum[c][k]=protection==1?wet[c]:(protection==0?dry:dry+protection*(wet[c]-dry));
            }
        }
    }
    void renderSpectral() noexcept {
        const int n = spectral.size, half = n / 2;
        const float morph = target.morphEnabled ? current.morph * .01f : 0.0f;
        const float threshold = std::pow(10.0f, current.gateDb / 20.0f);
        updateMotionFrame();
        std::array<double, 3> mainEnergy{}, resonanceEnergy{};
        for (int k = 0; k <= half; ++k) {
            const float hz = static_cast<float>(k * fs / n);
            colorWeights[k] = colorBandWeights(hz, current.colorSplitLowHz, current.colorSplitHighHz);
            // Calibration has fixed internal bands. Moving a user crossover
            // with neutral amount/EQ must not change the calibrated timbre.
            const auto& weights = resonanceWeights[k];
            const double protection = bandAmount(hz);
            const double main = (std::norm(spectral.spectrum[0][k]) + std::norm(spectral.spectrum[1][k])) * protection * protection;
            const double layer = (std::norm(spectral.spectrum[4][k]) + std::norm(spectral.spectrum[5][k])) * protection * protection;
            for (int b = 0; b < 3; ++b) {
                mainEnergy[b] += weights[b] * main;
                resonanceEnergy[b] += weights[b] * layer;
            }
        }
        // The pole input coefficient (1-r) preserves a sine's centre gain,
        // but its broadband excitation loses power as Decay gets longer.
        // Calibrate the separate resonance layer against available wet-band
        // energy BEFORE user amount, Morph, Gate or EQ. Neither the main wet
        // signal nor the user's gains are normalized by this control.
        const double referenceRelease = std::exp(-2 * std::log(1000.0) * spectral.hop / (fs * current.decayMs * .001));
        for (int b = 0; b < 3; ++b) {
            resonanceReference[b] = std::max(mainEnergy[b], resonanceReference[b] * referenceRelease);
            // During the tail, retain the last calibration. Otherwise a dry
            // reference falling to zero would close the resonance like a gate.
            // The reference envelope has the pole's power decay, and a fresh
            // onset always applies the current frame's downward bound first.
            if (mainEnergy[b] > std::max(1.0e-16, resonanceReference[b] * .02) && resonanceEnergy[b] > 1.0e-20) {
                const float ratio = static_cast<float>(std::sqrt(resonanceReference[b] / resonanceEnergy[b]));
                const float wanted = std::min(32.0f, .65f * ratio);
                const float previous = resonanceCalibration[b];
                resonanceCalibration[b] = approach(previous, wanted, wanted < previous ? .75f : .08f);
                resonanceCalibration[b] = std::min(resonanceCalibration[b], .85f * ratio);
            }
            if (resonanceReference[b] < 1.0e-24) resonanceReference[b] = 0;
        }
        const float extra = std::max(0.0f, (current.color - 100) * .01f);
        const std::array<float, 3> layerAmount{{current.bandColorLow * .01f, current.bandColorMid * .01f, current.bandColorHigh * .01f}};
        const std::array<float, 3> equalizerDb{{current.bandEqLowDb, current.bandEqMidDb, current.bandEqHighDb}};
        const bool uniformAmount = layerAmount[0] == layerAmount[1] && layerAmount[1] == layerAmount[2];
        const bool unityEq = equalizerDb[0] == 0 && equalizerDb[1] == 0 && equalizerDb[2] == 0;
        bool flatGraphicEq = true;
        for (float db : current.wetEqDb) flatGraphicEq = flatGraphicEq && db == 0;
        const float formantAmount = formantEnableMix * current.formantMix * .01f;
        const bool clarityActive=prismMix!=0&&current.clarity>0;
        for (int k = 0; k <= half; ++k) {
            const auto& weights = colorWeights[k];
            float calibration = 0, userAmount = 0, eqDb = 0;
            for (int b = 0; b < 3; ++b) {
                calibration += resonanceWeights[k][b] * resonanceCalibration[b];
                userAmount += weights[b] * layerAmount[b];
                eqDb += weights[b] * equalizerDb[b];
            }
            float layerGain = extra * calibration * (uniformAmount ? layerAmount[0] : userAmount);
            const float hz = static_cast<float>(k * fs / n);
            if (attackTailMix != 0) layerGain *= colorEnvelope;
            if (scaleMotionMix != 0) layerGain *= motionGain(hz);
            if (!flatGraphicEq) eqDb += graphicEqDbAt(hz, current.wetEqDb);
            if (formantAmount != 0) eqDb += formantAmount * formantDbAt(hz, liveVowel);
            const float eqGain = unityEq && flatGraphicEq && formantAmount == 0 ? 1.0f : std::pow(10.0f, eqDb * .05f);
            const float dryMagnitude[2] = {std::abs(spectral.spectrum[2][k]), std::abs(spectral.spectrum[3][k])};
            const float reference = std::max(dryMagnitude[0], dryMagnitude[1]) * (4.0f / n);
            float requested = 1;
            if (target.gateEnabled) {
                requested = reference >= threshold ? 1.0f : 0.0f;
            }
            // Binary dry-sidechain decision per FFT bin. The overlapping
            // analysis/synthesis windows already interpolate adjacent frames;
            // an extra 40 ms release allowed clearly dry-silent wet tails.
            gateGain[k] = requested;
            Complex wet[2];
            for (int c = 0; c < 2; ++c) {
                Complex original = layerGain == 0 ? spectral.spectrum[c][k]
                    : spectral.spectrum[c][k] + spectral.spectrum[c + 4][k] * layerGain;
                // The three motion controls now act on the principal Prism
                // colour signal, including Color100, instead of a quiet tail.
                if (prismMix != 0) {
                    float movement = 1;
                    if (attackTailMix != 0) movement *= colorEnvelope;
                    original *= 1 + prismMix * (movement - 1);
                }
                const float magnitude = std::abs(original);
                const float newMagnitude = ((1 - morph) * magnitude + morph * dryMagnitude[c]) * gateGain[k];
                if (morph == 0 && gateGain[k] == 1) wet[c] = original;
                else if (magnitude > 1.0e-15f) wet[c] = original * (newMagnitude / magnitude);
                else if (dryMagnitude[c] > 1.0e-15f) wet[c] = spectral.spectrum[c+2][k] * (newMagnitude / dryMagnitude[c]);
                else wet[c] = Complex{};
                // EQ is intentionally after Morph: with nonzero EQ, the
                // PROCESSED display at Morph 100 is the dry magnitude shaped
                // by this EQ, not the unmodified dry-magnitude endpoint.
                wet[c] *= eqGain;
                if (tamerMix != 0 || clarityActive) creativeWet[c][k] = wet[c];
            }
            const Complex mid = (wet[0] + wet[1]) * .5f;
            const Complex side = (wet[0] - wet[1]) * (current.width * .005f);
            wet[0] = mid + side; wet[1] = mid - side;
            const float amount = spectralProtection(k * fs / n);
            for (int c = 0; c < 2; ++c) {
                const Complex dry = spectral.spectrum[c+2][k];
                spectral.spectrum[c][k] = amount == 1 ? wet[c] : (amount == 0 ? dry : dry + amount * (wet[c] - dry));
            }
        }
        if (clarityActive) applyClarity();
        if (tamerMix != 0) tamePeaks();
        else { std::fill(tamerGainDb.begin(), tamerGainDb.end(), 0.0f); visual.tamerReductionDb = 0; }
        for (int c = 0; c < 2; ++c) {
            spectral.spectrum[c][0].imag(0); spectral.spectrum[c][half].imag(0);
            for (int k = 1; k < half; ++k) spectral.spectrum[c][n-k] = std::conj(spectral.spectrum[c][k]);
        }
        ++visual.frameCounter;
        visual.sampleRate = static_cast<float>(fs);
        visual.activeMask = activeMask();
        visual.attackTailGain = attackTailMix == 0 ? 1 : colorEnvelope;
        visual.diveSemitones = diveOffset;
        visual.motionPitchClass = scaleMotionMix == 0 ? -1 : static_cast<int16_t>(motionClass);
        visual.motionHostSynced = scaleMotionMix != 0 && target.scaleMotionSync && transportPositionValid;
        visual.formantVowel = liveVowel;
        const auto db = [](float v) noexcept { return std::max(-120.0f, 20.0f * std::log10(std::max(v, 1.0e-6f))); };
        for (int i = 0; i < kVisualBins; ++i) {
            const double hz = kVisualMinHz * std::pow(double(kVisualMaxHz / kVisualMinHz), double(i) / (kVisualBins - 1));
            if (hz >= fs * .5) {
                visual.dryDb[i] = visual.wetDb[i] = -120;
                visual.gateGain[i] = 1;
                continue;
            }
            const double position = std::min(double(half), hz * n / fs);
            const int a = static_cast<int>(position), b = std::min(a + 1, half);
            const float mix = static_cast<float>(position - a);
            const auto magnitude = [&](int k, int c) noexcept {
                return std::max(std::abs(spectral.spectrum[c][k]), std::abs(spectral.spectrum[c + 1][k])) * (4.0f / n);
            };
            visual.dryDb[i] = db((1 - mix) * magnitude(a, 2) + mix * magnitude(b, 2));
            visual.wetDb[i] = db((1 - mix) * magnitude(a, 0) + mix * magnitude(b, 0));
            visual.gateGain[i] = (1 - mix) * gateGain[a] + mix * gateGain[b];
        }
        visual.moveCount = 0;
        for (int i = 0; i < peakCount; ++i) {
            const auto& p = peaks[i];
            if (std::abs(p.target - p.frequency) < .05 || p.frequency < current.lowHz || p.frequency > current.highHz) continue;
            VisualMove move{static_cast<float>(p.frequency), static_cast<float>(p.target), db(p.magnitude * (2.82842712474619f / core.size))};
            int at = static_cast<int>(visual.moveCount);
            if (at == static_cast<int>(visual.moves.size())) {
                if (move.levelDb <= visual.moves.back().levelDb) continue;
                --at;
            } else ++visual.moveCount;
            while (at > 0 && visual.moves[at - 1].levelDb < move.levelDb) { visual.moves[at] = visual.moves[at - 1]; --at; }
            visual.moves[at] = move;
        }
    }
    float transientStrength(float l, float r) noexcept {
        const float v = std::max(std::abs(l), std::abs(r));
        fastEnvelope += (v > fastEnvelope ? fastAttack : fastRelease) * (v - fastEnvelope);
        slowEnvelope += (v > slowEnvelope ? slowAttack : slowRelease) * (v - slowEnvelope);
        const float strength = std::max(0.0f, std::min(1.0f, 1.5f * (fastEnvelope - slowEnvelope) / (fastEnvelope + 1.0e-8f)));
        onsetEnvelope = std::max(strength, onsetEnvelope * onsetRelease);
        if (onsetEnvelope < 1.0e-12f) onsetEnvelope = 0;
        return onsetEnvelope;
    }
    void reset() noexcept {
        core.reset(); spectral.reset();
        prism.reset(); prismMix = target.engineMode == 1 ? 1.0f : 0.0f;
        body.reset(); bodyRunning = false;
        for (auto& d : prismOuterDelay) d.reset();
        for (auto& d : coreDry) d.reset();
        for (auto& d : finalDry) d.reset();
        onsetDelay.reset();
        colorEnvelopeDelay.reset();
        creativeFast = creativeSlow = 0; onsetAge = 10; onsetArmed = true;
        colorEnvelope = 1; diveOffset = 0; poleCountdown = 0;
        motionStep = formantPhase = transportBeatPhase = 0;
        transportTempo = 120; transportPpq = 0; transportPlaying = true; transportPositionValid = false;
        liveVowel = target.formantVowel;
        transportSamples = 0; motionClassGain.fill(1); motionClass = -1;
        attackTailMix = target.attackTailEnabled ? 1 : 0;
        pitchDiveMix = target.pitchDiveEnabled ? 1 : 0;
        scaleMotionMix = target.scaleMotionEnabled ? 1 : 0;
        formantEnableMix = target.formantEnabled ? 1 : 0;
        tamerMix = target.peakTamerEnabled ? 1 : 0;
        std::fill(tamerGainDb.begin(), tamerGainDb.end(), 0.0f);
        for (auto& x : previousPhase) std::fill(x.begin(), x.end(), 0);
        std::fill(previousMagnitude.begin(), previousMagnitude.end(), 0.0f);
        std::fill(collisionGain.begin(), collisionGain.end(), 1.0f);
        std::fill(gateGain.begin(), gateGain.end(), 1.0f);
        for (auto& r : resonators) r.clear();
        resonanceCalibration = {{.65f, .65f, .65f}};
        resonanceReference = {};
        held = {}; peakCount = previousPeakCount = 0;
        havePreviousFrame = started = false; current = target;
        fastEnvelope = slowEnvelope = onsetEnvelope = 0;
        bypassMix = target.bypass ? 1.0f : 0.0f;
        gain = std::pow(10.0f, target.outputDb / 20.0f);
        if (prepared) {
            configureResonators();
            for (auto& r : resonators) { r.movedRe = r.re; r.movedIm = r.im; r.stepRe = r.stepIm = 0; }
        }
        visual = VisualFrame{}; visual.sampleRate = static_cast<float>(fs); visual.activeMask = activeMask();
    }
};
Engine::Engine() : impl_(new Impl) {}
Engine::~Engine() = default;
Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;
void Engine::prepare(double sampleRate, int coreFft, int spectralFft) {
    auto& p = *impl_;
    p.fs = std::isfinite(sampleRate) ? std::max(8000.0, std::min(sampleRate, 192000.0)) : 48000.0;
    coreFft = coreFft == 4096 ? 4096 : 2048;
    spectralFft = spectralFft == 512 ? 512 : 1024;
    p.target = sanitized(p.target, p.fs);
    p.core.prepare(coreFft, 2); p.spectral.prepare(spectralFft, 6);
    p.prism.prepare(p.fs);
    p.body.prepare(p.fs);
    for (auto& d : p.prismOuterDelay) d.prepare(spectralFft);
    p.latency = coreFft + spectralFft;
    p.linkedMagnitude.resize(coreFft / 2 + 1);
    p.previousMagnitude.resize(coreFft / 2 + 1);
    p.mappedEnergy.resize(coreFft / 2 + 1); p.collisionGain.resize(coreFft / 2 + 1);
    for (auto& x : p.previousPhase) x.resize(coreFft / 2 + 1);
    for (auto& x : p.mapped) x.resize(coreFft);
    for (auto& x : p.regionMapped) x.resize(coreFft);
    p.peaks.resize(coreFft / 2 + 1); p.previousPeaks.resize(coreFft / 2 + 1);
    p.gateGain.resize(spectralFft / 2 + 1);
    p.colorWeights.resize(spectralFft / 2 + 1);
    p.resonanceWeights.resize(spectralFft / 2 + 1);
    for (auto& wet : p.creativeWet) wet.resize(spectralFft / 2 + 1);
    p.tamerPower.resize(spectralFft / 2 + 1);
    p.tamerGainDb.resize(spectralFft / 2 + 1);
    p.tamerRequestedDb.resize(spectralFft / 2 + 1);
    p.clarityGain.resize(spectralFft / 2 + 1);
    p.clarityWeight.resize(spectralFft / 2 + 1);
    for (int k = 0; k <= spectralFft / 2; ++k)
        p.resonanceWeights[k] = colorBandWeights(static_cast<float>(k * p.fs / spectralFft), 700, 3000);
    for (auto& d : p.coreDry) d.prepare(coreFft);
    for (auto& d : p.finalDry) d.prepare(p.latency);
    p.onsetDelay.prepare(p.latency);
    p.colorEnvelopeDelay.prepare(spectralFft / 2 - 1);
    p.smoothing = p.coefficient(.012);
    p.fastAttack = p.coefficient(.0003); p.fastRelease = p.coefficient(.010);
    p.slowAttack = p.coefficient(.015); p.slowRelease = p.coefficient(.080);
    p.onsetRelease = static_cast<float>(std::exp(-1.0 / (p.fs * .025)));
    p.resonanceSmoothing = p.coefficient(.008);
    p.creativeAttack = p.coefficient(.0003); p.creativeRelease = p.coefficient(.012);
    p.creativeSlowAttack = p.coefficient(.020); p.creativeSlowRelease = p.coefficient(.090);
    p.prepared = true; p.reset(); p.configureResonators();
}
void Engine::reset() noexcept { impl_->reset(); }
void Engine::setParameters(const Parameters& values) noexcept {
    auto& p = *impl_; p.target = sanitized(values, p.fs);
    if (!p.started) {
        p.current = p.target; p.bypassMix = p.target.bypass ? 1.0f : 0.0f;
        p.prismMix = p.target.engineMode == 1 ? 1.0f : 0.0f;
        p.attackTailMix = p.target.attackTailEnabled ? 1 : 0;
        p.pitchDiveMix = p.target.pitchDiveEnabled ? 1 : 0;
        p.scaleMotionMix = p.target.scaleMotionEnabled ? 1 : 0;
        p.formantEnableMix = p.target.formantEnabled ? 1 : 0;
        p.tamerMix = p.target.peakTamerEnabled ? 1 : 0;
        p.gain = std::pow(10.0f, p.target.outputDb / 20.0f);
        if (p.prepared) {
            p.configureResonators();
            for (auto& r : p.resonators) { r.movedRe = r.re; r.movedIm = r.im; r.stepRe = r.stepIm = 0; }
        }
    }
}
void Engine::noteOn(int note) noexcept {
    if (note >= 0 && note < 128 && impl_->held[note] < 255) ++impl_->held[note];
}
void Engine::noteOff(int note) noexcept {
    if (note >= 0 && note < 128 && impl_->held[note]) --impl_->held[note];
}
void Engine::allNotesOff() noexcept { impl_->held = {}; }
void Engine::setTransport(double tempo, double quarterNotePosition, bool playing, bool positionValid) noexcept {
    auto& p = *impl_;
    const double previousEndpoint = p.transportPositionValid
        ? p.transportPpq + (p.transportPlaying ? p.transportSamples * p.transportTempo / (60 * p.fs) : 0)
        : p.transportBeatPhase;
    p.transportTempo = std::isfinite(tempo) && tempo >= 1 && tempo <= 1000 ? tempo : 120;
    const bool valid = positionValid && std::isfinite(quarterNotePosition) && std::abs(quarterNotePosition) <= 1e9;
    if (valid) {
        p.transportPpq = quarterNotePosition;
        p.transportBeatPhase = quarterNotePosition;
    } else if (p.transportPositionValid) {
        p.transportBeatPhase = previousEndpoint;
    }
    p.transportSamples = 0;
    p.transportPlaying = playing;
    p.transportPositionValid = valid;
}
void Engine::process(const float* inL, const float* inR, float* outL, float* outR, int n) noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    const ScopedDenormalsOff denormalGuard;
#endif
    if (n <= 0) return;
    auto& p = *impl_;
    if (!p.prepared) {
        for (int i = 0; i < n; ++i) {
            const float l = inL ? clean(inL[i]) : 0, r = inR ? clean(inR[i]) : l;
            if (outL) outL[i] = l;
            if (outR) outR[i] = r;
        }
        return;
    }
    p.started = true;
    for (int i = 0; i < n; ++i) {
        // Read both channels before any write to support in-place buffers.
        const float raw[2] = {inL ? clean(inL[i]) : 0, inR ? clean(inR[i]) : (inL ? clean(inL[i]) : 0)};
        const float input[2] = {std::max(-32.0f, std::min(32.0f, raw[0])), std::max(-32.0f, std::min(32.0f, raw[1]))};
        p.smoothParameters();
        float coreWet[2];
        if (p.core.push(input, coreWet)) { p.renderCore(); p.core.commit(); }
        const float alignedCoreDry[2] = {p.coreDry[0].push(input[0]), p.coreDry[1].push(input[1])};
        p.creativeTiming(alignedCoreDry);
        float resonance[2];
        p.renderResonance(coreWet, resonance);
        float prismWet[2], prismDry[2], prismOuter[2];
        p.prism.process(alignedCoreDry, prismWet, prismDry, prismOuter, p.current, p.activeMask(), p.diveOffset, p.motionClassGain);
        // Crossfade the guard input, then delay it together with its envelope.
        // Multiplying a delayed guard by the current mode would add it before
        // the previous core had left the spectral window, boosting the sub.
        const float outer[2] = {p.prismOuterDelay[0].push(prismOuter[0] * p.prismMix), p.prismOuterDelay[1].push(prismOuter[1] * p.prismMix)};
        float spectralInput[6];
        for (int c = 0; c < 2; ++c) {
            // Exact endpoints retain the 0.4 order of operations in Legacy.
            spectralInput[c] = p.prismMix == 0 ? coreWet[c] : (p.prismMix == 1 ? prismWet[c] : coreWet[c] + p.prismMix * (prismWet[c] - coreWet[c]));
            spectralInput[c+2] = p.prismMix == 0 ? alignedCoreDry[c] : (p.prismMix == 1 ? prismDry[c] : alignedCoreDry[c] + p.prismMix * (prismDry[c] - alignedCoreDry[c]));
            spectralInput[c+4] = p.prismMix == 0 ? resonance[c] : resonance[c] * (1 - p.prismMix);
        }
        float wet[2];
        if (p.spectral.push(spectralInput, wet)) { p.renderSpectral(); p.spectral.commit(); }
        for (int c = 0; c < 2; ++c) if (outer[c] != 0) wet[c] += outer[c];
        const float dry[2] = {p.finalDry[0].push(raw[0]), p.finalDry[1].push(raw[1])};
        if (p.current.bodyMix > 0) {
            float layer[2];
            p.body.process(dry, layer, p.current, p.target.root, p.activeMask());
            p.bodyRunning = true;
            for (int c = 0; c < 2; ++c) if (layer[c] != 0) wet[c] += layer[c];
        } else if (p.bodyRunning) {
            p.body.reset(); p.bodyRunning = false;
        }
        const float retain = p.onsetDelay.push(p.transientStrength(input[0], input[1])) * p.current.transient * .01f;
        const float amount = std::min(1.0f, p.current.color * .01f) * (1 - retain);
        float result[2];
        for (int c = 0; c < 2; ++c) {
            float y = (amount == 0 ? dry[c] : dry[c] + amount * (wet[c] - dry[c])) * p.gain;
            if (p.target.softLimit) y = kneeLimit(y);
            if (!std::isfinite(y)) y = 0;
            result[c] = p.bypassMix == 1 ? dry[c] : (p.bypassMix == 0 ? y : y + p.bypassMix * (dry[c] - y));
        }
        if (outL) outL[i] = result[0];
        if (outR) outR[i] = result[1];
    }
}
int Engine::latencySamples() const noexcept { return impl_->latency; }
double Engine::tailSeconds() const noexcept {
    if (impl_->target.bodyMix > 0 || impl_->current.bodyMix > 0) {
        const double original = impl_->target.engineMode == 1
            ? impl_->prism.longestTailSeconds(impl_->target)
            : (impl_->target.color > 100 ? impl_->target.decayMs * .002 : 0.0) + .12;
        const float requestedDecay = std::max(impl_->target.bodyDecayMs, impl_->current.bodyDecayMs);
        return impl_->latency / impl_->fs + std::max(original, impl_->body.tailSeconds(requestedDecay));
    }
    if (impl_->target.engineMode == 1)
        return impl_->latency / impl_->fs + impl_->prism.longestTailSeconds(impl_->target);
    return impl_->latency / impl_->fs + (impl_->target.color > 100 ? impl_->target.decayMs * .002 : 0.0) + .12;
}
void Engine::copyVisual(VisualFrame& out) const noexcept { out = impl_->visual; }
} // namespace iris
