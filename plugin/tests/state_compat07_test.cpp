// Migration uses bytes emitted by the unmodified released 0.6 serializer.
#define main iris_older_state_contracts
#include "state_compat04_test.cpp"
#undef main
#include "fixtures/state06_literal.h"
#include <iostream>

int main() {
    try {
        checks=0;
        MemoryStream old({iris07_fixture::state06.begin(),iris07_fixture::state06.end()});
        Values migrated{};
        require(readState(&old,migrated),"Released 0.6 state was rejected");
        require(migrated.at(78)==0 && toDSP(migrated).bodyMix==0,"Old session unexpectedly enabled Body");
        for(std::size_t at=12;at<iris07_fixture::state06.size();at+=12) {
            const auto id=u32(iris07_fixture::state06.data()+at);
            require(migrated.at(id)==wireDouble(iris07_fixture::state06.data()+at+4),
                    "Migration changed released value at ID "+std::to_string(id));
        }
        require(std::abs(toDSP(migrated).clarity-67.5)<.001 && toDSP(migrated).engineMode==1,
                "Migration lost explicit Prism/Clarity values");
        pass("Frozen released 0.6 state retains every original value and leaves Body off");

        auto sparse=legacy();
        append(sparse,900,.7); append(sparse,76,.35); append(sparse,75,1);
        MemoryStream unknown(sparse);
        require(readState(&unknown,migrated) && migrated.at(78)==0,
                "Unknown future records enabled Body in a sparse older state");
        pass("Sparse states and unknown IDs do not infer a Body opt-in");

        for(int material=0;material<4;++material) for(double amount:{0.,.35,1.}) {
            auto state=defaults();
            state[77]=normalized(77,material); state[78]=amount;
            state[79]=normalized(79,1200); state[80]=.67; state[81]=.73; state[82]=.81;
            MemoryStream saved;
            require(writeState(&saved,state) && saved.bytes.size()==984 && u32(saved.bytes.data()+8)==81,
                    "0.7 state must contain 81 records in the existing v1 wire format");
            Values recalled{};saved.position=0;
            require(readState(&saved,recalled) && recalled==state,"Body state roundtrip lost values");
            const auto p=toDSP(recalled);
            require(p.bodyMaterial==material && std::abs(p.bodyMix-amount*100)<.001,
                    "Body material or level mapped incorrectly after restore");
            require(fromDSP(p)==recalled,"Roundtrip DSP mapping lost a sound parameter");
            auto broken=saved.bytes;broken.pop_back();MemoryStream truncated(broken);
            auto original=recalled;
            require(!readState(&truncated,recalled) && recalled==original,
                    "Truncated Body state partially changed a session");
        }
        pass("All four materials and endpoint levels roundtrip; truncated states are atomic");

        auto invalid=legacy();append(invalid,82,std::numeric_limits<double>::quiet_NaN());
        MemoryStream nonfinite(invalid);auto before=migrated;
        require(!readState(&nonfinite,migrated) && migrated==before,"Nonfinite Body state partially applied");
        pass("Nonfinite new records are rejected without altering the previous session");
        std::cout<<"PASS "<<checks<<" Body migration/state groups\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n';return 1; }
}
