// These tests freeze pre-0.5 behavior; select the Legacy core explicitly.
#include "IrisDSP.h"
#include "IrisColorBands.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {
constexpr int fs = 48000;
constexpr double pi = 3.14159265358979323846;
int failures = 0;
void check(bool ok, const char* text) {
    std::cout << (ok ? "PASS " : "FAIL ") << text << '\n';
    failures += !ok;
}
iris::Parameters base() {
    iris::Parameters p; p.engineMode = 0;
    p.color=100; p.snap=0; p.morphEnabled=false; p.gateEnabled=false;
    p.lowHz=180; p.highHz=8000; p.transient=0; p.width=100;
    p.outputDb=0; p.softLimit=false; p.root=4; p.scale=2;
    return p;
}
std::vector<float> render(const std::vector<float>& x, iris::Parameters p, int rate=fs) {
    iris::Engine e; e.prepare(rate); e.setParameters(p);
    std::vector<float> padded=x; padded.resize(x.size()+e.latencySamples());
    std::vector<float> y(padded.size()), r(y.size());
    for (size_t i=0;i<y.size();i+=257) {
        const int count=static_cast<int>(std::min<size_t>(257,y.size()-i));
        e.process(padded.data()+i,nullptr,y.data()+i,r.data()+i,count);
    }
    y.erase(y.begin(),y.begin()+e.latencySamples());
    return y;
}
double error(const std::vector<float>& a,const std::vector<float>& b) {
    double e=0; for(size_t i=0;i<a.size();++i)e=std::max(e,std::abs(double(a[i])-b[i])); return e;
}
double energy(const std::vector<float>& x,int a,int b) {
    double e=0; for(int i=a;i<b;++i)e+=double(x[i])*x[i]; return e/std::max(1,b-a);
}
double tone(const std::vector<float>& x,double hz,int rate=fs) {
    const int a=rate*3/10,b=rate*7/10;
    double re=0,im=0;
    for(int i=a;i<b;++i) {
        const double w=.5-.5*std::cos(2*pi*(i-a)/(b-a));
        re+=x[i]*w*std::cos(2*pi*hz*i/rate); im-=x[i]*w*std::sin(2*pi*hz*i/rate);
    }
    return 4*std::hypot(re,im)/(b-a);
}
std::vector<float> tones(int rate=fs) {
    std::vector<float> x(rate);
    const double frequencies[]={93.75,375,1500,6000};
    for(int i=0;i<rate*8/10;++i) for(double f:frequencies)
        if(f<rate*.45)x[i]+=.025f*static_cast<float>(std::sin(2*pi*f*i/rate));
    return x;
}
std::vector<float> noise() {
    std::vector<float> x(fs);
    uint32_t state=2197; float lo=0,hi=0;
    for(int i=0;i<fs*8/10;++i) {
        state^=state<<13; state^=state>>17; state^=state<<5;
        const float white=float(double(state)/2147483648.0-1)*.20f;
        hi+=.65f*(white-hi); lo+=.03f*(hi-lo); x[i]=hi-lo;
    }
    return x;
}
void weightContracts() {
    bool bounds=true, continuity=true;
    for(float low:{120.0f,700.0f,2000.0f}) for(float high:{2500.0f,3000.0f,12000.0f}) {
        std::array<float,3> previous{{1,0,0}};
        for(int i=0;i<20001;++i) {
            const float hz=20*std::pow(1000.0f,i/20000.0f);
            const auto w=iris::colorBandWeights(hz,low,high);
            bounds=bounds && w[0]>=0 && w[1]>=0 && w[2]>=0 && std::abs(w[0]+w[1]+w[2]-1)<2e-7;
            for(int b=0;b<3;++b)continuity=continuity && std::abs(w[b]-previous[b])<.002f;
            previous=w;
        }
    }
    check(bounds && continuity,"shared crossover weights are continuous, nonnegative, and sum to one");
    const auto lo=iris::colorBandWeights(700,700,3000),hi=iris::colorBandWeights(3000,700,3000);
    check(lo[0]==.5f && lo[1]==.5f && hi[1]==.5f && hi[2]==.5f,
          "displayed split frequencies are exact half-weight centres");
}
void neutralControls() {
    const auto x=noise(); auto p=base(); p.color=200;
    const auto a=render(x,p); p.colorSplitLowHz=250; p.colorSplitHighHz=11000;
    const auto moved=render(x,p);
    std::cout << "neutral split error=" << error(a,moved) << '\n';
    check(error(a,moved)<3e-7,"neutral band amounts and EQ make crossover changes null");
    p.bandColorLow=p.bandColorMid=p.bandColorHigh=0;
    const auto withoutLayer=render(x,p); p.color=100;
    const auto main=render(x,p);
    check(error(withoutLayer,main)<3e-7,"all band amounts at zero remove only the extra resonance layer");
    p.bandColorLow=200; p.bandColorMid=13; p.bandColorHigh=157;
    const auto arbitrary=render(x,p);
    check(error(main,arbitrary)<3e-7,"Color 100 ignores all resonance band amounts");
    p.color=0;
    const auto dry=render(x,p); p.bandEqLowDb=12; p.bandEqMidDb=-12; p.bandEqHighDb=7;
    check(error(dry,render(x,p))==0,"Color 0 dry output is unaffected by wet EQ");
    p.color=200; p.bypass=true; p.outputDb=-12;
    check(error(x,render(x,p))==0,"bypass remains exact delayed dry with extreme new controls");
}
void equalizerScope() {
    const auto x=tones(); auto p=base(); const auto neutral=render(x,p);
    p.bandEqLowDb=6; p.bandEqMidDb=-6; p.bandEqHighDb=3;
    const auto eq=render(x,p);
    bool independent=true;
    const double hz[]={375,1500,6000}, db[]={6,-6,3};
    for(int b=0;b<3;++b) {
        const double measured=20*std::log10(tone(eq,hz[b])/tone(neutral,hz[b]));
        std::cout << "EQ " << hz[b] << " Hz=" << measured << " dB\n";
        independent=independent && std::abs(measured-db[b])<.015;
    }
    check(independent,"EQ shapes the main wet signal independently in low, mid, and high bands");
    check(std::abs(tone(eq,93.75)-tone(neutral,93.75))<2e-6,
          "dry sub protection is applied after wet EQ");
    p.bandEqLowDb=p.bandEqMidDb=p.bandEqHighDb=6;
    const auto flat=render(x,p);
    check(std::abs(20*std::log10(tone(flat,1500)/tone(neutral,1500))-6)<.002,
          "three equal EQ values produce that exact dB gain in the wet band");
    p.morphEnabled=true; p.morph=100;
    const auto morphEq=render(x,p);
    check(std::abs(20*std::log10(tone(morphEq,1500)/tone(neutral,1500))-6)<.002,
          "EQ follows Morph and therefore still acts at the Morph 100 endpoint");
    p.gateEnabled=true; p.gateDb=-20; p.bandEqLowDb=p.bandEqMidDb=p.bandEqHighDb=12;
    const auto closed=render(x,p);
    check(tone(closed,1500)<1e-8,"EQ boost cannot reopen a spectral bin closed by the dry gate");
}
void amountScopeAndSanitization() {
    const auto x=tones(); auto p=base(); p.color=200;
    p.bandColorLow=p.bandColorMid=p.bandColorHigh=0;
    const auto main=render(x,p);
    std::array<std::vector<float>,3> isolated;
    for(int b=0;b<3;++b) {
        p.bandColorLow=b==0?100.0f:0; p.bandColorMid=b==1?100.0f:0; p.bandColorHigh=b==2?100.0f:0;
        auto y=render(x,p);
        for(size_t i=0;i<y.size();++i)y[i]-=main[i];
        isolated[b]=std::move(y);
    }
    bool own=true,separated=true;
    const double hz[]={375,1500,6000};
    for(int b=0;b<3;++b) for(int other=0;other<3;++other) {
        const double amplitude=tone(isolated[b],hz[other]);
        if(other==b)own=own && amplitude>.002;
        else separated=separated && amplitude<2e-6;
    }
    check(own && separated,"each Color band amount changes its resonance layer without changing other bands");
    p=base(); p.colorSplitLowHz=2000; p.colorSplitHighHz=1000; p.bandEqMidDb=12;
    const auto crossed=render(x,p); p.colorSplitHighHz=2500;
    check(error(crossed,render(x,p))==0,"DSP sanitizes crossing splits to high >= low times 1.25");
    p=base(); p.bandColorLow=std::numeric_limits<float>::quiet_NaN();
    p.bandEqMidDb=std::numeric_limits<float>::infinity(); p.colorSplitHighHz=-30;
    const auto safe=render(x,p); bool finite=true;
    for(float v:safe)finite=finite && std::isfinite(v);
    check(finite,"nonfinite and out-of-range new parameters are sanitized");
    p=base(); p.color=200; p.colorSplitLowHz=2000; p.colorSplitHighHz=12000;
    p.bandColorLow=p.bandColorMid=p.bandColorHigh=200;
    p.bandEqLowDb=p.bandEqMidDb=p.bandEqHighDb=12; p.softLimit=true;
    const auto lowRate=render(tones(8000),p,8000); finite=true;
    for(float v:lowRate)finite=finite && std::isfinite(v) && std::abs(v)<=1.000001;
    check(finite,"low sample rate and above-Nyquist split values remain finite and soft-limited");
}
void onsetAfterNoise() {
    auto x=noise(); x.resize(fs*2);
    for(int i=0;i<fs;++i)if(i>fs/2)x[i]=0;
    const int begin=fs*11/10;
    for(int i=begin;i<fs*18/10;++i)
        x[i]=.2f*static_cast<float>(std::sin(2*pi*329.6275569*(i-begin)/fs))*std::min(1.0f,float(i-begin)/96);
    auto p=base(); p.color=200; p.decayMs=1000;
    const auto y=render(x,p); double peak=0;
    for(int i=begin;i<begin+fs/10;++i)peak=std::max(peak,std::abs(double(y[i])));
    std::cout << "new-onset peak after noise and silence=" << peak << '\n';
    check(peak<.50,"stored broadband calibration cannot create a large peak on a later on-scale onset");
    const double early=energy(y,fs*19/10,fs*195/100);
    check(early>1e-10,"calibration preserves a real resonance tail when the original signal stops");
}
}
int main() {
    weightContracts(); neutralControls(); equalizerScope(); amountScopeAndSanitization(); onsetAfterNoise();
    std::cout << failures << " failures\n"; return failures ? 1 : 0;
}
