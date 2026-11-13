// Level completion through the retail end-of-level flow, on the real runtime
// (native systems + original overlay scripts), for every campaign level:
//   1. find the level's talisman speaker: the conversation moby whose header
//      kind byte is 1 (the speaker class sets 80066f44 when Spyro talks to it,
//      and line command 1 of 8003a0d8 then picks the talisman line);
//   2. stand Spyro in front of it (a teleport stands in for the walk), talk,
//      read the conversation to the end and let the award play out;
//   3. check the talisman (save flag and count), the raised Return Home arch;
//   4. walk through Return Home and check the travel request for the home
//      world (the workbench --level-exit-check then loads it for real).
// Usage: spyro_level_exits <retail data> [slot ...]
#include "spyro/assets.hpp"
#include "spyro/npc.hpp"
#include "spyro/retail_scripts.hpp"
#include "spyro/runtime.hpp"
#include "spyro/travel.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <bit>
#include <iostream>

using namespace spyro;
namespace {
void keep_alive(Game& g){g.player.health=std::max(g.player.health,3u);g.player.lives=std::max(g.player.lives,4u);g.player.invulnerable_ticks=std::max(g.player.invulnerable_ticks,4u);}
bool in_front(Game& g,const RetailObject& o,float distance) {
    const float yaw=retail::native_heading(o.rotation[2]*16);
    const auto under=g.level.raycast(o.position+Vec3{0,1,0},{0,-1,0},3);
    const float level_y=under?under->point.y:o.position.y;
    for(const float r:{distance,distance*.7f,distance*1.4f})for(float spread:{0.0f,0.4f,-0.4f,0.8f,-0.8f,1.6f,-1.6f,2.4f,-2.4f,3.14f}) {
        const Vec3 d{std::sin(yaw+spread),0,std::cos(yaw+spread)};
        const auto at=o.position+d*r;
        const auto floor=g.level.raycast({at.x,level_y+1.2f,at.z},{0,-1,0},2.4f);
        if(!floor || floor->point.y-level_y>.2f || level_y-floor->point.y>.35f || floor->normal.y<.7f)continue;
        g.player.position=g.player.previous_position=floor->point+Vec3{0,0.01f,0};g.player.velocity={};
        g.player.yaw=g.player.previous_yaw=std::atan2(-d.x,-d.z);return true;
    }
    return false;
}
}
int main(int argc,char** argv) {
    if(argc<2){std::cerr<<"Usage: spyro_level_exits <retail data> [slot ...]\n";return 2;}
    try {
        AssetStore assets(argv[1]);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25");
        std::vector<unsigned> slots;for(int i=2;i<argc;++i)slots.push_back(std::stoul(argv[i]));
        if(slots.empty())for(unsigned s=15;s<=69;s+=2)slots.push_back(s);
        unsigned failures=0;
        for(const auto slot:slots) {
            if(!retail_level_id(slot))continue;
            const auto archive=wad.read(slot);
            Level level=decode_level(archive,slot);decode_travel(archive,exe,level);auto models=decode_object_models(archive,exe);
            initialize_retail_objects(level,models);level.script_image=make_retail_script_image(wad,exe,slot);
            if(std::getenv("HAND_OVER") && slot!=17 && slot!=39) {   // experiment: the talisman speaker runs its own class code
                std::erase_if(level.npcs,[](const NpcDefinition& n){return std::any_of(n.lines.begin(),n.lines.end(),[](const DialogueLine& l){return l.talisman>0;});});
            }
            Scene scene;scene.wad_slot=slot;scene.spawn=level.spawn;Game g(level,scene,{},&models,nullptr);
            for(unsigned t=0;t<4;++t){keep_alive(g);g.advance(FixedClock::step,{});}
            auto& v=g.overlay_scripts;
            std::cout<<"slot "<<slot<<" scene "<<g.level.campaign_scene<<" bridge "<<v.dialogue_bridge();
            // The talisman speaker: a conversation with a command-1 line.
            std::vector<unsigned> givers;std::vector<int> rewards;
            for(unsigned i=0;i<g.retail_objects.size();++i) {
                if(g.retail_objects[i].state<0)continue;
                std::optional<NpcDefinition> npc;
                try{npc=decode_npc_record(archive,slot,i);}catch(const std::exception&){}
                if(!npc)continue;
                for(const auto& line:npc->lines)if(line.talisman>0){givers.push_back(i);rewards.push_back(line.talisman);break;}
                unsigned orbs=0;for(const auto& line:npc->lines)orbs|=line.orb_bits;
                if(orbs && npc->lines.size()<=32)std::cout<<"  orb-speaker "<<i<<" type "<<std::hex<<npc->type<<" bits "<<orbs<<std::dec<<" '"<<npc->name<<"'"<<std::endl;
            }
            unsigned home=0;for(const auto& p:g.level.portals)if(p.return_home)++home;
            std::cout<<" talisman-speakers "<<givers.size()<<" return-home "<<home<<"\n";
            const auto exit=std::find_if(g.level.portals.begin(),g.level.portals.end(),[](const auto& p){return p.return_home;});
            for(std::size_t n=0;n<givers.size();++n) {
                const auto i=givers[n];const auto& o=g.retail_objects[i];
                std::cout<<"  speaker "<<i<<" reward "<<std::hex<<rewards[n]<<" model "<<models.models.contains(static_cast<unsigned>(rewards[n]))<<" type "<<std::hex<<o.type<<std::dec<<" at "<<o.position.x<<','<<o.position.y<<','<<o.position.z<<" script "<<o.retail_script;
                if(exit!=g.level.portals.end())std::cout<<" exit-distance "<<length(g.retail_objects[exit->object].position-o.position);
                const auto h=v.peek(v.moby_address(i));std::cout<<std::hex<<" first-commands";
                for(unsigned k=0;k<8;++k){const auto line=v.peek(h+0x10+k*4);if(line<0x80000000u || line>=0x80800000u)break;std::cout<<' '<<v.peek(line+1,1);}
                std::cout<<std::dec<<"\n";
                if(std::getenv("DUMP_HEADER") && h>=0x80000000u) {   // the moby (state +48, substate +49) and its header bytes
                    const auto m=v.moby_address(i);std::cout<<std::hex<<"    moby "<<m<<" +40..4f:";
                    for(unsigned b=0x40;b<0x50;++b)std::cout<<' '<<v.peek(m+b,1);
                    std::cout<<"\n    header "<<h<<":";for(unsigned b=0;b<0x50;++b)std::cout<<(b%16?" ":"\n     ")<<v.peek(h+b,1);std::cout<<std::dec<<"\n";
                    if(const auto linked=v.peek(h+0x44);linked<g.retail_objects.size()) {   // 13B kind 2 waits for this moby's state 2
                        const auto& l=g.retail_objects[linked];const auto lm=v.moby_address(linked);
                        std::cout<<"    linked "<<linked<<" type "<<std::hex<<l.type<<" state "<<v.peek(lm+0x48,1)<<"/"<<v.peek(lm+0x49,1)<<std::dec
                                 <<" script "<<l.retail_script<<" enemy "<<l.enemy.has_value()<<" at "<<l.position.x<<','<<l.position.y<<','<<l.position.z<<"\n";
                    }
                }
                if(std::getenv("DUMP_LINES") && h>=0x80000000u)for(unsigned k=0;k<=v.peek(h+3,1) && k<16;++k) {
                    const auto line=v.peek(h+0x10+k*4);const auto n=v.peek(line,1);
                    std::cout<<"    line "<<k<<std::hex<<" @"<<line<<":";for(unsigned b=0;b<n && b<40;++b)std::cout<<' '<<v.peek(line+b,1);std::cout<<std::dec<<"\n";
                }
            }
            // Magma Cone: the host 13B (kind 2) waits for its linked moby (header
            // +44, class 401) to reach state 2; 401 starts when Spyro stands in
            // its rectangle (header +14: centre, half sizes +20/+24, angle +28).
            if(std::getenv("TRIGGER_401"))for(unsigned i=0;i<g.retail_objects.size();++i)if(g.retail_objects[i].type==0x401) {
                const auto m=v.moby_address(i),h=v.peek(m);
                const retail::IVec3 c{static_cast<int>(v.peek(h+0x14)),static_cast<int>(v.peek(h+0x18)),static_cast<int>(v.peek(h+0x1c))};
                const auto centre=retail::to_world(c);
                std::cout<<"  401 #"<<i<<" box "<<centre.x<<','<<centre.y<<','<<centre.z<<" half "<<v.peek(h+0x20)<<','<<v.peek(h+0x24)<<" angle "<<v.peek(h+0x28)
                         <<" linked "<<v.peek(h+0x30)<<" moby z "<<static_cast<int>(v.peek(m+0x14))<<"\n";
                for(const auto& w:g.level.whirlwinds)if(w.object==v.peek(h+0x30)) {
                    const auto& o=g.retail_objects[w.object];
                    std::cout<<"  whirlwind #"<<w.object<<" kind "<<w.kind<<" active "<<w.active<<" at "<<o.position.x<<','<<o.position.y<<','<<o.position.z<<" height "<<w.height<<" radius "<<w.radius<<"\n";
                }
                const auto floor=g.level.raycast(centre+Vec3{0,3,0},{0,-1,0},12);
                if(floor){g.player.position=g.player.previous_position=floor->point+Vec3{0,.01f,0};g.player.velocity={};}
                for(unsigned t=0;t<900 && !g.dialogue.active();++t) {
                    keep_alive(g);g.advance(FixedClock::step,{});
                    if(t%60==0) {
                        const auto sm=v.moby_address(44);
                        std::cout<<"   t "<<t<<" 401 "<<v.peek(m+0x48,1)<<" 13b "<<v.peek(sm+0x48,1)<<'/'<<v.peek(sm+0x49,1)<<" cam "<<v.peek(0x80067ed0)<<'/'<<v.peek(0x80067ed4)
                                 <<" req "<<std::hex<<v.peek(0x8006a1fc)<<std::dec<<" spyro "<<g.player.position.x<<','<<g.player.position.y<<','<<g.player.position.z<<" grounded "<<g.player.grounded<<"\n";
                    }
                }
                std::cout<<"  dialogue "<<g.dialogue.active()<<" 80066f44 "<<v.peek(0x80066f44,1)<<"\n";
                {   // 8003a98c's conditions for the host
                    const auto sm=v.moby_address(44),sh=v.peek(sm);const auto linked=v.peek(h+0x30);
                    const int sx=static_cast<int>(v.peek(0x80069ff0)),sy=static_cast<int>(v.peek(0x80069ff4)),sz=static_cast<int>(v.peek(0x80069ff8));
                    const int mx=static_cast<int>(v.peek(sm+0xc)),my=static_cast<int>(v.peek(sm+0x10)),mz=static_cast<int>(v.peek(sm+0x14));
                    std::cout<<"  host: dxyz "<<sx-mx<<','<<sy-my<<','<<sz-mz<<" +38 "<<static_cast<std::int16_t>(v.peek(sm+0x38,2))<<" +46 "<<v.peek(sm+0x46,1)
                             <<" +4d "<<v.peek(sm+0x4d,1)<<" +5 "<<v.peek(sh+5,1)<<" +7 "<<v.peek(sh+7,1)<<" spyro-heading "<<v.peek(0x80069ffe,1)<<" air "<<v.peek(0x8006a0b0)
                             <<" mode "<<v.peek(0x8006a048)<<" 80067100 "<<v.peek(0x80067100)<<" 8006701c "<<v.peek(0x8006701c)<<" state "<<v.peek(0x800681c8)
                             <<" | 401 linked "<<linked<<" type "<<std::hex<<(linked<g.retail_objects.size()?g.retail_objects[linked].type:0)<<std::dec
                             <<" state "<<(linked<g.retail_objects.size()?v.peek(v.moby_address(linked)+0x48,1):0)<<"\n";
                }
            }
            if(std::getenv("NO_TALK"))continue;
            for(const auto i:givers) {
                if(g.guide_awards.talisman)break;
                bool open=false;
                // Stand at the end first: an end-of-level event (Hurricos 11A) may
                // arm 80066f44 and make the speaker talk by itself (+5 = 2).
                if(in_front(g,g.retail_objects[i],2.0f))for(unsigned t=0;t<300 && !g.dialogue.active();++t){keep_alive(g);g.advance(FixedClock::step,{});}
                {const auto h=v.peek(v.moby_address(i));std::cout<<"  waited: 80066f44 "<<v.peek(0x80066f44,1)<<" auto "<<(h>=0x80000000u?v.peek(h+5,1):255)
                    <<" dialogue "<<g.dialogue.active()<<" shots "<<v.camera_shots<<"\n";}
                for(unsigned t=0;t<240 && !g.dialogue.active();++t) {
                    if(t%30==0 && !in_front(g,g.retail_objects[i],0.9f))break;
                    keep_alive(g);InputState in;in.dialogue.interact=(t%8)==0;g.advance(FixedClock::step,in);
                }
                open=g.dialogue.active();unsigned lines=0;
                for(unsigned step=0;step<80 && g.dialogue.active();++step) {
                    for(unsigned t=0;t<14;++t){keep_alive(g);g.advance(FixedClock::step,{});}
                    InputState in;const auto* line=g.dialogue.line();
                    if(line && line->yes>=0 && line->no>=0)in.dialogue.choice=0;else in.dialogue.next=true;
                    keep_alive(g);g.advance(FixedClock::step,in);++lines;
                }
                for(unsigned t=0;t<400;++t){keep_alive(g);g.advance(FixedClock::step,{});}
                const unsigned index=(slot-15)/2;
                std::cout<<"  talk "<<i<<" open "<<open<<" lines "<<lines<<" 80066f44 "<<v.peek(0x80066f44,1)<<" ram-talisman "<<v.peek(0x8006b264+index,1)
                    <<" awarded "<<g.guide_awards.talisman<<" talismans "<<std::popcount(g.player.talismans)<<" reward "<<v.peek(0x80066f94)<<" faults "<<v.faults<<(v.faults?" "+v.fault:"")<<"\n";
            }
            if(exit!=g.level.portals.end()) {
                for(unsigned t=0;t<120;++t){keep_alive(g);g.advance(FixedClock::step,{});}
                std::cout<<"  return-home enabled "<<exit->enabled<<" rise "<<exit->rise<<"\n";
                if(!g.guide_awards.talisman)++failures;
            }
        }
        return failures?1:0;
    } catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}
