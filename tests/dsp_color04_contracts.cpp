// These tests freeze pre-0.5 behavior; select the Legacy core explicitly.
#include "IrisDSP.h"
#include "IrisCreative.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
constexpr int fs=48000;
constexpr double pi=3.14159265358979323846;
int failures=0;
void check(bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';failures+=!ok;}
iris::Parameters base(){iris::Parameters p; p.engineMode = 0;p.color=200;p.snap=100;p.morphEnabled=p.gateEnabled=false;p.transient=0;p.width=100;p.outputDb=0;p.softLimit=false;p.root=4;p.scale=2;return p;}
std::vector<float> tone(double hz,int length=fs){std::vector<float>x(length);for(int i=0;i<length;++i)x[i]=.1f*float(std::sin(2*pi*hz*i/fs));return x;}
struct Result{std::vector<float> samples;iris::VisualFrame visual;};
Result render(const std::vector<float>& x,iris::Parameters p,int chunk=257,bool transport=false,double tempo=120,double ppq=0){
    iris::Engine engine;engine.prepare(fs);engine.setParameters(p);std::vector<float>in=x;in.resize(x.size()+engine.latencySamples());Result y;y.samples.resize(in.size());std::vector<float>r(in.size());
    for(int i=0;i<int(in.size());i+=chunk){const int n=std::min(chunk,int(in.size())-i);if(transport)engine.setTransport(tempo,ppq+double(i)/fs*tempo/60,true,true);engine.process(in.data()+i,nullptr,y.samples.data()+i,r.data()+i,n);}engine.copyVisual(y.visual);y.samples.erase(y.samples.begin(),y.samples.begin()+engine.latencySamples());return y;
}
double error(const std::vector<float>&a,const std::vector<float>&b,int first=0,int last=-1){if(last<0)last=int(a.size());double e=0;for(int i=first;i<last;++i)e=std::max(e,std::abs(double(a[i])-b[i]));return e;}
double energy(const std::vector<float>& x,int a,int b){double e=0;for(int i=a;i<b;++i)e+=double(x[i])*x[i];return e/(b-a);}
void scope(){
    auto p=base();p.color=100;const auto x=tone(659.2551138);const auto normal=render(x,p).samples;
    p.attackTailEnabled=p.pitchDiveEnabled=p.scaleMotionEnabled=true;p.attackColor=0;p.tailColor=200;p.pitchDiveSemitones=24;p.scaleMotionDepth=100;
    check(error(normal,render(x,p).samples)==0,"Attack/Tail, Pitch Dive, and Scale Motion leave Color 100 main wet unchanged");
    p.formantEnabled=p.peakTamerEnabled=true;p.wetEqDb.fill(12);p.color=0;
    check(error(x,render(x,p).samples)==0,"Color 0 returns exact aligned dry even with all creative effects");
    p.color=200;p.bypass=true;p.outputDb=12;
    check(error(x,render(x,p).samples)==0,"Bypass returns exact dry with all creative effects");
    p=base();p.color=100;p.gateEnabled=true;p.gateDb=0;p.wetEqDb.fill(12);p.formantEnabled=true;
    auto closed=p;closed.wetEqDb.fill(0);closed.formantEnabled=false;
    check(error(render(tone(1500),p).samples,render(tone(1500),closed).samples)==0,"Graphic EQ and Formant cannot reopen dry-gated bins");
    p=base();p.color=100;p.wetEqDb.fill(-24);const auto sub=tone(93.75);
    auto q=base();q.color=100;
    check(error(render(sub,p).samples,render(sub,q).samples,fs/4,fs*3/4)<1e-6,"Graphic EQ leaves protected sub frequencies unchanged");
}
void graphicEq(){
    std::array<float,16> db{};for(int i=0;i<16;++i)db[i]=-24+float(i)*36/15;
    bool centres=true;
    for(int i=0;i<16;++i)centres=centres&&std::abs(iris::graphicEqDbAt(iris::graphicEqNodeHz(i),db)-db[i])<3e-5;
    check(centres,"All 16 drawn EQ nodes have exact log-frequency response coordinates");
    const float halfway=std::sqrt(iris::graphicEqNodeHz(5)*iris::graphicEqNodeHz(6));
    check(std::abs(iris::graphicEqDbAt(halfway,db)-(db[5]+db[6])/2)<3e-5,"Graphic EQ interpolates dB at the geometric frequency midpoint");
    check(iris::graphicEqDbAt(1,db)==db.front()&&iris::graphicEqDbAt(99999,db)==db.back(),"Graphic EQ retains its endpoint gains outside 80–16000 Hz");
}
void envelope(){
    iris::Engine e;e.prepare(fs);auto p=base();p.attackTailEnabled=p.pitchDiveEnabled=true;p.attackColor=0;p.tailColor=150;p.colorTransitionMs=100;p.pitchDiveMs=150;e.setParameters(p);
    std::vector<float>x(fs),l(fs),r(fs);const int onset=fs/4;for(int i=onset;i<fs/2;++i)x[i]=.12f*float(std::sin(2*pi*659.2551138*(i-onset)/fs));
    iris::VisualFrame v;float earlyGain=2,tailGain=0,peakDive=0,lastDive=24;
    for(int i=0;i<fs;i+=256){const int n=std::min(256,fs-i);e.process(x.data()+i,nullptr,l.data()+i,r.data()+i,n);e.copyVisual(v);const double sourceTime=double(i+n-1-2048-511-onset)/fs;if(sourceTime>=.012&&sourceTime<.035)earlyGain=std::min(earlyGain,v.attackTailGain);if(sourceTime>.18&&sourceTime<.23)tailGain=std::max(tailGain,v.attackTailGain);peakDive=std::max(peakDive,v.diveSemitones);if(sourceTime>.19&&sourceTime<.23)lastDive=std::min(lastDive,v.diveSemitones);}
    std::cout<<"onset earlyGain="<<earlyGain<<" tailGain="<<tailGain<<" peakDive="<<peakDive<<" settledDive="<<lastDive<<'\n';
    check(earlyGain<.5&&tailGain>1.45,"Attack/Tail follows source-aligned onset from low attack to requested tail");
    check(peakDive>10&&lastDive==0,"Pitch Dive triggers at dry onset and reaches exactly zero at its duration");
}
void motion(){
    auto p=base();p.scaleMotionEnabled=true;p.scaleMotionDepth=100;p.scaleMotionSync=true;p.scaleMotionDivision=3;
    auto x=tone(659.2551138,fs*2);for(int i=0;i<int(x.size());++i)x[i]+=.07f*float(std::sin(2*pi*783.990872*i/fs));
    const auto a=render(x,p,127,true,137,-2.125),b=render(x,p,257,true,137,-2.125),c=render(x,p,4096,true,137,-2.125);
    const double difference=std::max(error(a.samples,b.samples),error(a.samples,c.samples));std::cout<<"PPQ block difference="<<difference<<'\n';
    check(difference==0&&a.visual.motionPitchClass==b.visual.motionPitchClass&&a.visual.motionPitchClass==c.visual.motionPitchClass,"Synced motion is bit-identical across block and automation-span sizes");
    iris::Engine e;e.prepare(fs);e.setParameters(p);std::vector<float>zeros(1024),l(1024),r(1024);iris::VisualFrame v;
    e.setTransport(120,1.0,false,true);e.process(zeros.data(),nullptr,l.data(),r.data(),1024);e.copyVisual(v);const int first=v.motionPitchClass;
    for(int i=0;i<50;++i)e.process(zeros.data(),nullptr,l.data(),r.data(),1024);e.copyVisual(v);
    check(first==v.motionPitchClass&&v.motionHostSynced,"Valid stopped transport freezes the step and retains host-sync indication");
    e.setTransport(120,2.0,false,true);e.process(zeros.data(),nullptr,l.data(),r.data(),1024);e.copyVisual(v);
    check(first!=v.motionPitchClass,"A host seek reanchors the selected scale step");
    e.setTransport(120,0,false,false);e.process(zeros.data(),nullptr,l.data(),r.data(),1024);e.copyVisual(v);
    check(!v.motionHostSynced,"Missing host PPQ uses an explicitly unsynced fallback clock");
    const int fallbackStopped=v.motionPitchClass;
    for(int i=0;i<50;++i)e.process(zeros.data(),nullptr,l.data(),r.data(),1024);e.copyVisual(v);
    check(fallbackStopped==v.motionPitchClass,"A known stopped transport also freezes the fallback beat clock");
}
void tamer(){
    auto p=base();p.color=100;const auto x=tone(659.2551138);const auto original=render(x,p).samples;p.peakTamerEnabled=true;p.peakTamerAmount=100;p.peakTamerMaxDb=18;const auto y=render(x,p);
    const double measured=10*std::log10(energy(y.samples,fs/2,fs*3/4)/energy(original,fs/2,fs*3/4));
    std::cout<<"single in-key sine tamer="<<measured<<" dB\n";
    check(std::abs(measured)<.05,"Peak Tamer preserves a single stable in-key tone");
    check(y.visual.tamerReductionDb<=18.0001,"Tamer telemetry respects the maximum gain-reduction cap");
}
}
int main(){scope();graphicEq();envelope();motion();tamer();std::cout<<"Failures="<<failures<<'\n';return failures?1:0;}
