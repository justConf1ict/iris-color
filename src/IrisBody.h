#pragma once

#include "IrisDSP.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace iris {

inline constexpr int kBodyModeCount = 24;

namespace body_detail {
inline double bounded(double value, double fallback, double lo, double hi) noexcept {
    return std::isfinite(value) ? std::max(lo, std::min(value, hi)) : fallback;
}
inline int materialIndex(int material) noexcept { return std::max(0, std::min(material, 3)); }
inline int modeIndex(int mode) noexcept { return std::max(0, std::min(mode, kBodyModeCount - 1)); }
inline constexpr double pi = 3.1415926535897932384626433832795;
inline constexpr double log1000 = 6.90775527898213705205;
} // namespace body_detail

// Original, physically inspired target spectra, not measured object models.
// All families have an exactly harmonic endpoint at Inharmonicity zero.
// The String family is a stiff-string modal approximation, not a delay-line
// string, nonlinear contact model, or spatial/PDE simulation.
inline float bodyModeRatio(int material, int mode, float inharmonicityPercent) noexcept {
    const int m = body_detail::modeIndex(mode);
    const double n = m + 1.0;
    const double amount = body_detail::bounded(inharmonicityPercent, 25, 0, 100) * .01;
    double target = n;
    switch (body_detail::materialIndex(material)) {
        case 0: target = 1 + 1.74 * m + .18 * m * m + .06 * std::sin(2.4 * m); break;
        case 1: target = 1 + 1.11 * m + .047 * m * m + .13 * std::sin(1.71 * m); break;
        case 2: target = std::pow(n, 1.63); break;
        case 3: {
            const double stiffness = .0035 * amount;
            return static_cast<float>(n * std::sqrt((1 + stiffness * n * n) / (1 + stiffness)));
        }
    }
    return static_cast<float>(n + amount * (target - n));
}

// The first mode's amplitude T60 is exactly Decay; successive mode orders
// have progressively greater losses. These same functions drive the audio
// coefficients and the editor's static model view.
inline float bodyModeDecaySeconds(int material, int mode, float decayMs, float dampingPercent) noexcept {
    static constexpr double loss[] = {.18, .105, .68, .085};
    const int m = body_detail::modeIndex(mode);
    const double seconds = body_detail::bounded(decayMs, 360, 40, 1800) * .001;
    const double damping = body_detail::bounded(dampingPercent, 45, 0, 100) * .01;
    const double losses = 1 + loss[body_detail::materialIndex(material)]
        * std::pow(double(m), .85) * (.15 + 4 * damping);
    return static_cast<float>(seconds / losses);
}

// A bounded bank of damped complex modes. It owns only fixed-size storage.
// The caller supplies final-delay-aligned dry input and performs the existing
// Color/transient, output, limiter and bypass operations after adding this
// layer to wet. There is no new lookahead or buffering latency.
class MaterialBody {
public:
    void prepare(double sampleRate) noexcept {
        fs_ = body_detail::bounded(sampleRate, 48000, 8000, 192000);
        poleSmoothing_ = 1 - std::exp(-1 / (fs_ * .020));
        gateSmoothing_ = 1 - std::exp(-1 / (fs_ * .005));
        fastAttack_ = 1 - std::exp(-1 / (fs_ * .0005));
        fastRelease_ = 1 - std::exp(-1 / (fs_ * .015));
        slowAttack_ = 1 - std::exp(-1 / (fs_ * .020));
        slowRelease_ = 1 - std::exp(-1 / (fs_ * .100));
        strikeRelease_ = std::exp(-1 / (fs_ * .018));
        static constexpr double q[] = {.541196100146197, 1.306562964876377};
        for (int c = 0; c < 2; ++c) for (int section = 0; section < 2; ++section) {
            inputHighpass_[c][section].prepare(fs_, 150, q[section]);
            outputHighpass_[c][section].prepare(fs_, 130, q[section]);
        }
        reset();
    }

    void reset() noexcept {
        clearEnergy();
        for (auto& mode : modes_) mode = Mode{};
        initialized_ = false;
        controlCountdown_ = 0;
        lastMask_ = 0; lastRoot_ = lastMaterial_ = -1;
        gate_ = 0; silentSeconds_ = 0; tailDecayCeiling_ = .36;
    }

    double tailSeconds(float requestedDecayMs) const noexcept {
        // A hard reset after three longest T60s plus 160 ms ensures the
        // reported duration bounds even numerical/filter residue. Under
        // ordinary operation this boundary is already below -180 dB.
        return 3 * std::max(tailDecayCeiling_, body_detail::bounded(requestedDecayMs, 360, 40, 1800) * .001) + .16;
    }

    void process(const float* alignedDry, float* layer, const Parameters& p,
                 int root, uint16_t activeMask) noexcept {
        layer[0] = layer[1] = 0;
        const unsigned mask = activeMask & 0x0fffu;
        const double mix = body_detail::bounded(p.bodyMix, 0, 0, 100) * .01;
        if (mix == 0) return;
        const int material = body_detail::materialIndex(p.bodyMaterial);
        root = std::max(0, std::min(root, 11));
        const double decay = body_detail::bounded(p.bodyDecayMs, 360, 40, 1800) * .001;
        tailDecayCeiling_ = std::max(tailDecayCeiling_, decay);
        gate_ += gateSmoothing_ * ((mask ? 1.0 : 0.0) - gate_);
        if (!mask && gate_ < 1e-8) {
            gate_ = 0; clearEnergy(); silentSeconds_ = 0; tailDecayCeiling_ = decay;
            return;
        }
        if (mask != lastMask_ || root != lastRoot_ || material != lastMaterial_) controlCountdown_ = 0;
        if (--controlCountdown_ <= 0) {
            configure(p, root, mask, material);
            controlCountdown_ = 32;
        }
        double input[2];
        bool sourcePresent = false;
        for (int c = 0; c < 2; ++c) {
            input[c] = body_detail::bounded(alignedDry[c], 0, -4, 4);
            sourcePresent |= input[c] != 0;
            for (auto& filter : inputHighpass_[c]) input[c] = filter.process(input[c]);
        }
        silentSeconds_ = sourcePresent ? 0 : std::min(10.0, silentSeconds_ + 1 / fs_);
        if (silentSeconds_ >= 3 * tailDecayCeiling_ + .16) {
            clearEnergy(); tailDecayCeiling_ = decay;
            return;
        }
        const double magnitude = std::max(std::abs(input[0]), std::abs(input[1]));
        fast_ += (magnitude > fast_ ? fastAttack_ : fastRelease_) * (magnitude - fast_);
        slow_ += (magnitude > slow_ ? slowAttack_ : slowRelease_) * (magnitude - slow_);
        if (fast_ < slow_ * 1.1 || fast_ < 1e-6) onsetArmed_ = true;
        if (mask && onsetArmed_ && onsetAge_ >= .045 && fast_ > 1e-5 && fast_ > slow_ * 1.8) {
            onset_ = 1; onsetAge_ = 0; onsetArmed_ = false;
        }
        onsetAge_ = std::min(10.0, onsetAge_ + 1 / fs_);
        onset_ *= strikeRelease_;
        if (onset_ < 1e-12) onset_ = 0;
        const double strike = body_detail::bounded(p.bodyStrike, 40, 0, 100) * .01;
        // Explicit onset emphasis: no added noise, automatic level matching,
        // envelope-dependent output gain or hidden feedback sustain.
        const double excitation = (1 - .8 * strike) + 1.35 * strike * onset_;
        for (auto& value : input) value = mask ? std::max(-4.0, std::min(4.0, value * excitation)) : 0;
        double sum[2]{};
        for (auto& mode : modes_) {
            // Convex interpolation between complex poles strictly inside
            // the unit disk remains stable, including large pitch changes.
            mode.re += poleSmoothing_ * (mode.targetRe - mode.re);
            mode.im += poleSmoothing_ * (mode.targetIm - mode.im);
            mode.drive += poleSmoothing_ * (mode.targetDrive - mode.drive);
            mode.weight += poleSmoothing_ * (mode.targetWeight - mode.weight);
            for (int c = 0; c < 2; ++c) {
                const double nextRe = mode.re * mode.zr[c] - mode.im * mode.zi[c] + mode.drive * input[c];
                const double nextIm = mode.im * mode.zr[c] + mode.re * mode.zi[c];
                mode.zr[c] = body_detail::bounded(nextRe, 0, -8, 8);
                mode.zi[c] = body_detail::bounded(nextIm, 0, -8, 8);
                if (std::abs(mode.zr[c]) + std::abs(mode.zi[c]) < 1e-24) mode.zr[c] = mode.zi[c] = 0;
                // A velocity impulse enters the real state; displacement
                // is the quadrature state. The aligned attack sample is
                // therefore unchanged while the body rings after it.
                sum[c] += mode.weight * mode.zi[c];
            }
        }
        for (int c = 0; c < 2; ++c) {
            for (auto& filter : outputHighpass_[c]) sum[c] = filter.process(sum[c]);
            // Fixed normalization and bounded Mix, with a safety clamp only
            // for abusive levels. Normal level does not change the gain law.
            layer[c] = static_cast<float>(std::max(-6.0, std::min(6.0, 25.6 * mix * gate_ * sum[c])));
        }
    }

private:
    struct Highpass {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void prepare(double fs, double frequency, double q) noexcept {
            const double k = std::tan(body_detail::pi * frequency / fs);
            const double norm = 1 / (1 + k / q + k * k);
            b0 = norm; b1 = -2 * norm; b2 = norm;
            a1 = 2 * (k * k - 1) * norm; a2 = (1 - k / q + k * k) * norm;
            z1 = z2 = 0;
        }
        double process(double x) noexcept {
            const double y = body_detail::bounded(b0 * x + z1, 0, -32, 32);
            z1 = body_detail::bounded(b1 * x - a1 * y + z2, 0, -64, 64);
            z2 = body_detail::bounded(b2 * x - a2 * y, 0, -64, 64);
            if (std::abs(z1) + std::abs(z2) < 1e-24) z1 = z2 = 0;
            return y;
        }
        void reset() noexcept { z1 = z2 = 0; }
    };
    struct Mode {
        double re = 0, im = 0, drive = 0, weight = 0;
        double targetRe = 0, targetIm = 0, targetDrive = 0, targetWeight = 0;
        std::array<double, 2> zr{}, zi{};
    };
    void clearEnergy() noexcept {
        for (auto& mode : modes_) { mode.zr = {}; mode.zi = {}; }
        for (auto& channel : inputHighpass_) for (auto& filter : channel) filter.reset();
        for (auto& channel : outputHighpass_) for (auto& filter : channel) filter.reset();
        fast_ = slow_ = onset_ = 0; onsetAge_ = 10; onsetArmed_ = true;
    }
    void configure(const Parameters& p, int root, unsigned mask, int material) noexcept {
        const int center = 48 + root; // Root-selected octave 3; MIDI uses the existing pitch-class mask.
        int note = center, nearest = 100;
        for (int candidate = center - 12; candidate <= center + 12; ++candidate) {
            if (!(mask & (1u << (candidate % 12)))) continue;
            const int distance = std::abs(candidate - center);
            if (distance < nearest) { note = candidate; nearest = distance; }
        }
        const double fundamental = 440 * std::exp2((note - 69) / 12.0);
        static constexpr double slope[] = {.36, .50, .72, .84};
        double normalization = 0;
        for (int i = 0; i < kBodyModeCount; ++i) {
            auto& mode = modes_[i];
            const double hz = fundamental * bodyModeRatio(material, i, p.bodyInharmonicity);
            double guard = 1;
            if (hz >= fs_ * .43) guard = 0;
            else if (hz > fs_ * .36) {
                const double x = (hz / fs_ - .36) / .07;
                guard = 1 - x * x * (3 - 2 * x);
            }
            const double radius = std::exp(-body_detail::log1000
                / (fs_ * bodyModeDecaySeconds(material, i, p.bodyDecayMs, p.bodyDamping)));
            const double angle = 2 * body_detail::pi * std::min(hz, fs_ * .43) / fs_;
            mode.targetRe = radius * std::cos(angle); mode.targetIm = radius * std::sin(angle);
            // A sinusoid at a mode's frequency has bounded resonant gain.
            // Material/mode weighting is independent of the source envelope.
            mode.targetDrive = mask ? 2 * (1 - radius) * guard : 0;
            mode.targetWeight = guard / std::pow(i + 1.0, slope[material]);
            normalization += mode.targetWeight;
        }
        for (auto& mode : modes_) {
            mode.targetWeight /= std::max(1.0, normalization);
            if (!initialized_) {
                mode.re = mode.targetRe; mode.im = mode.targetIm;
                mode.drive = mode.targetDrive; mode.weight = mode.targetWeight;
            }
        }
        initialized_ = true; lastMask_ = mask; lastRoot_ = root; lastMaterial_ = material;
    }
    std::array<Mode, kBodyModeCount> modes_{};
    std::array<std::array<Highpass, 2>, 2> inputHighpass_{}, outputHighpass_{};
    double fs_ = 48000, poleSmoothing_ = 0, gateSmoothing_ = 0;
    double fastAttack_ = 0, fastRelease_ = 0, slowAttack_ = 0, slowRelease_ = 0, strikeRelease_ = 0;
    double fast_ = 0, slow_ = 0, onset_ = 0, onsetAge_ = 10, gate_ = 0;
    double silentSeconds_ = 0, tailDecayCeiling_ = .36;
    unsigned lastMask_ = 0;
    int lastRoot_ = -1, lastMaterial_ = -1, controlCountdown_ = 0;
    bool initialized_ = false, onsetArmed_ = true;
};

} // namespace iris
