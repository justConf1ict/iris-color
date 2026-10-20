// v0.5 session recall and appended Clarity contract; real serializer and frozen old bytes.
#define main iris04_state_contract_main
#include "state_compat04_test.cpp"
#undef main
#include "fixtures/state05_literal.h"
#include <type_traits>

namespace {
constexpr unsigned kClarityId06 = 76;
template<class T, class = void> struct HasClarity : std::false_type {};
template<class T> struct HasClarity<T, std::void_t<decltype(std::declval<T>().clarity)>> : std::true_type {};
template<class T> double clarityOf(const T& p) {
    if constexpr (HasClarity<T>::value) return p.clarity;
    else return -1;
}
template<class T> void setClarity(T& p, float v) {
    if constexpr (HasClarity<T>::value) p.clarity = v;
}
}
int main() {
    try {
        checks = 0;
        const auto fresh = defaults();
        require(fresh.size() >= 77 && kSoundParameterCount >= 75,
                "Released Clarity ID 76 must remain present after appended extensions");
        require(std::abs(fresh.at(kClarityId06) - .35) < 1e-12,
                "New plugin instances must start at Clarity 35 percent");
        require(kSpecs.at(kClarityId06).minimum == 0 && kSpecs.at(kClarityId06).maximum == 100 &&
                kSpecs.at(kClarityId06).steps == 0, "Clarity must remain a continuous 0..100 percent control");
        pass("Clarity appends ID 76 without shifting existing IDs, with new-instance default 35");

        MemoryStream old({iris06_fixture::state05.begin(), iris06_fixture::state05.end()});
        Values migrated{};
        require(readState(&old, migrated), "Literal released v0.5 component rejected");
        require(migrated.at(kClarityId06) == 0, "Old v0.5 project unexpectedly enabled Clarity");
        require(migrated.at(75) == 1, "v0.5 Prism core changed on migration");
        for (std::size_t at = 12; at < iris06_fixture::state05.size(); at += 12) {
            const unsigned id = u32(iris06_fixture::state05.data() + at);
            require(migrated.at(id) == wireDouble(iris06_fixture::state05.data() + at + 4),
                    "Released parameter changed at ID " + std::to_string(id));
        }
        require(clarityOf(toDSP(migrated)) == 0, "Old project's DSP mapping must preserve Clarity 0");
        pass("Genuine 900-byte v0.5 state retains all old values and Prism, with Clarity 0");

        // The older fixture is checked separately by state_compat05_test; this test uses sparse v1 too.
        auto sparse = legacy();
        for (unsigned id = 800; id < 850; ++id) append(sparse, id, .125);
        MemoryStream legacySparse(sparse);
        require(readState(&legacySparse, migrated) && migrated.at(kClarityId06) == 0 &&
                migrated.at(75) == 0, "Migration must inspect record presence, not record count");
        pass("Sparse and unknown-field legacy states stay Legacy with Clarity 0");

        for (double n : {0., .35, .7, 1.}) {
            auto values = fresh;
            values.at(kClarityId06) = n;
            MemoryStream saved;
            require(writeState(&saved, values) && saved.bytes.size() == 12 + 12 * kSoundParameterCount &&
                    u32(saved.bytes.data() + 8) == kSoundParameterCount, "Current state must contain every declared sound record");
            Values recalled{};
            saved.position = 0;
            require(readState(&saved, recalled) && recalled == values, "Clarity state roundtrip lost values");
            auto p = toDSP(recalled);
            require(std::abs(clarityOf(p) - n * 100.) < 1e-5, "VST-to-DSP Clarity units incorrect");
            setClarity(p, 70);
            require(std::abs(fromDSP(p).at(kClarityId06) - .7) < 1e-12, "DSP-to-VST Clarity units incorrect");
        }
        pass("Clarity automation values roundtrip through state and both DSP mappings");

        auto bad = legacy();
        append(bad, kClarityId06, std::numeric_limits<double>::quiet_NaN());
        rejectsAtomically(bad, "nonfinite Clarity");
        MemoryStream latest; require(writeState(&latest, fresh), "Current state encoding failed");
        for (std::size_t n = 0; n < latest.bytes.size(); ++n)
            rejectsAtomically({latest.bytes.begin(), latest.bytes.begin() + n}, "truncated v0.6 state");
        pass("Nonfinite Clarity and every truncated current state reject without partial mutation");
        std::printf("Independent v0.6 state review: %d groups passed, 0 failures.\n", checks);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL %s\n", e.what()); return 1;
    }
}

