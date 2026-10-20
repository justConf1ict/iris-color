#include "IrisPlugin.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <cstring>
#include <limits>

namespace iris::vst3 {
using namespace Steinberg;
using namespace Steinberg::Vst;

Processor::Processor() {
    setControllerClass(kControllerUID);
    processContextRequirements.flags = IProcessContextRequirements::kNeedTempo |
        IProcessContextRequirements::kNeedProjectTimeMusic | IProcessContextRequirements::kNeedTransportState;
    auto initial = defaults();
    for (std::size_t i = 0; i < initial.size(); ++i) values_[i].store(initial[i], std::memory_order_relaxed);
}
tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    const auto result = AudioEffect::initialize(context);
    if (result != kResultOk) return result;
    addAudioInput(STR16("Input"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Output"), SpeakerArr::kStereo);
    addEventInput(STR16("MIDI Notes"), 16);
    return kResultOk;
}
tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* ins, int32 numIns,
                                                 SpeakerArrangement* outs, int32 numOuts) {
    if (!ins || !outs || numIns != 1 || numOuts != 1 || ins[0] != outs[0] ||
        (ins[0] != SpeakerArr::kStereo && ins[0] != SpeakerArr::kMono)) return kResultFalse;
    return AudioEffect::setBusArrangements(ins, numIns, outs, numOuts);
}
tresult PLUGIN_API Processor::canProcessSampleSize(int32 size) {
    return size == kSample32 || size == kSample64 ? kResultTrue : kResultFalse;
}
tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    if (!std::isfinite(setup.sampleRate) || setup.sampleRate < 8000 || setup.sampleRate > 192000 ||
        setup.maxSamplesPerBlock < 1 || setup.maxSamplesPerBlock > 1048576 ||
        canProcessSampleSize(setup.symbolicSampleSize) != kResultTrue) return kInvalidArgument;
    prepared_ = false;
    try {
        for (auto& buffer : scratch_) buffer.assign(static_cast<std::size_t>(setup.maxSamplesPerBlock), 0.f);
        engine_.prepare(setup.sampleRate, 2048, 1024);
    } catch (...) { return kOutOfMemory; }
    sampleRate_ = setup.sampleRate;
    visual_.clear(); publishedVisualFrame_ = 0;
    Values values{};
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = values_[i].load(std::memory_order_relaxed);
    updateDSP(values);
    latency_.store(static_cast<uint32>(engine_.latencySamples()), std::memory_order_relaxed);
    prepared_ = true;
    return AudioEffect::setupProcessing(setup);
}
tresult PLUGIN_API Processor::setActive(TBool state) {
    if (prepared_) { engine_.reset(); engine_.allNotesOff(); visual_.clear(); publishedVisualFrame_ = 0; }
    return AudioEffect::setActive(state);
}
tresult PLUGIN_API Processor::setProcessing(TBool state) {
    if (state && !prepared_) return kNotInitialized;
    if (prepared_) { engine_.reset(); engine_.allNotesOff(); visual_.clear(); publishedVisualFrame_ = 0; }
    return kResultOk;
}
void Processor::updateDSP(const Values& values) noexcept {
    engine_.setParameters(toDSP(values));
    const double samples = std::ceil(engine_.tailSeconds() * sampleRate_);
    tail_.store(static_cast<uint32>(std::clamp(samples, 0.0,
        static_cast<double>(std::numeric_limits<uint32>::max() - 1))), std::memory_order_relaxed);
}
tresult PLUGIN_API Processor::setState(IBStream* stream) {
    Values decoded{};
    if (!readState(stream, decoded)) return kResultFalse;
    for (std::size_t i = 0; i < decoded.size(); ++i) values_[i].store(decoded[i], std::memory_order_relaxed);
    return kResultOk;
}
tresult PLUGIN_API Processor::getState(IBStream* stream) {
    Values values{};
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = values_[i].load(std::memory_order_relaxed);
    return writeState(stream, values) ? kResultOk : kResultFalse;
}
void Processor::processSpan(ProcessData& data, int offset, int count) noexcept {
    if (!prepared_ || data.numOutputs < 1 || !data.outputs || data.outputs[0].numChannels < 1) return;
    auto& output = data.outputs[0];
    const auto* input = data.numInputs > 0 && data.inputs ? &data.inputs[0] : nullptr;
    const int inputChannels = input ? input->numChannels : 0;
    const int outputChannels = std::min<int>(output.numChannels, 2);
    const int capacity = static_cast<int>(scratch_[0].size());
    for (int done = 0; done < count;) {
        const int n = std::min(capacity, count - done);
        const int at = offset + done;
        // VST PPQ describes the callback's first sample. Every automation/event
        // subspan needs its own offset, including internal scratch-buffer chunks.
        const auto* context = data.processContext;
        const bool tempoValid = context && (context->state & ProcessContext::kTempoValid) &&
            std::isfinite(context->tempo) && context->tempo >= 1.0 && context->tempo <= 1000.0;
        const double tempo = tempoValid ? context->tempo : 120.0;
        const bool positionValid = tempoValid && (context->state & ProcessContext::kProjectTimeMusicValid) &&
            std::isfinite(context->projectTimeMusic) && std::abs(context->projectTimeMusic) <= 1.0e9;
        const bool playing = !context || (context->state & ProcessContext::kPlaying);
        const double quarterNotePosition = positionValid ? context->projectTimeMusic +
            (playing ? double(at) * tempo / (60.0 * sampleRate_) : 0.0) : 0.0;
        engine_.setTransport(tempo, quarterNotePosition, playing, positionValid);
        for (int channel = 0; channel < 2; ++channel) {
            const int source = inputChannels == 1 ? 0 : channel;
            const bool silent = !input || source >= inputChannels || ((input->silenceFlags >> source) & 1);
            if (data.symbolicSampleSize == kSample32) {
                const float* in = !silent && input->channelBuffers32 ? input->channelBuffers32[source] : nullptr;
                for (int i = 0; i < n; ++i) scratch_[channel][i] = in ? in[at + i] : 0.f;
            } else {
                const double* in = !silent && input->channelBuffers64 ? input->channelBuffers64[source] : nullptr;
                for (int i = 0; i < n; ++i) scratch_[channel][i] = in ? static_cast<float>(in[at + i]) : 0.f;
            }
        }
        engine_.process(scratch_[0].data(), scratch_[1].data(), scratch_[2].data(), scratch_[3].data(), n);
        VisualFrame frame;
        engine_.copyVisual(frame);
        if (frame.frameCounter && frame.frameCounter != publishedVisualFrame_) {
            visual_.publish(frame);
            publishedVisualFrame_ = frame.frameCounter;
        }
        for (int channel = 0; channel < outputChannels; ++channel) {
            if (data.symbolicSampleSize == kSample32) {
                float* out = output.channelBuffers32 ? output.channelBuffers32[channel] : nullptr;
                if (out) for (int i = 0; i < n; ++i)
                    out[at + i] = outputChannels == 1 ? 0.5f * (scratch_[2][i] + scratch_[3][i]) : scratch_[2 + channel][i];
            } else {
                double* out = output.channelBuffers64 ? output.channelBuffers64[channel] : nullptr;
                if (out) for (int i = 0; i < n; ++i)
                    out[at + i] = outputChannels == 1 ? 0.5 * (scratch_[2][i] + scratch_[3][i]) : scratch_[2 + channel][i];
            }
        }
        done += n;
    }
    output.silenceFlags = 0; // The spectral tail can sound even when the input is silent.
}
tresult PLUGIN_API Processor::process(ProcessData& data) {
    if (data.numSamples < 0 || canProcessSampleSize(data.symbolicSampleSize) != kResultTrue) return kInvalidArgument;
    Values values{};
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = values_[i].load(std::memory_order_relaxed);
    if (prepared_) updateDSP(values);

    struct Cursor { IParamValueQueue* queue = nullptr; int32 point = 0, count = 0, offset = 0; double value = 0; };
    std::array<Cursor, kTotalParameterCount> cursors{};
    auto advance = [](Cursor& c) {
        while (c.point < c.count) {
            if (c.queue->getPoint(c.point, c.offset, c.value) == kResultOk) return;
            ++c.point;
        }
    };
    if (data.inputParameterChanges) {
        const int count = data.inputParameterChanges->getParameterCount();
        for (int i = 0; i < count; ++i) {
            auto* q = data.inputParameterChanges->getParameterData(i);
            if (!q || q->getParameterId() >= kTotalParameterCount) continue;
            auto& c = cursors[q->getParameterId()];
            c.queue = q; c.count = q->getPointCount();
            advance(c);
        }
    }
    int eventIndex = 0;
    const int eventCount = data.inputEvents ? data.inputEvents->getEventCount() : 0;
    Event event{};
    bool hasEvent = false;
    auto nextEvent = [&]() {
        hasEvent = false;
        while (eventIndex < eventCount)
            if (data.inputEvents->getEvent(eventIndex++, event) == kResultOk) { hasEvent = true; return; }
    };
    nextEvent();
    int position = 0;
    while (true) {
        bool changed = false;
        for (uint32 id = 0; id < kTotalParameterCount; ++id) {
            auto& c = cursors[id];
            while (c.point < c.count && c.offset <= position) {
                if (isSoundParameter(id)) {
                    values[id] = sanitize(id, c.value);
                    values_[id].store(values[id], std::memory_order_relaxed);
                    changed = true;
                } else if (prepared_ && (id == kAllNotesOff || id == kAllSoundsOff)) engine_.allNotesOff();
                ++c.point;
                advance(c);
            }
        }
        if (changed && prepared_) updateDSP(values);
        while (hasEvent && event.sampleOffset <= position) {
            if (prepared_ && event.busIndex == 0) {
                if (event.type == Event::kNoteOnEvent && event.noteOn.pitch >= 0 && event.noteOn.pitch <= 127) {
                    if (event.noteOn.velocity > 0.f) engine_.noteOn(event.noteOn.pitch);
                    else engine_.noteOff(event.noteOn.pitch);
                } else if (event.type == Event::kNoteOffEvent) engine_.noteOff(event.noteOff.pitch);
                else if (event.type == Event::kLegacyMIDICCOutEvent &&
                    (event.midiCCOut.controlNumber == kCtrlAllNotesOff || event.midiCCOut.controlNumber == kCtrlAllSoundsOff))
                    engine_.allNotesOff();
            }
            nextEvent();
        }
        if (position >= data.numSamples) break;
        int boundary = data.numSamples;
        for (const auto& c : cursors)
            if (c.point < c.count && c.offset > position) boundary = std::min<int>(boundary, c.offset);
        if (hasEvent && event.sampleOffset > position) boundary = std::min<int>(boundary, event.sampleOffset);
        processSpan(data, position, boundary - position);
        position = boundary;
    }
    return kResultOk;
}

tresult PLUGIN_API Processor::notify(IMessage* message) {
    if (!message || !message->getMessageID()) return kInvalidArgument;
    if (std::strcmp(message->getMessageID(), kVisualRequest) != 0) return AudioEffect::notify(message);
    VisualFrame frame;
    if (!visual_.take(frame) || !frame.frameCounter) return kResultOk;
    if (auto reply = owned(allocateMessage())) {
        reply->setMessageID(kVisualMessage);
        auto* a = reply->getAttributes();
        if (!a) return kResultFalse;
        a->setInt("frame", static_cast<int64>(frame.frameCounter));
        a->setInt("mask", frame.activeMask);
        a->setInt("moveCount", frame.moveCount);
        a->setFloat("sampleRate", frame.sampleRate);
        a->setFloat("attackTailGain", frame.attackTailGain);
        a->setFloat("diveSemitones", frame.diveSemitones);
        a->setFloat("tamerReductionDb", frame.tamerReductionDb);
        a->setFloat("formantVowel", frame.formantVowel);
        a->setInt("motionPitchClass", frame.motionPitchClass);
        a->setInt("motionHostSynced", frame.motionHostSynced ? 1 : 0);
        a->setBinary("dry", frame.dryDb.data(), static_cast<uint32>(sizeof(frame.dryDb)));
        a->setBinary("wet", frame.wetDb.data(), static_cast<uint32>(sizeof(frame.wetDb)));
        a->setBinary("gate", frame.gateGain.data(), static_cast<uint32>(sizeof(frame.gateGain)));
        static_assert(sizeof(VisualMove) == 3 * sizeof(float), "Wire move format has no padding");
        a->setBinary("moves", frame.moves.data(), static_cast<uint32>(sizeof(frame.moves)));
        return sendMessage(reply);
    }
    return kResultFalse;
}

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    const auto result = EditController::initialize(context);
    if (result != kResultOk) return result;
    for (uint32 id = 0; id < kParameterCount; ++id) {
        if (!isSoundParameter(id)) continue;
        String128 title{}, units{};
        UString(title, 128).fromAscii(kSpecs[id].name);
        UString(units, 128).fromAscii(kSpecs[id].units);
        if (id == kRoot || id == kScale || id == kScaleMotionDivision || id == kEngineMode || id == kBodyMaterial) {
            auto* p = new StringListParameter(title, id);
            const int size = id == kRoot ? 12 : id == kEngineMode ? 2 : id == kBodyMaterial ? 4 : 6;
            const char* const* labels = id == kRoot ? kNoteNames : id == kScale ? kScaleNames :
                id == kEngineMode ? kEngineModeNames : id == kBodyMaterial ? kBodyMaterialNames : kMotionDivisionNames;
            for (int i = 0; i < size; ++i) {
                String128 label{};
                UString(label, 128).fromAscii(labels[i]);
                p->appendString(label);
            }
            // StringListParameter's constructor fixes its metadata default at
            // zero. A nonzero initial choice must update both host metadata and
            // the current value; otherwise a host reset selects the wrong
            // timing division or silently switches the processing core.
            p->getInfo().defaultNormalizedValue = normalized(id, kSpecs[id].initial);
            p->setNormalized(p->getInfo().defaultNormalizedValue);
            parameters.addParameter(p);
        } else {
            int flags = ParameterInfo::kCanAutomate;
            if (id == kBypass) flags |= ParameterInfo::kIsBypass;
            auto* p = new RangeParameter(title, id, units, kSpecs[id].minimum, kSpecs[id].maximum,
                kSpecs[id].initial, kSpecs[id].steps, flags);
            p->setPrecision(id == kOutputDb || id == kGateDb || id >= kColorEqLowDb ? 1 : 0);
            parameters.addParameter(p);
        }
    }
    parameters.addParameter(STR16("MIDI All Notes Off"), nullptr, 0, 0., ParameterInfo::kIsHidden, kAllNotesOff);
    parameters.addParameter(STR16("MIDI All Sounds Off"), nullptr, 0, 0., ParameterInfo::kIsHidden, kAllSoundsOff);
    return kResultOk;
}
tresult PLUGIN_API Controller::setComponentState(IBStream* stream) {
    Values values{};
    if (!readState(stream, values)) return kResultFalse;
    for (uint32 id = 0; id < kParameterCount; ++id)
        if (isSoundParameter(id)) setParamNormalized(id, values[id]);
    return kResultOk;
}
IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    return name && std::strcmp(name, ViewType::kEditor) == 0 ? createNativeEditor(this) : nullptr;
}
tresult PLUGIN_API Controller::getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber number, ParamID& id) {
    if (bus != 0 || channel < 0 || channel >= 16) return kResultFalse;
    if (number == kCtrlAllNotesOff) { id = kAllNotesOff; return kResultTrue; }
    if (number == kCtrlAllSoundsOff) { id = kAllSoundsOff; return kResultTrue; }
    return kResultFalse;
}
Values Controller::snapshot() {
    Values values{};
    for (uint32 id = 0; id < kParameterCount; ++id) values[id] = sanitize(id, getParamNormalized(id));
    return values;
}
void Controller::edit(ParamID id, double value) {
    if (!isSoundParameter(id)) return;
    value = sanitize(id, value);
    beginEdit(id); setParamNormalized(id, value); performEdit(id, value); endEdit(id);
}
void Controller::applySnapshot(const Values& values) {
    startGroupEdit();
    for (uint32 id = 0; id < kParameterCount; ++id) edit(id, values[id]);
    finishGroupEdit();
}
void Controller::loadPreset(int index) {
    if (index < 0 || index >= iris::kPresetCount) return;
    const Values before = snapshot();
    Values after = fromDSP(iris::presetParameters(index));
    // Loading a timbre must not change the composition's key, MIDI setup,
    // bypass state, output trim/limiter, or the explicitly selected core.
    // In particular, choosing a preset in an old project must stay Legacy.
    for (const Param id : {kRoot, kScale, kMidiEnabled, kBypass, kOutputDb, kSoftLimit, kEngineMode})
        after[id] = before[id];
    for (unsigned note = 0; note < 12; ++note) after[kCustomC + note] = before[kCustomC + note];
    applySnapshot(after);
}
int Controller::currentPresetIndex() { return iris::matchingPreset(toDSP(snapshot())); }
void Controller::requestVisual() { sendMessageID(kVisualRequest); }
bool Controller::visualSnapshot(VisualFrame& frame) const noexcept { return visual_.take(frame); }
tresult PLUGIN_API Controller::notify(IMessage* message) {
    if (!message || !message->getMessageID()) return kInvalidArgument;
    if (std::strcmp(message->getMessageID(), kVisualMessage) != 0) return EditController::notify(message);
    auto* a = message->getAttributes();
    if (!a) return kResultFalse;
    VisualFrame frame;
    int64 number = 0, mask = 0, count = 0;
    double rate = 0;
    if (a->getInt("frame", number) != kResultOk || number <= 0 ||
        a->getInt("mask", mask) != kResultOk || mask < 0 || mask > 4095 ||
        a->getInt("moveCount", count) != kResultOk || count < 0 || count > 32 ||
        a->getFloat("sampleRate", rate) != kResultOk || !std::isfinite(rate) || rate < 8000 || rate > 192000)
        return kResultFalse;
    auto readArray = [&](const char* name, auto& result, float lo, float hi) {
        const void* data = nullptr; uint32 size = 0;
        if (a->getBinary(name, data, size) != kResultOk || !data || size != sizeof(result)) return false;
        std::memcpy(result.data(), data, size);
        for (float value : result) if (!std::isfinite(value) || value < lo || value > hi) return false;
        return true;
    };
    if (!readArray("dry", frame.dryDb, -240.f, 120.f) ||
        !readArray("wet", frame.wetDb, -240.f, 120.f) ||
        !readArray("gate", frame.gateGain, 0.f, 1.f)) return kResultFalse;
    const void* moves = nullptr; uint32 size = 0;
    if (a->getBinary("moves", moves, size) != kResultOk || !moves || size != sizeof(frame.moves)) return kResultFalse;
    std::memcpy(frame.moves.data(), moves, size);
    for (int64 i = 0; i < count; ++i) {
        const auto& m = frame.moves[static_cast<std::size_t>(i)];
        if (!std::isfinite(m.sourceHz) || !std::isfinite(m.targetHz) || !std::isfinite(m.levelDb) ||
            m.sourceHz <= 0 || m.targetHz <= 0 || m.sourceHz > rate / 2 || m.targetHz > rate / 2 ||
            m.levelDb < -240 || m.levelDb > 120) return kResultFalse;
    }
    frame.frameCounter = static_cast<std::uint64_t>(number);
    frame.activeMask = static_cast<std::uint16_t>(mask);
    frame.moveCount = static_cast<std::uint16_t>(count);
    frame.sampleRate = static_cast<float>(rate);
    // Optional fields keep the original v1 array protocol compatible. Readable
    // fields outside the finite/range contract reject the complete snapshot.
    auto readCreativeFloat = [&](const char* key, float& target, double lo, double hi) {
        double value = 0;
        if (a->getFloat(key, value) != kResultOk) return true;
        if (!std::isfinite(value) || value < lo || value > hi) return false;
        target = static_cast<float>(value); return true;
    };
    if (!readCreativeFloat("attackTailGain", frame.attackTailGain, 0, 2.01) ||
        !readCreativeFloat("diveSemitones", frame.diveSemitones, -24.01, 24.01) ||
        !readCreativeFloat("tamerReductionDb", frame.tamerReductionDb, 0, 18.01) ||
        !readCreativeFloat("formantVowel", frame.formantVowel, 0, 4.001)) return kResultFalse;
    int64 pitchClass = -1, hostSync = 0;
    if (a->getInt("motionPitchClass", pitchClass) == kResultOk) {
        if (pitchClass < -1 || pitchClass > 11) return kResultFalse;
        frame.motionPitchClass = static_cast<int16_t>(pitchClass);
    }
    if (a->getInt("motionHostSynced", hostSync) == kResultOk) {
        if (hostSync < 0 || hostSync > 1) return kResultFalse;
        frame.motionHostSynced = hostSync != 0;
    }
    visual_.publish(frame);
    return kResultOk;
}

} // namespace iris::vst3
