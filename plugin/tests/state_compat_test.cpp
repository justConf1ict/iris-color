// Independent 0.2 -> 0.3 state/ID regression. Link this file with the real
// IrisParameters.cpp and the SDK support used by the native host test.
// The legacy fixture is literal v1 wire data, not output from the new encoder.
#include "plugin/IrisParameters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace iris::vst3;

// Existing host tests compare mappings with these enums. Pin the wire numbers
// here so moving both an enum and its host assertion cannot silently pass.
static_assert(kAllNotesOff == 29, "CC123 automation must retain parameter ID 29");
static_assert(kAllSoundsOff == 30, "CC120 automation must retain parameter ID 30");
static_assert(kColorLow == 31 && kColorMid == 32 && kColorHigh == 33,
              "Band resonance parameter IDs are persistent");
static_assert(kColorLowMidHz == 34 && kColorMidHighHz == 35,
              "Crossover parameter IDs are persistent");
static_assert(kColorEqLowDb == 36 && kColorEqMidDb == 37 && kColorEqHighDb == 38,
              "Color EQ parameter IDs are persistent");

namespace {
constexpr std::array<std::uint8_t, 360> kLegacy02 {{
    0x49, 0x52, 0x49, 0x53, 0x01, 0x00, 0x00, 0x00, 0x1d, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0xeb, 0x3f,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f,
    0x02, 0x00, 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0xd6, 0x3f,
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x3f,
    0x04, 0x00, 0x00, 0x00, 0x19, 0xe5, 0x48, 0x74, 0xf6, 0x7c, 0xb0, 0x3f,
    0x05, 0x00, 0x00, 0x00, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0xb5, 0x3f,
    0x06, 0x00, 0x00, 0x00, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0xda, 0x3f,
    0x07, 0x00, 0x00, 0x00, 0x9a, 0x99, 0x99, 0x99, 0x99, 0x99, 0xc9, 0x3f,
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe6, 0x3f,
    0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x3f,
    0x0a, 0x00, 0x00, 0x00, 0x17, 0x5d, 0x74, 0xd1, 0x45, 0x17, 0xdd, 0x3f,
    0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f,
    0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f,
    0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f,
    0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f,
    0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f,
    0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f,
    0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f,
    0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x1b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f,
}};
// Sound values correspond to 0.2 Gun, with deliberately nondefault context:
// Root F, Custom E/G/B, MIDI on. These expectations do not use new defaults.
constexpr std::array<double, 29> kLegacyValues {{
    .85, 1., .35, .5, 19./295., 1./12., 7./17., .2, .6875, .5,
    5./11., 1., 1., 1., 1., 0., 1.,
    0., 0., 0., 0., 1., 0., 0., 1., 0., 0., 0., 1.
}};

void require(bool value, const std::string& what) {
    if (!value) throw std::runtime_error(what);
}
void pass(const char* what) { std::printf("PASS %s\n", what); }

class MemoryStream final : public IBStream {
public:
    std::vector<std::uint8_t> bytes;
    std::size_t position = 0;
    uint32 references = 1;
    MemoryStream() = default;
    explicit MemoryStream(std::vector<std::uint8_t> data) : bytes(std::move(data)) {}
    uint32 PLUGIN_API addRef() override { return ++references; }
    uint32 PLUGIN_API release() override { return references ? --references : 0; }
    tresult PLUGIN_API queryInterface(const TUID id, void** result) override {
        if (!result) return kInvalidArgument;
        *result = nullptr;
        if (FUnknownPrivate::iidEqual(id, IBStream::iid) ||
            FUnknownPrivate::iidEqual(id, FUnknown::iid)) {
            *result = static_cast<IBStream*>(this); addRef(); return kResultOk;
        }
        return kNoInterface;
    }
    tresult PLUGIN_API read(void* data, int32 count, int32* actual = nullptr) override {
        if (count < 0 || (!data && count)) return kInvalidArgument;
        const auto n = std::min<std::size_t>(static_cast<std::size_t>(count),
            position <= bytes.size() ? bytes.size() - position : 0);
        if (n) std::memcpy(data, bytes.data() + position, n);
        position += n;
        if (actual) *actual = static_cast<int32>(n);
        return n == static_cast<std::size_t>(count) ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API write(void* data, int32 count, int32* actual = nullptr) override {
        if (count < 0 || (!data && count)) return kInvalidArgument;
        const auto n = static_cast<std::size_t>(count);
        if (position + n > bytes.size()) bytes.resize(position + n);
        if (n) std::memcpy(bytes.data() + position, data, n);
        position += n;
        if (actual) *actual = count;
        return kResultOk;
    }
    tresult PLUGIN_API seek(int64 offset, int32 mode, int64* result = nullptr) override {
        const int64 base = mode == kIBSeekSet ? 0 : mode == kIBSeekCur ? static_cast<int64>(position) :
            mode == kIBSeekEnd ? static_cast<int64>(bytes.size()) : -1;
        if (base < 0 || offset < -base || offset > std::numeric_limits<int64>::max() - base)
            return kInvalidArgument;
        position = static_cast<std::size_t>(base + offset);
        if (result) *result = static_cast<int64>(position);
        return kResultOk;
    }
    tresult PLUGIN_API tell(int64* result) override {
        if (!result) return kInvalidArgument;
        *result = static_cast<int64>(position); return kResultOk;
    }
};

std::vector<std::uint8_t> legacy() { return {kLegacy02.begin(), kLegacy02.end()}; }
std::uint32_t u32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
        (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
void put32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<std::uint8_t>(value >> (8*i));
}
void appendRecord(std::vector<std::uint8_t>& bytes, std::uint32_t id, double value) {
    const auto at = bytes.size(); bytes.resize(at + 12);
    put32(bytes, at, id);
    std::uint64_t bits = 0; std::memcpy(&bits, &value, sizeof(bits));
    for (unsigned i = 0; i < 8; ++i) bytes[at + 4 + i] = static_cast<std::uint8_t>(bits >> (8*i));
    put32(bytes, 8, u32(bytes.data() + 8) + 1);
}
double wireDouble(const std::uint8_t* p) {
    const std::uint64_t bits = u32(p) | (std::uint64_t(u32(p + 4)) << 32);
    double value = 0; std::memcpy(&value, &bits, sizeof(value)); return value;
}
void validateWrittenState(const MemoryStream& stream, const Values& expected) {
    require(stream.bytes.size() == 12 + 81 * 12, "Writer must emit exactly 81 sound records");
    require(u32(stream.bytes.data()) == 0x53495249 && u32(stream.bytes.data() + 4) == 1,
            "State envelope must remain IRIS v1");
    require(u32(stream.bytes.data() + 8) == 81, "Record count must exclude panic IDs");
    std::array<bool, 83> seen{};
    for (std::size_t at = 12; at < stream.bytes.size(); at += 12) {
        const auto id = u32(stream.bytes.data() + at);
        require(id < 83 && id != 29 && id != 30, "Writer serialized reserved or unknown ID");
        require(!seen[id], "Writer emitted duplicate sound ID"); seen[id] = true;
        require(wireDouble(stream.bytes.data() + at + 4) == expected[id], "Serialized value changed at ID " + std::to_string(id));
    }
    for (unsigned id = 0; id < seen.size(); ++id)
        require(seen[id] == (id < 29 || id >= 31), "Wrong wire ID coverage");
}
void rejectedWithoutMutation(const std::vector<std::uint8_t>& bytes, const char* label) {
    MemoryStream stream(bytes);
    Values values{}; values.fill(.123456789);
    const Values before = values;
    require(!readState(&stream, values), std::string(label) + ": malformed state accepted");
    require(values == before, std::string(label) + ": partial live snapshot mutation");
}
}

int main() {
    try {
        require(Values{}.size() == 83, "ID-addressed snapshot must include Body, Core, Clarity and 29/30 reserved slots");
        const Param originalIDs[] = {kColor,kSnap,kMorph,kGateDb,kDecayMs,kLowHz,kHighHz,
            kTransient,kWidth,kOutputDb,kRoot,kScale,kMorphEnabled,kGateEnabled,
            kMidiEnabled,kBypass,kSoftLimit,kCustomC};
        for (unsigned id = 0; id < std::size(originalIDs); ++id)
            require(static_cast<unsigned>(originalIDs[id]) == id, "Existing sound parameter ID changed");
        for (unsigned id = 0; id < 83; ++id)
            require(isSoundParameter(id) == (id < 29 || id >= 31), "Sound/panic ID predicate disagrees with wire contract");
        require(!isSoundParameter(83) && !isSoundParameter(0xffffffffu), "Out-of-range IDs classified as sound controls");
        pass("legacy sound IDs, hardcoded MIDI panic 29/30, and appended IDs 31..38 stay fixed");

        MemoryStream old(legacy());
        Values migrated{}; migrated.fill(.987654321);
        require(readState(&old, migrated), "Frozen 0.2 v1 chunk rejected");
        for (unsigned id = 0; id < 29; ++id)
            require(migrated[id] == kLegacyValues[id], "Legacy normalized value changed at ID " + std::to_string(id));
        require(discrete(kRoot, migrated[10]) == 5 && discrete(kScale, migrated[11]) == 5,
                "Legacy F / Custom context changed");
        require(toDSP(migrated).customMask == 0x890 && toDSP(migrated).midiEnabled,
                "Legacy absolute E/G/B mask or MIDI context changed");
        const auto initial = defaults();
        for (unsigned id = 31; id < 75; ++id)
            require(migrated[id] == initial[id], "Absent new field did not get neutral migration default");
        require(migrated.at(75)==0 && initial.at(75)==1,"0.2 migration must choose Legacy independently of new-instance default");
        require(migrated.at(76)==0,"0.2 migration must leave Clarity neutral");
        for (const Param id : {kColorLow,kColorMid,kColorHigh})
            require(std::abs(plain(id, migrated[id]) - 100.) < 1e-9, "Old state migration changed band resonance from neutral 100%");
        for (const Param id : {kColorEqLowDb,kColorEqMidDb,kColorEqHighDb})
            require(std::abs(plain(id, migrated[id])) < 1e-9, "Old state migration introduced nonzero EQ");
        require(plain(kColorLowMidHz,migrated[kColorLowMidHz]) > 0 &&
            plain(kColorMidHighHz,migrated[kColorMidHighHz]) > plain(kColorLowMidHz,migrated[kColorLowMidHz]),
            "Migrated crossovers are not ordered positive frequencies");
        pass("literal 360-byte 0.2 state retains all 29 old controls and defaults only appended fields");

        MemoryStream saved;
        require(writeState(&saved, migrated), "Migrated state write failed");
        validateWrittenState(saved, migrated);
        pass("v1 writer emits 81 unique sound IDs and never persists IDs 29/30");

        Values desired = migrated;
        for (unsigned id = 31; id < 83; ++id) desired[id] = sanitize(id, ((id * 17 + 23) % 101) / 100.);
        desired[29] = .625; desired[30] = .875; // Must never leak into saved sound state.
        MemoryStream current;
        require(writeState(&current, desired), "New-field state write failed");
        validateWrittenState(current, desired);
        current.position = 0;
        Values restored{};
        require(readState(&current, restored), "New-field state read failed");
        for (unsigned id = 0; id < 83; ++id)
            require(restored[id] == ((id == 29 || id == 30) ? initial[id] : desired[id]), "New-field round trip or reserved slot failed");
        pass("all 46 controls appended since 0.2 round-trip independently and panic slot poison is discarded");

        auto extended = legacy();
        appendRecord(extended, 29, .25); appendRecord(extended, 30, .75);
        appendRecord(extended, 999, .875);
        MemoryStream future(extended);
        Values ignored{};
        require(readState(&future, ignored) && ignored == migrated, "Panic or unknown records changed sound values");
        pass("finite records at hardcoded panic IDs and unknown future IDs are ignored");

        for (std::size_t length = 0; length < kLegacy02.size(); ++length)
            rejectedWithoutMutation({kLegacy02.begin(), kLegacy02.begin() + length}, "truncated legacy chunk");
        for (std::size_t length = 0; length < current.bytes.size(); ++length)
            rejectedWithoutMutation({current.bytes.begin(), current.bytes.begin() + length}, "truncated new chunk");
        pass("every truncated prefix of old and new chunks rejects without partial mutation");

        auto bad = legacy(); put32(bad, 0, 0); rejectedWithoutMutation(bad, "wrong magic");
        bad = legacy(); put32(bad, 4, 2); rejectedWithoutMutation(bad, "unsupported version");
        bad = legacy(); put32(bad, 8, 1025); rejectedWithoutMutation(bad, "oversized record count");
        for (const double value : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
            bad = legacy(); appendRecord(bad, 31, value); rejectedWithoutMutation(bad, "nonfinite new sound field");
            bad = legacy(); appendRecord(bad, 999, value); rejectedWithoutMutation(bad, "nonfinite unknown field");
        }
        pass("invalid headers and nonfinite records reject atomically");
        std::puts("All state compatibility regression checks passed.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
