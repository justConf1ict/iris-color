#include "IrisParameters.h"
#include "base/source/fstreamer.h"

namespace iris::vst3 {
namespace {
constexpr Steinberg::uint32 kStateMagic = 0x53495249; // "IRIS" little endian
constexpr Steinberg::uint32 kStateVersion = 1;
}
bool readState(Steinberg::IBStream* stream, Values& values) {
    if (!stream) return false;
    Steinberg::IBStreamer reader(stream, kLittleEndian);
    Steinberg::uint32 magic = 0, version = 0, count = 0;
    if (!reader.readInt32u(magic) || magic != kStateMagic ||
        !reader.readInt32u(version) || version != kStateVersion ||
        !reader.readInt32u(count) || count > 1024) return false;
    Values decoded = defaults();
    // A missing Core record denotes a project/preset written before 0.5.
    // This differs deliberately from a brand-new instance's Prism default.
    // Detect the actual record, not its count: older chunks may carry unknown
    // future IDs, and a sparse new chunk may contain only Core.
    decoded[kEngineMode] = 0.0;
    // Old projects did not opt into the v0.6 tonal balance. Only an actual
    // Clarity record enables it, independently of new-instance defaults.
    decoded[kClarity] = 0.0;
    // Body is opt-in in both new instances and all pre-0.7 sessions.
    // Detect its actual record independently of unknown future records.
    decoded[kBodyMix] = 0.0;
    for (Steinberg::uint32 i = 0; i < count; ++i) {
        Steinberg::uint32 id = 0;
        double value = 0;
        if (!reader.readInt32u(id) || !reader.readDouble(value) || !std::isfinite(value)) return false;
        if (isSoundParameter(id)) decoded[id] = sanitize(id, value);
    }
    values = decoded;
    return true;
}
bool writeState(Steinberg::IBStream* stream, const Values& values) {
    if (!stream) return false;
    Steinberg::IBStreamer writer(stream, kLittleEndian);
    if (!writer.writeInt32u(kStateMagic) || !writer.writeInt32u(kStateVersion) ||
        !writer.writeInt32u(kSoundParameterCount)) return false;
    for (Steinberg::uint32 id = 0; id < kParameterCount; ++id)
        if (isSoundParameter(id) &&
            (!writer.writeInt32u(id) || !writer.writeDouble(sanitize(id, values[id])))) return false;
    return true;
}
} // namespace iris::vst3
