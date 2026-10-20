// Use the actual registry, state writer and Steinberg preset container writer.
#include "plugin/IrisParameters.h"
#include "plugin/IrisPlugin.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("Usage: iris-reference-presets <output-directory>");
        const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
        for(int index=0;index<7;++index) {
            iris::Parameters p;
            p.engineMode=1;p.root=3;p.scale=2;p.color=150;p.snap=100;p.lowHz=230;p.highHz=4000;
            p.decayMs=200;p.morphEnabled=p.gateEnabled=false;p.transient=0;p.width=100;p.outputDb=-6;p.softLimit=true;
            const char* names[]={"01-Prism-Reference-150","02-Prism-Heavy-200","03-Prism-Bloom","04-Prism-Dive","05-Prism-Ripple","06-Prism-Vowel","07-Prism-Tamed"};
            if(index==1){p.color=200;p.outputDb=-9;}
            if(index==2){p.attackTailEnabled=true;p.attackColor=5;p.tailColor=175;p.colorTransitionMs=100;p.outputDb=-9;}
            if(index==3){p.pitchDiveEnabled=true;p.pitchDiveSemitones=12;p.pitchDiveMs=160;p.outputDb=-9;}
            if(index==4){p.scaleMotionEnabled=true;p.scaleMotionDepth=100;p.scaleMotionSync=true;p.scaleMotionDivision=3;}
            if(index==5){p.formantEnabled=true;p.formantMix=90;p.formantVowel=2.6f;p.formantMotion=45;p.formantRateHz=.7f;p.outputDb=-9;}
            if(index==6){p.peakTamerEnabled=true;p.peakTamerAmount=75;p.peakTamerMaxDb=8;}
            const auto values=iris::vst3::fromDSP(p);
            Steinberg::MemoryStream component,container;
            if(!iris::vst3::writeState(&component,values))throw std::runtime_error("Component writer failed");
            component.seek(0,Steinberg::IBStream::kIBSeekSet,nullptr);
            if(!Steinberg::Vst::PresetFile::savePreset(&container,iris::vst3::kProcessorUID,
                    static_cast<Steinberg::IBStream*>(&component),nullptr))throw std::runtime_error("SDK preset writer failed");
            Steinberg::Vst::PresetFile check(&container);
            if(!check.readChunkList()||check.getClassID()!=iris::vst3::kProcessorUID||!check.seekToComponentState())
                throw std::runtime_error("SDK preset readback failed");
            iris::vst3::Values restored{};
            if(!iris::vst3::readState(&container,restored)||restored!=values)throw std::runtime_error("Preset changed a parameter");
            const auto path=directory/(std::string(names[index])+".vstpreset");
            std::ofstream file(path,std::ios::binary);file.write(container.getData(),container.getSize());file.close();
            if(!file)throw std::runtime_error("Preset file write failed");
            std::cout<<path.filename().string()<<" : "<<container.getSize()<<" bytes; SDK/state roundtrip exact\n";
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
