// These tests freeze pre-0.5 behavior; select the Legacy core explicitly.
#include "IrisDSP.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
constexpr int fs = 48000;
constexpr double pi = 3.14159265358979323846;
int failures = 0;
void check(bool ok, const char* name) { std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; failures += !ok; }
iris::Parameters base() {
    iris::Parameters p; p.engineMode = 0; p.color=200; p.snap=0; p.morphEnabled=p.gateEnabled=false;
    p.lowHz=180; p.highHz=8000; p.transient=0; p.width=100; p.outputDb=0; p.softLimit=false;
    p.root=4; p.scale=2; return p;
}
std::vector<float> source(bool steady=false) {
    std::vector<float> x(fs*2); uint32_t seed=82431; float lp=0;
    for(int i=0;i<int(x.size());++i) {
        seed^=seed<<13; seed^=seed>>17; seed^=seed<<5;
        const float noise=float(double(seed)/2147483648.0-1)*.18f;
        lp+=.3f*(noise-lp);
        const double t=double(i)/fs, local=std::fmod(t,.4);
        const float envelope=steady?1.0f:float((local<.30)?std::min(1.0,local/.001)*std::exp(-local*4):0);
        x[i]=envelope*(lp+.04f*float(std::sin(2*pi*659.2551138*t)));
    }
    return x;
}
std::vector<float> render(const std::vector<float>& x, const iris::Parameters& p) {
    iris::Engine e; e.prepare(fs); e.setParameters(p);
    std::vector<float> in=x; in.resize(x.size()+e.latencySamples());
    std::vector<float> y(in.size()), r(y.size());
    for(size_t i=0;i<in.size();i+=257) {
        int n=int(std::min<size_t>(257,in.size()-i));
        e.process(in.data()+i,nullptr,y.data()+i,r.data()+i,n);
    }
    y.erase(y.begin(),y.begin()+e.latencySamples()); return y;
}
double relativeDifference(const std::vector<float>& a,const std::vector<float>& b,int first=0,int last=-1) {
    if(last<0)last=int(a.size()); double difference=0,reference=0;
    for(int i=first;i<last;++i) {difference+=double(a[i]-b[i])*(a[i]-b[i]);reference+=double(b[i])*b[i];}
    return std::sqrt(difference/std::max(reference,1e-20));
}
double energy(const std::vector<float>& x,int a,int b) { double sum=0;for(int i=a;i<b;++i)sum+=double(x[i])*x[i];return sum/(b-a); }
void features() {
    const auto x=source(); auto p=base(); const auto dry=render(x,p);
    p.attackTailEnabled=true; p.attackColor=0;p.tailColor=150;p.colorTransitionMs=120;
    const auto shaped=render(x,p);
    check(relativeDifference(shaped,dry)>.03,"Attack/Tail changes the added layer after repeated dry onsets");
    p=base();p.pitchDiveEnabled=true;p.pitchDiveSemitones=12;p.pitchDiveMs=180;
    check(relativeDifference(render(x,p),dry)>.08,"Pitch Dive audibly changes the extra resonance");
    p=base();p.scaleMotionEnabled=true;p.scaleMotionDepth=100;p.scaleMotionSync=false;p.scaleMotionRateHz=5;
    check(relativeDifference(render(x,p),dry)>.08,"Scale Motion redistributes allowed-note resonance over time");
    p=base();p.formantEnabled=true;p.formantMix=100;p.formantVowel=2;
    check(relativeDifference(render(x,p),dry)>.12,"Formant shapes the wet spectrum");
    p=base();p.peakTamerEnabled=true;p.peakTamerAmount=100;p.peakTamerMaxDb=12;
    auto peaky=source(true);for(int i=0;i<int(peaky.size());++i)peaky[i]+=.16f*float(std::sin(2*pi*2812.5*i/fs));
    check(relativeDifference(render(peaky,p),render(peaky,base()))>.015,"Peak Tamer reduces a prominent narrow peak on broadband material");
    p=base();p.color=100;p.wetEqDb.fill(-6);
    auto flat=base();flat.color=100;
    std::vector<float> middle(x.size());
    for(int i=0;i<int(middle.size());++i)middle[i]=.1f*float(std::sin(2*pi*1500*i/fs));
    const double db=10*std::log10(energy(render(middle,p),fs/2,fs*3/2)/energy(render(middle,flat),fs/2,fs*3/2));
    std::cout << "Graphic EQ -6 dB midband result=" << db << " dB\n";
    check(std::abs(db+6)<.015,"Graphic EQ applies the requested dB to main wet audio");
}
}
int main(){ features(); std::cout<<"Failures="<<failures<<'\n';return failures?1:0; }
