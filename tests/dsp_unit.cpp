// These tests freeze pre-0.5 behavior; select the Legacy core explicitly.
#include "../src/IrisDSP.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <vector>
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#define IRIS_TEST_X64_MXCSR 1
#endif

static bool watchAllocations = false;
static size_t allocations = 0;
void* operator new(std::size_t n) {
    if (watchAllocations) ++allocations;
    if (void* p=std::malloc(n)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
constexpr double pi = 3.14159265358979323846;
int failures = 0;
void check(bool condition, const std::string& message) {
    std::cout << (condition ? "PASS " : "FAIL ") << message << '\n';
    if (!condition) ++failures;
}
iris::Parameters neutral() {
    iris::Parameters p; p.engineMode = 0;
    p.color = 0; p.outputDb = 0; p.softLimit = false;
    p.morphEnabled = false; p.gateEnabled = false; p.transient = 0;
    return p;
}
std::vector<float> render(iris::Engine& e, const std::vector<float>& x, int block=257) {
    std::vector<float> y(x.size()), r(x.size());
    for (size_t i=0;i<x.size();i+=block) {
        const int n=static_cast<int>(std::min<size_t>(block,x.size()-i));
        e.process(x.data()+i,nullptr,y.data()+i,r.data()+i,n);
    }
    return y;
}
double rms(const std::vector<float>& x, int a, int b) {
    double e=0; for(int i=a;i<b;++i) e+=double(x[i])*x[i];
    return std::sqrt(e/std::max(1,b-a));
}
double tone(const std::vector<float>& x, double f, double fs, int a, int b) {
    double re=0,im=0;
    for(int i=a;i<b;++i) { const double w=.5-.5*std::cos(2*pi*(i-a)/(b-a));
        re+=x[i]*w*std::cos(2*pi*f*i/fs); im-=x[i]*w*std::sin(2*pi*f*i/fs); }
    return 4*std::hypot(re,im)/(b-a);
}
void neutralDelay() {
    for (int n : {2048,4096}) for(int s : {512,1024}) {
        iris::Engine e; e.prepare(48000,n,s); e.setParameters(neutral());
        std::vector<float> x(30000);
        unsigned state=17; for(auto& v:x) { state=1664525*state+1013904223; v=.3f*(float(state>>8)/8388608.f-1.f); }
        const auto y=render(e,x); const int d=e.latencySamples();
        double err=0; for(int i=0;i<(int)x.size();++i) err=std::max(err,std::abs(double(y[i])-(i>=d?x[i-d]:0)));
        check(d==n+s && err<2e-6,"Color 0 exact reported delay "+std::to_string(n)+"/"+std::to_string(s)+" error="+std::to_string(err));
    }
}
void identityWola() {
    iris::Engine e; e.prepare(48000); auto p=neutral(); p.color=100; p.snap=0;
    e.setParameters(p); std::vector<float> x(30000);
    for(int i=0;i<(int)x.size();++i) x[i]=.15f*std::sin(2*pi*197.3*i/48000)+.1f*std::cos(2*pi*1601.9*i/48000);
    const auto y=render(e,x); const int d=e.latencySamples(); double err=0;
    for(int i=d;i<(int)x.size();++i) err=std::max(err,std::abs(double(y[i])-x[i-d]));
    check(err<3e-6,"WOLA identity with fully wet snap 0 error="+std::to_string(err));
}
void actualFrequencyShift() {
    iris::Engine e; e.prepare(48000); auto p=neutral(); p.color=100; p.snap=100;
    p.lowHz=120; p.scale=5; p.customMask=1u<<4; p.root=0; // E only
    e.setParameters(p); std::vector<float> x(144000);
    const double f=349.2282314330039, target=329.6275569128699;
    for(int i=0;i<(int)x.size();++i) x[i]=.2f*std::sin(2*pi*f*i/48000);
    const auto y=render(e,x);
    const double at=tone(y,target,48000,48000,144000), old=tone(y,f,48000,48000,144000);
    check(at>.08 && at>old*20,"F4 becomes E4: target="+std::to_string(at)+" source="+std::to_string(old));
}
void silenceAndTail() {
    iris::Engine e; e.prepare(48000); auto p=neutral(); p.color=200; p.decayMs=500;
    p.scale=5; p.customMask=1u<<4; p.lowHz=120; e.setParameters(p);
    std::vector<float> z(48000); const auto quiet=render(e,z);
    check(rms(quiet,0,48000)==0,"Exact silence does not self excite");
    e.reset(); std::vector<float>x(96000);
    for(int i=0;i<24000;++i) x[i]=.25f*std::sin(2*pi*329.6275569*i/48000);
    const auto tail=render(e,x); const int end=24000+e.latencySamples();
    check(rms(tail,end+2400,end+4800)>1e-4,"Color above 100 creates a measurable tuned tail");
    check(rms(tail,90000,96000)<1e-6,"Resonance decays to silence");
}
void gateAndBypass() {
    std::vector<float>x(48000); for(int i=0;i<48000;++i)x[i]=.001f*std::sin(2*pi*440*i/48000);
    auto p=neutral(); p.color=100; p.snap=0; p.gateEnabled=true; p.gateDb=-30; p.lowHz=40;
    iris::Engine e; e.prepare(48000); e.setParameters(p); auto y=render(e,x);
    check(rms(y,24000,48000)<rms(x,24000,48000)*.02,"Spectral gate suppresses bins below dry threshold");
    e.reset(); p.bypass=true; p.outputDb=-20; p.softLimit=true; e.setParameters(p); y=render(e,x);
    double err=0; const int d=e.latencySamples(); for(int i=d;i<48000;++i)err=std::max(err,std::abs(double(y[i])-x[i-d]));
    check(err<1e-7,"Bypass is unity delayed dry irrespective of output trim");
}
void customAndRateContracts() {
    for (int fs : {8000,44100,48000,96000}) {
        iris::Engine e; e.prepare(fs); auto p=neutral(); p.color=100; p.snap=100;
        p.lowHz=80; p.scale=5; p.root=7; p.customMask=1u<<4;
        e.setParameters(p); std::vector<float>x(fs*2);
        for(int i=0;i<(int)x.size();++i)x[i]=.2f*std::sin(2*pi*349.228231433*i/fs);
        auto y=render(e,x);
        const double eTone=tone(y,329.627556913,fs,fs,fs*2);
        check(eTone>.08,"Absolute custom E mask ignores root at sample rate "+std::to_string(fs)+" target="+std::to_string(eTone));
    }
    iris::Engine e; e.prepare(48000); auto p=neutral(); p.color=200; p.scale=5; p.customMask=0;
    e.setParameters(p); std::vector<float>x(48000);
    for(int i=0;i<(int)x.size();++i)x[i]=.2f*std::sin(2*pi*349.228231433*i/48000);
    auto y=render(e,x); const int d=e.latencySamples(); double err=0;
    for(int i=d;i<(int)x.size();++i)err=std::max(err,std::abs(double(y[i])-x[i-d]));
    check(err<3e-6,"Empty custom mask disables tuning and resonance");
}
void automationConverges() {
    iris::Engine e; e.prepare(96000); auto p=neutral(); p.outputDb=-24; e.setParameters(p);
    std::vector<float>x(288000,.25f); std::vector<float>first(1000,.25f); render(e,first);
    p.outputDb=0; e.setParameters(p); const auto y=render(e,x);
    check(std::abs(y.back()-.25f)<1e-7,"Automated unity output gain converges exactly");
}
void midiAndRealtime() {
    iris::Engine e; e.prepare(48000); auto p=neutral(); p.color=100; p.snap=100;
    p.scale=5; p.customMask=1u<<4; p.midiEnabled=true; p.lowHz=80; e.setParameters(p);
    std::vector<float>x(48000);
    for(int i=0;i<48000;++i)x[i]=.2f*std::sin(2*pi*349.228231433*i/48000);
    e.noteOn(65); e.noteOn(65); e.noteOff(65);
    auto y=render(e,x);
    check(tone(y,349.228231433,48000,24000,48000)>.1,"Held MIDI pitch class overrides scale, including overlapping notes");
    e.allNotesOff(); e.noteOff(65); y=render(e,x);
    check(tone(y,329.627556913,48000,24000,48000)>.1,"All Notes Off reliably restores scale fallback");
    p.color=200; p.morphEnabled=true; p.gateEnabled=true; e.setParameters(p);
    std::vector<float>r(48000); unsigned state=11;
    for(auto& v:x){state=1664525*state+1013904223;v=.3f*(float(state>>8)/8388608.f-1.f);}
    allocations=0; watchAllocations=true;
    const auto start=std::chrono::steady_clock::now();
    e.process(x.data(),nullptr,y.data(),r.data(),48000);
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    watchAllocations=false;
    check(allocations==0,"No allocations in active stereo process callback");
    std::cout<<"BENCH 1 second of default-size spectral/color noise: "<<elapsed<<" seconds\n";
}
void glideReachesStablePhase() {
    constexpr int fs=48000;
    constexpr double f0=349.2282314330039, f1=329.6275569128699;
    iris::Engine e; e.prepare(fs); auto p=neutral(); p.color=100; p.snap=100;
    p.scale=5; p.customMask=1u<<4; p.root=0; p.lowHz=60; p.highHz=18000;
    e.setParameters(p); std::vector<float>x(fs*4); double phase=0;
    for(int i=0;i<(int)x.size();++i) {
        const double t=double(i)/fs;
        const double f=t<1?f0:(t<1.5?f0+(f1-f0)*(t-1)*2:f1);
        phase+=2*pi*f/fs; x[i]=.2f*std::sin(phase);
    }
    const auto y=render(e,x);
    // Fit sine and cosine jointly, so nonintegral cycles do not make the
    // amplitude estimate depend on the unknown output phase.
    double minimum=1, maximum=0, sum=0, sumSquares=0; int count=0;
    for(int start=fs*2;start+512<fs*4-4096;start+=128) {
        double cc=0,ss=0,cs=0,yc=0,ys=0;
        for(int i=start;i<start+512;++i) {
            const double c=std::cos(2*pi*f1*i/fs), s=std::sin(2*pi*f1*i/fs);
            cc+=c*c; ss+=s*s; cs+=c*s; yc+=y[i]*c; ys+=y[i]*s;
        }
        const double d=cc*ss-cs*cs;
        const double a=(yc*ss-ys*cs)/d, b=(ys*cc-yc*cs)/d;
        const double amplitude=std::hypot(a,b);
        minimum=std::min(minimum,amplitude); maximum=std::max(maximum,amplitude);
        sum+=amplitude; sumSquares+=amplitude*amplitude; ++count;
    }
    const double stddev=std::sqrt(std::max(0.,sumSquares/count-(sum/count)*(sum/count)));
    check(minimum>.195 && maximum<.205 && stddev<.001,
          "Glide into exact E4 retains continuous output phase: min="+std::to_string(minimum)+
          " max="+std::to_string(maximum)+" std="+std::to_string(stddev));
}
void scopedDenormalProtection() {
#if defined(IRIS_TEST_X64_MXCSR)
    const unsigned original=_mm_getcsr();
    // Mask exceptions, use a nondefault rounding direction, and start with
    // both denormal controls disabled plus an unrelated sticky status flag.
    const unsigned host=((original|0x1f80u)&~(0x8040u|0x6000u|0x3fu))|0x2000u|0x10u;
    struct RunResult {double seconds; bool preserved; bool zero; size_t allocations;};
    auto run=[&](float sample,unsigned flags) {
        iris::Engine e; e.prepare(48000); iris::Parameters p; p.engineMode = 0; p.color=200; e.setParameters(p);
        std::vector<float>x(48000,sample),y(48000),r(48000);
        bool preserved=true;
        allocations=0; watchAllocations=true;
        const auto start=std::chrono::steady_clock::now();
        for(int repeat=0;repeat<2;++repeat) {
            _mm_setcsr(flags);
            e.process(x.data(),nullptr,y.data(),r.data(),48000);
            preserved=preserved && _mm_getcsr()==flags;
        }
        watchAllocations=false;
        const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        _mm_setcsr(original);
        bool zero=true;
        for(size_t i=0;i<y.size();++i)zero=zero && y[i]==0 && r[i]==0;
        return RunResult{elapsed,preserved,zero,allocations};
    };
    const auto normal=run(.01f,host);
    const auto tiny=run(1e-40f,host);
    const auto alreadyFlushing=run(1e-40f,host|0x8040u);
    check(normal.preserved && tiny.preserved && alreadyFlushing.preserved,
          "process restores the host MXCSR exactly, including rounding/status and prior FTZ/DAZ");
    check(tiny.zero,"Subnormal excitation is flushed inside the active audio callback");
    check(normal.allocations==0 && tiny.allocations==0 && alreadyFlushing.allocations==0,
          "Scoped floating-point protection performs zero callback allocations");
    check(tiny.seconds<std::max(normal.seconds,alreadyFlushing.seconds)*5.0,
          "Subnormal processing has no exceptional slowdown relative to normal/host-FTZ processing");
    std::cout<<"BENCH 2 seconds: normal="<<normal.seconds<<" denormal="<<tiny.seconds
             <<" host-already-FTZ/DAZ="<<alreadyFlushing.seconds<<" seconds\n";
    _mm_setcsr(original);
#else
    std::cout<<"SKIP x64 MXCSR regression on this architecture\n";
#endif
}
}
int main(){neutralDelay();identityWola();actualFrequencyShift();silenceAndTail();gateAndBypass();customAndRateContracts();automationConverges();midiAndRealtime();glideReachesStablePhase();scopedDenormalProtection();
    std::cout<<failures<<" failures\n"; return failures?EXIT_FAILURE:EXIT_SUCCESS;}
