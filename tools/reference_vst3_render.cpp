// Generic offline VST3 comparison host. Original host/WAV helpers from Iris Color (MIT).
// Loads a Windows VST3 through its published API; no plugin DSP is reimplemented.
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace {
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
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
    tresult PLUGIN_API getName(String128 name) override { UString(name,128).fromAscii("Reference Offline Host"); return kResultOk; }
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

uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t u32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
void put16(std::ostream& s, uint16_t x) { char b[2]={char(x),char(x>>8)}; s.write(b,2); }
void put32(std::ostream& s, uint32_t x) { char b[4]={char(x),char(x>>8),char(x>>16),char(x>>24)}; s.write(b,4); }

struct Wave { int rate=48000; int channels=2; std::vector<float> left, right; };

Wave readWave(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open input WAV: " + path);
    const auto length=file.tellg();
    if (length < 12 || length > std::streamoff(1024ull*1024*1024))
        throw std::runtime_error("WAV must be a RIFF file between 12 bytes and 1 GiB.");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    file.seekg(0); file.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!file || std::memcmp(bytes.data(),"RIFF",4) || std::memcmp(bytes.data()+8,"WAVE",4))
        throw std::runtime_error("Expected little-endian RIFF WAVE (convert MP3/FLAC in your DAW first).");
    uint16_t format=0, channels=0, bits=0, align=0;
    uint32_t rate=0; size_t dataPos=0, dataSize=0;
    for (size_t p=12; p+8<=bytes.size();) {
        const uint32_t size=u32(bytes.data()+p+4); const size_t start=p+8;
        if (size > bytes.size()-start) throw std::runtime_error("Truncated WAV chunk.");
        if (!std::memcmp(bytes.data()+p,"fmt ",4)) {
            if (size<16) throw std::runtime_error("Invalid WAV format chunk.");
            auto* f=bytes.data()+start; format=u16(f); channels=u16(f+2); rate=u32(f+4); align=u16(f+12); bits=u16(f+14);
            if (format==0xfffe) {
                if(size<40 || u16(f+16)<22) throw std::runtime_error("Invalid extensible WAV format.");
                const uint8_t expectedTail[14]={0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
                if(std::memcmp(f+26,expectedTail,14)) throw std::runtime_error("Unsupported extensible WAV subformat.");
                format=u16(f+24);
            }
        } else if (!std::memcmp(bytes.data()+p,"data",4) && !dataPos) { dataPos=start; dataSize=size; }
        const size_t next=start+size+(size&1u); if(next<=p) break; p=next;
    }
    if ((format!=1 && format!=3) || (channels!=1 && channels!=2) || rate<8000 || rate>192000 || !dataPos)
        throw std::runtime_error("Supported input: mono/stereo PCM or IEEE float WAV, 8–192 kHz.");
    if ((format==1 && bits!=8 && bits!=16 && bits!=24 && bits!=32) || (format==3 && bits!=32 && bits!=64))
        throw std::runtime_error("Unsupported WAV bit depth.");
    const size_t stride=bits/8;
    if (align!=channels*stride || dataSize%align) throw std::runtime_error("Invalid WAV block alignment.");
    const size_t frames=dataSize/align;
    if(frames==0) throw std::runtime_error("Input WAV is empty.");
    Wave w; w.rate=int(rate); w.channels=channels; w.left.resize(frames); w.right.resize(frames);
    auto sample=[&](const uint8_t* p)->float {
        double x=0;
        if(format==3 && bits==32) { const auto b=u32(p); float f; std::memcpy(&f,&b,4); x=f; }
        else if(format==3 && bits==64) { uint64_t b=u32(p)|(uint64_t(u32(p+4))<<32); double d; std::memcpy(&d,&b,8); x=d; }
        else if(bits==8) x=(int(p[0])-128)/128.0;
        else if(bits==16) { uint16_t b=u16(p); int16_t n; std::memcpy(&n,&b,2); x=n/32768.0; }
        else if(bits==24) { int32_t n=int32_t(p[0])|(int32_t(p[1])<<8)|(int32_t(p[2])<<16); if(n&0x800000) n-=0x1000000; x=n/8388608.0; }
        else { const auto b=u32(p); int32_t n; std::memcpy(&n,&b,4); x=n/2147483648.0; }
        return std::isfinite(x) ? float(std::clamp(x,-16.0,16.0)) : 0.f;
    };
    for(size_t i=0;i<frames;++i) {
        const auto* p=bytes.data()+dataPos+i*align;
        w.left[i]=sample(p); w.right[i]=channels==2?sample(p+stride):w.left[i];
    }
    return w;
}

void writeWave(const std::string& path, int rate, const std::vector<float>& l, const std::vector<float>& r, size_t start, int channels=2) {
    if(l.size()!=r.size() || start>l.size()) throw std::runtime_error("Invalid output buffers.");
    const size_t frames=l.size()-start;
    if(frames>(std::numeric_limits<uint32_t>::max()-50ull)/8) throw std::runtime_error("Output exceeds RIFF size limit.");
    std::ofstream out(path,std::ios::binary); if(!out) throw std::runtime_error("Cannot create output WAV: "+path);
    // IEEE float WAV with WAVEFORMATEX and a fact chunk. Values are not normalized.
    out.write("RIFF",4); put32(out,uint32_t(50+frames*channels*4)); out.write("WAVEfmt ",8);
    put32(out,18); put16(out,3); put16(out,uint16_t(channels)); put32(out,uint32_t(rate)); put32(out,uint32_t(rate*channels*4)); put16(out,uint16_t(channels*4)); put16(out,32); put16(out,0);
    out.write("fact",4); put32(out,4); put32(out,uint32_t(frames)); out.write("data",4); put32(out,uint32_t(frames*channels*4));
    for(size_t i=start;i<l.size();++i) {
        uint32_t a,b; std::memcpy(&a,&l[i],4); std::memcpy(&b,&r[i],4); put32(out,a); if(channels==2)put32(out,b);
    }
    if(!out) throw std::runtime_error("Writing WAV failed (disk full or destination inaccessible).");
}

std::string quoted(const std::string& text) {
    std::string result="\"";
    for (unsigned char c : text) {
        if (c=='\"') result+="\\\"";
        else if (c=='\\') result+="\\\\";
        else if (c=='\n') result+="\\n";
        else if (c=='\r') result+="\\r";
        else if (c=='\t') result+="\\t";
        else if (c<32) { const char hex[]="0123456789abcdef"; result+="\\u00"; result+=hex[c>>4]; result+=hex[c&15]; }
        else result+=char(c);
    }
    return result+'\"';
}
std::string utf8(const TChar* text, int capacity=128) {
    int length=0; while(length<capacity && text[length]) ++length;
    if(!length) return {};
    int bytes=WideCharToMultiByte(CP_UTF8,0,reinterpret_cast<const WCHAR*>(text),length,nullptr,0,nullptr,nullptr);
    std::string result(bytes,'\0');
    WideCharToMultiByte(CP_UTF8,0,reinterpret_cast<const WCHAR*>(text),length,result.data(),bytes,nullptr,nullptr);
    return result;
}
std::string number(double value) {
    if(!std::isfinite(value)) return "null";
    std::ostringstream stream; stream << std::setprecision(17) << value; return stream.str();
}
std::string uid(const TUID id) {
    const char hex[]="0123456789abcdef"; std::string result;
    for(int i=0;i<16;++i) { auto c=static_cast<unsigned char>(id[i]); result+=hex[c>>4]; result+=hex[c&15]; }
    return result;
}

class Module {
public:
    HMODULE handle=nullptr;
    explicit Module(const std::string& path) {
        handle=LoadLibraryA(path.c_str());
        require(handle!=nullptr,"LoadLibrary failed: Windows error "+std::to_string(GetLastError()));
        auto enter=reinterpret_cast<bool(*)()>(symbol("InitDll"));
        if(enter) require(enter(),"InitDll rejected module initialization");
    }
    ~Module() {
        if(handle) {
            auto leave=reinterpret_cast<bool(*)()>(symbol("ExitDll"));
            if(leave) leave();
            FreeLibrary(handle);
        }
    }
    void* symbol(const char* name) { return reinterpret_cast<void*>(GetProcAddress(handle,name)); }
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
    PFactoryInfo factoryInfo{};
    PClassInfo effectInfo{};
    bool combinedController=false, connected=false, stateSynced=false, active=false;
    int processMode=kOffline, inputBuses=0, outputBuses=0;
    Session(Module& module,Host& h) : host(h) {
        auto getFactory=reinterpret_cast<IPluginFactory*(PLUGIN_API*)()>(module.symbol("GetPluginFactory"));
        require(getFactory!=nullptr,"GetPluginFactory export missing"); factory=getFactory();
        require(factory && factory->countClasses()>0 && factory->countClasses()<1024,"Invalid factory class count");
        require(factory->getFactoryInfo(&factoryInfo)==kResultOk,"Factory info unavailable");
        IPluginFactory3* f3=nullptr;
        if(factory->queryInterface(IPluginFactory3::iid,reinterpret_cast<void**>(&f3))==kResultOk && f3) {
            f3->setHostContext(static_cast<IHostApplication*>(&host)); f3->release();
        }
        bool found=false;
        for(int i=0;i<factory->countClasses();++i) {
            PClassInfo info{};
            if(factory->getClassInfo(i,&info)==kResultOk && std::strcmp(info.category,kVstAudioEffectClass)==0) {
                effectInfo=info; found=true; break;
            }
        }
        require(found,"No VST3 audio effect class was advertised");
        require(factory->createInstance(effectInfo.cid,IComponent::iid,reinterpret_cast<void**>(&component))==kResultOk && component,"Component creation failed");
        require(component->initialize(static_cast<IHostApplication*>(&host))==kResultOk,"Component initialize failed");
        require(component->queryInterface(IAudioProcessor::iid,reinterpret_cast<void**>(&processor))==kResultOk && processor,"IAudioProcessor unavailable");
        if(component->queryInterface(IEditController::iid,reinterpret_cast<void**>(&controller))==kResultOk && controller) {
            combinedController=true;
        } else {
            TUID controllerID{};
            require(component->getControllerClassId(controllerID)==kResultOk,"Controller class unavailable");
            require(factory->createInstance(controllerID,IEditController::iid,reinterpret_cast<void**>(&controller))==kResultOk && controller,"Controller creation failed");
            require(controller->initialize(static_cast<IHostApplication*>(&host))==kResultOk,"Controller initialize failed");
        }
        require(controller->setComponentHandler(&host)==kResultOk,"Controller rejected component handler");
        component->queryInterface(IConnectionPoint::iid,reinterpret_cast<void**>(&processorConnection));
        controller->queryInterface(IConnectionPoint::iid,reinterpret_cast<void**>(&controllerConnection));
        if(processorConnection && controllerConnection) {
            auto a=processorConnection->connect(controllerConnection);
            auto b=controllerConnection->connect(processorConnection);
            connected=(a==kResultOk && b==kResultOk);
        }
        MemoryStream state;
        if(component->getState(&state)==kResultOk) {
            state.rewind(); stateSynced=(controller->setComponentState(&state)==kResultOk);
        }
    }
    ~Session() {
        if(active) { processor->setProcessing(false); component->setActive(false); }
        if(connected) { processorConnection->disconnect(controllerConnection); controllerConnection->disconnect(processorConnection); }
        if(processorConnection) processorConnection->release();
        if(controllerConnection) controllerConnection->release();
        if(controller) { controller->setComponentHandler(nullptr); if(!combinedController)controller->terminate(); controller->release(); }
        if(component) component->terminate();
        if(processor) processor->release();
        if(component) component->release();
        if(factory) factory->release();
    }
    std::string textValue(ParamID id,double normalized) {
        String128 text{};
        return controller->getParamStringByValue(id,normalized,text)==kResultOk ? utf8(text) : "";
    }
    void apply(std::map<ParamID,double>& values) {
        std::set<ParamID> known;
        for(int i=0;i<controller->getParameterCount();++i) {
            ParameterInfo info{};
            if(controller->getParameterInfo(i,info)==kResultOk && !(info.flags&ParameterInfo::kIsReadOnly))known.insert(info.id);
        }
        for(auto& setting:values) {
            require(known.count(setting.first)!=0,"Unknown or read-only parameter ID "+std::to_string(setting.first));
            require(controller->setParamNormalized(setting.first,setting.second)==kResultOk,"Controller rejected parameter "+std::to_string(setting.first));
            setting.second=controller->getParamNormalized(setting.first);
            require(std::isfinite(setting.second) && setting.second>=0 && setting.second<=1,"Controller emitted invalid normalized value");
        }
    }
    void prepare(double rate,int channels,int block) {
        require(processor->canProcessSampleSize(kSample32)==kResultOk,"Plugin cannot process float32 audio");
        inputBuses=component->getBusCount(kAudio,kInput); outputBuses=component->getBusCount(kAudio,kOutput);
        require(inputBuses>=1 && inputBuses<=8 && outputBuses>=1 && outputBuses<=8,"Unsupported audio bus layout");
        std::vector<SpeakerArrangement> inputs(inputBuses,SpeakerArr::kEmpty),outputs(outputBuses,SpeakerArr::kEmpty);
        inputs[0]=outputs[0]=channels==1?SpeakerArr::kMono:SpeakerArr::kStereo;
        require(processor->setBusArrangements(inputs.data(),inputBuses,outputs.data(),outputBuses)==kResultOk,"Plugin rejected requested mono/stereo main buses");
        for(int i=0;i<inputBuses;++i)require(component->activateBus(kAudio,kInput,i,i==0)==kResultOk,"Input bus activation failed");
        for(int i=0;i<outputBuses;++i)require(component->activateBus(kAudio,kOutput,i,i==0)==kResultOk,"Output bus activation failed");
        for(int i=0;i<component->getBusCount(kEvent,kInput);++i)component->activateBus(kEvent,kInput,i,i==0);
        for(int i=0;i<component->getBusCount(kEvent,kOutput);++i)component->activateBus(kEvent,kOutput,i,i==0);
        ProcessSetup setup{}; setup.processMode=kOffline; setup.symbolicSampleSize=kSample32; setup.maxSamplesPerBlock=block; setup.sampleRate=rate;
        if(processor->setupProcessing(setup)!=kResultOk) {
            setup.processMode=kRealtime;
            require(processor->setupProcessing(setup)==kResultOk,"Plugin rejected processing setup");
        }
        processMode=setup.processMode;
        require(component->setActive(true)==kResultOk,"Plugin activation failed"); active=true;
        require(processor->setProcessing(true)==kResultOk,"Plugin processing activation failed");
    }
    void list(std::ostream& out) {
        out << "{\n  \"host\": \"Generic offline VST3 API host\",\n  \"effect\": " << quoted(effectInfo.name)
            << ",\n  \"vendor\": " << quoted(factoryInfo.vendor) << ",\n  \"class_id_bytes\": " << quoted(uid(effectInfo.cid))
            << ",\n  \"combined_controller\": " << (combinedController?"true":"false")
            << ",\n  \"initial_state_synced\": " << (stateSynced?"true":"false")
            << ",\n  \"audio_input_buses\": " << component->getBusCount(kAudio,kInput)
            << ",\n  \"audio_output_buses\": " << component->getBusCount(kAudio,kOutput)
            << ",\n  \"supports_float32\": " << (processor->canProcessSampleSize(kSample32)==kResultOk?"true":"false")
            << ",\n  \"parameters\": [\n";
        const auto count=controller->getParameterCount(); require(count>=0 && count<=100000,"Invalid parameter count");
        for(int i=0;i<count;++i) {
            ParameterInfo info{}; require(controller->getParameterInfo(i,info)==kResultOk,"Parameter info failed");
            out << (i?",\n":"") << "    {\"index\":" << i << ",\"id\":" << info.id
                << ",\"title\":" << quoted(utf8(info.title)) << ",\"short_title\":" << quoted(utf8(info.shortTitle))
                << ",\"units\":" << quoted(utf8(info.units)) << ",\"step_count\":" << info.stepCount
                << ",\"flags\":" << info.flags << ",\"unit_id\":" << info.unitId
                << ",\"default_normalized\":" << number(info.defaultNormalizedValue)
                << ",\"current_normalized\":" << number(controller->getParamNormalized(info.id))
                << ",\"default_display\":" << quoted(textValue(info.id,info.defaultNormalizedValue))
                << ",\"plain_at_zero\":" << number(controller->normalizedParamToPlain(info.id,0))
                << ",\"plain_at_one\":" << number(controller->normalizedParamToPlain(info.id,1))
                << ",\"samples\":[";
            for(int j=0;j<=4;++j) {
                const double norm=j/4.0;
                out << (j?",":"") << "{\"normalized\":" << number(norm) << ",\"plain\":" << number(controller->normalizedParamToPlain(info.id,norm))
                    << ",\"display\":" << quoted(textValue(info.id,norm)) << "}";
            }
            out << "]";
            if(info.stepCount>0 && info.stepCount<=128) {
                out << ",\"choices\":[";
                for(int j=0;j<=info.stepCount;++j) out << (j?",":"") << "{\"normalized\":" << number(double(j)/info.stepCount)
                    << ",\"display\":" << quoted(textValue(info.id,double(j)/info.stepCount)) << "}";
                out << "]";
            }
            out << "}";
        }
        out << "\n  ]\n}\n";
    }
};

double finiteNumber(const std::string& text) {
    size_t used=0; const double result=std::stod(text,&used);
    require(used==text.size() && std::isfinite(result),"Expected finite number: "+text); return result;
}
int boundedInteger(const std::string& text,int low,int high) {
    const double result=finiteNumber(text);
    require(result>=low && result<=high && result==std::floor(result),"Integer outside supported range: "+text);
    return int(result);
}
void render(Session& session,const Wave& wave,const std::string& output,const std::map<ParamID,double>& values,
            double tail,double tempo,int block,int channels,bool keepLatency,std::ostream& report) {
    session.prepare(wave.rate,channels,block);
    const uint32 latency=session.processor->getLatencySamples();
    require(latency<=uint32(wave.rate*30),"Plugin latency exceeds the 30-second renderer limit");
    const size_t inputFrames=wave.left.size(), tailFrames=size_t(std::llround(tail*wave.rate));
    const size_t frames=inputFrames+tailFrames+latency;
    std::vector<float> left(frames),right(frames),inLeft(block),inRight(block),outLeft(block),outRight(block);
    std::vector<AudioBusBuffers> inputs(session.inputBuses),outputs(session.outputBuses);
    float* inputPointers[]={inLeft.data(),inRight.data()}; float* outputPointers[]={outLeft.data(),outRight.data()};
    inputs[0].numChannels=channels; inputs[0].channelBuffers32=inputPointers;
    outputs[0].numChannels=channels; outputs[0].channelBuffers32=outputPointers;
    Changes initial;
    for(const auto& setting:values)initial.add(setting.first,0,setting.second);
    ProcessContext context{};
    context.state=ProcessContext::kPlaying|ProcessContext::kTempoValid|ProcessContext::kTimeSigValid|
        ProcessContext::kProjectTimeMusicValid|ProcessContext::kBarPositionValid|ProcessContext::kContTimeValid;
    context.sampleRate=wave.rate; context.tempo=tempo; context.timeSigNumerator=4; context.timeSigDenominator=4;
    Events inEvents,outEvents;
    Changes outChanges;
    const auto started=std::chrono::steady_clock::now();
    for(size_t offset=0;offset<frames;) {
        const int n=int(std::min<size_t>(block,frames-offset));
        bool silent=true;
        for(int i=0;i<n;++i) {
            inLeft[i]=(offset+i<inputFrames)?wave.left[offset+i]:0;
            inRight[i]=(offset+i<inputFrames)?wave.right[offset+i]:0;
            silent=silent && inLeft[i]==0 && (channels==1 || inRight[i]==0);
            outLeft[i]=outRight[i]=0;
        }
        inputs[0].silenceFlags=silent?((uint64(1)<<channels)-1):0; outputs[0].silenceFlags=0;
        context.projectTimeSamples=int64(offset); context.continousTimeSamples=int64(offset);
        context.projectTimeMusic=double(offset)/wave.rate*tempo/60.0;
        context.barPositionMusic=std::floor(context.projectTimeMusic/4.0)*4.0;
        outChanges.queues.clear(); outEvents.events.clear();
        ProcessData data{}; data.processMode=session.processMode; data.symbolicSampleSize=kSample32; data.numSamples=n;
        data.numInputs=session.inputBuses; data.numOutputs=session.outputBuses; data.inputs=inputs.data(); data.outputs=outputs.data();
        data.inputParameterChanges=offset==0?&initial:nullptr; data.outputParameterChanges=&outChanges;
        data.inputEvents=&inEvents; data.outputEvents=&outEvents; data.processContext=&context;
        require(session.processor->process(data)==kResultOk,"Plugin process call failed at sample "+std::to_string(offset));
        for(int i=0;i<n;++i) {
            require(std::isfinite(outLeft[i]) && (channels==1 || std::isfinite(outRight[i])),"Plugin produced nonfinite output");
            left[offset+i]=outLeft[i]; right[offset+i]=channels==1?outLeft[i]:outRight[i];
        }
        MSG message;
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        offset+=n;
    }
    const auto duration=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    require(session.processor->getLatencySamples()==latency,"Plugin changed latency during rendering; output was not written");
    const size_t start=keepLatency?0:latency;
    writeWave(output,wave.rate,left,right,start,channels);
    double peak=0,sum=0;
    for(size_t i=start;i<frames;++i) { peak=std::max(peak,std::abs(double(left[i]))); sum+=double(left[i])*left[i]; if(channels==2){peak=std::max(peak,std::abs(double(right[i])));sum+=double(right[i])*right[i];} }
    report << "{\n  \"effect\": " << quoted(session.effectInfo.name) << ",\n  \"vendor\": " << quoted(session.factoryInfo.vendor)
        << ",\n  \"processing\": \"Actual dynamically loaded VST3 process(), float32, no audio device\""
        << ",\n  \"process_mode\": " << quoted(session.processMode==kOffline?"offline":"realtime API mode, offline file execution")
        << ",\n  \"sample_rate\": " << wave.rate << ",\n  \"input_channels\": " << wave.channels << ",\n  \"processing_channels\": " << channels
        << ",\n  \"block_size\": " << block << ",\n  \"tempo_bpm\": " << number(tempo) << ",\n  \"host_transport\": \"playing, 4/4, sample/PPQ/bar positions advance each block\""
        << ",\n  \"input_frames\": " << inputFrames << ",\n  \"tail_frames\": " << tailFrames << ",\n  \"latency_samples\": " << latency
        << ",\n  \"removed_latency_samples\": " << start << ",\n  \"output_frames\": " << frames-start
        << ",\n  \"normalization\": \"none\",\n  \"output_peak\": " << number(peak)
        << ",\n  \"output_rms\": " << number(std::sqrt(sum/double((frames-start)*channels)))
        << ",\n  \"processing_seconds\": " << number(duration) << ",\n  \"parameter_overrides\": [";
    bool first=true;
    for(const auto& setting:values) {
        report << (first?"":",") << "{\"id\":" << setting.first << ",\"normalized\":" << number(setting.second)
            << ",\"plain\":" << number(session.controller->normalizedParamToPlain(setting.first,setting.second))
            << ",\"display\":" << quoted(session.textValue(setting.first,setting.second)) << "}";
        first=false;
    }
    report << "]\n}\n";
}
} // namespace

int main(int argc,char** argv) {
    const auto comResult=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try {
        require(argc>=3,"usage: reference-vst3-render.exe plugin.vst3 --list [--json output.json] OR --input input.wav --output output.wav [--set ID:NORM] [--set-text ID:DISPLAY] [--tail seconds] [--tempo BPM] [--block N] [--channels 1|2] [--keep-latency] [--json report.json]");
        std::string plugin=argv[1], jsonPath,input,output;
        bool list=false,keepLatency=false;
        double tail=1,tempo=150; int block=512,channels=2;
        std::map<ParamID,double> values;
        std::map<ParamID,std::string> textValues;
        for(int i=2;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--list") list=true;
            else if(arg=="--keep-latency")keepLatency=true;
            else if(arg=="--json" && i+1<argc) jsonPath=argv[++i];
            else if(arg=="--input" && i+1<argc)input=argv[++i];
            else if(arg=="--output" && i+1<argc)output=argv[++i];
            else if(arg=="--tail" && i+1<argc){tail=finiteNumber(argv[++i]);require(tail>=0 && tail<=30,"Tail must be 0..30 seconds");}
            else if(arg=="--tempo" && i+1<argc){tempo=finiteNumber(argv[++i]);require(tempo>=1 && tempo<=1000,"Tempo must be 1..1000 BPM");}
            else if(arg=="--block" && i+1<argc)block=boundedInteger(argv[++i],1,16384);
            else if(arg=="--channels" && i+1<argc)channels=boundedInteger(argv[++i],1,2);
            else if(arg=="--set" && i+1<argc) {
                const std::string item=argv[++i]; const auto colon=item.find(':');
                require(colon!=std::string::npos,"Parameter setting needs ID:NORM");
                const auto idText=item.substr(0,colon); size_t used=0;
                const auto id=std::stoull(idText,&used,idText.rfind("0x",0)==0?16:10);
                const auto value=finiteNumber(item.substr(colon+1));
                require(used==idText.size() && id<=std::numeric_limits<ParamID>::max() && value>=0 && value<=1,"Invalid parameter ID or normalized value");
                values[ParamID(id)]=value;
            }
            else if(arg=="--set-text" && i+1<argc) {
                const std::string item=argv[++i]; const auto colon=item.find(':');
                require(colon!=std::string::npos,"Text parameter setting needs ID:DISPLAY");
                const auto idText=item.substr(0,colon); size_t used=0;
                const auto id=std::stoull(idText,&used,idText.rfind("0x",0)==0?16:10);
                require(used==idText.size() && id<=std::numeric_limits<ParamID>::max(),"Invalid text parameter ID");
                textValues[ParamID(id)]=item.substr(colon+1);
            }
            else throw std::runtime_error("Unknown option: "+arg);
        }
        require(list || (!input.empty() && !output.empty()),"Specify --list, or --input and --output");
        require(list || input!=output,"Input and output WAV paths must differ");
        {
            Host host; Module module(plugin); Session session(module,host);
            for(const auto& setting:textValues) {
                String128 text{}; UString(text,128).fromAscii(setting.second.c_str()); ParamValue normalized=0;
                require(session.controller->getParamValueByString(setting.first,text,normalized)==kResultOk,"Plugin rejected display-value parsing for ID "+std::to_string(setting.first));
                require(std::isfinite(normalized) && normalized>=0 && normalized<=1,"Plugin parsed an invalid normalized value");
                values[setting.first]=normalized;
            }
            session.apply(values);
            std::ofstream file;
            if(!jsonPath.empty()){file.open(jsonPath);require(bool(file),"Cannot create JSON output");}
            std::ostream& report=jsonPath.empty()?std::cout:file;
            if(list)session.list(report);
            else render(session,readWave(input),output,values,tail,tempo,block,channels,keepLatency,report);
            require(bool(report),"JSON output write failed");
        }
        if(SUCCEEDED(comResult))CoUninitialize();
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "Reference VST3 host: " << error.what() << '\n';
        if(SUCCEEDED(comResult))CoUninitialize();
        return 1;
    }
}

