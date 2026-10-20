// These tests freeze pre-0.5 behavior; select the Legacy core explicitly.
#include "../src/IrisDSP.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace {
constexpr double pi = 3.14159265358979323846;
int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "PASS " : "FAIL ") << what << '\n';
    if (!ok) ++failures;
}
iris::VisualFrame capture(float morph, float output = 0, int rate = 48000, bool silent = false) {
    iris::Engine e;
    e.prepare(rate);
    iris::Parameters p; p.engineMode = 0;
    p.color = 185; p.snap = 100; p.scale = 5; p.customMask = 1u << 4;
    p.morph = morph; p.morphEnabled = true; p.gateEnabled = false;
    p.lowHz = 60; p.highHz = 18000; p.width = 100; p.transient = 0;
    p.outputDb = output; p.softLimit = false;
    e.setParameters(p);
    std::array<float, 128> x{}, l{}, r{};
    for (int offset = 0; offset < rate; offset += 128) {
        const int count = std::min(128, rate - offset);
        for (int i = 0; i < count; ++i)
            x[i] = silent ? 0 : .2f * static_cast<float>(std::sin(2 * pi * 349.228231433 * (offset + i) / rate));
        e.process(x.data(), nullptr, l.data(), r.data(), count);
    }
    iris::VisualFrame v; e.copyVisual(v);
    check(e.latencySamples() == 3072, "telemetry preserves fixed 3072 sample latency");
    return v;
}
void boundedTargets() {
    struct Case { int rate; double source; int mask; double expected; };
    const Case cases[] = {{8000, 3500, 1, 2093.004522405},
                          {44100, 17900, 1 << 7, 12543.85395142},
                          {48000, 17000, 1 << 6, 11839.82152677}};
    for (const auto& c : cases) {
        iris::Engine e; e.prepare(c.rate);
        iris::Parameters p; p.engineMode = 0; p.color = 100; p.snap = 100; p.scale = 5;
        p.customMask = static_cast<uint16_t>(c.mask);
        p.lowHz = 60; p.highHz = static_cast<float>(c.rate * .45);
        p.morphEnabled = false; p.gateEnabled = false; p.transient = 0;
        p.outputDb = 0; p.softLimit = false;
        e.setParameters(p);
        std::array<float, 128> x{}, l{}, r{};
        bool bounded = true; double energy = 0; int count = 0;
        for (int at = 0; at < c.rate * 2; at += 128) {
            for (int i = 0; i < 128; ++i)
                x[i] = .2f * static_cast<float>(std::sin(2 * pi * c.source * (at + i) / c.rate));
            e.process(x.data(), nullptr, l.data(), r.data(), 128);
            iris::VisualFrame f; e.copyVisual(f);
            for (int i = 0; i < f.moveCount; ++i)
                bounded = bounded && f.moves[i].sourceHz <= c.rate * .49 && f.moves[i].targetHz < c.rate * .49;
            if (at > c.rate) for (float v : l) { energy += v*v; ++count; }
        }
        iris::VisualFrame f; e.copyVisual(f);
        bool target = false;
        for (int i = 0; i < f.moveCount; ++i)
            if (std::abs(f.moves[i].targetHz - c.expected) < 1 && f.moves[i].levelDb > -40) target = true;
        check(bounded && target, "sparse pitch mask chooses the closest physically realizable note below 0.49 Fs");
        check(count && std::sqrt(energy/count) > .03, "upper-band tuning does not fix invalid movement by muting the signal");
    }
}
}
int main() {
    const auto silent = capture(0, 0, 48000, true);
    check(silent.frameCounter > 0 && silent.sampleRate == 48000 && silent.activeMask == 16 && silent.moveCount == 0,
          "silent snapshot has real timing, sample rate, note mask and no invented movements");
    bool zero = true;
    for (int i = 0; i < iris::kVisualBins; ++i)
        zero = zero && silent.dryDb[i] == -120 && silent.wetDb[i] == -120 && silent.gateGain[i] == 1;
    check(zero, "silent spectra stay at the documented visual floor");

    const auto a = capture(0), b = capture(50), c = capture(100), boosted = capture(50, 6);
    bool finite = true, gains = true, outputIndependent = true;
    double midpointError = 0, endpointError = 0;
    for (int i = 0; i < iris::kVisualBins; ++i) {
        finite = finite && std::isfinite(a.dryDb[i]) && std::isfinite(a.wetDb[i]);
        gains = gains && a.gateGain[i] == 1;
        outputIndependent = outputIndependent && b.wetDb[i] == boosted.wetDb[i] && b.dryDb[i] == boosted.dryDb[i];
        const double hz = iris::kVisualMinHz * std::pow(double(iris::kVisualMaxHz / iris::kVisualMinHz), double(i) / (iris::kVisualBins - 1));
        if (hz > 150 && hz < 14000 && a.wetDb[i] > -85 && a.dryDb[i] > -85) {
            const double ma = std::pow(10., a.wetDb[i] / 20.), mb = std::pow(10., b.wetDb[i] / 20.), mc = std::pow(10., c.wetDb[i] / 20.);
            midpointError = std::max(midpointError, std::abs(mb - .5 * (ma + mc)));
            endpointError = std::max(endpointError, std::abs(double(c.wetDb[i] - c.dryDb[i])));
        }
    }
    check(finite && gains, "spectral telemetry is finite and reports actual disabled-gate unity gains");
    check(outputIndependent, "visual wet spectrum is pre-output, as documented");
    check(midpointError < 2e-6 && endpointError < .0002, "live spectral Morph midpoint and dry-magnitude endpoint match their contracts");
    bool realMove = false;
    for (int i = 0; i < a.moveCount; ++i)
        if (std::abs(a.moves[i].sourceHz - 349.228231433) < 1 && std::abs(a.moves[i].targetHz - 329.627556913) < 1 && a.moves[i].levelDb > -40)
            realMove = true;
    check(a.moveCount <= a.moves.size() && realMove, "movement telemetry contains measured F4 to E4 tuning");
    const auto lowRate = capture(0, 0, 8000);
    bool nyquist = true;
    for (int i = 0; i < iris::kVisualBins; ++i) {
        const double hz = iris::kVisualMinHz * std::pow(double(iris::kVisualMaxHz / iris::kVisualMinHz), double(i) / (iris::kVisualBins - 1));
        if (hz >= 4000) nyquist = nyquist && lowRate.dryDb[i] == -120 && lowRate.wetDb[i] == -120;
    }
    check(nyquist, "visualization does not fabricate repeated Nyquist energy above the sample-rate limit");
    boundedTargets();
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
