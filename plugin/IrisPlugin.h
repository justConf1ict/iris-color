#pragma once

#include "IrisParameters.h"
#include "src/IrisPresets.h"
#include "IrisVisual.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <atomic>
#include <vector>

namespace iris::vst3 {

static_assert(std::atomic<double>::is_always_lock_free, "Iris Color requires lock-free x64 parameter snapshots");

inline const Steinberg::FUID kProcessorUID(0x29F66820, 0xA54B492E, 0xB25A7E0B, 0x1D4982CF);
inline const Steinberg::FUID kControllerUID(0x51078DA9, 0x7F4A4F35, 0x9DC31A07, 0x875E0AD4);

class Processor final : public Steinberg::Vst::AudioEffect {
public:
    Processor();
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor);
    }
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement*, Steinberg::int32,
                                                   Steinberg::Vst::SpeakerArrangement*, Steinberg::int32) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup&) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool) override;
    Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData&) override;
    Steinberg::uint32 PLUGIN_API getLatencySamples() override { return latency_.load(std::memory_order_relaxed); }
    Steinberg::uint32 PLUGIN_API getTailSamples() override { return tail_.load(std::memory_order_relaxed); }
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage*) override;
private:
    void updateDSP(const Values&) noexcept;
    void processSpan(Steinberg::Vst::ProcessData&, int offset, int count) noexcept;
    Engine engine_;
    std::array<std::atomic<double>, kParameterCount> values_;
    std::array<std::vector<float>, 4> scratch_;
    std::atomic<Steinberg::uint32> latency_{0}, tail_{0};
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
    VisualMailbox visual_;
    std::uint64_t publishedVisualFrame_ = 0;
};

class Controller final : public Steinberg::Vst::EditController, public Steinberg::Vst::IMidiMapping {
public:
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller);
    }
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown*) override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream*) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(Steinberg::int32 bus, Steinberg::int16 channel,
        Steinberg::Vst::CtrlNumber number, Steinberg::Vst::ParamID& id) override;
    Values snapshot();
    void edit(Steinberg::Vst::ParamID id, double value);
    void applySnapshot(const Values&);
    void loadPreset(int index);
    int currentPresetIndex();
    // Called by the native editor timer. Host messaging stays off the audio
    // callback; incoming frames may be delivered asynchronously by the host.
    void requestVisual();
    bool visualSnapshot(VisualFrame& frame) const noexcept;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage*) override;

    OBJ_METHODS(Controller, Steinberg::Vst::EditController)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
    END_DEFINE_INTERFACES(Steinberg::Vst::EditController)
    REFCOUNT_METHODS(Steinberg::Vst::EditController)
private:
    VisualMailbox visual_;
};

Steinberg::IPlugView* createNativeEditor(Controller* controller);

} // namespace iris::vst3
