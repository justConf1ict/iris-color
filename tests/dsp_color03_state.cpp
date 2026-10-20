// These tests freeze pre-0.5 behavior; select the Legacy core explicitly.
#include "IrisDSP.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

static bool watchAllocations=false;
static size_t allocations=0;
void* operator new(std::size_t n) {
    if(watchAllocations)++allocations;
    if(void* p=std::malloc(n))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}

namespace {
constexpr double pi=3.14159265358979323846;
int failures=0;
void check(bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failures+=!ok;}
iris::Parameters parameters() {
    iris::Parameters p; p.engineMode = 0;
    p.color=200;p.snap=0;p.morphEnabled=false;p.gateEnabled=false;
    p.width=100;p.transient=0;p.outputDb=0;p.softLimit=false;
    p.root=4;p.scale=2;p.lowHz=180;p.highHz=8000;p.decayMs=1500;
    return p;
}
std::vector<float> run(iris::Engine& e,const std::vector<float>& input,bool automate=false) {
    auto p=parameters();
    std::vector<float> x=input;x.resize(input.size()+e.latencySamples());
    std::vector<float> y(x.size()),right(x.size());
    allocations=0;watchAllocations=true;
    for(size_t at=0;at<x.size();at+=127) {
        if(automate && at%1270==0) {
            p.bandColorLow=float((at/1270)%201);p.bandColorMid=200-p.bandColorLow;
            p.bandEqLowDb=float((at/1270)%25)-12;p.bandEqHighDb=-p.bandEqLowDb;
            p.colorSplitLowHz=120+float((at/1270)%1881);
            p.colorSplitHighHz=1000+float((at/1270)%11001);
            e.setParameters(p);
        }
        const int count=static_cast<int>(std::min<size_t>(127,x.size()-at));
        e.process(x.data()+at,nullptr,y.data()+at,right.data()+at,count);
        iris::VisualFrame frame;e.copyVisual(frame);
    }
    watchAllocations=false;
    check(allocations==0,"new color controls, telemetry and audio processing allocate nothing after prepare");
    y.erase(y.begin(),y.begin()+e.latencySamples());return y;
}
void extremeNextOnset(int rate,float amplitude) {
    std::vector<float> x(static_cast<size_t>(rate*7));
    uint32_t state=4561;
    for(int i=0;i<rate*3/10;++i) {
        state^=state<<13;state^=state>>17;state^=state<<5;
        x[i]=float(double(state)/2147483648.0-1)*.3f;
    }
    const int begin=rate*64/10;
    for(int i=begin;i<rate*68/10;++i)
        x[i]=amplitude*static_cast<float>(std::sin(2*pi*329.6275569*(i-begin)/rate))
            *std::min(1.0f,float(i-begin)/std::max(1,rate/500));
    iris::Engine e;e.prepare(rate);e.setParameters(parameters());
    const auto y=run(e,x);
    double peak=0;bool finite=true;
    for(int i=begin;i<rate*68/10;++i) {
        finite=finite && std::isfinite(y[i]);peak=std::max(peak,std::abs(double(y[i])));
    }
    std::cout<<"state rate="<<rate<<" onset="<<amplitude<<" peak/onset="<<peak/amplitude<<'\n';
    check(finite && peak<2.2*amplitude+1e-10,"long silence cannot turn stored calibration into an excessive weak/loud onset peak");
}
void automationAndReset() {
    constexpr int rate=48000;
    std::vector<float> x(rate);
    for(int i=0;i<rate;++i)x[i]=.1f*static_cast<float>(std::sin(2*pi*1174.65907167*i/rate));
    iris::Engine e;e.prepare(rate);auto p=parameters();e.setParameters(p);
    const auto automated=run(e,x,true);bool finite=true;
    for(float v:automated)finite=finite && std::isfinite(v);
    check(finite,"automating all new gains and split frequencies remains finite");
    e.reset();e.setParameters(p);const auto reset=run(e,x);
    iris::Engine fresh;fresh.prepare(rate);fresh.setParameters(p);const auto reference=run(fresh,x);
    double error=0;for(size_t i=0;i<reset.size();++i)error=std::max(error,std::abs(double(reset[i])-reference[i]));
    check(error<1e-7,"reset clears calibration history and reproduces a fresh engine");
    check(e.latencySamples()==3072,"new spectral routing retains fixed default latency");
}
}
int main() {
    for(int rate:{8000,48000,192000})for(float amplitude:{2e-6f,4.0f})extremeNextOnset(rate,amplitude);
    automationAndReset();
    std::cout<<failures<<" failures\n";return failures?1:0;
}
