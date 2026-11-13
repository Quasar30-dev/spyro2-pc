// Idle census of the retail-script runtime: every campaign scene runs for N
// ticks with an invulnerable, idle Spyro at the spawn. Reports script mobys
// that disappear, move far, or never animate, grouped by class, so scripts
// waiting on missing engine state stand out.
// Usage: spyro_script_census <retail data> [ticks] [slot ...]
#include "spyro/assets.hpp"
#include "spyro/retail_scripts.hpp"
#include "spyro/runtime.hpp"
#include "spyro/travel.hpp"
#include <iostream>
#include <map>

using namespace spyro;
int main(int argc,char** argv) {
    if(argc<2){std::cerr<<"Usage: spyro_script_census <retail data> [ticks] [slot ...]\n";return 2;}
    try {
        AssetStore assets(argv[1]);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25");
        const unsigned ticks=argc>2?std::stoul(argv[2]):600;
        std::vector<unsigned> slots;for(int i=3;i<argc;++i)slots.push_back(std::stoul(argv[i]));
        if(slots.empty())for(unsigned s=15;s<=69;s+=2)slots.push_back(s);
        for(const auto slot:slots) {
            const auto archive=wad.read(slot);
            Level level=decode_level(archive,slot);decode_travel(archive,exe,level);auto models=decode_object_models(archive,exe);
            initialize_retail_objects(level,models);level.script_image=make_retail_script_image(wad,exe,slot);
            Scene scene;scene.wad_slot=slot;scene.spawn=level.spawn;Game g(level,scene,{},&models,nullptr);
            std::vector<Vec3> start;
            for(unsigned t=0;t<ticks;++t) {
                g.player.invulnerable_ticks=std::max(g.player.invulnerable_ticks,2u);g.advance(FixedClock::step,{});
                if(t==1)for(const auto& o:g.retail_objects)start.push_back(o.position);
            }
            std::map<unsigned,std::array<unsigned,4>> by_class;   // owned, removed, moved, animated
            for(std::size_t i=0;i<level.objects.size();++i) {
                const auto& o=g.retail_objects[i];if(!o.retail_script || level.objects[i].state<0)continue;
                auto& c=by_class[level.objects[i].type];++c[0];
                if(o.state<0)++c[1];
                if(o.state>=0 && length(o.position-start[i])>2)++c[2];
                if(o.animation.frame || o.animation.clip!=level.objects[i].animation.clip)++c[3];
            }
            std::cout<<"slot "<<slot<<": owned "<<g.overlay_scripts.owned()<<" spawned "<<g.overlay_scripts.spawned<<" faults "<<g.overlay_scripts.faults
                <<" hits "<<g.overlay_scripts.hits<<" instr/tick "<<(g.overlay_scripts.script_ticks?g.overlay_scripts.instructions/g.overlay_scripts.script_ticks:0)
                <<(g.overlay_scripts.fault.empty()?"":" fault "+g.overlay_scripts.fault)<<"\n  ";
            for(const auto& [type,c]:by_class)std::cout<<std::hex<<type<<std::dec<<":"<<c[0]<<(c[1]?" -"+std::to_string(c[1]):"")<<(c[2]?" ~"+std::to_string(c[2]):"")<<(c[3]?" a"+std::to_string(c[3]):"")<<"  ";
            std::map<unsigned,std::array<unsigned,2>> spawned;   // live, retired
            for(std::size_t i=level.objects.size();i<g.retail_objects.size();++i)if(g.retail_objects[i].retail_script)
                ++spawned[g.retail_objects[i].type][g.retail_objects[i].state<0?1:0];
            std::cout<<"\n  spawned (live/retired):";
            for(const auto& [type,c]:spawned)std::cout<<" "<<std::hex<<type<<std::dec<<":"<<c[0]<<"/"<<c[1];
            std::cout<<"\n";
        }
        return 0;
    } catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}
