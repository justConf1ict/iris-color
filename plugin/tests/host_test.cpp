// Dynamic VST3 host regression checks. This loads the built module through its
// exported factory; it does not bypass the VST3 adapter to test the DSP directly.
#include "plugin/IrisParameters.h"
#include "src/IrisPresets.h"
#include "fixtures/state04_literal.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace iris::vst3;

namespace {
const char* capturePath=nullptr;
const char* visualAudioPath=nullptr;
#if defined(_WIN32)
void CALLBACK chooseLastPreset(HWND window,UINT,UINT_PTR timer,DWORD) {
    KillTimer(window,timer);
    INPUT keys[4]{};
    for(auto& key:keys)key.type=INPUT_KEYBOARD;
    keys[0].ki.wVk=VK_END;keys[1].ki.wVk=VK_END;keys[1].ki.dwFlags=KEYEVENTF_KEYUP;
    keys[2].ki.wVk=VK_RETURN;keys[3].ki.wVk=VK_RETURN;keys[3].ki.dwFlags=KEYEVENTF_KEYUP;
    SendInput(4,keys,sizeof(INPUT));
}
void CALLBACK chooseFirstCore(HWND window,UINT,UINT_PTR timer,DWORD) {
    KillTimer(window,timer);
    INPUT keys[4]{};for(auto& key:keys)key.type=INPUT_KEYBOARD;
    keys[0].ki.wVk=VK_HOME;keys[1].ki.wVk=VK_HOME;keys[1].ki.dwFlags=KEYEVENTF_KEYUP;
    keys[2].ki.wVk=VK_RETURN;keys[3].ki.wVk=VK_RETURN;keys[3].ki.dwFlags=KEYEVENTF_KEYUP;
    SendInput(4,keys,sizeof(INPUT));
}
void CALLBACK cancelStuckMenu(HWND window,UINT,UINT_PTR timer,DWORD) {
    KillTimer(window,timer); EndMenu();
}
#endif
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
void pass(const char* name) { std::cout << "PASS  " << name << '\n'; }

#define STACK_UNKNOWN(Interface) \
    uint32 references = 1; \
    uint32 PLUGIN_API addRef() override { return ++references; } \
    uint32 PLUGIN_API release() override { return references ? --references : 0; } \
    tresult PLUGIN_API queryInterface(const TUID id, void** object) override { \
        if (FUnknownPrivate::iidEqual(id, Interface::iid) || FUnknownPrivate::iidEqual(id, FUnknown::iid)) { \
            *object = static_cast<Interface*>(this); addRef(); return kResultOk; \
        } *object = nullptr; return kNoInterface; \
    }

struct MemoryStream final : IBStream {
    STACK_UNKNOWN(IBStream)
    std::vector<std::uint8_t> bytes;
    std::size_t position = 0;
    void rewind() { position = 0; }
    tresult PLUGIN_API read(void* buffer, int32 count, int32* readCount = nullptr) override {
        if (count < 0 || (!buffer && count)) return kInvalidArgument;
        const auto n = std::min<std::size_t>(count, position <= bytes.size() ? bytes.size()-position : 0);
        if (n) std::memcpy(buffer, bytes.data()+position, n);
        position += n; if (readCount) *readCount = static_cast<int32>(n);
        return n == static_cast<std::size_t>(count) ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API write(void* buffer, int32 count, int32* written = nullptr) override {
        if (count < 0 || (!buffer && count)) return kInvalidArgument;
        if (position+count > bytes.size()) bytes.resize(position+count);
        if (count) std::memcpy(bytes.data()+position, buffer, count);
        position += count; if (written) *written = count; return kResultOk;
    }
    tresult PLUGIN_API seek(int64 offset, int32 mode, int64* result = nullptr) override {
        const int64 base = mode == kIBSeekSet ? 0 : mode == kIBSeekCur ? static_cast<int64>(position) :
            mode == kIBSeekEnd ? static_cast<int64>(bytes.size()) : -1;
        if (base < 0 || base+offset < 0) return kInvalidArgument;
        position = static_cast<std::size_t>(base+offset); if (result) *result=position; return kResultOk;
    }
    tresult PLUGIN_API tell(int64* result) override {
        if (!result) return kInvalidArgument; *result=position; return kResultOk;
    }
};

struct Host final : IHostApplication, IComponentHandler {
    uint32 references = 1;
    int beginCount=0, performCount=0, endCount=0;
    uint32 PLUGIN_API addRef() override { return ++references; }
    uint32 PLUGIN_API release() override { return references ? --references : 0; }
    tresult PLUGIN_API queryInterface(const TUID id, void** object) override {
        if (FUnknownPrivate::iidEqual(id,IHostApplication::iid) || FUnknownPrivate::iidEqual(id,FUnknown::iid))
            *object=static_cast<IHostApplication*>(this);
        else if (FUnknownPrivate::iidEqual(id,IComponentHandler::iid)) *object=static_cast<IComponentHandler*>(this);
        else { *object=nullptr; return kNoInterface; }
        addRef(); return kResultOk;
    }
    tresult PLUGIN_API getName(String128 name) override { UString(name,128).fromAscii("Iris regression host"); return kResultOk; }
    tresult PLUGIN_API createInstance(TUID cid, TUID iid, void** object) override {
        HostApplication allocator;
        return allocator.createInstance(cid, iid, object);
    }
    tresult PLUGIN_API beginEdit(ParamID) override { ++beginCount; return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID, ParamValue) override { ++performCount; return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID) override { ++endCount; return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
};

struct Queue final : IParamValueQueue {
    STACK_UNKNOWN(IParamValueQueue)
    explicit Queue(ParamID parameter) : id(parameter) {}
    ParamID id;
    std::vector<std::pair<int32,ParamValue>> points;
    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return static_cast<int32>(points.size()); }
    tresult PLUGIN_API getPoint(int32 index,int32& offset,ParamValue& value) override {
        if(index<0 || index>=getPointCount()) return kInvalidArgument;
        offset=points[index].first; value=points[index].second; return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32 offset,ParamValue value,int32& index) override {
        index=getPointCount(); points.emplace_back(offset,value); return kResultOk;
    }
};
struct Changes final : IParameterChanges {
    STACK_UNKNOWN(IParameterChanges)
    std::vector<std::unique_ptr<Queue>> queues;
    int32 PLUGIN_API getParameterCount() override { return static_cast<int32>(queues.size()); }
    IParamValueQueue* PLUGIN_API getParameterData(int32 index) override {
        return index<0 || index>=getParameterCount() ? nullptr : queues[index].get();
    }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id,int32& index) override {
        for(int32 i=0;i<getParameterCount();++i) if(queues[i]->id==id) { index=i; return queues[i].get(); }
        index=getParameterCount(); queues.emplace_back(std::make_unique<Queue>(id)); return queues.back().get();
    }
    void add(ParamID id,int offset,double value) { int32 index=0; addParameterData(id,index)->addPoint(offset,value,index); }
};
struct Events final : IEventList {
    STACK_UNKNOWN(IEventList)
    std::vector<Event> events;
    int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events.size()); }
    tresult PLUGIN_API getEvent(int32 index,Event& e) override {
        if(index<0 || index>=getEventCount()) return kInvalidArgument; e=events[index]; return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event& e) override { events.push_back(e); return kResultOk; }
};

class Module {
public:
    explicit Module(const char* path) {
#if defined(_WIN32)
        handle=LoadLibraryA(path);
        require(handle!=nullptr,"LoadLibrary failed: Windows error "+std::to_string(GetLastError()));
        auto enter=reinterpret_cast<bool(*)()>(symbol("InitDll"));
        require(enter && enter(),"InitDll failed");
#else
        handle=dlopen(path,RTLD_NOW|RTLD_LOCAL);
        const char* error=handle?nullptr:dlerror();
        require(handle!=nullptr,std::string("dlopen failed: ")+(error?error:"unknown"));
        auto enter=reinterpret_cast<bool(*)(void*)>(symbol("ModuleEntry"));
        require(enter && enter(handle),"ModuleEntry failed");
#endif
    }
    ~Module() {
        if (!handle) return;
#if defined(_WIN32)
        auto leave=reinterpret_cast<bool(*)()>(symbol("ExitDll")); if(leave) leave(); FreeLibrary(handle);
#else
        auto leave=reinterpret_cast<bool(*)()>(symbol("ModuleExit")); if(leave) leave(); dlclose(handle);
#endif
    }
    void* symbol(const char* name) {
#if defined(_WIN32)
        return reinterpret_cast<void*>(GetProcAddress(handle,name));
#else
        return dlsym(handle,name);
#endif
    }
private:
#if defined(_WIN32)
    HMODULE handle=nullptr;
#else
    void* handle=nullptr;
#endif
};

class Session {
public:
    Host& host;
    IPluginFactory* factory=nullptr;
    IComponent* component=nullptr;
    IAudioProcessor* processor=nullptr;
    IEditController* controller=nullptr;
    IConnectionPoint* processorConnection=nullptr;
    IConnectionPoint* controllerConnection=nullptr;
    Session(Module& module,Host& h) : host(h) {
        auto getFactory=reinterpret_cast<IPluginFactory*(PLUGIN_API*)()>(module.symbol("GetPluginFactory"));
        require(getFactory!=nullptr,"GetPluginFactory export missing"); factory=getFactory();
        require(factory && factory->countClasses()==2,"Expected processor and controller factory classes");
        PFactoryInfo factoryInfo{}; require(factory->getFactoryInfo(&factoryInfo)==kResultOk,"Factory info failed");
        require(std::strcmp(factoryInfo.vendor,"Independent Audio Lab")==0,"Unexpected vendor");
        PClassInfo processorInfo{};
        for(int i=0;i<factory->countClasses();++i) {
            PClassInfo info{}; require(factory->getClassInfo(i,&info)==kResultOk,"Class info failed");
            if(std::strcmp(info.category,kVstAudioEffectClass)==0) processorInfo=info;
        }
        require(std::strcmp(processorInfo.name,"Iris Color")==0,"Expected Iris Color processor");
        require(factory->createInstance(processorInfo.cid,IComponent::iid,reinterpret_cast<void**>(&component))==kResultOk,"Component creation failed");
        require(component->initialize(static_cast<IHostApplication*>(&host))==kResultOk,"Component initialize failed");
        require(component->queryInterface(IAudioProcessor::iid,reinterpret_cast<void**>(&processor))==kResultOk,"IAudioProcessor unavailable");
        TUID controllerID{}; require(component->getControllerClassId(controllerID)==kResultOk,"Controller ID missing");
        require(factory->createInstance(controllerID,IEditController::iid,reinterpret_cast<void**>(&controller))==kResultOk,"Controller creation failed");
        require(controller->initialize(static_cast<IHostApplication*>(&host))==kResultOk,"Controller initialize failed");
        require(controller->setComponentHandler(&host)==kResultOk,"Component handler failed");
        component->queryInterface(IConnectionPoint::iid,reinterpret_cast<void**>(&processorConnection));
        controller->queryInterface(IConnectionPoint::iid,reinterpret_cast<void**>(&controllerConnection));
        if(processorConnection && controllerConnection) {
            require(processorConnection->connect(controllerConnection)==kResultOk,"Processor connect failed");
            require(controllerConnection->connect(processorConnection)==kResultOk,"Controller connect failed");
        }
    }
    ~Session() {
        if(processor) processor->setProcessing(false);
        if(component) component->setActive(false);
        if(processorConnection && controllerConnection) {
            processorConnection->disconnect(controllerConnection); controllerConnection->disconnect(processorConnection);
        }
        if(processorConnection) processorConnection->release();
        if(controllerConnection) controllerConnection->release();
        if(controller) { controller->setComponentHandler(nullptr); controller->terminate(); controller->release(); }
        if(component) component->terminate();
        if(processor) processor->release();
        if(component) component->release();
        if(factory) factory->release();
    }
    void state(const Values& values) {
        MemoryStream stream; require(writeState(&stream,values),"State encoder failed"); stream.rewind();
        require(component->setState(&stream)==kResultOk,"Component setState failed");
        stream.rewind(); require(controller->setComponentState(&stream)==kResultOk,"Controller setComponentState failed");
    }
    Values state() {
        MemoryStream stream; require(component->getState(&stream)==kResultOk,"Component getState failed"); stream.rewind();
        Values result{}; require(readState(&stream,result),"Component emitted invalid state"); return result;
    }
    void prepare(double rate,int sampleSize,int channels=2) {
        processor->setProcessing(false); component->setActive(false);
        SpeakerArrangement arrangement=channels==1?SpeakerArr::kMono:SpeakerArr::kStereo;
        require(processor->setBusArrangements(&arrangement,1,&arrangement,1)==kResultOk,"Bus arrangement rejected");
        ProcessSetup setup{}; setup.processMode=kRealtime; setup.symbolicSampleSize=sampleSize;
        setup.maxSamplesPerBlock=1024; setup.sampleRate=rate;
        require(processor->setupProcessing(setup)==kResultOk,"setupProcessing failed");
        require(component->activateBus(kAudio,kInput,0,true)==kResultOk,"Input activation failed");
        require(component->activateBus(kAudio,kOutput,0,true)==kResultOk,"Output activation failed");
        require(component->activateBus(kEvent,kInput,0,true)==kResultOk,"MIDI bus activation failed");
        require(component->setActive(true)==kResultOk,"setActive failed");
        require(processor->setProcessing(true)==kResultOk,"setProcessing failed");
    }
};

void checkMetadata(Session& s) {
    require(s.component->getBusCount(kAudio,kInput)==1 && s.component->getBusCount(kAudio,kOutput)==1,"Audio buses wrong");
    require(s.component->getBusCount(kEvent,kInput)==1,"MIDI bus missing");
    require(s.controller->getParameterCount()==83,"Expected 81 sound controls and 2 published MIDI panic controls");
    require(std::abs(s.controller->getParamNormalized(kClarity)-.35)<1e-12 &&
            std::abs(s.state()[kClarity]-.35)<1e-12,
            "New component and controller must both start with Clarity 35");
    require(s.controller->getParamNormalized(kEngineMode)==1 && s.state()[kEngineMode]==1,
            "New component and controller must both start with Prism");
    std::set<ParamID> ids;
    for(int i=0;i<s.controller->getParameterCount();++i) {
        ParameterInfo info{}; require(s.controller->getParameterInfo(i,info)==kResultOk,"Parameter info failed");
        require(ids.insert(info.id).second,"Duplicate parameter ID");
        if(info.id==kBypass) require((info.flags&ParameterInfo::kIsBypass)!=0,"Host bypass flag missing");
        if(info.id==29 || info.id==30)
            require((info.flags&ParameterInfo::kIsHidden)!=0,"Published MIDI panic ID was reused as a sound control");
        if(isSoundParameter(info.id)) {
            String128 label{};
            require(s.controller->getParamStringByValue(info.id,info.defaultNormalizedValue,label)==kResultOk,"Parameter formatting failed");
            require(std::abs(s.controller->normalizedParamToPlain(info.id,info.defaultNormalizedValue)-kSpecs[info.id].initial)<1e-6,
                "Parameter default mapping mismatch at ID "+std::to_string(info.id)+": got "+
                std::to_string(s.controller->normalizedParamToPlain(info.id,info.defaultNormalizedValue))+" expected "+
                std::to_string(kSpecs[info.id].initial));
        }
    }
    for(ParamID id : {static_cast<ParamID>(kRoot),static_cast<ParamID>(kScale),static_cast<ParamID>(kScaleMotionDivision),static_cast<ParamID>(kEngineMode),static_cast<ParamID>(kBodyMaterial)})
        for(int step=0;step<=1000;++step) {
            const double normalizedValue=step/1000.0;
            require(std::abs(s.controller->normalizedParamToPlain(id,normalizedValue)-plain(id,normalizedValue))<1e-9,
                "Discrete automation must select the same note/scale as the host's displayed value");
        }
    IMidiMapping* mapping=nullptr;
    require(s.controller->queryInterface(IMidiMapping::iid,reinterpret_cast<void**>(&mapping))==kResultOk,"MIDI mapping interface missing");
    ParamID id=999;
    require(mapping->getMidiControllerAssignment(0,15,kCtrlAllNotesOff,id)==kResultOk && id==29,"CC123 must retain published ID 29");
    require(mapping->getMidiControllerAssignment(0,0,kCtrlAllSoundsOff,id)==kResultOk && id==30,"CC120 must retain published ID 30");
    mapping->release();
    require(s.processor->canProcessSampleSize(kSample32)==kResultOk && s.processor->canProcessSampleSize(kSample64)==kResultOk,"Sample format support missing");
    IProcessContextRequirements* requirements=nullptr;
    require(s.processor->queryInterface(IProcessContextRequirements::iid,reinterpret_cast<void**>(&requirements))==kResultOk,
        "Processor does not advertise the transport data it needs");
    const auto flags=requirements->getProcessContextRequirements(); requirements->release();
    const auto needed=IProcessContextRequirements::kNeedTempo | IProcessContextRequirements::kNeedProjectTimeMusic |
        IProcessContextRequirements::kNeedTransportState;
    require((flags&needed)==needed,"Tempo, PPQ, or transport-state requirement missing");
    require(s.controller->getParamNormalized(kBodyMix)==0,"Body must be opt-in for new plugin instances");
    pass("factory, class IDs, 81 sound + 2 hidden parameters, Core/Clarity/Body defaults, buses, bypass, panic IDs 29/30, tempo/PPQ requirements");
}

void checkState(Session& s) {
    Values desired=defaults();
    for(unsigned id=0;id<kParameterCount;++id) desired[id]=sanitize(id,((id*37+19)%101)/100.0);
    s.state(desired); require(s.state()==desired,"State round trip changed normalized values");
    for(unsigned id=0;id<kParameterCount;++id)
        require(std::abs(s.controller->getParamNormalized(id)-desired[id])<1e-12,"Controller state did not follow component");
    MemoryStream truncated; require(s.component->getState(&truncated)==kResultOk,"State save failed");
    truncated.bytes.resize(truncated.bytes.size()-7); truncated.rewind();
    require(s.component->setState(&truncated)!=kResultOk,"Truncated state accepted");
    require(s.state()==desired,"Rejected state changed live parameters");
    MemoryStream old;old.bytes.assign(iris05_fixture::state04.begin(),iris05_fixture::state04.end());
    require(s.component->setState(&old)==kResultOk,"Component rejected frozen 0.4 state");old.rewind();
    require(s.controller->setComponentState(&old)==kResultOk,"Controller rejected frozen 0.4 state");
    const auto legacy=s.state();
    require(legacy[kEngineMode]==0 && s.controller->getParamNormalized(kEngineMode)==0,
            "Old component/controller state did not select Legacy");
    for(unsigned id=0;id<75;++id)
        require(legacy[id]==iris05_fixture::normalized04[id],"Old state parameter changed on real component restore");
    s.state(desired);
    pass("all controls save/restore and synchronize; truncated state rejects without partial mutation");
}

void checkSampleRateBounds(Session& s) {
    s.processor->setProcessing(false); s.component->setActive(false);
    ProcessSetup setup{}; setup.processMode=kRealtime; setup.symbolicSampleSize=kSample32;
    setup.maxSamplesPerBlock=1024; setup.sampleRate=384000;
    require(s.processor->setupProcessing(setup)!=kResultOk,"384 kHz must be rejected because the core supports at most 192 kHz");
    setup.sampleRate=192000;
    require(s.processor->setupProcessing(setup)==kResultOk,"192 kHz must be accepted");
    pass("sample-rate contract accepts 192 kHz and rejects unsupported 384 kHz");
}

template<typename Sample>
void checkBypass(Session& s,int sampleSize,int channels,double rate,bool inPlace) {
    auto values=defaults(); values[kBypass]=1.; s.state(values); s.prepare(rate,sampleSize,channels);
    const int latency=static_cast<int>(s.processor->getLatencySamples());
    require(latency==3072,"Expected 3072 samples reported latency, got "+std::to_string(latency));
    require(s.processor->getTailSamples()>=static_cast<uint32>(latency),"Tail smaller than pipeline latency");
    constexpr int total=10000;
    std::array<std::vector<Sample>,2> input,output;
    for(auto& v:input)v.resize(total);
    for(auto& v:output)v.resize(total);
    input[0][0]=Sample(.25); input[1][0]=Sample(-.375);
    const int sizes[]={1,7,64,127,255,513,1024}; int index=0;
    for(int pos=0;pos<total;) {
        const int n=std::min(sizes[index++%7],total-pos);
        Sample* ins[2]={input[0].data()+pos,input[1].data()+pos};
        Sample* outs[2]={inPlace?ins[0]:output[0].data()+pos,inPlace?ins[1]:output[1].data()+pos};
        AudioBusBuffers in{},out{}; in.numChannels=out.numChannels=channels;
        if constexpr(std::is_same_v<Sample,float>) { in.channelBuffers32=ins; out.channelBuffers32=outs; }
        else { in.channelBuffers64=ins; out.channelBuffers64=outs; }
        ProcessData data{}; data.processMode=kRealtime; data.symbolicSampleSize=sampleSize; data.numSamples=n;
        data.numInputs=data.numOutputs=1; data.inputs=&in; data.outputs=&out;
        require(s.processor->process(data)==kResultOk,"Bypass processing failed");
        pos+=n;
    }
    double error=0;
    for(int ch=0;ch<channels;++ch) for(int i=0;i<total;++i) {
        const double expected=i==latency?(ch==0?.25:-.375):0.;
        const double actual=inPlace?input[ch][i]:output[ch][i];
        require(std::isfinite(actual),"Non-finite bypass output"); error=std::max(error,std::abs(actual-expected));
    }
    require(error<2e-6,"Latency-aligned unity bypass failed, maximum error "+std::to_string(error));
}

struct ParameterAt { int sample; ParamID id; double value; };
struct NoteAt { int sample; bool on; int note; };
std::array<std::vector<float>,2> runSequence(Session& s,int block,bool automated) {
    auto values=defaults(); values[kMidiEnabled]=1.; values[kOutputDb]=normalized(kOutputDb,-9); s.state(values);
    s.prepare(48000,kSample32);
    const std::vector<ParameterAt> parameters{
        {17,kColor,.32},{611,kMorph,.8},{1537,kColor,.96},{2173,kWidth,.72},
        {3300,kOutputDb,normalized(kOutputDb,-3)},{3400,31,.8},{3551,32,.3},{4012,33,.95},
        {4503,34,normalized(kColorLowMidHz,1200)},{4631,35,normalized(kColorMidHighHz,2600)},
        {4780,36,normalized(kColorEqLowDb,3)},{4991,37,normalized(kColorEqMidDb,-4)},
        {5102,38,normalized(kColorEqHighDb,5)},{5999,29,0},{6101,kGateDb,normalized(kGateDb,-42)},{7007,30,0},
        {1901,kAttackTailEnabled,1},{1902,kAttackColor,normalized(kAttackColor,12)},
        {1917,kTailColor,normalized(kTailColor,170)},{1997,kColorTransitionMs,normalized(kColorTransitionMs,65)},
        {2201,kPitchDiveEnabled,1},{2403,kPitchDiveSemitones,normalized(kPitchDiveSemitones,-17)},
        {2461,kPitchDiveMs,normalized(kPitchDiveMs,180)},
        {2701,kScaleMotionEnabled,1},{2702,kScaleMotionDepth,normalized(kScaleMotionDepth,95)},
        {2751,kScaleMotionDivision,normalized(kScaleMotionDivision,4)},
        {2803,kFormantEnabled,1},{2807,kFormantMix,normalized(kFormantMix,72)},
        {2831,kFormantVowel,normalized(kFormantVowel,2.25)},
        {2869,kFormantMotion,normalized(kFormantMotion,70)},
        {2887,kFormantRateHz,normalized(kFormantRateHz,3)},
        {2901,kPeakTamerEnabled,1},{2902,kPeakTamerAmount,normalized(kPeakTamerAmount,90)},
        {2903,kPeakTamerMaxDb,normalized(kPeakTamerMaxDb,11)},
        {3031,kWetEqBand0,normalized(kWetEqBand0,-12)},
        {3073,kWetEqBand0+7,normalized(kWetEqBand0+7,6)},
        {3113,kWetEqBand15,normalized(kWetEqBand15,-18)},
        {9203,kScaleMotionSync,0},{9239,kScaleMotionRateHz,normalized(kScaleMotionRateHz,9)},
        {11401,kScaleMotionSync,1},
        {549,kEngineMode,0},{10037,kEngineMode,1},
        {1201,kBodyMix,normalized(kBodyMix,42)},
        {1283,kBodyMaterial,normalized(kBodyMaterial,0)},
        {1311,kBodyDecayMs,normalized(kBodyDecayMs,310)},
        {1327,kBodyDamping,normalized(kBodyDamping,28)},
        {1349,kBodyInharmonicity,normalized(kBodyInharmonicity,54)},
        {1379,kBodyStrike,normalized(kBodyStrike,82)},
        {5203,kBodyMaterial,normalized(kBodyMaterial,2)},
        {5271,kBodyDecayMs,normalized(kBodyDecayMs,1800)},
        {7307,kBodyMaterial,normalized(kBodyMaterial,3)},
        {7621,kBodyInharmonicity,normalized(kBodyInharmonicity,3)},
        {12013,kBodyMix,0}
    };
    const std::array<NoteAt,7> notes{{{0,true,36},{75,true,43},{700,false,36},
        {1537,true,39},{3300,false,43},{6333,true,48},{7001,false,48}}};
    constexpr int total=18000;
    std::array<std::vector<float>,2> result; for(auto& v:result)v.resize(total);
    for(int pos=0;pos<total;pos+=block) {
        const int n=std::min(block,total-pos);
        std::vector<float> left(n),right(n);
        for(int i=0;i<n;++i) {
            const double t=(pos+i)/48000.0;
            const float source=pos+i<9000?static_cast<float>(.12*std::sin(2*3.141592653589793*147*t)+.08*std::sin(2*3.141592653589793*1103*t)):0.f;
            left[i]=source; right[i]=source*.8f;
        }
        float* ins[2]={left.data(),right.data()}; float* outs[2]={result[0].data()+pos,result[1].data()+pos};
        AudioBusBuffers in{},out{}; in.numChannels=out.numChannels=2; in.channelBuffers32=ins; out.channelBuffers32=outs;
        if(pos>=9000) in.silenceFlags=3;
        Changes changes; Events events;
        if(automated) {
            for(const auto& p:parameters) if(p.sample>=pos && p.sample<pos+n) changes.add(p.id,p.sample-pos,p.value);
            for(const auto& note:notes) if(note.sample>=pos && note.sample<pos+n) {
                Event event{}; event.busIndex=0; event.sampleOffset=note.sample-pos;
                event.type=note.on?Event::kNoteOnEvent:Event::kNoteOffEvent;
                if(note.on) { event.noteOn.pitch=static_cast<int16>(note.note); event.noteOn.velocity=.8f; event.noteOn.noteId=note.note; }
                else { event.noteOff.pitch=static_cast<int16>(note.note); event.noteOff.noteId=note.note; }
                events.addEvent(event);
            }
        }
        ProcessData data{}; data.processMode=kRealtime; data.symbolicSampleSize=kSample32; data.numSamples=n;
        data.numInputs=data.numOutputs=1; data.inputs=&in; data.outputs=&out;
        data.inputParameterChanges=&changes; data.inputEvents=&events;
        ProcessContext context{};
        context.state=ProcessContext::kPlaying | ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid;
        context.sampleRate=48000; context.tempo=173;
        context.projectTimeMusic=2.375+pos*context.tempo/(60.0*48000);
        data.processContext=&context;
        require(s.processor->process(data)==kResultOk,"Automation sequence process failed");
    }
    return result;
}

void checkAutomation(Session& s) {
    const auto full=runSequence(s,1017,true);
    const auto split=runSequence(s,64,true);
    const auto oversized=runSequence(s,4096,true);
    const auto noAutomation=runSequence(s,1017,false);
    double maxError=0, energy=0, difference=0, tailEnergy=0;
    for(unsigned ch=0;ch<2;++ch) for(unsigned i=0;i<full[ch].size();++i) {
        require(std::isfinite(full[ch][i]) && std::isfinite(split[ch][i]),"Non-finite processed output");
        maxError=std::max(maxError,std::abs(static_cast<double>(full[ch][i]-split[ch][i])));
        maxError=std::max(maxError,std::abs(static_cast<double>(full[ch][i]-oversized[ch][i])));
        energy+=std::abs(full[ch][i]); difference+=std::abs(full[ch][i]-noAutomation[ch][i]);
        if(i>=9000) tailEnergy+=std::abs(full[ch][i]);
    }
    require(maxError<2e-5,"Sample-offset events/automation depend on host block boundaries: "+std::to_string(maxError));
    require(energy>.1 && difference>.01,"Processed audio or event/automation response is missing");
    require(tailEnergy>.01,"Silence flags incorrectly suppressed the pipeline tail");
    Changes changes; changes.add(kColor,0,.415); changes.add(kScale,0,1.0); changes.add(kAllSoundsOff,0,0);
    ProcessData flush{}; flush.symbolicSampleSize=kSample32; flush.inputParameterChanges=&changes;
    require(s.processor->process(flush)==kResultOk,"Zero-frame parameter flush failed");
    const auto state=s.state(); require(state[kColor]==.415 && state[kScale]==1.0,"Flush values did not persist");
    std::cout << "      max block-equivalence error = " << maxError << ", tail absolute sum = " << tailEnergy << '\n';
    pass("sample-offset MIDI and old/new automation, PPQ across 64/1017/4096 blocks, audible processing, silent tail, zero-frame flush");
}

// The host routes real IConnectionPoint messages and records the plugin's payload.
// A broken/absent bridge, stale fabricated spectrum, or malformed controller
// message must be observable independently of the editor's rendering code.
struct VisualProbe final : IConnectionPoint {
    STACK_UNKNOWN(IConnectionPoint)
    Session& session;
    int received=0;
    std::array<float,192> dry{},wet{},gate{};
    int64 frame=0,mask=0;
    double sampleRate=0;
    double attackTailGain=1,diveSemitones=0,tamerReductionDb=0,formantVowel=0;
    int64 motionPitchClass=-1,motionHostSynced=0;
    explicit VisualProbe(Session& s) : session(s) {
        session.processorConnection->disconnect(session.controllerConnection);
        require(session.processorConnection->connect(this)==kResultOk,"Visual probe connect failed");
    }
    ~VisualProbe() {
        session.processorConnection->disconnect(this);
        session.processorConnection->connect(session.controllerConnection);
    }
    tresult PLUGIN_API connect(IConnectionPoint*) override { return kResultOk; }
    tresult PLUGIN_API disconnect(IConnectionPoint*) override { return kResultOk; }
    tresult PLUGIN_API notify(IMessage* message) override {
        if(message && message->getMessageID() && std::strcmp(message->getMessageID(),"Iris.Visual.Frame.v1")==0) {
            auto* a=message->getAttributes(); require(a!=nullptr,"Visual frame lacks attributes");
            require(a->getInt("frame",frame)==kResultOk && frame>0,"Visual frame counter invalid");
            require(a->getInt("mask",mask)==kResultOk && mask>=0 && mask<=4095,"Visual note mask invalid");
            require(a->getFloat("sampleRate",sampleRate)==kResultOk && sampleRate==48000,"Visual rate does not reflect audio");
            auto copy=[&](const char* key,auto& dst) {
                const void* bytes=nullptr; uint32 size=0;
                require(a->getBinary(key,bytes,size)==kResultOk && bytes && size==sizeof(dst),"Visual array invalid: "+std::string(key));
                std::memcpy(dst.data(),bytes,size);
                for(float x:dst) require(std::isfinite(x),"Nonfinite visual data");
            };
            copy("dry",dry); copy("wet",wet); copy("gate",gate);
            for(float x:gate) require(x>=0 && x<=1,"Gate telemetry outside 0..1");
            auto creativeFloat=[&](const char* key,double& value,double lo,double hi) {
                require(a->getFloat(key,value)==kResultOk && std::isfinite(value) && value>=lo && value<=hi,
                    "Missing or invalid real creative telemetry: "+std::string(key));
            };
            creativeFloat("attackTailGain",attackTailGain,0,2.01);
            creativeFloat("diveSemitones",diveSemitones,-24.01,24.01);
            creativeFloat("tamerReductionDb",tamerReductionDb,0,18.01);
            creativeFloat("formantVowel",formantVowel,0,4.001);
            require(a->getInt("motionPitchClass",motionPitchClass)==kResultOk && motionPitchClass>=-1 && motionPitchClass<=11,
                "Real motion pitch telemetry missing or invalid");
            require(a->getInt("motionHostSynced",motionHostSynced)==kResultOk && motionHostSynced>=0 && motionHostSynced<=1,
                "Real motion synchronization telemetry missing or invalid");
            ++received;
        }
        return session.controllerConnection->notify(message);
    }
};

void feedVisualTone(Session& s,float amplitude,int blocks=96) {
    std::array<float,512> l{},r{},ol{},orr{};
    float* ip[]{l.data(),r.data()}; float* op[]{ol.data(),orr.data()};
    AudioBusBuffers in{},out{}; in.numChannels=out.numChannels=2; in.channelBuffers32=ip;out.channelBuffers32=op;
    ProcessData data{}; data.processMode=kRealtime;data.symbolicSampleSize=kSample32;data.numSamples=512;
    data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;
    for(int b=0;b<blocks;++b) {
        for(int i=0;i<512;++i) l[i]=r[i]=amplitude*static_cast<float>(std::sin(2*3.141592653589793*1000*(b*512+i)/48000));
        require(s.processor->process(data)==kResultOk,"Visual test audio process failed");
    }
}

void checkVisual(Session& s) {
    auto v=defaults();v[kSnap]=0;v[kColor]=normalized(kColor,100);v[kMorphEnabled]=0;v[kGateEnabled]=0;
    v[kWidth]=normalized(kWidth,100);v[kOutputDb]=normalized(kOutputDb,0);v[kSoftLimit]=0;
    s.state(v);s.prepare(48000,kSample32);
    VisualProbe probe(s);
    auto request=owned<IMessage>(new HostMessage);request->setMessageID("Iris.Visual.Request.v1");
    feedVisualTone(s,.25f);
    require(s.processorConnection->notify(request)==kResultOk,"Processor must answer live spectrum requests");
    require(probe.received==1,"No real spectrum reached controller");
    const auto peak=std::max_element(probe.dry.begin(),probe.dry.end())-probe.dry.begin();
    const double peakHz=20*std::pow(1000.,static_cast<double>(peak)/191);
    require(peakHz>900 && peakHz<1120 && probe.dry[peak]>-20,"Spectrum must locate the real 1 kHz input tone");
    require(std::abs(probe.dry[peak]-probe.wet[peak])<.2,"Neutral wet visual disagrees with dry");
    require(probe.attackTailGain==1 && probe.diveSemitones==0 && probe.tamerReductionDb==0 &&
        probe.motionPitchClass==-1 && probe.motionHostSynced==0,"Disabled creative effects emitted non-neutral live status");
    const int64 previousFrame=probe.frame;
    feedVisualTone(s,0.f);
    require(s.processorConnection->notify(request)==kResultOk && probe.frame>previousFrame,"Visual spectrum did not advance");
    require(*std::max_element(probe.dry.begin(),probe.dry.end()) < -100,"Silence retained a fabricated or stale input spectrum");
    auto malformed=owned<IMessage>(new HostMessage);malformed->setMessageID("Iris.Visual.Frame.v1");
    require(s.controllerConnection->notify(malformed)!=kResultOk,"Controller accepted incomplete visual payload");
    auto completePacket=[&]() {
        auto packet=owned<IMessage>(new HostMessage);packet->setMessageID("Iris.Visual.Frame.v1");
        auto* a=packet->getAttributes();a->setInt("frame",probe.frame+1);a->setInt("mask",0);a->setInt("moveCount",0);
        a->setFloat("sampleRate",48000);a->setBinary("dry",probe.dry.data(),sizeof(probe.dry));
        a->setBinary("wet",probe.wet.data(),sizeof(probe.wet));a->setBinary("gate",probe.gate.data(),sizeof(probe.gate));
        std::array<iris::VisualMove,32> moves{};a->setBinary("moves",moves.data(),sizeof(moves));return packet;
    };
    auto legacy=completePacket();require(s.controllerConnection->notify(legacy)==kResultOk,"Legacy v1 visualization without new fields must remain readable");
    for(const auto& item:std::array<std::pair<const char*,double>,6>{{
        {"attackTailGain",-1},{"diveSemitones",25},{"tamerReductionDb",19},{"formantVowel",4.1},
        {"attackTailGain",std::numeric_limits<double>::quiet_NaN()},{"formantVowel",std::numeric_limits<double>::infinity()}
    }}) {
        auto packet=completePacket();packet->getAttributes()->setFloat(item.first,item.second);
        require(s.controllerConnection->notify(packet)!=kResultOk,"Controller accepted out-of-range/nonfinite creative float telemetry");
    }
    for(const auto& item:std::array<std::pair<const char*,int64>,3>{{
        {"motionPitchClass",12},{"motionPitchClass",-2},{"motionHostSynced",2}
    }}) {
        auto packet=completePacket();packet->getAttributes()->setInt(item.first,item.second);
        require(s.controllerConnection->notify(packet)!=kResultOk,"Controller accepted invalid creative integer telemetry");
    }
    pass("real audio/creative telemetry, source peak and silence, legacy visualization, malformed/out-of-range new fields rejected");
}

// Targeted reproduction of the user's 0.3 bug, run against the frozen 0.3 DLL.
// It deliberately grabs the VISIBLE LINE BODY, not the bottom label used by the
// earlier regression. No 0.4 parameter-count assumption enters this probe.
void checkCrossoverLineProbe(Session& s) {
#if defined(_WIN32)
    s.state(defaults()); s.prepare(48000,kSample32);
    auto* view=s.controller->createView(ViewType::kEditor);
    require(view!=nullptr,"Native editor missing in crossover reproduction");
    HWND parent=CreateWindowExW(0,L"STATIC",L"Iris crossover reproduction",WS_OVERLAPPEDWINDOW,
        20,20,1080,820,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(parent!=nullptr,"Crossover probe parent failed");
    require(view->attached(parent,kPlatformTypeHWND)==kResultOk,"Crossover probe attach failed");
    ShowWindow(parent,SW_SHOWNORMAL);UpdateWindow(parent);
    HWND child=GetWindow(parent,GW_CHILD);require(child!=nullptr,"Crossover probe child missing");
    SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(572,45));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(572,45));
    SendMessageW(child,WM_TIMER,1,0);UpdateWindow(child);
    const double before=plain(kColorLowMidHz,s.controller->getParamNormalized(kColorLowMidHz));
    SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(534,210));
    SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(576,210));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(576,210));
    const double after=plain(kColorLowMidHz,s.controller->getParamNormalized(kColorLowMidHz));
    std::cout << "      visible crossover line drag: " << before << " Hz -> " << after << " Hz\n";
    view->removed();DestroyWindow(parent);view->release();
    require(after>before+100,"Visible Color band crossover line did not respond to dragging");
    pass("visible crossover line body edits frequency");
#else
    throw std::runtime_error("Crossover line probe requires the native Windows editor");
#endif
}

void checkEditor(Session& s) {
    s.state(defaults());
    s.prepare(48000,kSample32);
    IPlugView* view=s.controller->createView(ViewType::kEditor);
#if defined(_WIN32)
    require(view!=nullptr,"Windows custom editor missing");
    require(view->isPlatformTypeSupported(kPlatformTypeHWND)==kResultOk,"HWND editor unsupported");
    ViewRect size{}; require(view->getSize(&size)==kResultOk && size.getWidth()==1040 && size.getHeight()==760,"Editor initial size wrong");
    RECT frame{0,0,size.getWidth(),size.getHeight()}; AdjustWindowRect(&frame,WS_OVERLAPPEDWINDOW,FALSE);
    HWND parent=CreateWindowExW(0,L"STATIC",L"Iris host smoke test",WS_OVERLAPPEDWINDOW,
        20,20,frame.right-frame.left,frame.bottom-frame.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(parent!=nullptr,"Host test window creation failed");
    require(view->attached(parent,kPlatformTypeHWND)==kResultOk,"Editor attach failed");
    ShowWindow(parent,SW_SHOWNORMAL); UpdateWindow(parent);
    MSG message{}; while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    HWND child=GetWindow(parent,GW_CHILD); require(child!=nullptr,"Native child window missing");
    // Exercise the real HWND gestures and verify that the host receives balanced edits.
    const int begins=s.host.beginCount,ends=s.host.endCount;
    SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(121,502));
    SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(121,484));
    const auto normalDrag=s.controller->getParamNormalized(kColor);
    require(normalDrag>.8,"Color knob drag did not change the controller");
    SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON|MK_SHIFT,MAKELPARAM(121,466));
    const auto fineDrag=s.controller->getParamNormalized(kColor);
    require(fineDrag>normalDrag && fineDrag-normalDrag<(normalDrag-.8)*.3,
            "Pressing Shift midway through a drag jumped or ignored fine control");
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(121,466));
    SendMessageW(child,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(121,502));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(121,502));
    require(std::abs(s.controller->getParamNormalized(kColor)-.8)<1e-9,"Color knob double-click reset failed");
    SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(350,250));
    SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(390,250));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(390,250));
    require(plain(kLowHz,s.controller->getParamNormalized(kLowHz))>230,"Spectrum low boundary did not operate the real parameter");
    SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(480,500));
    SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(480,480));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(480,480));
    require(plain(kGateDb,s.controller->getParamNormalized(kGateDb))>-40,"Gate threshold drag failed");
    const auto morphAmount=s.controller->getParamNormalized(kMorph);
    SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(270,432));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(270,432));
    require(s.controller->getParamNormalized(kMorphEnabled)==0,"Morph toggle hit its amount control instead");
    require(s.controller->getParamNormalized(kMorph)==morphAmount,"Morph switch changed its amount");
    SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(467,637));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(467,637));
    require(s.controller->getParamNormalized(kScale)==1 && s.controller->getParamNormalized(kCustomC+1)==1,"Black keyboard key selection failed");
    auto click=[&](int x,int y) {
        SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
        SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(x,y));
    };
    click(542,45);
    const std::array<std::array<int,3>,6> bandKnobs{{
        {{31,308,540}},{{36,422,540}},{{32,564,540}},{{37,678,540}},{{33,820,540}},{{38,934,540}}
    }};
    for(const auto& knob:bandKnobs) {
        const ParamID id=static_cast<ParamID>(knob[0]);const int x=knob[1],y=knob[2];
        const auto before=s.controller->getParamNormalized(id);
        SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
        SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x,y-18));
        SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(x,y-18));
        require(s.controller->getParamNormalized(id)>before,"Visible band knob did not edit its own parameter");
        SendMessageW(child,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(x,y));
        SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(x,y));
        require(std::abs(s.controller->getParamNormalized(id)-.5)<1e-9,"Band amount/EQ double-click did not restore neutral value");
    }
    const auto lowProtection=s.controller->getParamNormalized(kLowHz);
    SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(534,210));
    SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(576,210));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(576,210));
    require(plain(kColorLowMidHz,s.controller->getParamNormalized(kColorLowMidHz))>800,"Visible low/mid LINE BODY did not change crossover");
    require(s.controller->getParamNormalized(kLowHz)==lowProtection,"Colored crossover changed the protected Low frequency");
    SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(731,190));
    SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(702,190));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(702,190));
    require(plain(kColorMidHighHz,s.controller->getParamNormalized(kColorMidHighHz))<3000,"Visible mid/high LINE BODY did not change crossover");
    auto pump=[&] { while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message);DispatchMessageW(&message); } };
    auto beginNumber=[&](int x,int y,ParamID id,const wchar_t* text) {
        click(x,y);
        HWND edit=GetDlgItem(child,static_cast<int>(1000+id));
        require(edit && IsWindow(edit),"Native value EDIT missing for parameter "+std::to_string(id));
        SendMessageW(edit,WM_SETTEXT,0,reinterpret_cast<LPARAM>(text));return edit;
    };
    auto enterNumber=[&](int x,int y,ParamID id,const wchar_t* text,double expected) {
        HWND edit=beginNumber(x,y,id,text);SendMessageW(edit,WM_KEYDOWN,VK_RETURN,0);
        require(!IsWindow(edit),"Valid numeric entry did not commit and close");
        require(std::abs(plain(id,s.controller->getParamNormalized(id))-expected)<1e-6,
            "Typed value did not reach the real controller at ID "+std::to_string(id));
    };
    enterNumber(347,114,kColorLowMidHz,L"700",700);
    enterNumber(520,114,kColorMidHighHz,L"3k",3000);
    enterNumber(520,114,kColorMidHighHz,L"3 kHz",3000);
    for(const auto* bad:{L"7 0 0",L"nan",L"1e999",L"700 dB"}) {
        HWND edit=beginNumber(347,114,kColorLowMidHz,bad);
        SendMessageW(edit,WM_KEYDOWN,VK_RETURN,0);
        require(IsWindow(edit) && std::abs(plain(kColorLowMidHz,s.controller->getParamNormalized(kColorLowMidHz))-700)<1e-6,
            "Malformed/incorrect-unit numeric input changed the parameter or silently closed");
        SendMessageW(edit,WM_KEYDOWN,VK_ESCAPE,0);
        require(!IsWindow(edit),"Escape did not cancel invalid input");
    }
    HWND edit=beginNumber(347,114,kColorLowMidHz,L"1234");
    SendMessageW(edit,WM_KEYDOWN,VK_ESCAPE,0);
    require(std::abs(plain(kColorLowMidHz,s.controller->getParamNormalized(kColorLowMidHz))-700)<1e-6,"Escape committed input");
    edit=beginNumber(347,114,kColorLowMidHz,L"1234");SetFocus(parent);pump();
    require(!IsWindow(edit) && std::abs(plain(kColorLowMidHz,s.controller->getParamNormalized(kColorLowMidHz))-700)<1e-6,
        "Focus loss must cancel entry without changing frequency");
    require(GetFocus()==parent,"Cancelling input stole focus back from the host");
    enterNumber(347,114,kColorLowMidHz,L"0 Hz",120);
    enterNumber(347,114,kColorLowMidHz,L"99999 Hz",2000);
    enterNumber(520,114,kColorMidHighHz,L"-12 Hz",2500);
    enterNumber(520,114,kColorMidHighHz,L"99999 Hz",12000);
    enterNumber(347,114,kColorLowMidHz,L"700 Hz",700);
    enterNumber(520,114,kColorMidHighHz,L"3 kHz",3000);
    // A coincident white processing boundary must not steal a colored-line hit.
    s.controller->setParamNormalized(kLowHz,normalized(kLowHz,700));
    SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(534,190));
    SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(554,190));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(554,190));
    require(plain(kColorLowMidHz,s.controller->getParamNormalized(kColorLowMidHz))>700 &&
        std::abs(plain(kLowHz,s.controller->getParamNormalized(kLowHz))-700)<1e-6,"Coincident colored crossover lost priority");
    SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(534,137));
    SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(514,137));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(514,137));
    require(plain(kLowHz,s.controller->getParamNormalized(kLowHz))<700,"White processing boundary top label became inaccessible");
    pass("whole-line crossover drag, coincident boundary priority, real Hz/kHz entry, range/order bounds, invalid entry, Escape and focus cancellation");
    // Sparse pointer delivery still paints every crossed frequency node.
    const int paintBegins=s.host.beginCount,paintEnds=s.host.endCount;
    SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(282,389));
    SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(982,431));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(982,431));
    double previous=13;
    for(unsigned i=0;i<16;++i) {
        const double db=plain(kWetEqBand0+i,s.controller->getParamNormalized(kWetEqBand0+i));
        require(db<-.1 && db<previous,"Sparse EQ stroke skipped a band or lost cross-band interpolation");previous=db;
    }
    require(s.host.beginCount-paintBegins==16 && s.host.endCount-paintEnds==16,"EQ stroke automation gestures did not balance per band");
    enterNumber(845,323,kWetEqBand15,L"-6.25 dB",-6.25);
    SendMessageW(child,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(282,389));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(282,389));
    require(std::abs(plain(kWetEqBand0,s.controller->getParamNormalized(kWetEqBand0)))<1e-6,"Graphic EQ double-click did not reset only its band");
    require(std::abs(plain(kWetEqBand15,s.controller->getParamNormalized(kWetEqBand15))+6.25)<1e-6,"Single-band reset changed another band");
    s.controller->setParamNormalized(kColorEqMidDb,normalized(kColorEqMidDb,5));
    const double coreBeforeFlat=s.controller->getParamNormalized(kEngineMode);
    click(956,323);
    for(unsigned id=0;id<kParameterCount;++id) if((id>=36 && id<=38) || (id>=kWetEqBand0 && id<=kWetEqBand15))
        require(std::abs(plain(id,s.controller->getParamNormalized(id)))<1e-6,"Flat EQ left a nonzero static EQ band");
    require(s.controller->getParamNormalized(kEngineMode)==coreBeforeFlat,"Flat EQ changed the processing core");
    // A cancelled draw releases every automation gesture and the mouse capture.
    SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(282,400));
    SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(562,420));
    SendMessageW(child,WM_CANCELMODE,0,0);
    require(GetCapture()!=child && s.host.beginCount-begins==s.host.endCount-ends,"Cancelled EQ draw leaked capture or an automation gesture");
    click(956,323);
    pass("16 real EQ parameters painted from sparse input, exact dB entry, individual reset, all-19 Flat EQ, cancelled gesture cleanup");
    // Focus cycles within the visible page. Hidden Main controls must not take
    // the arrow key intended for the next visible EQ knob.
    click(308,540);
    SendMessageW(child,WM_KEYDOWN,VK_TAB,0);
    const auto eqBefore=s.controller->getParamNormalized(kColorEqLowDb);
    SendMessageW(child,WM_KEYDOWN,VK_UP,0);
    require(s.controller->getParamNormalized(kColorEqLowDb)>eqBefore,"Band page Tab navigation reached a hidden Main control");
    click(656,45);
    const std::array<std::array<int,3>,5> motionSwitches{{
        {{39,286,295}},{{43,614,295}},{{56,954,295}},{{46,451,460}},{{51,954,460}}
    }};
    for(const auto& item:motionSwitches) {
        click(item[1],item[2]);require(s.controller->getParamNormalized(item[0])==1,"Motion effect switch did not enable its own processor");
    }
    click(394,506);require(s.controller->getParamNormalized(kScaleMotionSync)==0,"Scale Motion free/sync switch failed");
    const std::array<std::array<int,3>,13> motionKnobs{{
        {{40,76,361}},{{41,178,361}},{{42,280,361}},{{44,440,361}},{{45,583,361}},
        {{57,775,361}},{{58,918,361}},{{47,91,523}},{{48,229,523}},
        {{52,575,523}},{{53,691,523}},{{54,807,523}},{{55,923,523}}
    }};
    for(const auto& item:motionKnobs) {
        const auto before=s.controller->getParamNormalized(item[0]);
        SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(item[1],item[2]));
        SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(item[1],item[2]-12));
        SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(item[1],item[2]-12));
        require(s.controller->getParamNormalized(item[0])>before,"Motion knob did not change its own DSP parameter: "+std::to_string(item[0]));
    }
    click(394,506);require(s.controller->getParamNormalized(kScaleMotionSync)==1,"Scale Motion sync could not be restored");
    SetForegroundWindow(parent);SetFocus(child);SetTimer(child,0x401,120,chooseLastPreset);SetTimer(child,0x402,2000,cancelStuckMenu);
    click(394,545);KillTimer(child,0x401);KillTimer(child,0x402);
    require(discrete(kScaleMotionDivision,s.controller->getParamNormalized(kScaleMotionDivision))==5,"Native beat-division popup did not select 1/32");
    click(434,45);
    require(s.host.beginCount>begins && s.host.beginCount-begins==s.host.endCount-ends,"UI automation gestures unbalanced");
    pass("six band knobs, page-aware keyboard navigation, five effect switches, thirteen motion knobs, free/sync and native division popup");
    // Clarity is a new horizontal control: verify its real hit target, fine
    // drag, direct percentage entry and reset without touching its neighbours.
    s.controller->setParamNormalized(kClarity,.35);
    const auto snapBeforeClarity=s.controller->getParamNormalized(kSnap);
    SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(906,420));
    require(std::abs(s.controller->getParamNormalized(kClarity)-.7)<1e-9,
        "Clarity track click did not select 70 percent");
    SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON|MK_SHIFT,MAKELPARAM(916,420));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(916,420));
    require(std::abs(s.controller->getParamNormalized(kClarity)-.715)<1e-9,
        "Clarity Shift drag ignored horizontal fine control");
    SendMessageW(child,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(900,420));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(900,420));
    require(std::abs(s.controller->getParamNormalized(kClarity)-.35)<1e-9,
        "Clarity double-click did not restore the new 35 percent default");
    SendMessageW(child,WM_RBUTTONDOWN,0,MAKELPARAM(980,420));
    HWND clarityEdit=GetDlgItem(child,static_cast<int>(1000+kClarity));
    require(clarityEdit && IsWindow(clarityEdit),"Clarity direct value entry did not open");
    SendMessageW(clarityEdit,WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"70 %"));
    SendMessageW(clarityEdit,WM_KEYDOWN,VK_RETURN,0);
    require(!IsWindow(clarityEdit) && std::abs(s.controller->getParamNormalized(kClarity)-.7)<1e-9,
        "Clarity percentage entry did not reach the actual controller");
    SendMessageW(child,WM_KEYDOWN,VK_HOME,0);
    require(s.controller->getParamNormalized(kClarity)==0,"Clarity keyboard minimum failed");
    SendMessageW(child,WM_KEYDOWN,VK_END,0);
    require(s.controller->getParamNormalized(kClarity)==1,"Clarity keyboard maximum failed");
    require(s.controller->getParamNormalized(kSnap)==snapBeforeClarity,
        "Clarity edited its neighbouring Snap parameter");
    require(s.host.beginCount-begins==s.host.endCount-ends,"Clarity leaked an automation gesture");
    pass("real Clarity track, horizontal Shift drag, percentage entry, default reset, keyboard bounds and balanced automation");
    const auto beforeBodyPage=s.state();
    click(450,113); // Main's material summary is navigation, never an audio switch.
    require(s.state()==beforeBodyPage,"Opening Body changed the audio parameters");
    for(int material=0;material<4;++material) {
        click(490+141*material,125);
        require(discrete(kBodyMaterial,s.controller->getParamNormalized(kBodyMaterial))==material,
            "Material tile selected the wrong DSP model");
    }
    const std::array<std::array<int,3>,5> bodyKnobs{{
        {{kBodyMix,127,507}},{{kBodyDecayMs,324,509}},{{kBodyDamping,522,509}},
        {{kBodyInharmonicity,720,509}},{{kBodyStrike,918,509}}
    }};
    for(const auto& knob:bodyKnobs) {
        const ParamID id=static_cast<ParamID>(knob[0]);const int x=knob[1],y=knob[2];
        const auto before=s.controller->getParamNormalized(id);
        SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
        SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x,y-18));
        SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(x,y-18));
        require(s.controller->getParamNormalized(id)>before,"Body knob did not edit its own parameter");
    }
    SendMessageW(child,WM_RBUTTONDOWN,0,MAKELPARAM(324,509));
    HWND bodyEdit=GetDlgItem(child,static_cast<int>(1000+kBodyDecayMs));
    require(bodyEdit && IsWindow(bodyEdit),"Body decay native numeric entry missing");
    SendMessageW(bodyEdit,WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"475 ms"));
    SendMessageW(bodyEdit,WM_KEYDOWN,VK_RETURN,0);
    require(!IsWindow(bodyEdit) && std::abs(plain(kBodyDecayMs,s.controller->getParamNormalized(kBodyDecayMs))-475)<1e-6,
        "Body decay typed value did not reach the DSP parameter");
    click(127,507);SendMessageW(child,WM_KEYDOWN,VK_TAB,0);
    const auto bodyDecayBefore=s.controller->getParamNormalized(kBodyDecayMs);
    SendMessageW(child,WM_KEYDOWN,VK_UP,0);
    require(s.controller->getParamNormalized(kBodyDecayMs)>bodyDecayBefore,"Body keyboard focus reached a hidden page");
    click(490,125);SendMessageW(child,WM_KEYDOWN,VK_RIGHT,0);
    require(discrete(kBodyMaterial,s.controller->getParamNormalized(kBodyMaterial))==1,"Material arrow key did not select Metal");
    SetForegroundWindow(parent);SetFocus(child);SetTimer(child,0x401,120,chooseLastPreset);SetTimer(child,0x402,2000,cancelStuckMenu);
    SendMessageW(child,WM_RBUTTONDOWN,0,MAKELPARAM(490,125));KillTimer(child,0x401);KillTimer(child,0x402);
    require(discrete(kBodyMaterial,s.controller->getParamNormalized(kBodyMaterial))==3,"Material popup did not select String");
    SendMessageW(child,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(127,507));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(127,507));
    require(s.controller->getParamNormalized(kBodyMix)==0,"Body amount reset must disable the new layer");
    require(s.host.beginCount-begins==s.host.endCount-ends,"Body leaked an automation gesture");
    click(403,45);
    pass("Body navigation preserves sound, four material tiles, five real knobs, ms entry, page focus, material popup and Amount zero reset");
    SetFocus(parent);
    SendMessageW(child,WM_MOUSELEAVE,0,0);
    auto desired=defaults();desired[kRoot]=normalized(kRoot,4);desired[kScale]=normalized(kScale,2);
    s.state(desired);s.prepare(48000,kSample32);
    auto tick=[&] { SendMessageW(child,WM_TIMER,1,0);RedrawWindow(parent,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW); };
    auto capture=[&](const char* filename) {
        RECT r{}; GetClientRect(child,&r);
        HDC source=GetDC(child),destination=CreateCompatibleDC(source);
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=r.right; info.bmiHeader.biHeight=-r.bottom;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr; HBITMAP bitmap=CreateDIBSection(source,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        require(bitmap && pixels,"Screenshot bitmap allocation failed");
        auto old=SelectObject(destination,bitmap);
        require(BitBlt(destination,0,0,r.right,r.bottom,source,0,0,SRCCOPY)!=0,"Screenshot BitBlt failed");
        GdiFlush();
        BITMAPFILEHEADER header{}; header.bfType=0x4d42;
        header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);
        const DWORD byteCount=static_cast<DWORD>(r.right*r.bottom*4); header.bfSize=header.bfOffBits+byteCount;
        std::vector<std::uint32_t> copied(static_cast<const std::uint32_t*>(pixels),static_cast<const std::uint32_t*>(pixels)+r.right*r.bottom);
        if(filename) {
            std::ofstream file(filename,std::ios::binary);
            file.write(reinterpret_cast<const char*>(&header),sizeof(header));
            file.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));
            file.write(static_cast<const char*>(pixels),byteCount);
            require(bool(file),"Screenshot file write failed");
            std::cout << "      Windows editor BMP captured: " << filename << '\n';
        }
        SelectObject(destination,old); DeleteObject(bitmap); DeleteDC(destination); ReleaseDC(child,source);
        return copied;
    };
    tick();const auto initLabel=capture(capturePath?(std::string(capturePath)+".core-before.bmp").c_str():nullptr);
    SetForegroundWindow(parent);SetFocus(child);
    SetTimer(child,0x401,120,chooseFirstCore);SetTimer(child,0x402,2000,cancelStuckMenu);
    click(165,73);KillTimer(child,0x401);KillTimer(child,0x402);tick();
    require(s.controller->getParamNormalized(kEngineMode)==0,"Real Core popup did not select Legacy");
    const auto legacyCorePixels=capture(capturePath?(std::string(capturePath)+".core-after.bmp").c_str():nullptr);
    auto coreDifference=[](const auto& a,const auto& b) {
        int changed=0;
        for(int y=63;y<84;++y)for(int x=88;x<242;++x)changed+=a[y*1040+x]!=b[y*1040+x];
        return changed;
    };
    require(coreDifference(initLabel,legacyCorePixels)>20,"Core menu label did not display the selected engine");
    SetForegroundWindow(parent);SetFocus(child);
    SetTimer(child,0x401,120,chooseLastPreset);
    SetTimer(child,0x402,2000,cancelStuckMenu);
    click(821,45); // The real native popup handles End/Enter from the timer.
    KillTimer(child,0x401);KillTimer(child,0x402);
    require(std::abs(plain(kColor,s.controller->getParamNormalized(kColor))-145)<.001 &&
        discrete(kBodyMaterial,s.controller->getParamNormalized(kBodyMaterial))==3 &&
        std::abs(plain(kBodyMix,s.controller->getParamNormalized(kBodyMix))-30)<.001,
        "Selecting String Halo in the real preset menu failed");
    require(s.controller->getParamNormalized(kEngineMode)==0,"Preset selection silently changed Legacy to Prism");
    SetFocus(parent);SendMessageW(child,WM_MOUSELEAVE,0,0);tick();
    const auto waterLabel=capture(nullptr);
    auto labelDifference=[](const auto& a,const auto& b) {
        int changed=0;
        for(int y=31;y<60;++y)for(int x=746;x<884;++x)
            changed+=a[y*1040+x]!=b[y*1040+x];
        return changed;
    };
    require(labelDifference(initLabel,waterLabel)>20,"The preset button text is still a constant label");
    Values water{};
    for(unsigned id=0;id<kParameterCount;++id)water[id]=sanitize(id,s.controller->getParamNormalized(id));
    s.controller->setParamNormalized(kColor,.91);tick();
    const auto customLabel=capture(nullptr);
    require(labelDifference(waterLabel,customLabel)>20,"Host sound automation did not redraw the preset name as Custom");
    s.state(water);tick();
    require(labelDifference(waterLabel,capture(nullptr))==0,"Component state restore did not restore the preset label pixels");
    s.controller->setParamNormalized(kClarity,.7);tick();
    require(labelDifference(waterLabel,capture(nullptr))>20,"Clarity automation did not redraw the preset name as Custom");
    s.state(water);tick();
    require(view->removed()==kResultOk,"Editor detach before reopen failed");
    require(view->attached(parent,kPlatformTypeHWND)==kResultOk,"Editor reopen failed");
    child=GetWindow(parent,GW_CHILD);require(child!=nullptr,"Reopened native child missing");
    SetFocus(parent);SendMessageW(child,WM_MOUSELEAVE,0,0);tick();
    require(labelDifference(waterLabel,capture(nullptr))==0,"Editor reopen lost the actual preset label");
    require(coreDifference(legacyCorePixels,capture(nullptr))==0,"Editor reopen lost the actual Legacy core label");
    SetForegroundWindow(parent);SetFocus(child);
    SetTimer(child,0x401,120,chooseLastPreset);SetTimer(child,0x402,2000,cancelStuckMenu);
    click(165,73);KillTimer(child,0x401);KillTimer(child,0x402);tick();
    require(s.controller->getParamNormalized(kEngineMode)==1,"Real Core popup did not select Prism");
    require(coreDifference(legacyCorePixels,capture(nullptr))>20,"Core label did not redraw for Prism");
    pass("real native preset popup, changed label pixels, Custom after automation, state restore and editor reopen");
    pass("actual Core menu selects both engines; preset, component restore and editor reopen preserve its displayed choice");
    // Use a named Glass preset for the actual product picture, retaining key E.
    desired[kColor]=normalized(kColor,150);desired[kDecayMs]=normalized(kDecayMs,220);desired[kWidth]=normalized(kWidth,115);
    s.state(desired);s.prepare(48000,kSample32);
    feedVisualTone(s,0.f);for(int i=0;i<32;++i)tick();
    const auto quiet=capture(nullptr);
    // Trigger the real native timer -> Controller::requestVisual -> processor
    // message -> controller mailbox -> GUI rendering loop with actual audio.
    for(int i=0;i<12;++i) { feedVisualTone(s,.25f,4);tick(); }
    const auto sounding=capture(nullptr);
    auto difference=[](const auto& a,const auto& b) {
        double change=0;
        for(int y=160;y<335;y+=3)for(int x=55;x<985;x+=3) {
            const auto p=a[y*1040+x],q=b[y*1040+x];
            for(int shift=0;shift<24;shift+=8)change+=std::abs(static_cast<int>((p>>shift)&255)-static_cast<int>((q>>shift)&255));
        }
        return change;
    };
    require(difference(quiet,sounding)>10000,"The displayed spectrum did not react to actual audio through the complete bridge");
    // Stop processing: the native display must age out, not keep a fake/live spectrum.
    Sleep(250);for(int i=0;i<40;++i)tick();
    const std::string silentPath=capturePath?std::string(capturePath)+".silent.bmp":"";
    const auto stopped=capture(capturePath?silentPath.c_str():nullptr);
    require(difference(quiet,stopped)<difference(quiet,sounding)*.1,"Stopped transport left a stale live spectrum");
    auto feedPicture=[&] {
    if(visualAudioPath) {
        std::ifstream file(visualAudioPath,std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});
        require(bytes.size()>44 && std::memcmp(bytes.data(),"RIFF",4)==0,"UI audio fixture is not RIFF WAV");
        auto u16=[&](std::size_t p){return static_cast<unsigned char>(bytes[p])|(static_cast<unsigned char>(bytes[p+1])<<8);};
        auto u32=[&](std::size_t p){return static_cast<std::uint32_t>(u16(p))|(static_cast<std::uint32_t>(u16(p+2))<<16);};
        std::size_t begin=0,length=0;bool format=false;
        for(std::size_t at=12;at+8<=bytes.size();) {
            const auto n=u32(at+4);if(at+8+n>bytes.size())break;
            if(std::memcmp(bytes.data()+at,"fmt ",4)==0 && n>=16)
                format=u16(at+8)==3 && u16(at+10)==2 && u32(at+12)==48000 && u16(at+22)==32;
            if(std::memcmp(bytes.data()+at,"data",4)==0) {begin=at+8;length=n;}
            at+=8+n+(n&1);
        }
        require(format && length>=32*512*8,"UI audio fixture must be 48k stereo float32");
        s.prepare(48000,kSample32);
        std::array<float,512> l{},r{},ol{},orr{};float* ip[]{l.data(),r.data()},*op[]{ol.data(),orr.data()};
        AudioBusBuffers in{},out{};in.numChannels=out.numChannels=2;in.channelBuffers32=ip;out.channelBuffers32=op;
        ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample32;data.numSamples=512;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;
        ProcessContext context{};context.state=ProcessContext::kPlaying|ProcessContext::kTempoValid|ProcessContext::kProjectTimeMusicValid;
        context.sampleRate=48000;context.tempo=140;data.processContext=&context;
        for(int b=0;b<32;++b) {
            context.projectTimeMusic=b*512*context.tempo/(60.0*48000);
            for(int i=0;i<512;++i) {std::memcpy(&l[i],bytes.data()+begin+(b*512+i)*8,4);std::memcpy(&r[i],bytes.data()+begin+(b*512+i)*8+4,4);}
            require(s.processor->process(data)==kResultOk,"UI fixture processing failed");tick();
        }
    } else {
        for(int i=0;i<12;++i) {feedVisualTone(s,.25f,4);tick();}
    }
    };
    feedPicture();
    capture(capturePath);
    auto shaped=desired;
    shaped[kColor]=normalized(kColor,180);shaped[kColorLow]=normalized(kColorLow,65);
    shaped[kColorMid]=normalized(kColorMid,150);shaped[kColorHigh]=normalized(kColorHigh,80);
    shaped[kColorEqMidDb]=normalized(kColorEqMidDb,1.5);shaped[kColorEqHighDb]=normalized(kColorEqHighDb,-4);
    const double eqShape[16]={0,0,-1,-2,-1,2,3,1,-2,-5,-3,0,2,-1,-4,-6};
    for(unsigned i=0;i<16;++i)shaped[kWetEqBand0+i]=normalized(kWetEqBand0+i,eqShape[i]);
    s.state(shaped);s.prepare(48000,kSample32);click(542,45);SendMessageW(child,WM_MOUSELEAVE,0,0);
    feedPicture();
    const std::string bandsPath=capturePath?std::string(capturePath)+".bands.bmp":"";
    if(capturePath)capture(bandsPath.c_str());
    auto moving=desired;moving[kColor]=normalized(kColor,185);
    for(const auto id:{kAttackTailEnabled,kPitchDiveEnabled,kScaleMotionEnabled,kFormantEnabled,kPeakTamerEnabled})moving[id]=1;
    moving[kAttackColor]=normalized(kAttackColor,15);moving[kTailColor]=normalized(kTailColor,150);
    moving[kFormantMix]=normalized(kFormantMix,45);moving[kFormantVowel]=normalized(kFormantVowel,1.5);
    moving[kFormantMotion]=normalized(kFormantMotion,35);moving[kScaleMotionDepth]=normalized(kScaleMotionDepth,90);
    s.state(moving);click(656,45);SendMessageW(child,WM_MOUSELEAVE,0,0);feedPicture();
    const std::string motionPath=capturePath?std::string(capturePath)+".motion.bmp":"";
    if(capturePath)capture(motionPath.c_str());
    auto bodyState=fromDSP(iris::presetParameters(10));
    bodyState[kRoot]=desired[kRoot];bodyState[kScale]=desired[kScale];
    s.state(bodyState);click(474,45);SendMessageW(child,WM_MOUSELEAVE,0,0);feedPicture();
    const std::string bodyPath=capturePath?std::string(capturePath)+".body.bmp":"";
    if(capturePath)capture(bodyPath.c_str());
    click(542,45);s.state(shaped);
    IPlugViewContentScaleSupport* scaled=nullptr;
    require(view->queryInterface(IPlugViewContentScaleSupport::iid,reinterpret_cast<void**>(&scaled))==kResultOk,"Native scale interface missing");
    require(scaled->setContentScaleFactor(1.25f)==kResultOk,"125% host scale rejected");
    ViewRect larger{};require(view->getSize(&larger)==kResultOk && larger.getWidth()==1300 && larger.getHeight()==950,"Scaled editor geometry is wrong");
    RECT scaledFrame{0,0,1300,950};AdjustWindowRect(&scaledFrame,WS_OVERLAPPEDWINDOW,FALSE);
    SetWindowPos(parent,nullptr,0,0,scaledFrame.right-scaledFrame.left,scaledFrame.bottom-scaledFrame.top,SWP_NOMOVE|SWP_NOZORDER);
    // Hit testing must scale with the host's 125% content factor, too.
    const auto midEqBefore=s.controller->getParamNormalized(kColorEqMidDb);
    SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(848,675));
    SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(848,653));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(848,653));
    require(s.controller->getParamNormalized(kColorEqMidDb)>midEqBefore,"Scaled band EQ hit testing failed");
    s.state(shaped);feedPicture();
    const std::string scaledPath=capturePath?std::string(capturePath)+".125.bmp":"";
    if(capturePath)capture(scaledPath.c_str());
    click(543,56);s.state(desired);feedPicture();
    const std::string scaledMainPath=capturePath?std::string(capturePath)+".main125.bmp":"";
    if(capturePath)capture(scaledMainPath.c_str());
    click(820,56);s.state(moving);feedPicture();
    const std::string scaledMotionPath=capturePath?std::string(capturePath)+".motion125.bmp":"";
    if(capturePath)capture(scaledMotionPath.c_str());
    click(1193,369);require(s.controller->getParamNormalized(kPeakTamerEnabled)==0,"125% motion switch hit test failed");
    click(593,56);s.state(bodyState);feedPicture();
    const auto bodyAt125=s.controller->getParamNormalized(kBodyDamping);
    SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(653,636));
    SendMessageW(child,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(653,614));
    SendMessageW(child,WM_LBUTTONUP,0,MAKELPARAM(653,614));
    require(s.controller->getParamNormalized(kBodyDamping)>bodyAt125,"125% Body knob hit test failed");
    s.state(bodyState);feedPicture();
    const std::string body125Path=capturePath?std::string(capturePath)+".body125.bmp":"";
    if(capturePath)capture(body125Path.c_str());
    require(scaled->setContentScaleFactor(1.5f)==kResultOk,"150% host scale rejected");
    ViewRect largest{};require(view->getSize(&largest)==kResultOk && largest.getWidth()==1560 && largest.getHeight()==1140,"150% geometry wrong");
    RECT largestFrame{0,0,1560,1140};AdjustWindowRect(&largestFrame,WS_OVERLAPPEDWINDOW,FALSE);
    SetWindowPos(parent,nullptr,0,0,largestFrame.right-largestFrame.left,largestFrame.bottom-largestFrame.top,SWP_NOMOVE|SWP_NOZORDER);
    click(813,68);s.state(shaped);
    enterNumber(521,171,kColorLowMidHz,L"900 Hz",900);
    edit=beginNumber(780,171,kColorMidHighHz,L"4.2 kHz");
    RECT editBounds{};GetWindowRect(edit,&editBounds);
    require(editBounds.right-editBounds.left>=240 && editBounds.bottom-editBounds.top>=42,"Native numeric input did not scale with the editor");
    SendMessageW(edit,WM_KEYDOWN,VK_RETURN,0);
    require(std::abs(plain(kColorMidHighHz,s.controller->getParamNormalized(kColorMidHighHz))-4200)<1e-6,"150% frequency entry failed");
    feedPicture();const std::string bands150Path=capturePath?std::string(capturePath)+".bands150.bmp":"";
    if(capturePath)capture(bands150Path.c_str());
    click(984,68);s.state(moving);feedPicture();
    const std::string motion150Path=capturePath?std::string(capturePath)+".motion150.bmp":"";
    if(capturePath)capture(motion150Path.c_str());
    click(651,68);s.state(desired);feedPicture();
    const std::string main150Path=capturePath?std::string(capturePath)+".main150.bmp":"";
    if(capturePath)capture(main150Path.c_str());
    click(711,68);s.state(bodyState);feedPicture();
    click(947,188);
    require(discrete(kBodyMaterial,s.controller->getParamNormalized(kBodyMaterial))==1,"150% material tile hit test failed");
    s.state(bodyState);feedPicture();
    const std::string body150Path=capturePath?std::string(capturePath)+".body150.bmp":"";
    if(capturePath)capture(body150Path.c_str());
    require(s.host.beginCount-begins==s.host.endCount-ends,"Final native gesture balance failed");
    scaled->release();
    require(view->removed()==kResultOk,"Editor remove failed"); DestroyWindow(parent); view->release();
    pass("Windows HWND, full audio-to-spectrum bridge, stopped-spectrum decay, 100/125/150% native controls and input, removal");
#else
    require(view==nullptr,"Linux validation build should use host generic editor");
    pass("headless Linux build offers host parameter editor (Windows GDI editor not runtime-tested here)");
#endif
}
} // namespace

int main(int argc,char** argv) {
    try {
        require(argc>=2 && argc<=4,"Usage: iris-vst3-host-test <path-to-module-binary> [editor-capture.bmp] [48k-float-stereo-ui.wav]");
        const bool crossoverProbe=argc==3 && std::strcmp(argv[2],"--probe-crossover")==0;
        if(argc>=3 && !crossoverProbe) capturePath=argv[2];
        if(argc==4) visualAudioPath=argv[3];
        Module module(argv[1]); Host host;
        {
            Session session(module,host);
            if(crossoverProbe) { checkCrossoverLineProbe(session); return 0; }
            checkMetadata(session); checkState(session); checkSampleRateBounds(session);
            checkBypass<float>(session,kSample32,2,48000,false);
            checkBypass<float>(session,kSample32,2,44100,true);
            checkBypass<double>(session,kSample64,2,96000,false);
            checkBypass<float>(session,kSample32,2,192000,false);
            checkBypass<float>(session,kSample32,1,48000,false);
            pass("32/64-bit audio, mono/stereo, in-place buffers, rates 44.1/48/96/192 kHz, exact 3072-sample unity bypass");
            checkAutomation(session); checkVisual(session); checkEditor(session);
        }
        require(host.references==1,"Host context/component-handler references leaked");
        pass("processor/controller disconnect, terminate, reference cleanup and module unload");
        std::cout << "All native VST3 host regression checks passed.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "FAIL  " << error.what() << '\n'; return 1;
    }
}
