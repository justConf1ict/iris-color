// Iris Color offline WAV renderer. Original implementation, MIT license.
// The DSP path is shared with the VST3. File parsing/allocation occur off-thread.
#include "IrisDSP.h"
#include "IrisPresets.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t u32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
void put16(std::ostream& s, uint16_t x) { char b[2]={char(x),char(x>>8)}; s.write(b,2); }
void put32(std::ostream& s, uint32_t x) { char b[4]={char(x),char(x>>8),char(x>>16),char(x>>24)}; s.write(b,4); }

struct Wave { int rate=48000; std::vector<float> left, right; };

Wave readWave(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open input WAV: " + path);
    const auto length=file.tellg();
    if (length < 12 || length > std::streamoff(1024ull*1024*1024))
        throw std::runtime_error("WAV must be a RIFF file between 12 bytes and 1 GiB.");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    file.seekg(0); file.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!file || std::memcmp(bytes.data(),"RIFF",4) || std::memcmp(bytes.data()+8,"WAVE",4))
        throw std::runtime_error("Expected little-endian RIFF WAVE (convert MP3/FLAC in your DAW first).");
    uint16_t format=0, channels=0, bits=0, align=0;
    uint32_t rate=0; size_t dataPos=0, dataSize=0;
    for (size_t p=12; p+8<=bytes.size();) {
        const uint32_t size=u32(bytes.data()+p+4); const size_t start=p+8;
        if (size > bytes.size()-start) throw std::runtime_error("Truncated WAV chunk.");
        if (!std::memcmp(bytes.data()+p,"fmt ",4)) {
            if (size<16) throw std::runtime_error("Invalid WAV format chunk.");
            auto* f=bytes.data()+start; format=u16(f); channels=u16(f+2); rate=u32(f+4); align=u16(f+12); bits=u16(f+14);
            if (format==0xfffe) {
                if(size<40 || u16(f+16)<22) throw std::runtime_error("Invalid extensible WAV format.");
                const uint8_t expectedTail[14]={0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
                if(std::memcmp(f+26,expectedTail,14)) throw std::runtime_error("Unsupported extensible WAV subformat.");
                format=u16(f+24);
            }
        } else if (!std::memcmp(bytes.data()+p,"data",4) && !dataPos) { dataPos=start; dataSize=size; }
        const size_t next=start+size+(size&1u); if(next<=p) break; p=next;
    }
    if ((format!=1 && format!=3) || (channels!=1 && channels!=2) || rate<8000 || rate>192000 || !dataPos)
        throw std::runtime_error("Supported input: mono/stereo PCM or IEEE float WAV, 8–192 kHz.");
    if ((format==1 && bits!=8 && bits!=16 && bits!=24 && bits!=32) || (format==3 && bits!=32 && bits!=64))
        throw std::runtime_error("Unsupported WAV bit depth.");
    const size_t stride=bits/8;
    if (align!=channels*stride || dataSize%align) throw std::runtime_error("Invalid WAV block alignment.");
    const size_t frames=dataSize/align;
    if(frames==0) throw std::runtime_error("Input WAV is empty.");
    Wave w; w.rate=int(rate); w.left.resize(frames); w.right.resize(frames);
    auto sample=[&](const uint8_t* p)->float {
        double x=0;
        if(format==3 && bits==32) { const auto b=u32(p); float f; std::memcpy(&f,&b,4); x=f; }
        else if(format==3 && bits==64) { uint64_t b=u32(p)|(uint64_t(u32(p+4))<<32); double d; std::memcpy(&d,&b,8); x=d; }
        else if(bits==8) x=(int(p[0])-128)/128.0;
        else if(bits==16) { uint16_t b=u16(p); int16_t n; std::memcpy(&n,&b,2); x=n/32768.0; }
        else if(bits==24) { int32_t n=int32_t(p[0])|(int32_t(p[1])<<8)|(int32_t(p[2])<<16); if(n&0x800000) n-=0x1000000; x=n/8388608.0; }
        else { const auto b=u32(p); int32_t n; std::memcpy(&n,&b,4); x=n/2147483648.0; }
        return std::isfinite(x) ? float(std::clamp(x,-16.0,16.0)) : 0.f;
    };
    for(size_t i=0;i<frames;++i) {
        const auto* p=bytes.data()+dataPos+i*align;
        w.left[i]=sample(p); w.right[i]=channels==2?sample(p+stride):w.left[i];
    }
    return w;
}

void writeWave(const std::string& path, int rate, const std::vector<float>& l, const std::vector<float>& r, size_t start) {
    if(l.size()!=r.size() || start>l.size()) throw std::runtime_error("Invalid output buffers.");
    const size_t frames=l.size()-start;
    if(frames>(std::numeric_limits<uint32_t>::max()-50ull)/8) throw std::runtime_error("Output exceeds RIFF size limit.");
    std::ofstream out(path,std::ios::binary); if(!out) throw std::runtime_error("Cannot create output WAV: "+path);
    // IEEE float WAV with WAVEFORMATEX and a fact chunk. Values are not normalized.
    out.write("RIFF",4); put32(out,uint32_t(50+frames*8)); out.write("WAVEfmt ",8);
    put32(out,18); put16(out,3); put16(out,2); put32(out,uint32_t(rate)); put32(out,uint32_t(rate*8)); put16(out,8); put16(out,32); put16(out,0);
    out.write("fact",4); put32(out,4); put32(out,uint32_t(frames)); out.write("data",4); put32(out,uint32_t(frames*8));
    for(size_t i=start;i<l.size();++i) {
        uint32_t a,b; std::memcpy(&a,&l[i],4); std::memcpy(&b,&r[i],4); put32(out,a); put32(out,b);
    }
    if(!out) throw std::runtime_error("Writing WAV failed (disk full or destination inaccessible).");
}

void preset(iris::Parameters& p,const std::string& name) {
    const int index = iris::presetIndex(name.c_str());
    if(index < 0) throw std::runtime_error("Unknown preset: "+name);
    const int coreMode = p.engineMode;
    p = iris::presetParameters(index);
    p.engineMode = coreMode;
}
int rootNote(std::string s) {
    const std::array<std::string,12> names={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    const std::array<std::string,12> flats={"C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B"};
    for(int i=0;i<12;++i) if(s==names[i] || s==flats[i]) return i;
    size_t used=0; int v=std::stoi(s,&used); if(used!=s.size() || v<0 || v>11) throw std::runtime_error("Root must be C..B or 0..11."); return v;
}
int scaleName(const std::string& s) {
    const std::array<std::string,6> names={"minor","major","minor-pent","major-pent","chromatic","custom"};
    for(int i=0;i<6;++i) if(s==names[i]) return i;
    throw std::runtime_error("Scale must be minor, major, minor-pent, major-pent, chromatic, or custom.");
}
float number(const std::string& s) { size_t used=0; float v=std::stof(s,&used); if(used!=s.size() || !std::isfinite(v)) throw std::runtime_error("Expected finite number: "+s); return v; }
int integer(const std::string& s, int minimum, int maximum) {
    size_t used=0;
    const long long value=std::stoll(s,&used,10);
    if(used!=s.size() || value<minimum || value>maximum)
        throw std::runtime_error("Integer option outside its supported range: "+s);
    return static_cast<int>(value);
}
void help() {
    std::cout << "Iris Color v0.7.0 — material body and color bass WAV processor\n"
        "Usage: iris-render input.wav output.wav [options]\n"
        "  --engine legacy|prism (actual processing core; default prism; preserved by preset selection)\n"
        "  --preset init|glass|clean|gun|water|bloom|dive|ripple|vowel|silk|crystal-punch|chrome-growl|wooden-pluck|string-halo\n"
        "    Select a preset before individual parameter overrides.\n"
        "  --root C..B|0..11  --scale minor|major|minor-pent|major-pent|chromatic|custom\n"
        "  --color 0..200  --snap 0..100  --morph 0..100  --gate -96..0\n"
        "  --decay 25..1500 (Prism: note-Q reference; Legacy: ms)  --low 60..1500  --high 1000..18000 (Hz)\n"
        "  --transient 0..100  --width 0..160  --output -24..12 (dB)\n"
        "  --clarity 0..100 (Prism in-band tonal balance, default 35; 0 preserves v0.5)\n"
        "  --body-material glass|metal|wood|string  --body-mix 0..100 (default 0: layer off)\n"
        "  --body-decay 40..1800 (longest mode T60, ms)  --body-damping 0..100\n"
        "  --body-inharmonicity 0..100  --body-strike 0..100\n"
        "  --color-low 0..200  --color-mid 0..200  --color-high 0..200 (%)\n"
        "  --color-split-low 120..2000  --color-split-high 1000..12000 (Hz)\n"
        "  --eq-low -12..12  --eq-mid -12..12  --eq-high -12..12 (dB, wet only)\n"
        "  --attack-tail-on/off --attack-color 0..200 --tail-color 0..200 --color-transition 5..500\n"
        "  --dive-on/off --dive-semitones -24..24 --dive-time 10..1000\n"
        "  --motion-on/off --motion-depth 0..100 --motion-rate .25..20 (Hz)\n"
        "  --motion-sync/free --motion-division 0..5 (1/1 to 1/32) --tempo 1..1000\n"
        "  --formant-on/off --formant-mix 0..100 --vowel 0..4 (A/E/I/O/U)\n"
        "  --formant-motion 0..100 --formant-rate .05..8 (Hz)\n"
        "  --tamer-on/off --tamer-amount 0..100 --tamer-max 0..18 (dB)\n"
        "  --eq-band INDEX:DB (0..15, -24..12 dB); --eq-flat resets only graphic EQ\n"
        "  --morph-off  --gate-off  --no-limit  --bypass  --mask 0..4095\n"
        "  --fft 2048|4096  --spectral 512|1024  --block N (0=irregular)\n"
        "  --tail 0..10 (seconds, default 1)  --keep-latency\n"
        "Prism: Color >100 sharpens and boosts note bands; band Color adjusts local Q / gain.\n"
        "  Decay 200 at Color 150 gives Q 38 with neutral band Color; higher notes decay faster.\n"
        "  Attack/Tail, Pitch Dive and Scale Motion shape the main color, including Color 100.\n"
        "Legacy: those three effects and band Color shape added resonance above Color 100; Decay is ms.\n"
        "Output: stereo float32 WAV at input sample rate, delay compensated by default.\n"
        "No loudness normalization. Original input is never overwritten.\n";
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc==1 || (argc==2 && std::string(argv[1])=="--help")) { help(); return 0; }
        if(argc<3) { help(); return 2; }
        std::string input=argv[1], output=argv[2];
        std::error_code pathError;
        if(input==output || std::filesystem::equivalent(input,output,pathError))
            throw std::runtime_error("Use different input/output filenames; input will not be overwritten.");
        iris::Parameters p; int core=2048,spectral=1024,block=256; double tail=1, tempo=120; bool keepLatency=false;
        p.clarity = 35.f;
        for(int i=3;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--morph-off") p.morphEnabled=false;
            else if(arg=="--gate-off") p.gateEnabled=false;
            else if(arg=="--no-limit") p.softLimit=false;
            else if(arg=="--bypass") p.bypass=true;
            else if(arg=="--keep-latency") keepLatency=true;
            else if(arg=="--attack-tail-on") p.attackTailEnabled=true;
            else if(arg=="--attack-tail-off") p.attackTailEnabled=false;
            else if(arg=="--dive-on") p.pitchDiveEnabled=true;
            else if(arg=="--dive-off") p.pitchDiveEnabled=false;
            else if(arg=="--motion-on") p.scaleMotionEnabled=true;
            else if(arg=="--motion-off") p.scaleMotionEnabled=false;
            else if(arg=="--motion-sync") p.scaleMotionSync=true;
            else if(arg=="--motion-free") p.scaleMotionSync=false;
            else if(arg=="--formant-on") p.formantEnabled=true;
            else if(arg=="--formant-off") p.formantEnabled=false;
            else if(arg=="--tamer-on") p.peakTamerEnabled=true;
            else if(arg=="--tamer-off") p.peakTamerEnabled=false;
            else if(arg=="--eq-flat") p.wetEqDb.fill(0);
            else {
                if(i+1>=argc) throw std::runtime_error("Missing value for "+arg);
                std::string value=argv[++i];
                if(arg=="--preset") preset(p,value);
                else if(arg=="--engine") {
                    if(value=="legacy") p.engineMode=0;
                    else if(value=="prism") p.engineMode=1;
                    else throw std::runtime_error("Engine must be legacy or prism.");
                }
                else if(arg=="--root") p.root=rootNote(value);
                else if(arg=="--scale") p.scale=scaleName(value);
                else if(arg=="--color") p.color=number(value);
                else if(arg=="--snap") p.snap=number(value);
                else if(arg=="--morph") {p.morph=number(value);p.morphEnabled=true;}
                else if(arg=="--gate") {p.gateDb=number(value);p.gateEnabled=true;}
                else if(arg=="--decay") p.decayMs=number(value);
                else if(arg=="--low") p.lowHz=number(value);
                else if(arg=="--high") p.highHz=number(value);
                else if(arg=="--transient") p.transient=number(value);
                else if(arg=="--clarity") p.clarity=number(value);
                else if(arg=="--body-material") {
                    const std::array<std::string,4> materials={"glass","metal","wood","string"};
                    const auto it=std::find(materials.begin(),materials.end(),value);
                    if(it==materials.end()) throw std::runtime_error("Body material must be glass, metal, wood, or string.");
                    p.bodyMaterial=static_cast<int>(it-materials.begin());
                }
                else if(arg=="--body-mix") p.bodyMix=number(value);
                else if(arg=="--body-decay") p.bodyDecayMs=number(value);
                else if(arg=="--body-damping") p.bodyDamping=number(value);
                else if(arg=="--body-inharmonicity") p.bodyInharmonicity=number(value);
                else if(arg=="--body-strike") p.bodyStrike=number(value);
                else if(arg=="--width") p.width=number(value);
                else if(arg=="--output") p.outputDb=number(value);
                else if(arg=="--color-low") p.bandColorLow=number(value);
                else if(arg=="--color-mid") p.bandColorMid=number(value);
                else if(arg=="--color-high") p.bandColorHigh=number(value);
                else if(arg=="--color-split-low") p.colorSplitLowHz=number(value);
                else if(arg=="--color-split-high") p.colorSplitHighHz=number(value);
                else if(arg=="--eq-low") p.bandEqLowDb=number(value);
                else if(arg=="--eq-mid") p.bandEqMidDb=number(value);
                else if(arg=="--eq-high") p.bandEqHighDb=number(value);
                else if(arg=="--attack-color") p.attackColor=number(value);
                else if(arg=="--tail-color") p.tailColor=number(value);
                else if(arg=="--color-transition") p.colorTransitionMs=number(value);
                else if(arg=="--dive-semitones") p.pitchDiveSemitones=number(value);
                else if(arg=="--dive-time") p.pitchDiveMs=number(value);
                else if(arg=="--motion-depth") p.scaleMotionDepth=number(value);
                else if(arg=="--motion-rate") p.scaleMotionRateHz=number(value);
                else if(arg=="--motion-division") p.scaleMotionDivision=integer(value,0,5);
                else if(arg=="--formant-mix") p.formantMix=number(value);
                else if(arg=="--vowel") p.formantVowel=number(value);
                else if(arg=="--formant-motion") p.formantMotion=number(value);
                else if(arg=="--formant-rate") p.formantRateHz=number(value);
                else if(arg=="--tamer-amount") p.peakTamerAmount=number(value);
                else if(arg=="--tamer-max") p.peakTamerMaxDb=number(value);
                else if(arg=="--tempo") {tempo=number(value); if(tempo<1 || tempo>1000) throw std::runtime_error("Tempo must be1..1000 BPM.");}
                else if(arg=="--eq-band") {
                    const auto colon=value.find(':');
                    if(colon==std::string::npos) throw std::runtime_error("EQ band needs INDEX:DB, for example8:-6.");
                    const int index=integer(value.substr(0,colon),0,15);
                    const float gain=number(value.substr(colon+1));
                    if(gain < -24 || gain > 12) throw std::runtime_error("Graphic EQ gain must be-24..12 dB.");
                    p.wetEqDb[static_cast<std::size_t>(index)]=gain;
                }
                else if(arg=="--mask") p.customMask=static_cast<uint16_t>(integer(value,0,4095));
                else if(arg=="--fft") core=integer(value,2048,4096);
                else if(arg=="--spectral") spectral=integer(value,512,1024);
                else if(arg=="--block") block=integer(value,0,65536);
                else if(arg=="--tail") tail=number(value);
                else throw std::runtime_error("Unknown option: "+arg);
            }
        }
        if((core!=2048 && core!=4096) || (spectral!=512 && spectral!=1024)) throw std::runtime_error("FFT sizes: core 2048/4096; spectral 512/1024.");
        if(block<0 || block>65536 || tail<0 || tail>10) throw std::runtime_error("Block must be 0..65536; tail 0..10 seconds.");
        Wave w=readWave(input); const size_t originalFrames=w.left.size();
        iris::Engine engine; engine.prepare(w.rate,core,spectral); engine.setParameters(p); engine.reset();
        const int latency=engine.latencySamples();
        if(latency<0 || latency>1000000) throw std::runtime_error("Unexpected engine latency.");
        const size_t total=originalFrames+size_t(latency)+size_t(std::llround(tail*w.rate));
        w.left.resize(total,0); w.right.resize(total,0);
        std::vector<float> l(total),r(total);
        constexpr int irregular[]={1,17,64,511,128,7,256,1024,31}; size_t pattern=0;
        for(size_t pos=0;pos<total;) {
            int count=block ? block : irregular[pattern++%9]; count=int(std::min(size_t(count),total-pos));
            engine.setTransport(tempo, double(pos)*tempo/(60.0*w.rate), true, true);
            engine.process(w.left.data()+pos,w.right.data()+pos,l.data()+pos,r.data()+pos,count); pos+=size_t(count);
        }
        double peak=0,energy=0; const size_t start=keepLatency?0:size_t(latency);
        for(size_t i=start;i<total;++i) {
            if(!std::isfinite(l[i]) || !std::isfinite(r[i])) throw std::runtime_error("DSP returned a non-finite sample.");
            peak=std::max({peak,std::abs(double(l[i])),std::abs(double(r[i]))}); energy+=double(l[i])*l[i]+double(r[i])*r[i];
        }
        writeWave(output,w.rate,l,r,start);
        const double rms=std::sqrt(energy/std::max(1.0,2.0*double(total-start)));
        std::cout << std::setprecision(10) << "{\"sample_rate\":" << w.rate << ",\"input_frames\":" << originalFrames
            << ",\"output_frames\":" << total-start << ",\"latency_samples\":" << latency
            << ",\"latency_ms\":" << 1000.0*latency/w.rate << ",\"peak\":" << peak << ",\"rms\":" << rms << "}\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << "Iris Color: " << e.what() << '\n'; return 1; }
}
