#define main iris_existing_host_main
#include "host_test.cpp"
#undef main
#include <atomic>
#include <chrono>
#include <thread>

namespace {
void runAudio(Session& s,double rate,int block,int channels,int material,int seconds,bool realtime,
              std::atomic<bool>* ready=nullptr,std::atomic<bool>* stop=nullptr) {
    auto state=defaults(); state[kBodyMaterial]=normalized(kBodyMaterial,material%4);
    state[kBodyMix]=material==4?0:1.; state[kBodyDecayMs]=1.; s.state(state);
    s.prepare(rate,kSample32,channels);
    std::array<std::vector<float>,2> in,out;
    for(auto& b:in)b.resize(block); for(auto& b:out)b.resize(block);
    float* inp[]={in[0].data(),in[1].data()};float* outp[]={out[0].data(),out[1].data()};
    AudioBusBuffers input{},output{};input.numChannels=output.numChannels=channels;
    input.channelBuffers32=inp;output.channelBuffers32=outp;
    ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample32;
    data.numInputs=data.numOutputs=1;data.inputs=&input;data.outputs=&output;
    // Empty flush, absent input and absent output pointers are valid adapter paths.
    data.numSamples=0;require(s.processor->process(data)==kResultOk,"Empty flush rejected");
    data.numSamples=block;data.numInputs=0;data.inputs=nullptr;
    require(s.processor->process(data)==kResultOk,"Absent input rejected");
    data.numOutputs=0;data.outputs=nullptr;
    require(s.processor->process(data)==kResultOk,"Absent output rejected");
    data.numInputs=data.numOutputs=1;data.inputs=&input;data.outputs=&output;
    if(ready) ready->store(true,std::memory_order_release);
    const auto begin=std::chrono::steady_clock::now();
    int checkpoint=0;
    for(int64_t at=0;at<int64_t(rate*seconds);at+=block) {
        if(stop && stop->load())break;
        const int n=static_cast<int>(std::min<int64_t>(block,int64_t(rate*seconds)-at));
        const int phase=static_cast<int>(at/rate);
        for(int i=0;i<n;++i) {
            const auto sample=at+i;
            in[0][i]=phase%5==4?0.f:static_cast<float>(.2*std::sin(2*3.141592653589793*220*sample/rate));
            in[1][i]=in[0][i]*.8f;
        }
        Changes changes;
        if(seconds>=10) {
            changes.add(kBodyMaterial,0,normalized(kBodyMaterial,phase%4));
            changes.add(kBodyMix,0,phase%5==4?0.:1.);
            changes.add(kBypass,0,phase%11==10?1.:0.);
            changes.add(kBodyDecayMs,n/2,phase%2?0.:1.);
            changes.add(kBodyInharmonicity,n/2,phase%2?1.:0.);
        }
        data.inputParameterChanges=&changes;data.numSamples=n;
        input.silenceFlags=phase%5==4?((1ull<<channels)-1):0;
        require(s.processor->process(data)==kResultOk,"process rejected");
        for(int c=0;c<channels;++c)for(int i=0;i<n;++i)
            require(std::isfinite(out[c][i]) && std::abs(out[c][i])<100,"Invalid audio output");
        if(realtime)std::this_thread::sleep_until(begin+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>((at+n)/rate)));
        if(seconds>=10 && phase>=checkpoint) {std::cout<<"AUDIO seconds="<<phase<<std::endl;checkpoint=phase<10?10:phase<60?60:phase+60;}
    }
    std::cout<<"AUDIO PASS rate="<<rate<<" block="<<block<<" channels="<<channels<<" material="<<material
        <<" rendered_seconds="<<seconds<<" wall_seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<<std::endl;
}
}

int main(int argc,char** argv) {
    try {
        require(argc>=3,"stability_test <DLL> <matrix|audio|combined> [seconds=600]");
        Module module(argv[1]);Host host;Session session(module,host);
        const std::string mode=argv[2];
        if(mode=="matrix") {
            int count=0;
            for(double rate:{44100.,48000.,96000.})for(int block:{1,32,64,256,1024,2049})for(int channels:{1,2})for(int material:{0,1,2,3,4}) {
                runAudio(session,rate,block,channels,material,1,false);++count;
            }
            std::cout<<"MATRIX PASS "<<count<<" cases"<<std::endl;return 0;
        }
        const int seconds=argc>3?std::stoi(argv[3]):600;
        if(mode=="audio") {runAudio(session,48000,256,2,1,seconds,true);return 0;}
        require(mode=="combined","Unknown mode");
        std::atomic<bool> done{false},ready{false},stop{false};std::exception_ptr error;
        // Prepare before starting the worker. State is only changed through
        // VST3 audio queues inside runAudio while the GUI owns its controller.
        auto work=[&] {try {runAudio(session,48000,256,2,1,seconds,true,&ready,&stop);} catch(...) {error=std::current_exception();} done.store(true);};
        std::thread worker(work);
        while(!ready.load(std::memory_order_acquire) && !done.load())std::this_thread::yield();
        unsigned cycles=0;
        try {
            while(!done.load()) {
                HWND parent=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"Iris audio GUI stress",
                    WS_POPUP,-30000,-30000,1560,1140,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
                require(parent!=nullptr,"Parent creation failed");ShowWindow(parent,SW_SHOWNOACTIVATE);
                IPlugView* view=session.controller->createView(ViewType::kEditor);
                require(view && view->attached(parent,kPlatformTypeHWND)==kResultOk,"Editor attach failed");
                const auto child=FindWindowExW(parent,nullptr,L"IndependentAudioLab.IrisColor.Editor.v5",nullptr);
                require(child!=nullptr,"Editor HWND missing");
                IPlugViewContentScaleSupport* scale=nullptr;
                require(view->queryInterface(IPlugViewContentScaleSupport::iid,reinterpret_cast<void**>(&scale))==kResultOk,"Scale interface missing");
                const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(20);
                unsigned ticks=0;
                while(!done.load() && std::chrono::steady_clock::now()<until) {
                    MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}
                    if(ticks%90==0)require(scale->setContentScaleFactor(1.f+.25f*((ticks/90)%3))==kResultOk,"Scale failed");
                    InvalidateRect(child,nullptr,FALSE);SendMessageW(child,WM_PAINT,0,0);
                    ++ticks;std::this_thread::sleep_for(std::chrono::milliseconds(33));
                }
                scale->release();require(view->removed()==kResultOk,"Remove failed");view->release();DestroyWindow(parent);
                std::cout<<"GUI CYCLE PASS "<<++cycles<<std::endl;
            }
        }catch(...){stop.store(true);worker.join();throw;}
        worker.join();
        if(error)std::rethrow_exception(error);
        std::cout<<"COMBINED PASS seconds="<<seconds<<" GUI cycles="<<cycles<<std::endl;
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
