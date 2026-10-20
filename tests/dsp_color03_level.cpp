// These tests freeze pre-0.5 behavior; select the Legacy core explicitly.
#include "IrisDSP.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
constexpr int fs = 48000;
int failures = 0;
void check(bool ok, const char* label) {
    std::cout << (ok ? "PASS " : "FAIL ") << label << '\n';
    failures += !ok;
}
double rms(const std::vector<float>& a, int first, int last) {
    double e = 0;
    for (int i = first; i < last; ++i) e += double(a[i]) * a[i];
    return std::sqrt(e / (last-first));
}
std::vector<float> source() {
    std::vector<float> x(fs*2);
    uint32_t state = 310218;
    float low = 0, broad = 0;
    for (int i = fs/10; i < fs*14/10; ++i) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        const float white = float(double(state) / 2147483648.0 - 1) * .3f;
        broad += .58f * (white-broad);
        low += .04f * (broad-low);
        const float fade = std::min(1.0f, std::min(float(i-fs/10), float(fs*14/10-i))/96);
        x[i] = (broad-low) * fade;
    }
    return x;
}
std::vector<float> render(const std::vector<float>& x, float color, float decay = 200) {
    iris::Engine e; e.prepare(fs);
    iris::Parameters p; p.engineMode = 0;
    p.color=color; p.snap=0; p.decayMs=decay;
    p.root=4; p.scale=2; p.lowHz=180; p.highHz=8000;
    p.morphEnabled=false; p.gateEnabled=false; p.transient=0;
    p.width=100; p.outputDb=0; p.softLimit=false;
    e.setParameters(p);
    std::vector<float> y(x.size()+e.latencySamples()), z(y.size());
    std::vector<float> padded=x; padded.resize(y.size());
    for (size_t i=0; i<y.size(); i+=256) {
        const int count=static_cast<int>(std::min<size_t>(256,y.size()-i));
        e.process(padded.data()+i,nullptr,y.data()+i,z.data()+i,count);
    }
    y.erase(y.begin(),y.begin()+e.latencySamples());
    return y;
}
}
int main() {
    const auto x=source();
    const auto a=render(x,100), b=render(x,150), c=render(x,200), longDecay=render(x,200,1000);
    const int first=fs/2,last=fs*13/10;
    const double reference=rms(a,first,last), match=reference/rms(c,first,last);
    std::vector<float> added(a.size()), halfway(a.size()), residual(a.size()), longAdded(a.size());
    double linearError=0;
    float peak=0;
    for (size_t i=0;i<a.size();++i) {
        added[i]=c[i]-a[i]; halfway[i]=b[i]-a[i];
        longAdded[i]=longDecay[i]-a[i]; residual[i]=float(c[i]*match-a[i]);
        linearError=std::max(linearError,std::abs(double(halfway[i])-.5*added[i]));
        peak=std::max(peak,std::abs(c[i]));
    }
    const double ratio=rms(added,first,last)/reference;
    const double residualRatio=rms(residual,first,last)/reference;
    const double longRatio=rms(longAdded,first,last)/reference;
    std::cout << "layer/base=" << ratio << " gain-matched residual=" << residualRatio
              << " longDecay layer/base=" << longRatio << " peak=" << peak << '\n';
    check(ratio>.35 && ratio<1.15,"Color 200 adds a substantial bounded resonance layer to broadband input");
    check(residualRatio>.20,"Color change remains substantial after removing total RMS gain");
    check(linearError<2e-6,"Color 150 adds exactly half the Color 200 layer when Morph/Gate are off");
    check(longRatio>.30 && longRatio<1.20,"longer Decay does not make broadband excitation nearly disappear");
    check(peak<1.0f,"ordinary 0.3-amplitude input does not create an isolated full-scale peak");
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
