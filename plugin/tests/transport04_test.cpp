// Independent transport adapter review. This reuses only the public host
// scaffolding; every sample goes through a dynamically loaded IAudioProcessor.
#define main iris_host_legacy_main
#include "host_test.cpp"
#undef main

#include <limits>

namespace {
constexpr int kReviewRate=48000;
constexpr double kReviewPi=3.1415926535897932384626433832795;
struct MotionObservation {
    int endSample=0;
    int64 frame=0;
    int pitchClass=-1;
    bool synced=false;
};
struct MotionProbe final : IConnectionPoint {
    STACK_UNKNOWN(IConnectionPoint)
    Session& session;
    MotionObservation last;
    int received=0;
    explicit MotionProbe(Session& s) : session(s) {
        require(s.processorConnection && s.controllerConnection,"Transport test requires the real controller bridge");
        s.processorConnection->disconnect(s.controllerConnection);
        require(s.processorConnection->connect(this)==kResultOk,"Motion probe connect failed");
    }
    ~MotionProbe() {
        session.processorConnection->disconnect(this);
        session.processorConnection->connect(session.controllerConnection);
    }
    tresult PLUGIN_API connect(IConnectionPoint*) override { return kResultOk; }
    tresult PLUGIN_API disconnect(IConnectionPoint*) override { return kResultOk; }
    tresult PLUGIN_API notify(IMessage* message) override {
        if (message && message->getMessageID() && std::strcmp(message->getMessageID(),"Iris.Visual.Frame.v1")==0) {
            auto* attributes=message->getAttributes();
            require(attributes!=nullptr,"Motion frame missing attributes");
            int64 pitch=-99,sync=-99,frame=-1;
            require(attributes->getInt("motionPitchClass",pitch)==kResultOk && pitch>=0 && pitch<=11,
                    "Actual processor motion pitch class is absent or invalid");
            require(attributes->getInt("motionHostSynced",sync)==kResultOk && (sync==0 || sync==1),
                    "Actual processor synchronization status is absent or invalid");
            require(attributes->getInt("frame",frame)==kResultOk && frame>0,"Motion frame counter missing");
            last.frame=frame;last.pitchClass=static_cast<int>(pitch);last.synced=sync!=0;++received;
        }
        return session.controllerConnection->notify(message);
    }
};
struct TimingSettings {
    int samples=kReviewRate*2;
    double tempo=137,quarterStart=-.375;
    bool playing=true,neutralAutomation=false;
    int engineMode=0;
    int division=3;
    int seekSample=-1;
    double seekQuarter=0;
    int losePositionSample=-1;
};
struct MotionRun {
    std::array<std::vector<float>,2> audio;
    std::vector<MotionObservation> observations;
};
Values motionValues(const TimingSettings& timing) {
    auto p=defaults();
    p[kEngineMode]=normalized(kEngineMode,timing.engineMode);
    // Preserve the released Legacy fixture. Prism Snap 0 returns the original
    // mid band, so its voice bank must be engaged to test audible Scale Motion.
    p[kColor]=normalized(kColor,200);p[kSnap]=timing.engineMode==0?0:1;
    p[kMorphEnabled]=0;p[kGateEnabled]=0;
    p[kDecayMs]=normalized(kDecayMs,300);p[kTransient]=0;p[kWidth]=normalized(kWidth,100);
    p[kOutputDb]=normalized(kOutputDb,0);p[kSoftLimit]=0;p[kRoot]=0;p[kScale]=normalized(kScale,4);
    p[kScaleMotionEnabled]=1;p[kScaleMotionDepth]=1;p[kScaleMotionSync]=1;
    p[kScaleMotionDivision]=normalized(kScaleMotionDivision,timing.division);
    return p;
}
MotionRun renderMotion(Session& session,int block,const TimingSettings& timing) {
    session.state(motionValues(timing));session.prepare(kReviewRate,kSample32);
    MotionProbe probe(session);
    auto request=owned<IMessage>(new HostMessage);request->setMessageID("Iris.Visual.Request.v1");
    std::array<std::vector<float>,2> source;
    MotionRun result;
    for (auto& channel:source) channel.resize(timing.samples);
    for (auto& channel:result.audio) channel.resize(timing.samples);
    std::uint32_t seed=0x203918u;
    for (int i=0;i<timing.samples;++i) {
        seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
        const double noise=double(seed)/2147483648.-1.;
        const double t=double(i)/kReviewRate;
        source[0][i]=float(.095*noise+.04*std::sin(2*kReviewPi*329.6275569*t));
        source[1][i]=float(.08*noise+.035*std::sin(2*kReviewPi*493.8833013*t+.31));
    }
    const int parameterSamples[]={17,611,1537,7007,19999,38001,51406,79103};
    for (int pos=0;pos<timing.samples;) {
        int n=std::min(block,timing.samples-pos);
        // Discontinuities are placed exactly at a process-context boundary.
        for (int event:{timing.seekSample,timing.losePositionSample})
            if (event>pos) n=std::min(n,event-pos);
        const bool seek=timing.seekSample>=0 && pos>=timing.seekSample;
        const bool invalid=timing.losePositionSample>=0 && pos>=timing.losePositionSample;
        const double ppqStart=seek?timing.seekQuarter:timing.quarterStart;
        const int origin=seek?timing.seekSample:0;
        ProcessContext context{};
        context.state=ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid;
        if (timing.playing) context.state|=ProcessContext::kPlaying;
        context.sampleRate=kReviewRate;context.tempo=timing.tempo;context.projectTimeSamples=pos;
        context.projectTimeMusic=ppqStart+(timing.playing?double(pos-origin)*timing.tempo/(60*kReviewRate):0.);
        if (invalid) context.projectTimeMusic=std::numeric_limits<double>::quiet_NaN();
        Changes changes;
        if (timing.neutralAutomation) for (int when:parameterSamples)
            if (when>=pos && when<pos+n) changes.add(kScaleMotionDepth,when-pos,1.);
        float* inputs[]{source[0].data()+pos,source[1].data()+pos};
        float* outputs[]{result.audio[0].data()+pos,result.audio[1].data()+pos};
        AudioBusBuffers in{},out{};in.numChannels=out.numChannels=2;
        in.channelBuffers32=inputs;out.channelBuffers32=outputs;
        ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample32;data.numSamples=n;
        data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;
        data.processContext=&context;data.inputParameterChanges=&changes;
        require(session.processor->process(data)==kResultOk,"Actual wrapper transport processing failed");
        const int previous=probe.received;
        require(session.processorConnection->notify(request)==kResultOk,"Actual wrapper motion request failed");
        if (probe.received!=previous) {
            auto observed=probe.last;observed.endSample=pos+n;result.observations.push_back(observed);
        }
        pos+=n;
    }
    require(!result.observations.empty(),"No live motion observations from the dynamic plugin");
    double energy=0;
    for (const auto& channel:result.audio) for (float sample:channel) {
        require(std::isfinite(sample),"Nonfinite transport output");energy+=double(sample)*sample;
    }
    require(energy>1,"Native timing fixture produced no useful audible signal");
    return result;
}
double motionError(const MotionRun& a,const MotionRun& b) {
    double error=0;
    for (unsigned c=0;c<2;++c) {
        require(a.audio[c].size()==b.audio[c].size(),"Transport comparison lengths differ");
        for (std::size_t i=0;i<a.audio[c].size();++i)
            error=std::max(error,std::abs(double(a.audio[c][i])-b.audio[c][i]));
    }
    return error;
}
double motionDifferenceRms(const MotionRun& a,const MotionRun& b) {
    double changed=0,reference=0;
    for (unsigned c=0;c<2;++c) for (std::size_t i=4096;i<a.audio[c].size();++i) {
        const double delta=double(a.audio[c][i])-b.audio[c][i];changed+=delta*delta;
        reference+=double(a.audio[c][i])*a.audio[c][i];
    }
    return std::sqrt(changed/std::max(reference,1e-20));
}
void checkTimingDeclaration(Session& s) {
    IProcessContextRequirements* wanted=nullptr;
    require(s.component->queryInterface(IProcessContextRequirements::iid,reinterpret_cast<void**>(&wanted))==kResultOk,
            "Native plugin does not advertise process context requirements");
    const uint32 flags=wanted->getProcessContextRequirements();wanted->release();
    const uint32 need=IProcessContextRequirements::kNeedProjectTimeMusic |
        IProcessContextRequirements::kNeedTempo | IProcessContextRequirements::kNeedTransportState;
    require((flags&need)==need,"Native plugin did not request PPQ, tempo, and play state");
    pass("real VST3 component advertises PPQ, tempo, and transport-state requirements");
}
void checkNativeSpans(Session& session,int engineMode) {
    TimingSettings timing;timing.engineMode=engineMode;
    const auto large=renderMotion(session,1017,timing),shortBlocks=renderMotion(session,64,timing),
               scratchChunks=renderMotion(session,4097,timing);
    auto segmentedTiming=timing;segmentedTiming.neutralAutomation=true;
    const auto segmented=renderMotion(session,1017,segmentedTiming);
    const double smallError=motionError(large,shortBlocks),scratchError=motionError(large,scratchChunks),
                 parameterError=motionError(large,segmented);
    std::cout<<"      native sync maxerr: 64="<<smallError<<", 4097="<<scratchError
             <<", same-value automation="<<parameterError<<'\n';
    require(smallError<2e-6,"Native PPQ phase depends on 1017 vs 64 callback size");
    pass("valid 137 BPM / negative-start PPQ is equivalent across 1017 and 64-sample callbacks");
    require(scratchError<2e-6,"Native PPQ phase resets across internal 1024-sample scratch chunks");
    pass("oversized 4097-sample callback stress correctly offsets every internal 1024-sample chunk");
    require(parameterError<2e-6,"Same-value sample-offset automation altered native rhythmic phase");
    pass("eight neutral Depth automation points split processing without changing its rhythmic audio");
}
void checkStoppedSeekFallback(Session& session,int engineMode) {
    TimingSettings stopped;stopped.engineMode=engineMode;stopped.playing=false;stopped.quarterStart=10.25;
    const auto fixed=renderMotion(session,512,stopped);
    std::set<int> pitches;
    for (const auto& observation:fixed.observations) {
        require(observation.synced,"Stopped valid host PPQ incorrectly reports fallback");
        pitches.insert(observation.pitchClass);
    }
    require(pitches.size()==1,"Valid stopped host clock continues stepping");
    pass("stopped valid host transport holds one selected pitch class and keeps synchronization status");
    auto seeking=stopped;seeking.seekSample=kReviewRate;seeking.seekQuarter=stopped.quarterStart+.5;
    const auto seek=renderMotion(session,512,seeking);
    std::set<int> before,after;
    for (const auto& observation:seek.observations) {
        require(observation.synced,"A valid host seek lost synchronization");
        if (observation.endSample<=seeking.seekSample) before.insert(observation.pitchClass);
        else after.insert(observation.pitchClass);
    }
    require(before.size()==1 && after.size()==1 && *after.begin()==(*before.begin()+1)%12,
            "Half-quarter host seek did not reanchor the eighth-note step to the next pitch class");
    pass("valid seek reanchors the native step immediately, including while stopped");
    TimingSettings loss;loss.engineMode=engineMode;loss.losePositionSample=kReviewRate;
    const auto fallback=renderMotion(session,512,loss);
    bool sawValid=false,sawFallback=false;
    for (const auto& observation:fallback.observations) {
        if (observation.endSample<=loss.losePositionSample) { require(observation.synced,"Valid span mislabeled");sawValid=true; }
        else { require(!observation.synced,"Nonfinite host PPQ still labeled synchronized");sawFallback=true; }
    }
    require(sawValid && sawFallback,"Position-loss fixture did not exercise both timing sources");
    pass("nonfinite host position safely switches real telemetry to internal timing fallback");
}
void checkUnusualTempi(Session& session,int engineMode) {
    TimingSettings slow;slow.engineMode=engineMode;slow.tempo=15;slow.division=5;slow.quarterStart=.3125;
    auto fast=slow;fast.tempo=500;
    const auto low=renderMotion(session,1017,slow),high=renderMotion(session,1017,fast);
    // Periodic-Hann spectral analysis has a centre 2048+1024/2 samples
    // before the frame's one-past-end sample. This predicts the note from the
    // musical timeline, independently of callback and wrapper span boundaries.
    const auto verify=[&](const MotionRun& run,const TimingSettings& setting) {
        int checked=0;
        for (const auto& observation:run.observations) {
            require(observation.synced,"15/500 BPM was rejected or silently reported as fallback");
            const double beat=setting.quarterStart+(double(observation.frame)*256.-2560.)*setting.tempo/(60.*kReviewRate);
            const double phase=beat/.125;
            if (std::abs(phase-std::round(phase))<1e-7) continue;
            const int expected=(static_cast<int>(std::floor(phase))%12+12)%12;
            require(observation.pitchClass==expected,"15/500 BPM native melody used the wrong PPQ or fallback tempo");
            ++checked;
        }
        require(checked>30,"Too few actual musical-step observations");
    };
    verify(low,slow);verify(high,fast);
    const double delta=motionDifferenceRms(low,high);
    std::cout<<"      native 15 vs 500 BPM relative audio difference="<<delta<<'\n';
    require(delta>.005,"Distinct tempos produced no useful rhythmic audio difference");
    pass("15 and 500 BPM drive the predicted actual pitch-class sequence and distinct processed audio");
}
}

int main(int argc,char** argv) {
    try {
        require(argc==2,"Usage: transport04_test <path-to-real-module-binary>");
        Module module(argv[1]);Host host;
        for (int engineMode:{0,1}) {
            std::cout<<"Core: "<<kEngineModeNames[engineMode]<<'\n';
            Session session(module,host);
            checkTimingDeclaration(session);checkNativeSpans(session,engineMode);
            checkStoppedSeekFallback(session,engineMode);checkUnusualTempi(session,engineMode);
        }
        require(host.references==1,"Transport test leaked host references");
        std::cout<<"Independent native transport review: 8 groups per core / 16 groups passed, 0 failures.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAIL  "<<error.what()<<'\n';return 1;
    }
}
