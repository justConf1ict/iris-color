// Compile the untouched 0.3 DSP separately with -Diris=iris03 and pass its
// header via -DIRIS_BASELINE03_HEADER='"/absolute/path/src/IrisDSP.h"'.
#define iris iris03
#include IRIS_BASELINE03_HEADER
#undef iris
#include "IrisDSP.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    int failures=0;
    for(int fs : {8000,48000,192000}) for(int mode=0;mode<3;++mode) {
        iris03::Engine old; iris::Engine now; old.prepare(fs);now.prepare(fs);
        iris03::Parameters a;iris::Parameters b;
        b.engineMode=0; // Old projects explicitly select Legacy in 0.5.
        a.color=b.color=mode==0?0:(mode==1?100:195);
        a.snap=b.snap=mode==0?0:89;
        a.morph=b.morph=37;a.gateDb=b.gateDb=-61;a.decayMs=b.decayMs=345;
        a.transient=b.transient=27;a.width=b.width=135;a.outputDb=b.outputDb=-3;
        a.bandColorLow=b.bandColorLow=120;a.bandColorMid=b.bandColorMid=75;a.bandColorHigh=b.bandColorHigh=195;
        a.bandEqLowDb=b.bandEqLowDb=1.5;a.bandEqMidDb=b.bandEqMidDb=-4;a.bandEqHighDb=b.bandEqHighDb=3;
        a.colorSplitLowHz=b.colorSplitLowHz=960;a.colorSplitHighHz=b.colorSplitHighHz=2600;
        a.root=b.root=4;a.scale=b.scale=2;
        old.setParameters(a);now.setParameters(b);
        const int size=fs/3;std::vector<float> l(size),r(size),al(size),ar(size),bl(size),br(size);
        uint32_t seed=87461;
        for(int i=0;i<size;++i) {
            seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
            const float noise=float(double(seed)/2147483648.0-1)*.03f;
            l[i]=noise+.2f*float(std::sin(2*3.141592653589793*413.3*i/fs));
            r[i]=noise*.63f+.16f*float(std::sin(2*3.141592653589793*827.7*i/fs+.4));
        }
        bool equal=true;double worst=0;
        for(int i=0;i<size;i+=127) {
            int count=std::min(127,size-i);
            if(i>size/2 && i<size/2+127) {
                a.morph=b.morph=74;a.color=b.color=mode==0?0:177;a.root=b.root=7;
                old.setParameters(a);now.setParameters(b);
            }
            now.setTransport(137,double(i)/fs*137/60,true,true);
            old.process(l.data()+i,r.data()+i,al.data()+i,ar.data()+i,count);
            now.process(l.data()+i,r.data()+i,bl.data()+i,br.data()+i,count);
        }
        for(int i=0;i<size;++i) { equal=equal && al[i]==bl[i] && ar[i]==br[i];worst=std::max(worst,std::max(std::abs(double(al[i])-bl[i]),std::abs(double(ar[i])-br[i]))); }
        std::cout<<(equal?"PASS ":"FAIL ")<<"0.3 bit-null fs="<<fs<<" mode="<<mode<<" maxError="<<worst<<'\n';failures+=!equal;
    }
    return failures?1:0;
}
