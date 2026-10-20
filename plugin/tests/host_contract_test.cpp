// Existing VST3 adapter assertions, excluding the historical GUI test's
// process-global SendInput/menu automation. The GUI has separate isolated tests.
#define main iris_existing_host_main
#include "host_test.cpp"
#undef main
int main(int argc,char** argv) {
    try {
        require(argc==2,"host_contract_test <DLL>");
        Module module(argv[1]);Host host;
        {
            Session s(module,host);
            checkMetadata(s);checkState(s);checkSampleRateBounds(s);
            checkBypass<float>(s,kSample32,2,48000,false);
            checkBypass<float>(s,kSample32,2,44100,true);
            checkBypass<double>(s,kSample64,2,96000,false);
            checkBypass<float>(s,kSample32,2,192000,false);
            checkBypass<float>(s,kSample32,1,48000,false);
            checkAutomation(s);checkVisual(s);
        }
        require(host.references==1,"Host references leaked");
        pass("VST3 metadata, state, processing, automation, telemetry and lifetime contracts");return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
