#include "plugin/IrisParameters.h"
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class T, class = void> struct HasBody : std::false_type {};
template<class T> struct HasBody<T, std::void_t<decltype(std::declval<T>().bodyMix),
    decltype(std::declval<T>().bodyMaterial), decltype(std::declval<T>().bodyDecayMs),
    decltype(std::declval<T>().bodyDamping), decltype(std::declval<T>().bodyInharmonicity),
    decltype(std::declval<T>().bodyStrike)>> : std::true_type {};
template<class T> void bodyMappings() {
    using namespace iris::vst3;
    if constexpr (HasBody<T>::value) {
        T p; p.bodyMaterial=3; p.bodyMix=42; p.bodyDecayMs=980;
        p.bodyDamping=73; p.bodyInharmonicity=68; p.bodyStrike=91;
        auto v=fromDSP(p);
        const double expected[]={3,42,980,73,68,91};
        for(unsigned i=0;i<6;++i)
            require(std::abs(plain(77+i,v.at(77+i))-expected[i])<.001,"Body DSP-to-host units changed");
        T q=toDSP(v);
        require(q.bodyMaterial==3 && std::abs(q.bodyMix-42)<.001 && std::abs(q.bodyDecayMs-980)<.001 &&
            std::abs(q.bodyDamping-73)<.001 && std::abs(q.bodyInharmonicity-68)<.001 &&
            std::abs(q.bodyStrike-91)<.001,"Body host-to-DSP mapping lost a field");
        require(toDSP(defaults()).bodyMix==0,"New instances must keep Body opt-in");
    } else require(false,"Body DSP fields are missing");
}
}
int main() {
    try {
        using namespace iris::vst3;
        require(kParameterCount==83 && kSoundParameterCount==81,"Append six Body controls after released ID 76");
        require(kColor==0 && kAllNotesOff==29 && kAllSoundsOff==30 && kEngineMode==75 && kClarity==76,
                "Released parameter IDs changed");
        require(kSpecs.at(77).steps==3 && kSpecs.at(77).minimum==0 && kSpecs.at(77).maximum==3,
                "Body material must have exactly four choices");
        require(kSpecs.at(78).initial==0 && kSpecs.at(79).minimum==40 && kSpecs.at(79).maximum==1800,
                "Body default or decay limits changed");
        for(unsigned id=77;id<83;++id) {
            require(isSoundParameter(id),"Body control absent from serialized/automatable set");
            require(std::isfinite(sanitize(id,NAN)),"Nonfinite host value was not sanitized");
            require(sanitize(id,-10)==0 && sanitize(id,10)==1,"Host range is not clamped");
        }
        for(int i=0;i<4;++i)
            require(discrete(77,normalized(77,i))==i,"Material selection roundtrip changed");
        bodyMappings<iris::Parameters>();
        std::cout<<"PASS six appended Body controls, defaults, units, mapping, enum buckets, finite host sanitation\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n';return 1; }
}
