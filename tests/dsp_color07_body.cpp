#include "IrisDSP.h"
#include "IrisBody.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <limits>
#include <new>
#include <vector>

static size_t allocations = 0;
void* operator new(size_t n) {
    ++allocations;
    if (void* p = std::malloc(std::max(size_t(1), n))) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

static constexpr double pi = 3.14159265358979323846;
static int failed = 0;
static void check(bool okay, const char* description, double measured = 0) {
    std::printf("%s %s [%.12g]\n", okay ? "PASS" : "FAIL", description, measured);
    if (!okay) ++failed;
}

static iris::Parameters neutral() {
    iris::Parameters p;
    p.color = 100; p.snap = 0; p.morph = 0; p.gateEnabled = false;
    p.morphEnabled = false; p.transient = 0; p.width = 100;
    p.outputDb = 0; p.softLimit = false; p.clarity = 0;
    return p;
}

static iris::Parameters bodyParameters() {
    auto p = neutral();
    p.bodyMaterial = 0; p.bodyMix = 80; p.bodyDecayMs = 650;
    p.bodyDamping = 30; p.bodyInharmonicity = 40; p.bodyStrike = 60;
    return p;
}

static std::vector<float> render(const std::vector<float>& input, iris::Parameters p,
                                 double fs = 48000, int block = 256, int midiNote = -1) {
    iris::Engine engine;
    engine.prepare(fs);
    engine.setParameters(p);
    if (midiNote >= 0) engine.noteOn(midiNote);
    std::vector<float> output(input.size()), right(input.size());
    for (size_t at = 0; at < input.size(); at += block) {
        const int n = static_cast<int>(std::min(size_t(block), input.size() - at));
        engine.process(input.data() + at, input.data() + at, output.data() + at, right.data() + at, n);
    }
    return output;
}

static std::vector<float> difference(std::vector<float> a, const std::vector<float>& b) {
    for (size_t i = 0; i < a.size(); ++i) a[i] -= b[i];
    return a;
}
static double energy(const std::vector<float>& x, size_t first, size_t last) {
    double e = 0;
    for (size_t i = first; i < std::min(last, x.size()); ++i) e += double(x[i]) * x[i];
    return e;
}
static double peak(const std::vector<float>& x, size_t first = 0) {
    double p = 0;
    for (size_t i = first; i < x.size(); ++i) p = std::max(p, std::abs(double(x[i])));
    return p;
}
static double roughness(const std::vector<float>& x, size_t first, size_t last) {
    double e = 0;
    for (size_t i = std::max(size_t(1), first); i < std::min(last, x.size()); ++i) {
        const double d = x[i] - x[i - 1]; e += d * d;
    }
    return e / std::max(1e-30, energy(x, first, last));
}
static bool identical(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}
static std::vector<float> noise(size_t n) {
    std::vector<float> x(n); unsigned state = 1;
    for (auto& v : x) { state = state * 1664525u + 1013904223u; v = ((state >> 8) / 8388608.0f - 1) * .15f; }
    return x;
}

static void impulseAndMaterials() {
    std::vector<float> impulse(96000); impulse[0] = .5f;
    auto p = bodyParameters();
    auto off = p; off.bodyMix = 0;
    const auto baseline = render(impulse, off);
    std::array<std::vector<float>, 4> layers;
    for (int m = 0; m < 4; ++m) {
        p.bodyMaterial = m;
        layers[m] = difference(render(impulse, p), baseline);
        const double e = energy(layers[m], 3072 + 256, 48000);
        check(e > 1e-8, "impulse creates an added resonant tail for every material", e);
        check(std::abs(layers[m][3072]) < .025, "body preserves the impulse attack within five percent", layers[m][3072]);
    }
    for (int a = 0; a < 4; ++a) for (int b = a + 1; b < 4; ++b) {
        const double relative = energy(difference(layers[a], layers[b]), 3072, 48000)
            / std::max(1e-30, energy(layers[a], 3072, 48000) + energy(layers[b], 3072, 48000));
        check(relative > .08, "material pairs have measurably different impulse responses", relative);
    }
    p.bodyMaterial = 3; p.bodyStrike = 0; p.bodyDecayMs = 80;
    const auto shortTail = difference(render(impulse, p), baseline);
    p.bodyDecayMs = 1000;
    const auto longTail = difference(render(impulse, p), baseline);
    const size_t first = 3072 + 12000, last = 3072 + 21600;
    const double ratio = energy(longTail, first, last) / std::max(1e-30, energy(shortTail, first, last));
    check(ratio > 100, "longer Decay audibly extends the late resonant tail", ratio);
    p.bodyMaterial = 0; p.bodyDamping = 0; p.bodyDecayMs = 800;
    const auto bright = difference(render(impulse, p), baseline);
    p.bodyDamping = 100;
    const auto damped = difference(render(impulse, p), baseline);
    const double r = roughness(damped, first, last) / std::max(1e-30, roughness(bright, first, last));
    check(energy(bright, first, last) > 1e-12 && r < .85, "Damping reduces the high-frequency content of the late tail", r);
    check(peak(layers[0], 3072 + 72000) < 1e-7, "an excited body returns to silence without a held feedback state", peak(layers[0], 3072 + 72000));
}

static void zeroMixAndSub() {
    auto input = noise(24576);
    for (int variant = 0; variant < 3; ++variant) {
        auto p = neutral(); p.engineMode = variant == 0 ? 0 : 1;
        if (variant == 1) { p.color = 187; p.snap = 67; p.morph = 29; p.clarity = 42; p.gateEnabled = true; }
        if (variant == 2) {
            p.attackTailEnabled = true; p.pitchDiveEnabled = true; p.scaleMotionEnabled = true;
            p.formantEnabled = true; p.peakTamerEnabled = true; p.bandEqMidDb = 4; p.wetEqDb[8] = -3;
        }
        const auto original = render(input, p);
        p.bodyMaterial = 3; p.bodyDecayMs = 1800; p.bodyDamping = 100;
        p.bodyInharmonicity = 100; p.bodyStrike = 100;
        check(identical(original, render(input, p)), "Mix zero ignores body controls in existing Legacy/Prism/creative paths");
    }
    auto p = bodyParameters(); p.bodyMix = 100; p.bodyDecayMs = 40; p.bodyDamping = 100;
    input.resize(96000);
    for (size_t i = 0; i < input.size(); ++i) input[i] = .35f * std::sin(2 * pi * 40 * i / 48000);
    auto off = p; off.bodyMix = 0;
    const auto baseline = render(input, off);
    const auto delta = difference(render(input, p), baseline);
    const double relative = std::sqrt(energy(delta, 3072 + 48000, input.size())
        / std::max(1e-30, energy(baseline, 3072 + 48000, input.size())));
    check(relative < 2e-4, "steady 40 Hz sub changes by less than -74 dB at maximum body Mix", relative);
    p.color = 0;
    check(identical(render(input, p), render(input, [&] { auto q = p; q.bodyMix = 0; return q; }())),
          "Color zero remains the original dry endpoint with body controls active");
    p = bodyParameters(); p.bodyMix = 100; p.bypass = true;
    check(identical(render(input, p), render(input, [&] { auto q = p; q.bodyMix = 0; return q; }())),
          "final bypass excludes the new resonant layer");
}

static void silenceMasksAndMidi() {
    std::vector<float> zero(8192);
    for (double fs : {8000.0, 44100.0, 48000.0, 192000.0}) {
        for (int m = 0; m < 4; ++m) {
            auto p = bodyParameters(); p.bodyMaterial = m; p.bodyMix = 100; p.bodyDecayMs = 1800;
            check(peak(render(zero, p, fs)) == 0, "silence stays exactly silent at supported sample rates");
        }
    }
    auto input = noise(24576);
    auto p = bodyParameters(); p.scale = 5; p.customMask = 0; p.midiEnabled = true;
    auto off = p; off.bodyMix = 0;
    check(identical(render(input, p), render(input, off)), "empty custom mask prevents new body excitation");
    check(energy(difference(render(input, p, 48000, 256, 60), render(input, off, 48000, 256, 60)), 3072, input.size()) > 1e-7,
          "held MIDI pitch classes can excite an otherwise empty-mask body");
    iris::Engine engine; engine.prepare(48000); engine.setParameters(p); engine.noteOn(60);
    std::vector<float> impulse(48000), output(48000), right(48000); impulse[0] = .5f;
    engine.process(impulse.data(), impulse.data(), output.data(), right.data(), 8192);
    engine.allNotesOff();
    engine.process(impulse.data() + 8192, impulse.data() + 8192, output.data() + 8192, right.data() + 8192, 20000);
    engine.noteOn(60);
    engine.process(impulse.data() + 28192, impulse.data() + 28192, output.data() + 28192, right.data() + 28192, 48000 - 28192);
    check(peak(output, 20000) == 0, "closing and reopening an empty MIDI mask cannot revive stored body energy", peak(output, 20000));
}

static void realtimeAndContinuity() {
    auto input = noise(32768); auto p = bodyParameters(); p.bodyMix = 100;
    check(identical(render(input, p, 48000, 1), render(input, p, 48000, 509)),
          "output is bit-identical across one-sample and irregular host blocks");
    iris::Engine engine; engine.prepare(48000); engine.setParameters(p);
    std::array<float, 128> left{}, right{};
    const size_t before = allocations;
    for (int block = 0; block < 200; ++block) {
        p.bodyMaterial = block % 4; p.root = block % 12;
        p.bodyMix = block % 7 == 0 ? 0 : 100;
        p.bodyDecayMs = block % 2 ? 40 : 1800;
        p.bodyDamping = block % 3 ? 0 : 100; p.bodyInharmonicity = block % 2 ? 0 : 100;
        engine.setParameters(p); engine.noteOn(block % 128); engine.noteOff((block + 127) % 128);
        engine.process(input.data() + (block * 128) % (input.size() - 128), nullptr, left.data(), right.data(), 128);
    }
    check(allocations == before, "processing, MIDI and parameter updates allocate no memory", allocations - before);
    p = bodyParameters(); p.bodyMix = 100; p.bodyDecayMs = 1800;
    engine.setParameters(p);
    check(engine.latencySamples() == 3072, "Material Body adds zero samples to the default latency", engine.latencySamples());
    check(engine.tailSeconds() >= 3.6 + 3072.0 / 48000, "reported tail conservatively includes body decay", engine.tailSeconds());
    for (double fs : {8000.0, 44100.0, 48000.0, 192000.0}) {
        iris::Engine e; e.prepare(fs); e.setParameters(p);
        bool finite = true; double maxValue = 0;
        for (int block = 0; block < 120; ++block) {
            auto q = p; q.bodyMaterial = block % 6 - 1;
            q.bodyMix = block % 11 ? 100 : std::numeric_limits<float>::quiet_NaN();
            q.bodyDecayMs = block % 2 ? 40 : 1800;
            q.bodyDamping = block % 7 ? 100 : -std::numeric_limits<float>::infinity();
            q.bodyInharmonicity = block % 2 ? 100 : -100;
            q.bodyStrike = block % 3 ? 0 : 100; q.root = block % 12;
            e.setParameters(q);
            for (size_t i = 0; i < left.size(); ++i) {
                left[i] = (i + block) % 19 ? (i % 2 ? 32.0f : -32.0f) : std::numeric_limits<float>::quiet_NaN();
                right[i] = i % 23 ? left[i] : std::numeric_limits<float>::infinity();
            }
            e.process(left.data(), right.data(), left.data(), right.data(), 128);
            for (size_t i = 0; i < left.size(); ++i) {
                finite &= std::isfinite(left[i]) && std::isfinite(right[i]);
                maxValue = std::max(maxValue, std::max(std::abs(double(left[i])), std::abs(double(right[i]))));
            }
        }
        check(finite && maxValue < 128, "extreme controls, retuning and invalid input remain finite and bounded", maxValue);
    }
}

static std::vector<float> bodyOnly(const std::vector<float>& input, iris::Parameters p, double fs = 48000) {
    iris::MaterialBody body; body.prepare(fs);
    std::vector<float> output(input.size()); float in[2]{}, out[2]{};
    // Warm coefficients/gating with silence, as Engine's aligned dry delay
    // does before the first sample arrives in an ordinary prepared stream.
    for (int i = 0; i < 4096; ++i) body.process(in, out, p, p.root, static_cast<uint16_t>(1u << p.root));
    for (size_t i = 0; i < input.size(); ++i) {
        in[0] = in[1] = input[i]; body.process(in, out, p, p.root, static_cast<uint16_t>(1u << p.root)); output[i] = out[0];
    }
    return output;
}

static void usefulLevelAndResonantHeadroom() {
    auto source = noise(96000);
    // Deterministic broadband bass excitation: remove DC/sub and roll off
    // the white-noise ultrasonic emphasis before judging the body level.
    double low = 0, dc = 0;
    const double lowAlpha = 1 - std::exp(-2 * pi * 1500 / 48000);
    const double dcAlpha = 1 - std::exp(-2 * pi * 80 / 48000);
    for (auto& sample : source) {
        low += lowAlpha * (sample - low); dc += dcAlpha * (low - dc);
        sample = static_cast<float>(low - dc);
    }
    auto p = bodyParameters(); p.bodyMix = 40; p.bodyDecayMs = 360;
    p.bodyDamping = 45; p.bodyInharmonicity = 25; p.bodyStrike = 40;
    for (int material = 0; material < 4; ++material) {
        p.bodyMaterial = material;
        const auto body = bodyOnly(source, p);
        const double ratio = std::sqrt(energy(body, 12000, body.size()) / energy(source, 12000, source.size()));
        check(ratio > .075, "Amount 40 yields a useful layer on deterministic broadband excitation", ratio);
    }
    p.bodyMix = 100; p.bodyDecayMs = 1800; p.bodyDamping = 0;
    p.bodyInharmonicity = 0; p.bodyStrike = 0; p.root = 11;
    for (double fs : {8000.0, 48000.0, 192000.0}) {
        std::vector<float> tone(static_cast<size_t>(fs * 3));
        for (size_t i = 0; i < tone.size(); ++i) tone[i] = .45f * std::sin(2 * pi * 246.9416506 * i / fs);
        double largest = 0;
        for (int material = 0; material < 4; ++material) {
            p.bodyMaterial = material; const auto y = bodyOnly(tone, p, fs);
            largest = std::max(largest, peak(y));
        }
        check(largest > .04 && largest < 3.5, "maximum amount and decay stay bounded on a sustained resonant tone", largest);
    }
}

static void decayStrikeAndDc() {
    auto p = bodyParameters(); p.bodyMaterial = 3; p.bodyMix = 100;
    p.bodyDecayMs = 500; p.bodyDamping = 100; p.bodyInharmonicity = 0; p.bodyStrike = 0;
    std::vector<float> input(96000); input[0] = .5f;
    const auto impulse = bodyOnly(input, p);
    const double t60Db = 10 * std::log10(energy(impulse, 31200, 33600) / energy(impulse, 7200, 9600));
    check(t60Db > -66 && t60Db < -54, "measured late tail loses approximately 60 dB across a 500 ms T60", t60Db);
    bool harmonic = true, ordered = true, losses = true;
    for (int material = 0; material < 4; ++material) for (int mode = 0; mode < iris::kBodyModeCount; ++mode) {
        harmonic &= iris::bodyModeRatio(material, mode, 0) == mode + 1;
        if (mode) {
            ordered &= iris::bodyModeRatio(material, mode, 100) > iris::bodyModeRatio(material, mode - 1, 100);
            losses &= iris::bodyModeDecaySeconds(material, mode, 500, 100)
                < iris::bodyModeDecaySeconds(material, mode - 1, 500, 100);
        }
    }
    check(harmonic && ordered && losses, "shared model view has harmonic zero endpoint, ordered modes and increasing losses");
    for (size_t i = 0; i < input.size(); ++i) input[i] = .25f * std::sin(2 * pi * 261.6255653 * i / 48000);
    const auto sustain = bodyOnly(input, p);
    p.bodyStrike = 100;
    const auto struck = bodyOnly(input, p);
    const double strikeRatio = energy(struck, 48000, input.size()) / energy(sustain, 48000, input.size());
    check(strikeRatio > .025 && strikeRatio < .06, "Strike emphasizes onset while reducing sustained excitation", strikeRatio);
    std::fill(input.begin(), input.end(), .4f);
    const auto dc = bodyOnly(input, p);
    check(peak(dc, 72000) < 1e-8, "DC cannot continuously excite the body after the onset has decayed", peak(dc, 72000));
    p.bodyDecayMs = 40;
    std::fill(input.begin(), input.end(), 0); input[0] = .5f;
    check(peak(bodyOnly(input, p), 72000) == 0, "reported finite tail policy eventually clears all numerical residue");
}

static std::vector<float> automated(const std::vector<float>& input, int block, bool enabled) {
    iris::Engine engine; engine.prepare(48000);
    auto p = bodyParameters(); p.midiEnabled = true; p.scale = 5; p.customMask = 1;
    std::vector<float> output(input.size()), right(input.size());
    size_t at = 0;
    while (at < input.size()) {
        if (at % 4096 == 0) {
            const int event = static_cast<int>(at / 4096);
            p.bodyMix = enabled ? (event % 5 ? 70 : 0) : 0;
            p.bodyMaterial = event % 4; p.bodyDecayMs = event % 2 ? 40 : 1800;
            p.bodyDamping = event % 2 ? 0 : 100; p.bodyInharmonicity = event % 3 ? 100 : 0;
            p.root = event % 12;
            engine.setParameters(p); engine.allNotesOff();
            if (event % 3) engine.noteOn(48 + (event * 5) % 12);
        }
        const int n = static_cast<int>(std::min({size_t(block), input.size() - at, size_t(4096 - at % 4096)}));
        engine.process(input.data() + at, input.data() + at, output.data() + at, right.data() + at, n);
        at += n;
    }
    return output;
}

static void automationAndDormancy() {
    std::vector<float> input(49152);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = .16f * std::sin(2 * pi * 196 * i / 48000) + .08f * std::sin(2 * pi * 392 * i / 48000);
    const auto live = automated(input, 509, true);
    check(identical(live, automated(input, 1, true)), "material, pitch, Mix and MIDI automation is host-block invariant");
    const auto layer = difference(live, automated(input, 509, false));
    double maxStep = 0;
    for (size_t i = 1; i < layer.size(); ++i) maxStep = std::max(maxStep, std::abs(double(layer[i]) - layer[i - 1]));
    check(maxStep < .08, "retuning and material automation create no large added discontinuity", maxStep);
    iris::Engine engine; engine.prepare(48000); auto p = neutral(); engine.setParameters(p);
    std::fill(input.begin(), input.end(), 0); input[0] = .5f;
    std::vector<float> output(input.size()), right(input.size());
    engine.process(input.data(), input.data(), output.data(), right.data(), 12000);
    p.bodyMix = 100; engine.setParameters(p);
    engine.process(input.data() + 12000, input.data() + 12000, output.data() + 12000, right.data() + 12000, static_cast<int>(input.size()) - 12000);
    check(peak(output, 20000) == 0, "enabling an idle body cannot reveal an old hidden impulse tail", peak(output, 20000));
}

int main() {
    impulseAndMaterials(); zeroMixAndSub(); silenceMasksAndMidi(); realtimeAndContinuity();
    decayStrikeAndDc(); automationAndDormancy();
    usefulLevelAndResonantHeadroom();
    std::printf("Material Body behavior suite: %d failure(s)\n", failed);
    return failed ? 1 : 0;
}
