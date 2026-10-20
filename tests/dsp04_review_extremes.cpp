// Independent DSP review: transport edge cases, block invariance, stereo
// symmetry, in-place processing, hot/nonfinite parameters, and reset recovery.
// Build with -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all
// to make invalid PPQ-to-step casts fail rather than relying on audible output.
#include "src/IrisDSP.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
bool watchAllocations=false;
std::size_t allocations=0;
}
void* operator new(std::size_t n) {
    if (watchAllocations) ++allocations;
    if (void* p=std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }

namespace {
constexpr double pi=3.1415926535897932384626433832795;
int passed=0;
void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void pass(const char* message) { ++passed; std::printf("PASS %s\n",message); }
struct Audio { std::vector<float> l,r; };
std::uint32_t randomState=0x18203u;
std::uint32_t random32() {
    randomState^=randomState<<13; randomState^=randomState>>17; randomState^=randomState<<5;
    return randomState;
}
Audio signal(int fs,int samples) {
    Audio x; x.l.resize(samples); x.r.resize(samples);
    std::uint32_t rng=920375u;
    for (int i=0;i<samples;++i) {
        double t=double(i)/fs, phase=std::fmod(t,.29);
        rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
        double noise=double(rng)/2147483648.-1.;
        double env=std::min(1.,phase/.003)*std::exp(-phase*12.);
        x.l[i]=float(env*(.16*std::sin(2*pi*329.6275569*t)+.04*std::sin(2*pi*1178.3*t)+.025*noise));
        x.r[i]=float(env*(.12*std::sin(2*pi*493.8833013*t+.37)+.04*std::sin(2*pi*1178.3*t+.22)-.015*noise));
    }
    return x;
}
iris::Parameters creative() {
    iris::Parameters p;
    p.color=190;p.snap=100;p.root=4;p.scale=2;p.morph=30;p.gateDb=-66;
    p.decayMs=350;p.transient=10;p.width=100;p.outputDb=0;p.softLimit=false;
    p.attackTailEnabled=true;p.attackColor=15;p.tailColor=150;p.colorTransitionMs=75;
    p.pitchDiveEnabled=true;p.pitchDiveSemitones=12;p.pitchDiveMs=80;
    p.scaleMotionEnabled=true;p.scaleMotionDepth=80;p.scaleMotionSync=true;p.scaleMotionDivision=3;
    p.formantEnabled=true;p.formantMix=70;p.formantVowel=1.2f;p.formantMotion=65;p.formantRateHz=.7f;
    p.peakTamerEnabled=true;p.peakTamerAmount=90;p.peakTamerMaxDb=8;
    for (unsigned i=0;i<p.wetEqDb.size();++i) p.wetEqDb[i]=i%2 ? -3.f : 2.f;
    return p;
}
bool finite(const Audio& y) {
    for (auto* v : {&y.l,&y.r}) for (float x : *v) if (!std::isfinite(x)) return false;
    return true;
}
double difference(const std::vector<float>& x,const std::vector<float>& y) {
    double error=0;
    for (std::size_t i=0;i<x.size();++i) error=std::max(error,std::abs(double(x[i])-y[i]));
    return error;
}
bool visualFinite(const iris::VisualFrame& v) {
    if (!std::isfinite(v.attackTailGain) || !std::isfinite(v.diveSemitones) ||
        !std::isfinite(v.tamerReductionDb) || !std::isfinite(v.formantVowel) ||
        v.attackTailGain<0 || v.attackTailGain>2.001f || v.diveSemitones< -24.001f || v.diveSemitones>24.001f ||
        v.tamerReductionDb<0 || v.tamerReductionDb>18.001f || v.formantVowel<0 || v.formantVowel>4.001f ||
        v.motionPitchClass< -1 || v.motionPitchClass>11) return false;
    for (int i=0;i<iris::kVisualBins;++i)
        if (!std::isfinite(v.dryDb[i]) || !std::isfinite(v.wetDb[i]) || !std::isfinite(v.gateGain[i]) ||
            v.gateGain[i]<0 || v.gateGain[i]>1) return false;
    return true;
}
Audio render(iris::Engine& engine,const Audio& x,int fs,int block,bool inplace=false,bool playing=true) {
    Audio y;
    if (inplace) y=x;
    else { y.l.resize(x.l.size()); y.r.resize(x.r.size()); }
    allocations=0;watchAllocations=true;
    bool goodVisual=true;
    for (std::size_t at=0;at<x.l.size();at+=block) {
        const int n=static_cast<int>(std::min<std::size_t>(block,x.l.size()-at));
        const double ppq=37.125+(playing?double(at)*137./(60.*fs):0.);
        engine.setTransport(137,ppq,playing,true);
        engine.process(inplace?y.l.data()+at:x.l.data()+at,
                       inplace?y.r.data()+at:x.r.data()+at,y.l.data()+at,y.r.data()+at,n);
        iris::VisualFrame frame;engine.copyVisual(frame);goodVisual=goodVisual && visualFinite(frame);
    }
    watchAllocations=false;
    require(allocations==0,"Audio / transport / visual callback allocated memory");
    require(goodVisual,"Creative telemetry became invalid");
    return y;
}
void chunkAndStereo() {
    constexpr int fs=48000;
    const auto x=signal(fs,fs*2);
    iris::Engine a,b,c,swapped,paused;
    for (auto* e : {&a,&b,&c,&swapped,&paused}) { e->prepare(fs);e->setParameters(creative()); }
    const auto one=render(a,x,fs,1), varied=render(b,x,fs,257), inplace=render(c,x,fs,127,true);
    const double chunkError=std::max(difference(one.l,varied.l),difference(one.r,varied.r));
    const double inplaceError=std::max(difference(one.l,inplace.l),difference(one.r,inplace.r));
    std::printf("chunk maxerr=%.10g inplace maxerr=%.10g\n",chunkError,inplaceError);
    require(chunkError<5e-5,"Host-synced effects depend on callback chunk size");
    pass("all five effects and EQ remain consistent across 1 / 257-sample host spans");
    require(inplaceError<5e-5,"In-place creative processing changes audio");
    pass("all five effects and EQ support in-place 127-sample buffers");
    Audio exchanged{x.r,x.l};const auto swappedResult=render(swapped,exchanged,fs,257);
    const double swapError=std::max(difference(varied.l,swappedResult.r),difference(varied.r,swappedResult.l));
    std::printf("stereo swap maxerr=%.10g\n",swapError);
    require(swapError<5e-5,"Stereo-linked creative effects break channel-swap symmetry");
    pass("stereo channel exchange preserves linked peak suppression and creative routing");
    const auto stopped=render(paused,x,fs,257,false,false);
    iris::VisualFrame finalFrame;paused.copyVisual(finalFrame);
    require(finite(stopped) && finalFrame.motionHostSynced,"Stopped valid host transport loses sync status");
    require(a.latencySamples()==3072 && paused.latencySamples()==3072,"Creative processors changed fixed latency");
    pass("valid stopped transport stays finite and total fixed latency remains 3072 samples");
}
void adversarial(int fs) {
    iris::Engine e;e.prepare(fs);auto p=creative();e.setParameters(p);
    constexpr auto fields=std::array{
        &iris::Parameters::color,&iris::Parameters::snap,&iris::Parameters::morph,&iris::Parameters::gateDb,
        &iris::Parameters::decayMs,&iris::Parameters::lowHz,&iris::Parameters::highHz,&iris::Parameters::transient,
        &iris::Parameters::width,&iris::Parameters::outputDb,
        &iris::Parameters::bandColorLow,&iris::Parameters::bandColorMid,&iris::Parameters::bandColorHigh,
        &iris::Parameters::bandEqLowDb,&iris::Parameters::bandEqMidDb,&iris::Parameters::bandEqHighDb,
        &iris::Parameters::colorSplitLowHz,&iris::Parameters::colorSplitHighHz,
        &iris::Parameters::attackColor,&iris::Parameters::tailColor,&iris::Parameters::colorTransitionMs,
        &iris::Parameters::pitchDiveSemitones,&iris::Parameters::pitchDiveMs,
        &iris::Parameters::scaleMotionDepth,&iris::Parameters::scaleMotionRateHz,
        &iris::Parameters::formantMix,&iris::Parameters::formantVowel,&iris::Parameters::formantMotion,
        &iris::Parameters::formantRateHz,&iris::Parameters::peakTamerAmount,&iris::Parameters::peakTamerMaxDb
    };
    const std::array<float,13> badFloats{{0,1e-30f,-1e-30f,1e30f,-1e30f,
        std::numeric_limits<float>::max(),-std::numeric_limits<float>::max(),
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),.1f,-.1f,1.f}};
    const std::array<double,13> positions{{0,-.125,1e12,-1e12,1e300,-1e300,
        std::numeric_limits<double>::max(),-std::numeric_limits<double>::max(),
        std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(),-.3,37.1}};
    const std::array<double,9> tempi{{0,-120,1e-200,1e300,137,999999,
        std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()}};
    std::array<float,131> l{},r{},yl{},yr{};
    bool good=true;
    allocations=0;watchAllocations=true;
    for (int block=0;block<640;++block) {
        if ((block%8)==0) {
            p=creative();p.softLimit=false;
            for (auto field : fields) p.*field=badFloats[random32()%badFloats.size()];
            for (auto& v : p.wetEqDb) v=badFloats[random32()%badFloats.size()];
            p.root=static_cast<int>(random32());p.scale=static_cast<int>(random32());
            p.scaleMotionDivision=static_cast<int>(random32());
            p.customMask=static_cast<std::uint16_t>(random32());
            p.scaleMotionSync=(block%3)!=0;
            e.setParameters(p);
        }
        e.setTransport(tempi[block%tempi.size()],positions[block%positions.size()],(block%5)!=0,(block%7)!=0);
        for (unsigned i=0;i<l.size();++i) {
            l[i]=badFloats[(block+i)%badFloats.size()];
            r[i]=badFloats[(block+3*i+1)%badFloats.size()];
        }
        e.noteOn(static_cast<int>(random32()));e.noteOff(static_cast<int>(random32()));
        if (block%17==0) e.allNotesOff();
        e.process(l.data(),r.data(),yl.data(),yr.data(),static_cast<int>(l.size()));
        e.process(nullptr,nullptr,nullptr,nullptr,0);
        iris::VisualFrame v;e.copyVisual(v);good=good && visualFinite(v);
        for (unsigned i=0;i<l.size();++i) good=good && std::isfinite(yl[i]) && std::isfinite(yr[i]);
    }
    watchAllocations=false;
    require(good,"Adversarial audio/parameters/transport poisoned samples or telemetry");
    require(allocations==0,"Realtime extreme parameter/MIDI/transport path allocated");
    std::printf("extreme fs=%d sample-pairs=%d realtime-allocations=%zu\n",fs,640*131,allocations);
    pass("nonfinite and extreme transport / all 31 float fields / 16 EQ gains remain finite without allocation");

    // Recover without reconstructing the poisoned engine, then verify reset
    // produces exactly the same initial processing history as a fresh engine.
    e.setParameters(creative());e.reset();
    iris::Engine fresh;fresh.prepare(fs);fresh.setParameters(creative());
    const auto x=signal(fs,fs);
    const auto recovered=render(e,x,fs,127),reference=render(fresh,x,fs,127);
    const double error=std::max(difference(recovered.l,reference.l),difference(recovered.r,reference.r));
    double peak=0;for (float v : recovered.l) peak=std::max(peak,std::abs(double(v)));
    std::printf("recovery fs=%d maxerr=%.10g peak=%.10g\n",fs,error,peak);
    require(finite(recovered) && error<1e-7 && peak>1e-4,"Reset fails to clear creative or transport history");
    pass("reset after adversarial history reproduces a fresh sounding engine");
}
}
int main() {
    try {
        chunkAndStereo();
        for (int fs : {8000,48000,192000}) adversarial(fs);
        std::printf("Independent DSP 0.4 review: %d groups passed, 0 failures.\n",passed);
        return 0;
    } catch (const std::exception& error) {
        watchAllocations=false;std::fprintf(stderr,"FAIL %s\n",error.what());return 1;
    }
}
