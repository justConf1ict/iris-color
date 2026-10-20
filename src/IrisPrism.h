#pragma once

#include "IrisDSP.h"
#include "IrisColorBands.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace iris {

// Independent scale filter bank. No reference waveform, impulse response,
// per-note fitted gain, or external audio is used by the processor.
// A complex analysis pole isolates each chromatic neighbourhood. Heterodyning
// translates that neighbourhood to an allowed note; a second, tuned pole
// concentrates its energy. Both stereo channels share the same translation.
class PrismCore {
    using C = std::complex<double>;
    static constexpr double pi = 3.14159265358979323846;
    struct Biquad {
        double b0=1,b1=0,b2=0,a1=0,a2=0;
        std::array<double,2> z1{},z2{};
        void set(double hz,double fs,bool high) noexcept {
            const double k=std::tan(pi*hz/fs), k2=k*k;
            const double d=1/(1+std::sqrt(2.0)*k+k2);
            b0=(high?1:k2)*d; b1=(high?-2:2)*b0; b2=b0;
            a1=2*(k2-1)*d; a2=(1-std::sqrt(2.0)*k+k2)*d;
        }
        double tick(double x,int c) noexcept {
            const double y=b0*x+z1[c];
            z1[c]=b1*x-a1*y+z2[c]; z2[c]=b2*x-a2*y;
            return y;
        }
        void reset() noexcept {z1={};z2={};}
    };
    struct Voice {
        double hz=0,sourceInput=0;
        C sourcePole{},destinationPole{},poleStep{},rotation{1,0},rotationStep{1,0};
        std::array<C,2> source{},destination{};
        double input=0,inputStep=0,gain=0,gainStep=0,activation=0;
        int pitchClass=0;
        void reset() noexcept {
            source={};destination={};rotation={1,0};rotationStep={1,0};
            destinationPole={};poleStep={};input=inputStep=gain=gainStep=activation=0;
        }
    };
    std::array<Voice,128> voices_{};
    std::array<Biquad,2> low_,high_,midLow_,midHigh_;
    double fs_=48000,tonalHz_=440,tonalWanted_=0,tonalMix_=0;
    double floor_=0.28,activationSlew_=0;
    int countdown_=0,normalizationCountdown_=0;
    bool primed_=false;
    static constexpr int stride=32;
    int nearest(int note,unsigned mask,double low,double high) const noexcept {
        int result=-1;double distance=1e20;
        for(int k=std::max(0,note-12);k<=std::min(127,note+12);++k) {
            const double hz=voices_[k].hz;
            if(!(mask&(1u<<(k%12))) || hz<low || hz>high || hz>=fs_*.45)continue;
            const double d=std::abs(k-note);
            if(d<distance){distance=d;result=k;}
        }
        return result;
    }
    void configure(const Parameters& p,unsigned mask,double dive) noexcept {
        for(auto& f:low_)f.set(p.lowHz,fs_,false);
        for(auto& f:high_)f.set(p.highHz,fs_,true);
        for(auto& f:midLow_)f.set(p.lowHz,fs_,true);
        for(auto& f:midHigh_)f.set(p.highHz,fs_,false);
        int count=0;for(int pc=0;pc<12;++pc)count+=(mask>>pc)&1u;
        // Sparse custom masks should not leave a conspicuous off-key floor.
        floor_=.28*std::min(1.0,count/5.0)*(1-tonalMix_);
        const int tonalNote=std::clamp(int(std::lround(69+12*std::log2(std::max(1.0,tonalHz_)/440))),0,127);
        const double extra=std::max(0.0,(p.color-100)*.01);
        const double snap=p.snap*.01, diveRatio=std::exp2(dive/12);
        const double density=std::min(1.0,std::sqrt(5.0/std::max(1,count)));
        for(int n=0;n<128;++n) {
            auto& v=voices_[n];
            const int to=nearest(n,mask,p.lowHz,p.highHz);
            const bool allowed=v.hz>=p.lowHz&&v.hz<=p.highHz&&v.hz<fs_*.45&&to>=0;
            v.activation=allowed?1.0:0.0;
            if(!allowed) {
                v.gainStep=-v.gain/stride;
                v.poleStep={};v.inputStep=0;
                continue;
            }
            const double single=n==tonalNote?tonalMix_:0;
            const double source=v.hz+single*(tonalHz_-v.hz);
            const double nominal=voices_[to].hz;
            v.pitchClass=to%12;
            const double requestedHz=(source+snap*(nominal-source))*diveRatio;
            // A transposed voice above the synthesis guard must disappear,
            // rather than collect with other voices at one clamped frequency.
            const double guardX=std::clamp((fs_*.44-requestedHz)/(fs_*.04),0.0,1.0);
            const double guardGain=guardX*guardX*(3-2*guardX);
            const double destination=std::clamp(requestedHz,1.0,fs_*.44);
            const auto weights=colorBandWeights(static_cast<float>(nominal),p.colorSplitLowHz,p.colorSplitHighHz);
            const double userAmount=(weights[0]*p.bandColorLow+weights[1]*p.bandColorMid+weights[2]*p.bandColorHigh)*.01;
            const double localExtra=std::clamp(extra*userAmount,0.0,2.0);
            // Decay is a reference at A4, with higher notes decaying faster.
            // Color150 / Decay200 gives Q38 in the additional tuned stage.
            const double q=std::clamp(12+2*localExtra*(38*p.decayMs/200-12),4.0,300.0);
            const double radius=std::exp(-pi*destination/(q*fs_));
            const C pole=std::polar(radius,2*pi*destination/fs_);
            double requestedGain=(n==to?1.25+3.5*localExtra:.65+.7*localExtra)*density;
            requestedGain*=(1-tonalMix_+single)*guardGain;
            // Restore the analysis filter's attenuation only for an isolated
            // measured sinusoid. Broadband material keeps the fixed bank gain.
            if(single>0) {
                const C response=v.sourceInput/(C(1)-v.sourcePole*std::polar(1.0,-2*pi*tonalHz_/fs_));
                requestedGain*=1+single*(std::min(5.0,1/std::max(.01,std::abs(response)))-1);
            }
            const double input=1-radius;
            if(!primed_){v.destinationPole=pole;v.input=input;v.gain=requestedGain;}
            v.poleStep=(pole-v.destinationPole)/double(stride);
            v.inputStep=(input-v.input)/stride;v.gainStep=(requestedGain-v.gain)/stride;
            v.rotationStep=std::polar(1.0,2*pi*(destination-source)/fs_);
        }
        primed_=true;
    }
public:
    void prepare(double fs) noexcept {
        fs_=fs;activationSlew_=1-std::exp(-1/(fs*.015));
        for(int n=0;n<128;++n) {
            auto& v=voices_[n];v.hz=440*std::exp2((n-69)/12.0);
            const double r=std::exp(-pi*v.hz/(38*fs_));
            v.sourcePole=std::polar(r,2*pi*v.hz/fs_);v.sourceInput=1-r;
        }
        reset();
    }
    void reset() noexcept {
        for(auto& v:voices_)v.reset();
        for(auto* branch:{&low_,&high_,&midLow_,&midHigh_})for(auto& f:*branch)f.reset();
        countdown_=normalizationCountdown_=0;primed_=false;tonalWanted_=tonalMix_=0;tonalHz_=440;
    }
    // Supplied by the existing FFT analysis only when one stable main lobe
    // accounts for almost all processed-band energy. Polyphonic/noisy input
    // keeps the neighbourhood bank; no global fundamental is imposed on it.
    void isolatedTone(double hz,bool stable) noexcept {
        tonalWanted_=stable?1:0;
        if(stable&&std::isfinite(hz)&&hz>0)tonalHz_=hz;
    }
    void process(const float* input,float* wet,float* dryMid,float* outer,
                 const Parameters& p,unsigned mask,double dive,
                 const std::array<float,12>& noteGains) noexcept {
        tonalMix_+=activationSlew_*(tonalWanted_-tonalMix_);
        if(std::abs(tonalMix_-tonalWanted_)<1e-7)tonalMix_=tonalWanted_;
        if(countdown_--<=0){configure(p,mask,dive);countdown_=stride-1;}
        double middle[2];
        for(int c=0;c<2;++c) {
            double l=input[c],h=input[c],m=input[c];
            for(auto& f:low_)l=f.tick(l,c);
            for(auto& f:high_)h=f.tick(h,c);
            for(auto& f:midLow_)m=f.tick(m,c);
            for(auto& f:midHigh_)m=f.tick(m,c);
            middle[c]=m;dryMid[c]=static_cast<float>(m);outer[c]=static_cast<float>(l+h);
        }
        double sum[2]={floor_*middle[0],floor_*middle[1]};
        const bool renormalize=normalizationCountdown_++==0;
        if(normalizationCountdown_==1024)normalizationCountdown_=0;
        for(auto& v:voices_) {
            v.destinationPole+=v.poleStep;v.input+=v.inputStep;v.gain+=v.gainStep;
            if(v.activation==0&&std::abs(v.gain)<1e-7) {
                v.gain=0;v.gainStep=0;v.source={};v.destination={};continue;
            }
            for(int c=0;c<2;++c) {
                v.source[c]=v.sourcePole*v.source[c]+v.sourceInput*middle[c];
                v.destination[c]=v.destinationPole*v.destination[c]+v.input*v.source[c]*v.rotation;
                // Weight exact note voices before the short spectral FFT.
                // Binwise masking would suppress a selected low note whenever
                // it happens to fall between coarse analysis-bin centres.
                sum[c]+=2*v.gain*noteGains[v.pitchClass]*v.destination[c].real();
            }
            v.rotation*=v.rotationStep;
            if(renormalize)v.rotation/=std::max(1e-20,std::abs(v.rotation));
        }
        for(int c=0;c<2;++c) {
            if(!mask){wet[c]=dryMid[c]=input[c];outer[c]=0;}
            else wet[c]=static_cast<float>(middle[c]+p.snap*.01*(sum[c]-middle[c]));
            if(!std::isfinite(wet[c]))wet[c]=0;
        }
    }
    double longestTailSeconds(const Parameters& p) const noexcept {
        const double extra=std::max(0.0,(p.color-100)*.01)*2;
        const double q=std::clamp(12+2*extra*(38*p.decayMs/200-12),4.0,300.0);
        // Allow the polynomial envelope of two nearby poles to settle, plus
        // the time spent below the nominal frequency during a negative Dive.
        // This is a conservative host scheduling bound, not a release knob.
        const double lowerDive=p.pitchDiveEnabled&&p.pitchDiveSemitones<0?p.pitchDiveMs*.001:0;
        return 20*std::max(38.0,q)/(pi*std::max(60.0,double(p.lowHz)))+lowerDive+.25;
    }
};
} // namespace iris
