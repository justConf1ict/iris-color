// The 0.4 fixture is frozen from the unchanged released writer. This test can
// compile against the pre-0.5 registry: its first runtime check then proves the
// new mode is genuinely absent, rather than treating a compile error as RED.
#define main iris04_state_contract_main
#include "state_compat04_test.cpp"
#undef main
#include "fixtures/state04_literal.h"
#include <type_traits>
#include <utility>

namespace {
constexpr unsigned kModeId05=75;
template<class T,class=void> struct HasCoreMode : std::false_type {};
template<class T> struct HasCoreMode<T,std::void_t<decltype(std::declval<T>().engineMode)>> : std::true_type {};
template<class T> int coreMode(const T& p) {
    if constexpr (HasCoreMode<T>::value) return p.engineMode;
    else return -1;
}
template<class T> void chooseCore(T& p,int mode) {
    if constexpr (HasCoreMode<T>::value) p.engineMode=mode;
}
double modeValue(const Values& v) { return v.size()>kModeId05 ? v.at(kModeId05) : -1; }
double modeOnWire(const MemoryStream& s) {
    for (std::size_t at=12;at+12<=s.bytes.size();at+=12)
        if (u32(s.bytes.data()+at)==kModeId05) return wireDouble(s.bytes.data()+at+4);
    return -1;
}
std::vector<std::uint8_t> emptyState() {
    std::vector<std::uint8_t> b(12,0); put32(b,0,0x53495249); put32(b,4,1); return b;
}
}
int main() {
    try {
        checks=0;
        const auto fresh=defaults();
        MemoryStream saved; require(writeState(&saved,fresh),"Fresh state encode failed");
        require(saved.bytes.size()==12+12*kSoundParameterCount && modeOnWire(saved)==1,
                "New instance must persist Prism at ID 75 in the current extended sound record set");
        require(modeValue(fresh)==1 && coreMode(iris::Parameters{})==1,"Fresh registry and DSP Parameters disagree on Prism default");
        pass("fresh instances persist an explicit Prism core, with unchanged IRIS v1 envelope");

        MemoryStream old({iris05_fixture::state04.begin(),iris05_fixture::state04.end()});
        Values migrated{}; require(readState(&old,migrated),"Frozen 0.4 state rejected");
        require(modeValue(migrated)==0,"A 0.4 chunk without ID 75 silently changed to the Prism core");
        require(migrated.at(76)==0,"A 0.4 chunk silently enabled Clarity");
        for (unsigned id=0;id<75;++id)
            require(migrated[id]==iris05_fixture::normalized04[id],"0.4 parameter changed while choosing Legacy at ID "+std::to_string(id));
        require(coreMode(toDSP(migrated))==0,"Legacy state did not select Legacy in DSP Parameters");
        pass("literal 888-byte 0.4 chunk preserves every old field and selects Legacy");

        auto many=legacy();
        for (unsigned id=800;id<850;++id) append(many,id,.125);
        MemoryStream sparseOld(std::move(many));
        require(readState(&sparseOld,migrated) && modeValue(migrated)==0,
                "Mode migration incorrectly inferred release from record count");
        auto one=emptyState(); append(one,kModeId05,1);
        MemoryStream sparseNew(one);
        require(readState(&sparseNew,migrated) && modeValue(migrated)==1,
                "An explicit Prism record was ignored in a sparse state");
        pass("mode presence, not record count, distinguishes legacy and explicit new state");

        for (int mode : {0,1}) {
            auto v=fresh; v[kModeId05]=mode; v[kColor]=.785;
            MemoryStream stream; require(writeState(&stream,v),"Mode state encode failed");
            require(stream.bytes.size()==12+12*kSoundParameterCount && modeOnWire(stream)==mode,"Writer lost explicit mode");
            stream.position=0; Values restored{};
            require(readState(&stream,restored) && restored==v,"Core mode roundtrip changed the snapshot");
            auto p=toDSP(restored); require(coreMode(p)==mode,"VST to DSP mode mapping wrong");
            chooseCore(p,1-mode);
            require(modeValue(fromDSP(p))==1-mode,"DSP to VST mode mapping ignored the selected core");
        }
        pass("Legacy and Prism round-trip through state and both real DSP parameter mappings");

        for (const auto sample : {std::pair<double,int>{-.1,0},{.49,0},{.5,1},{.51,1},{1.1,1}}) {
            auto b=emptyState();append(b,kModeId05,sample.first);MemoryStream s(b);Values v{};
            require(readState(&s,v) && modeValue(v)==sample.second,"Core mode did not use canonical two-choice normalization");
        }
        pass("mode values use a canonical, bounded two-choice contract");

        for (double invalid : {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
            auto b=emptyState();append(b,kModeId05,invalid);rejectsAtomically(b,"nonfinite mode");
        }
        for (std::size_t n=0;n<saved.bytes.size();++n)
            rejectsAtomically({saved.bytes.begin(),saved.bytes.begin()+n},"truncated new state");
        pass("nonfinite mode and every truncated 0.5 prefix reject without partial mutation");
        std::printf("Independent 0.5 core-state review: %d groups passed, 0 failures.\n",checks);
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr,"FAIL %s\n",e.what());return 1; }
}
