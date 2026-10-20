// Regression test against the real Controller and shared preset registry.
// No copied preset values: loading, changing context, editing sound and restoring
// a component chunk must all agree on the current displayed preset identity.
#include "plugin/IrisPlugin.h"
#include "pluginterfaces/base/ustring.h"
#include "fixtures/state04_literal.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
// The editor object expects its module handle from VST3's DLL entry point.
// This Controller-only executable has an EXE entry point, so it supplies its
// own real module handle. Native window behavior is tested by the dynamic host.
HINSTANCE ghInst = nullptr;
#endif

using namespace Steinberg;
using namespace iris::vst3;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Stream final : IBStream {
    std::vector<unsigned char> bytes;
    size_t at = 0;
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API queryInterface(const TUID iid, void** out) override {
        if (FUnknownPrivate::iidEqual(iid, IBStream::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *out = static_cast<IBStream*>(this); return kResultOk;
        }
        *out = nullptr; return kNoInterface;
    }
    tresult PLUGIN_API read(void* data, int32 count, int32* actual=nullptr) override {
        if (count < 0 || (!data && count)) return kInvalidArgument;
        const size_t n = std::min<size_t>(count, at <= bytes.size() ? bytes.size()-at : 0);
        if (n) std::memcpy(data, bytes.data()+at, n);
        at += n; if(actual) *actual=static_cast<int32>(n);
        return n==static_cast<size_t>(count) ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API write(void* data, int32 count, int32* actual=nullptr) override {
        if (count < 0 || (!data && count)) return kInvalidArgument;
        bytes.resize(std::max(bytes.size(), at+static_cast<size_t>(count)));
        if (count) std::memcpy(bytes.data()+at, data, count);
        at += count; if(actual) *actual=count; return kResultOk;
    }
    tresult PLUGIN_API seek(int64 offset, int32 mode, int64* result=nullptr) override {
        const int64 origin = mode==kIBSeekSet ? 0 : mode==kIBSeekCur ? at : mode==kIBSeekEnd ? bytes.size() : -1;
        if(origin<0 || origin+offset<0) return kInvalidArgument;
        at=static_cast<size_t>(origin+offset); if(result) *result=at; return kResultOk;
    }
    tresult PLUGIN_API tell(int64* result) override { if(!result)return kInvalidArgument; *result=at; return kResultOk; }
};
}
int main() {
    try {
#if defined(_WIN32)
        ghInst = GetModuleHandleW(nullptr);
#endif
        auto controller = owned(new Controller);
        require(controller->initialize(nullptr)==kResultOk, "Controller initialization failed");
        require(controller->currentPresetIndex()==0, "Initial real values must display Init");
        require(controller->getParamNormalized(kEngineMode)==1,"New Controller did not start with Prism");
        for (int mode : {0,1}) {
            Steinberg::Vst::String128 value{}; char text[128]{};
            require(controller->getParamStringByValue(kEngineMode,mode,value)==kResultOk,"Core label conversion failed");
            UString(value,128).toAscii(text,128);
            require(std::strcmp(text,mode ? "Prism" : "Legacy")==0,"Core parameter does not expose its actual engine choices");
        }
        bool foundMode=false;
        for (int i=0;i<controller->getParameterCount();++i) {
            Steinberg::Vst::ParameterInfo info{};require(controller->getParameterInfo(i,info)==kResultOk,"Parameter info failed");
            if (info.id==kEngineMode) {
                foundMode=true;require(info.defaultNormalizedValue==1 && info.stepCount==1,
                    "Host reset metadata must select Prism from a two-choice list");
            }
        }
        require(foundMode,"Core parameter is not published to the host");
        int selections=0, soundEdits=0;
        for(int mode : {0,1}) for(int preset=0; preset<iris::kPresetCount; ++preset) {
            auto context=defaults();
            context[kRoot]=normalized(kRoot,7); context[kScale]=normalized(kScale,5);
            context[kCustomC]=0; context[kCustomC+1]=1; context[kMidiEnabled]=1;
            context[kBypass]=1; context[kOutputDb]=normalized(kOutputDb,-13.25); context[kSoftLimit]=0;
            context[kEngineMode]=mode;
            controller->applySnapshot(context);
            controller->loadPreset(preset);
            require(controller->currentPresetIndex()==preset, "Selected preset name did not track actual values");
            require(iris::presetIndex(iris::kPresetKeys[preset])==preset, "CLI preset key disagrees with registry");
            const auto loaded=controller->snapshot();
            for(const Param id : {kRoot,kScale,kMidiEnabled,kBypass,kOutputDb,kSoftLimit,kCustomC,kEngineMode})
                require(loaded[id]==context[id], "Loading timbre changed musical/output context");
            Stream saved; require(writeState(&saved,loaded), "Preset save failed");
            // Every user-controlled timbre parameter must invalidate the name
            // when changed by host automation, including switches and divisions.
            for(unsigned raw=0; raw<kParameterCount; ++raw) {
                if(!isSoundParameter(raw) || raw==kRoot || raw==kScale || raw==kMidiEnabled ||
                   raw==kBypass || raw==kOutputDb || raw==kSoftLimit || raw==kEngineMode ||
                   (raw>=kCustomC && raw<kCustomC+12)) continue;
                const auto id=static_cast<Param>(raw);
                const double altered = loaded[id] < .95 ? std::min(1.,loaded[id]+.05) : loaded[id]-.05;
                const double value=kSpecs[id].steps>0 ?
                    double((discrete(id,loaded[id])+1)%(kSpecs[id].steps+1))/kSpecs[id].steps : altered;
                controller->setParamNormalized(id,value);
                require(controller->currentPresetIndex()==-1, "Host sound edit left a stale preset name");
                controller->setParamNormalized(id,loaded[id]);
                require(controller->currentPresetIndex()==preset, "Restoring sound did not recover preset name");
                ++soundEdits;
            }
            controller->edit(kColorLow,.12);
            require(controller->currentPresetIndex()==-1, "Manual band edit must display Custom");
            saved.at=0;
            require(controller->setComponentState(&saved)==kResultOk, "Component preset restore failed");
            require(controller->currentPresetIndex()==preset, "Component restore lost preset identity");
            // A newly created controller stands in for reopening a host's
            // controller session; identity is derived, not stored only in a view.
            auto reopened=owned(new Controller);
            require(reopened->initialize(nullptr)==kResultOk, "Reopened controller initialization failed");
            saved.at=0; require(reopened->setComponentState(&saved)==kResultOk, "Reopened state restore failed");
            require(reopened->currentPresetIndex()==preset, "New controller forgot restored preset name");
            reopened->terminate(); ++selections;
            controller->edit(kEngineMode,1-mode);
            require(controller->currentPresetIndex()==preset,"Explicit Core switch lost the current preset family name");
        }
        Stream old;old.bytes.assign(iris05_fixture::state04.begin(),iris05_fixture::state04.end());
        require(controller->setComponentState(&old)==kResultOk && controller->getParamNormalized(kEngineMode)==0,
                "Controller restored a 0.4 chunk into the new core");
        controller->loadPreset(1);
        require(controller->getParamNormalized(kEngineMode)==0 && controller->currentPresetIndex()==1,
                "Selecting a preset after old-state restore silently upgraded the core");
        const auto before=controller->snapshot();
        controller->loadPreset(-1); controller->loadPreset(iris::kPresetCount);
        require(controller->snapshot()==before, "Invalid selection modified sound");
        require(std::strcmp(iris::presetName(-1),"Custom")==0, "Custom label missing");
        controller->terminate();
        std::cout << "PASS  " << selections << " real Controller presets: load, context preservation, component restore and new controller restore\n";
        std::cout << "PASS  " << soundEdits << " host sound edits invalidate and restore actual preset identity\n";
        std::cout << "PASS  invalid preset indices leave all controls unchanged\n";
        std::cout << "PASS  Core labels, host-reset default, explicit mode changes, and old-state/preset mode retention\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << "FAIL  " << e.what() << '\n'; return 1; }
}
