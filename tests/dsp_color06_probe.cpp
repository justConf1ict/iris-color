// SDK-free audio driver for the Clarity black-box regression. It can also be
// compiled against an untouched 0.5 tree by omitting IRIS_COLOR06_CLARITY.
#include "IrisDSP.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 6) return 2;
    std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
    if (!input) return 3;
    const auto bytes=input.tellg();
    if (bytes<=0 || bytes%std::streamoff(8)) return 4;
    const int count=int(bytes/std::streamoff(8));
    std::vector<float> packed(count*2), left(count), right(count);
    input.seekg(0); input.read(reinterpret_cast<char*>(packed.data()),bytes);
    for (int n=0;n<count;++n) {left[n]=packed[n*2];right[n]=packed[n*2+1];}
    iris::Parameters p;
    p.root=3;p.scale=2;p.color=150;p.snap=100;p.lowHz=230;p.highHz=4000;
    p.decayMs=200;p.width=100;p.outputDb=0;p.transient=0;
    p.morphEnabled=p.gateEnabled=p.softLimit=false;
    p.engineMode=argc>6?std::atoi(argv[6]):1;
    if(argc>7)p.color=std::atof(argv[7]);
    if(argc>8)p.bypass=std::atoi(argv[8])!=0;
#ifdef IRIS_COLOR06_CLARITY
    p.clarity=std::atof(argv[4]);
#endif
    iris::Engine engine;engine.prepare(std::atoi(argv[3]));engine.setParameters(p);
    const int latency=engine.latencySamples();
    const int size=count+latency;
    left.resize(size);right.resize(size);
    std::vector<float> outLeft(size),outRight(size);
    const int block=std::max(1,std::atoi(argv[5]));
    for(int at=0;at<size;at+=block) {
        const int frames=std::min(block,size-at);
        engine.process(left.data()+at,right.data()+at,outLeft.data()+at,outRight.data()+at,frames);
    }
    packed.resize(size*2);
    for(int n=0;n<size;++n){packed[n*2]=outLeft[n];packed[n*2+1]=outRight[n];}
    std::ofstream output(argv[2],std::ios::binary);
    output.write(reinterpret_cast<const char*>(packed.data()),std::streamsize(packed.size()*sizeof(float)));
    return output?0:5;
}
