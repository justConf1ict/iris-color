// Switching cores must not temporarily double the protected sub-band.
#include "IrisDSP.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
int main() {
    constexpr int fs=48000;
    iris::Engine e;e.prepare(fs);iris::Parameters p;
    p.engineMode=0;p.color=150;p.lowHz=230;p.highHz=4000;
    p.morphEnabled=p.gateEnabled=false;p.width=100;p.transient=0;p.outputDb=0;p.softLimit=false;
    e.setParameters(p);
    std::vector<float> x(fs*3),y(x.size()),r(x.size());
    for(int i=0;i<int(x.size());++i)x[i]=.2f*float(std::sin(2*3.141592653589793*80*i/fs));
    for(int i=0;i<int(x.size());i+=64) {
        if(i==fs){p.engineMode=1;e.setParameters(p);}
        if(i==fs*2){p.engineMode=0;e.setParameters(p);}
        const int count=std::min(64,int(x.size())-i);
        e.process(x.data()+i,x.data()+i,y.data()+i,r.data()+i,count);
    }
    double peak=0;for(int i=fs/2;i<int(y.size());++i)peak=std::max(peak,std::abs(double(y[i])));
    const bool good=peak<.22&&e.latencySamples()==3072;
    std::printf("%s protected 80 Hz core switches: peak=%.9f, bound=.22, latency=%d\n",good?"PASS":"FAIL",peak,e.latencySamples());
    return good?0:1;
}
