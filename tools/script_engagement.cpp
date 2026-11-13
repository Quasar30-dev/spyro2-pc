// Engagement census of the retail-script runtime. For every script-owned
// moby of a scene: a far baseline (Spyro at the spawn) records the RAM states
// it visits on its own; then Spyro is parked beside it and the census records
// what changes because he is there (new states, movement, clip changes, hits
// on Spyro), then whether the retail flame bit kills it. Classes that never
// react are the ones "not engaging with Spyro at all".
// Usage: spyro_script_engagement <retail data> [slot ...]
// Output: one line per class plus a JSON summary per scene on stdout.
#include "spyro/assets.hpp"
#include "spyro/object_interaction.hpp"
#include "spyro/retail_scripts.hpp"
#include "spyro/runtime.hpp"
#include "spyro/travel.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <numbers>
#include <set>

using namespace spyro;
namespace {
struct Probe {
    unsigned type{};bool spirit=false;
    std::set<int> far_states,near_states;
    float near_move=0;bool clip_change=false;unsigned hits=0;bool flamed=false,died=false,approached=false;
    std::uint32_t killed_by=0;unsigned faults=0;
};
void keep_alive(Game& g){g.player.health=std::max(g.player.health,3u);g.player.lives=std::max(g.player.lives,4u);}
}
int main(int argc,char** argv) {
    if(argc<2){std::cerr<<"Usage: spyro_script_engagement <retail data> [slot ...]\n";return 2;}
    try {
        AssetStore assets(argv[1]);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25");
        std::vector<unsigned> slots;for(int i=2;i<argc;++i)slots.push_back(std::stoul(argv[i]));
        if(slots.empty())for(unsigned s=15;s<=69;s+=2)slots.push_back(s);
        for(const auto slot:slots) {
            if(!retail_level_id(slot))continue;
            const auto archive=wad.read(slot);
            Level level=decode_level(archive,slot);decode_travel(archive,exe,level);auto models=decode_object_models(archive,exe);
            initialize_retail_objects(level,models);level.script_image=make_retail_script_image(wad,exe,slot);
            Scene scene;scene.wad_slot=slot;scene.spawn=level.spawn;Game g(level,scene,{},&models,nullptr);
            const auto state=[&](unsigned i){return int(static_cast<std::int8_t>(g.overlay_scripts.peek(g.overlay_scripts.moby_address(i)+0x48,1)));};
            std::vector<unsigned> owned;std::map<unsigned,Probe> probes;
            for(unsigned t=0;t<2;++t){keep_alive(g);g.advance(FixedClock::step,{});}
            for(unsigned i=0;i<level.objects.size();++i)if(g.retail_objects[i].retail_script && g.retail_objects[i].state>=0){owned.push_back(i);probes[i].type=level.objects[i].type;probes[i].spirit=(level.objects[i].record[83]&127)!=0;}
            if(std::getenv("LIST_TARGETS")) {   // capture targets: the first script-owned enemy (spirit carrier), else the first owned moby
                const char* wanted=std::getenv("TARGET_CLASS");   // optional: a chosen class (hex)
                const auto enemy=std::find_if(owned.begin(),owned.end(),[&](unsigned i){return wanted?probes[i].type==std::stoul(wanted,nullptr,16):probes[i].spirit;});
                const auto pick=enemy!=owned.end()?*enemy:owned.empty()?UINT32_MAX:owned.front();
                if(pick!=UINT32_MAX)std::cout<<"target "<<slot<<' '<<pick<<' '<<std::hex<<probes[pick].type<<std::dec<<' '<<(enemy!=owned.end()?"enemy":"moby")<<"\n";
                continue;
            }
            // Far baseline: 8 s with Spyro idle at the spawn.
            for(unsigned t=0;t<480;++t) {
                keep_alive(g);g.advance(FixedClock::step,{});
                if(t%2)for(const auto i:owned)probes[i].far_states.insert(state(i));
            }
            const auto spawn=g.player.position;
            for(const auto i:owned) {
                auto& p=probes[i];const auto O=[&]()->RetailObject&{return g.retail_objects[i];};   // the vector grows as scripts spawn
                if(O().state<0)continue;
                // Park Spyro about 3 units away on real ground facing the moby.
                std::optional<Vec3> stand;
                for(unsigned k=0;k<12 && !stand;++k) {
                    const float a=k*std::numbers::pi_v<float>/6,r=(k<8?3.0f:4.5f)*(std::getenv("PROBE_FAR")?2.5f:1.0f);
                    const Vec3 at=O().position+Vec3{std::sin(a)*r,0,std::cos(a)*r};
                    if(const auto hit=g.level.raycast(at+Vec3{0,3,0},{0,-1,0},8);hit && hit->normal.y>.6f && std::abs(hit->point.y-O().position.y)<3)stand=hit->point;
                }
                if(!stand)stand=O().position+Vec3{0,.1f,0};
                g.player.position=g.player.previous_position=*stand;g.player.velocity={};
                g.player.yaw=std::atan2(O().position.x-stand->x,O().position.z-stand->z);
                const auto start=O().position;const auto clip=O().animation.clip;const auto hits=g.overlay_scripts.hits;float closest=length(O().position-*stand);
                const auto faults=g.overlay_scripts.faults;
                for(unsigned t=0;t<240;++t) {
                    keep_alive(g);g.advance(FixedClock::step,{});
                    if(O().state<0)break;
                    if(t%2)p.near_states.insert(state(i));
                    p.near_move=std::max(p.near_move,length(O().position-start));p.clip_change|=O().animation.clip!=clip;
                    closest=std::min(closest,length(O().position-g.player.position));
                }
                p.hits=g.overlay_scripts.hits-hits;p.approached=closest<length(start-*stand)-1;
                // The retail flame bit, delivered the way breath is.
                for(const std::uint32_t mask:{flame_attack,charge_attack,headbash_attack,0x970000u,0x10570000u}) {
                    if(O().state<0 || state(i)<0)break;
                    p.flamed=true;
                    for(unsigned t=0;t<90 && O().state>=0 && state(i)>=0;++t){keep_alive(g);if(t%10==0)O().attack_flags|=mask;g.advance(FixedClock::step,{});}
                    if(O().state<0 || state(i)<0){p.died=true;p.killed_by=mask;}
                }
                p.faults=g.overlay_scripts.faults-faults;
                if(p.faults) {
                    std::cout<<"  FAULT near object "<<i<<" class "<<std::hex<<p.type<<std::dec<<": "<<g.overlay_scripts.fault<<"\n";
                    if(std::getenv("CHAIN_TRACE")) {   // header +8 / +0x14 chain of linked mobys
                        const auto& v=g.overlay_scripts;auto m=v.moby_address(i);auto h=v.peek(m);auto s0=v.peek(h+8);
                        std::cout<<std::hex<<"    moby "<<m<<" header "<<h<<" first "<<s0<<"\n";
                        for(unsigned k=0;k<12 && s0;++k){const auto hh=v.peek(s0);std::cout<<"    "<<s0<<" type "<<v.peek(s0+0x36,2)<<" state "<<v.peek(s0+0x48,1)<<" header "<<hh<<" next "<<v.peek(hh+0x14)<<"\n";s0=v.peek(hh+0x14);}
                        // The faulting loops step s = *(*s + 0x14) from a moby.
                        auto a=m;for(unsigned k=0;k<12 && a>=0x80000000u && a<0x80800000u;++k){const auto hh=v.peek(a);
                            std::cout<<"    walk "<<a<<" index "<<std::dec<<(a-v.moby_address(0))/88<<std::hex<<" type "<<v.peek(a+0x36,2)<<" state "<<v.peek(a+0x48,1)
                                <<" header "<<hh<<" next "<<v.peek(hh+0x14)<<"\n";a=v.peek(hh+0x14);}
                        std::cout<<std::dec;
                    }
                }
                g.player.position=g.player.previous_position=spawn;
            }
            std::map<unsigned,std::array<unsigned,8>> by_class;   // n, new-state, moved, clip, hits, killed, approached, spirit
            std::map<unsigned,std::uint32_t> kill_masks;
            for(const auto& [i,p]:probes)kill_masks[p.type]|=p.killed_by;
            for(const auto& [i,p]:probes) {
                auto& c=by_class[p.type];++c[0];
                if(std::any_of(p.near_states.begin(),p.near_states.end(),[&](int s){return !p.far_states.contains(s);}))++c[1];
                if(p.near_move>.5f)++c[2];if(p.clip_change)++c[3];if(p.hits)++c[4];if(p.died)++c[5];if(p.approached)++c[6];if(p.spirit)++c[7];
            }
            std::cout<<"slot "<<slot<<" owned "<<owned.size()<<" faults "<<g.overlay_scripts.faults<<(g.overlay_scripts.fault.empty()?"":" fault "+g.overlay_scripts.fault)<<"\n";
            for(const auto& [type,c]:by_class) {
                const bool silent=!c[1] && !c[2] && !c[3] && !c[4] && !c[5];
                // An enemy (spirit carrier) that never fights or dies is the real failure.
                std::cout<<"  "<<(silent?(c[7]?"ENEMY! ":"SILENT "):(c[7] && !c[5]?"NODIE! ":"       "))<<std::hex<<type<<std::dec<<" x"<<c[0]<<" newstate "<<c[1]<<" moved "<<c[2]<<" clip "<<c[3]
                    <<" hit "<<c[4]<<" killed "<<c[5]<<" approach "<<c[6]<<" spirit "<<c[7]<<" killmask "<<std::hex<<kill_masks[type]<<std::dec<<"\n";
            }
        }
        return 0;
    } catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}
