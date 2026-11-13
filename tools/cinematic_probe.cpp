#include "spyro/cinematic.hpp"
#include <iostream>
#include <cmath>
using namespace spyro;
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw FormatError("Usage: spyro_cinematic_probe retail-copy");
        AssetStore assets(std::filesystem::u8path(argv[1]));WadArchive wad(assets);auto exe=assets.read_all("SCUS_944.25");
        unsigned movies=0,frames=0;
        for(unsigned scene=15;scene<=95;scene+=2)for(bool outro:{false,true})if(auto slot=cinematic_slot(exe,scene,outro)) {
            try {
                auto c=decode_cinematic(wad.read(scene),wad.read(*slot),exe,scene);double energy=0;
                for(auto s:c.sound.samples)energy+=double(s)*s;
                for(unsigned tick=0;tick<c.duration;++tick) {
                    auto f=c.sample(float(tick));if(!std::isfinite(length(f.eye)) || std::abs(length(f.forward)-1)>0.001f)throw FormatError("Invalid camera");
                    if(f.actors.size()!=f.poses.size())throw FormatError("Actor pose mismatch");++frames;
                }
                if(scene!=95 && (c.sound.frames()!=c.duration*22050/60 || energy==0))throw FormatError("Missing / truncated cinematic sound");
                std::cout<<"{\"scene\":"<<scene<<",\"movie\":"<<*slot<<",\"ticks\":"<<c.duration<<",\"actors\":"<<c.models.models.size()<<",\"rms\":"<<(c.sound.samples.empty()?0:std::sqrt(energy/c.sound.samples.size()))<<"}"<<std::endl;++movies;
            }catch(const std::exception& e){throw FormatError("Movie "+std::to_string(*slot)+": "+e.what());}
        }
        if(movies!=47)throw FormatError("Unexpected movie coverage");
        std::cout<<movies<<" movies, "<<frames<<" camera/actor frames verified\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
