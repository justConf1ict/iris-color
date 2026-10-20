// Independent visual-mailbox regression. Run from the repository root with:
// g++ -std=c++17 -O2 -pthread -I. tests/visual_mailbox.cpp -o build/visual-mailbox-test
// SPSC must preserve coherent frames and increasing published IDs. The MPSC
// case is extra ownership stress only; its producer ordering is not promised.
// Allocation tracking starts inside each worker after thread construction.
#include "plugin/IrisVisual.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
#include <vector>

thread_local bool reviewWatch = false;
std::atomic<unsigned long long> reviewAllocations{0};
void* operator new(std::size_t n) { if (reviewWatch) reviewAllocations.fetch_add(1,std::memory_order_relaxed); if(void* p=std::malloc(n?n:1))return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}

void fill(iris::VisualFrame& f,std::uint64_t id) {
    f.frameCounter=id;f.sampleRate=8000.f+float(id%8)*1000.f;
    f.activeMask=static_cast<std::uint16_t>(id&4095);f.moveCount=static_cast<std::uint16_t>(id%33);
    f.attackTailGain=float(id%201)*.01f;f.diveSemitones=float(int(id%49)-24);
    f.tamerReductionDb=float(id%181)*.1f;f.formantVowel=float(id%41)*.1f;
    f.motionPitchClass=static_cast<std::int16_t>(int(id%13)-1);f.motionHostSynced=(id&1)!=0;
    for(unsigned i=0;i<f.dryDb.size();++i) {f.dryDb[i]=float((id+i*13)%65536);f.wetDb[i]=-f.dryDb[i]-10;f.gateGain[i]=float((id+i)%4)*.25f;}
    for(unsigned i=0;i<f.moves.size();++i) {const float a=float((id+i*17)%65536)+1;f.moves[i]={a,a+1,-float((id+i)%180)};}
}
bool coherent(const iris::VisualFrame& f) {
    const auto id=f.frameCounter;if(!id||f.sampleRate!=8000.f+float(id%8)*1000.f||f.activeMask!=(id&4095)||f.moveCount!=id%33)return false;
    if(f.attackTailGain!=float(id%201)*.01f||f.diveSemitones!=float(int(id%49)-24)||
       f.tamerReductionDb!=float(id%181)*.1f||f.formantVowel!=float(id%41)*.1f||
       f.motionPitchClass!=int(id%13)-1||f.motionHostSynced!=((id&1)!=0))return false;
    for(unsigned i=0;i<f.dryDb.size();++i)if(f.dryDb[i]!=float((id+i*13)%65536)||f.wetDb[i]!=-f.dryDb[i]-10||f.gateGain[i]!=float((id+i)%4)*.25f)return false;
    for(unsigned i=0;i<f.moves.size();++i){const float a=float((id+i*17)%65536)+1;if(f.moves[i].sourceHz!=a||f.moves[i].targetHz!=a+1||f.moves[i].levelDb!=-float((id+i)%180))return false;}
    return true;
}
int run(const char* label,int writers,int readers,bool clearing) {
    iris::vst3::VisualMailbox box;
    std::atomic<int> left{writers};std::atomic<bool> go{false};std::atomic<std::uint64_t> next{1},received{0},bad{0},regressed{0},dropped{0};
    std::vector<std::thread> threads;
    for(int j=0;j<writers;++j)threads.emplace_back([&]{while(!go.load(std::memory_order_acquire))std::this_thread::yield();reviewWatch=true;iris::VisualFrame f;for(unsigned n=0;n<750000;++n){fill(f,next.fetch_add(1,std::memory_order_relaxed));if(!box.publish(f))dropped.fetch_add(1,std::memory_order_relaxed);}reviewWatch=false;left.fetch_sub(1,std::memory_order_release);});
    for(int j=0;j<readers;++j)threads.emplace_back([&]{while(!go.load(std::memory_order_acquire))std::this_thread::yield();reviewWatch=true;iris::VisualFrame f;std::uint64_t previous=0;for(;;){if(box.take(f)){received.fetch_add(1,std::memory_order_relaxed);if(!coherent(f))bad.fetch_add(1,std::memory_order_relaxed);if(f.frameCounter<previous)regressed.fetch_add(1,std::memory_order_relaxed);previous=f.frameCounter;}else if(!left.load(std::memory_order_acquire))break;else std::this_thread::yield();}reviewWatch=false;});
    if(clearing)threads.emplace_back([&]{while(!go.load(std::memory_order_acquire))std::this_thread::yield();while(left.load(std::memory_order_acquire)){box.clear();std::this_thread::yield();}});
    const auto start=std::chrono::steady_clock::now();go.store(true,std::memory_order_release);for(auto& t:threads)t.join();
    std::printf("%s received=%llu inconsistent=%llu counter_regressions=%llu dropped=%llu allocs=%llu elapsed=%.3f sizeofFrame=%zu sizeofMailbox=%zu\n",label,(unsigned long long)received.load(),(unsigned long long)bad.load(),(unsigned long long)regressed.load(),(unsigned long long)dropped.load(),reviewAllocations.load(),std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),sizeof(iris::VisualFrame),sizeof(box));
    return bad.load()!=0 || reviewAllocations.load()!=0 || received.load()==0 || (writers==1 && regressed.load()!=0);
}
int main(){int fail=0;fail|=run("SPSC",1,1,false);fail|=run("SPSC plus clear",1,1,true);fail|=run("MPSC ownership stress",2,2,false);return fail;}
