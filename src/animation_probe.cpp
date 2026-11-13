#include "spyro/spyro_model.hpp"
#include "spyro/game_build.hpp"
#include <iostream>
#include <fstream>
using namespace spyro;
namespace {
void fields(const SpyroAnimationState& s) {std::cout<<'['<<s.clip<<','<<s.next_clip<<','<<s.frame<<','<<s.next_frame<<','<<s.blend<<']';}
}
int main(int argc,char** argv) {
    try {
        if(argc!=3 && argc!=5)throw FormatError("Usage: spyro_animation_probe <ISO|CUE|BIN|folder> <level slot> [--dump-clips output.json]");
        AssetStore source(std::filesystem::u8path(argv[1]));const auto executable=source.read_all(find_game_executable(source));
        const auto& profile=game_profile(executable);WadArchive wad(source,&profile);
        const auto model=decode_spyro(wad.read(0),wad.read(static_cast<unsigned>(std::stoul(argv[2]))),executable,
            argc==5 && wad.contains(29)?wad.read(29):Bytes{},argc==5?29:0);
        if(argc==5) {
            if(std::string(argv[3])!="--dump-clips")throw FormatError("Expected --dump-clips");
            const auto path=std::filesystem::u8path(argv[4]);
            if(path.extension()!=".json")throw FormatError("Clip dumps use .json");
            std::ofstream out(path);out<<"[";bool first_clip=true;
            const auto vector=[&](Vec3 v){out<<'['<<v.x<<','<<v.y<<','<<v.z<<']';};
            for(const auto& [id,clip]:model.animations) {
                if(!first_clip)out<<',';first_clip=false;
                out<<"\n{\"id\":"<<id<<",\"split\":"<<clip.split_vertex<<",\"loop\":["<<clip.loop_start<<','<<clip.loop_end
                   <<"],\"faces\":[";bool first=true;
                for(const auto& face:clip.faces) {
                    if(!first)out<<',';first=false;out<<"{\"vertices\":[";
                    for(unsigned i=0;i<(face.quad?4u:3u);++i){if(i)out<<',';out<<unsigned(face.vertices[i]);}
                    out<<"],\"colors\":[";
                    for(unsigned i=0;i<(face.quad?4u:3u);++i){if(i)out<<',';vector(face.colors[i]);}
                    out<<"]}";
                }
                out<<"],\"frames\":[";first=true;
                for(const auto& frame:clip.frames) {
                    if(!first)out<<',';first=false;
                    out<<"{\"ticks\":"<<frame.ticks<<",\"attachment\":";vector(frame.attachment);out<<",\"vertices\":[";
                    for(unsigned i=0;i<frame.vertices.size();++i){if(i)out<<',';vector(frame.vertices[i]);}
                    out<<"]}";
                }
                out<<"]}";
            }
            out<<"\n]\n";if(!out)throw FormatError("Cannot write clip dump");return 0;
        }
        unsigned scenario=0;
        for(unsigned a=0;a<8;++a)for(unsigned b=0;b<8;++b) {
            if(!model.animations.contains(model.secondary_clips[a]) || !model.animations.contains(model.secondary_clips[b]))continue;
            SpyroAnimator primary;primary.reset(&model,0);SpyroSecondaryAnimator head;head.reset(&model,primary.state());
            const auto initial=primary.state();
            for(unsigned tick=0;tick<180;++tick) {
                const unsigned body_ids[]={0,2,16,24,12,0};const unsigned requested=tick<20?a:(tick<50?b:0);
                primary.tick(body_ids[tick/30]);head.tick(requested,primary.state());
                // Sampling also validates the frame references / vertex layouts.
                model.secondary_track(primary.state().clip,primary.sample(0.37f),head.sample(0.37f));
                std::cout<<"{\"scenario\":"<<scenario<<",\"tick\":"<<tick<<",\"action\":"<<requested<<",\"initial\":";fields(initial);
                std::cout<<",\"primary\":";fields(primary.state());std::cout<<",\"head\":";fields(head.state().pose);
                std::cout<<",\"mode\":"<<head.state().mode<<",\"observed_action\":"<<head.state().action<<"}\n";
            }
            ++scenario;
        }
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
