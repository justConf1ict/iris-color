#include "IrisDSP.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// Black-box regression: an upward dive beyond the synthesis guard must not
// accumulate voices at a shared fs*0.44 pitch. The ordinary dry leak is allowed.
// The later, in-range note must remain audible, so a globally muted engine fails.
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr int fs = 8000;

double strongestSinusoid(const std::vector<float>& y, int first, int last,
                         double lo, double hi) {
    double largest = 0;
    const int count = last-first;
    for (double hz = lo; hz <= hi; hz += 1) {
        double real = 0, imag = 0, weight = 0;
        for (int n = 0; n < count; ++n) {
            const double w = .5-.5*std::cos(2*pi*n/(count-1));
            const double angle = 2*pi*hz*n/fs;
            real += w*y[first+n]*std::cos(angle);
            imag -= w*y[first+n]*std::sin(angle);
            weight += w;
        }
        largest = std::max(largest, 2*std::hypot(real,imag)/weight);
    }
    return largest;
}
double rms(const std::vector<float>& y, int first, int last) {
    double power = 0;
    for (int n = first; n < last; ++n) power += double(y[n])*y[n];
    return std::sqrt(power/(last-first));
}
}

int main() {
    iris::Engine engine;
    engine.prepare(fs,2048,1024);
    iris::Parameters p;
    p.engineMode=1; p.root=3; p.scale=2; // D# minor pentatonic.
    p.color=150; p.snap=100; p.lowHz=230; p.highHz=3600;
    p.decayMs=200; p.width=100; p.outputDb=0; p.transient=0;
    p.morphEnabled=false; p.gateEnabled=false; p.softLimit=false;
    p.attackTailEnabled=false; p.scaleMotionEnabled=false;
    p.formantEnabled=false; p.peakTamerEnabled=false;
    p.pitchDiveEnabled=true; p.pitchDiveSemitones=24; p.pitchDiveMs=1000;
    engine.setParameters(p);
    const int latency=engine.latencySamples();
    const int count=3*fs+latency;
    std::vector<float> input(count), left(count), right(count);
    for (int n=fs/20; n<3*fs; ++n)
        input[n]=static_cast<float>(.2*std::sin(2*pi*1960*(n-fs/20)/fs));
    for (int at=0; at<count; at+=137) {
        const int block=std::min(137,count-at);
        engine.process(input.data()+at,input.data()+at,left.data()+at,right.data()+at,block);
    }
    for (int n=0; n<count; ++n) {
        if (!std::isfinite(left[n]) || !std::isfinite(right[n])) {
            std::puts("FAIL finite upper-Dive output"); return 1;
        }
    }
    const double guardTone=strongestSinusoid(left,latency+int(.15*fs),latency+int(.30*fs),3480,3560);
    const double dryLeak=strongestSinusoid(left,latency+int(.15*fs),latency+int(.30*fs),1958,1962);
    const double settledRms=rms(left,latency+int(1.5*fs),latency+int(2.5*fs));
    std::printf("INFO fs=%d latency=%d guard_peak_amplitude=%.9g guard_dbfs=%.4f dry_leak_amplitude=%.9g settled_rms=%.9g\n",
        fs,latency,guardTone,20*std::log10(std::max(guardTone,1e-15)),dryLeak,settledRms);
    bool ok=true;
    if (latency!=3072) {std::puts("FAIL fixed PDC through upper Dive");ok=false;}
    if (!(guardTone<.005)) {std::puts("FAIL upward Dive pins an audible tone near fs*0.44");ok=false;}
    if (!(settledRms>.01)) {std::puts("FAIL in-range note must recover after Dive");ok=false;}
    if (ok)std::puts("PASS upper-Dive guard fades out-of-range voices while retaining later in-range audio");
    return ok?0:1;
}
