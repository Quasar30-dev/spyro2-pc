// Scripted-event census: every conversation in every campaign scene, played
// through the original logic. For each speaker, Spyro walks up and presses
// triangle; the conversation is read to the end, answering "yes" to every
// question (accepting challenges and purchases), then the scene runs on.
// Reports speakers that never open a conversation, conversations the engine
// shows without the original logic, and what the scripts did afterwards:
// camera shots, Spyro placements, script mobys that woke (RAM state change),
// rewards and interpreter faults.
// Usage: spyro_script_quests <retail data> [slot ...]
#include "spyro/assets.hpp"
#include "spyro/npc.hpp"
#include "spyro/retail_scripts.hpp"
#include "spyro/runtime.hpp"
#include "spyro/travel.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace spyro;
namespace {
bool in_front(Game& g,const RetailObject& o,float distance) {
    // Stand where a player would: on dry floor at the speaker's own height
    // (some stand on small tower tops or by the water), facing them.
    const float yaw=retail::native_heading(o.rotation[2]*16);
    const auto under=g.level.raycast(o.position+Vec3{0,1,0},{0,-1,0},3);
    const float level_y=under?under->point.y:o.position.y;
    for(const float r:{distance,distance*.7f,distance*1.4f})for(float spread:{0.0f,0.4f,-0.4f,0.8f,-0.8f,1.6f,-1.6f,2.4f,-2.4f,3.14f}) {
        const Vec3 d{std::sin(yaw+spread),0,std::cos(yaw+spread)};
        const auto at=o.position+d*r;
        const auto floor=g.level.raycast({at.x,level_y+1.2f,at.z},{0,-1,0},2.4f);
        // Speakers talk to a Spyro up to 800 units above their feet: a step
        // beside them (Zephyr's cannon base by Sergeant Tub) can be too high.
        if(!floor || floor->point.y-level_y>.2f || level_y-floor->point.y>.35f || floor->normal.y<.7f)continue;
        if(const auto water=g.level.water_at(floor->point+Vec3{0,.2f,0});water && water->height>floor->point.y+.05f)continue;
        g.player.position=g.player.previous_position=floor->point+Vec3{0,0.01f,0};g.player.velocity={};
        g.player.yaw=g.player.previous_yaw=std::atan2(-d.x,-d.z);return true;
    }
    return false;
}
void keep_alive(Game& g){g.player.health=std::max(g.player.health,3u);g.player.invulnerable_ticks=std::max(g.player.invulnerable_ticks,4u);}
// Zoe (class 110) ignores a Spyro who was already within 0x1000 when she woke
// until he has moved 0x1800 away (80073d40). A player always arrives from
// further off, so stand 8-14 units away on any floor first and let her wake.
bool approach_from_afar(Game& g,const RetailObject& o) {
    for(const float r:{8.0f,10.0f,12.0f,14.0f})for(unsigned k=0;k<16;++k) {
        const float a=k*3.14159265f/8;const Vec3 at=o.position+Vec3{std::sin(a)*r,0,std::cos(a)*r};
        const auto floor=g.level.raycast(at+Vec3{0,6,0},{0,-1,0},14);
        if(!floor || floor->normal.y<.7f)continue;
        g.player.position=g.player.previous_position=floor->point+Vec3{0,0.01f,0};g.player.velocity={};
        for(unsigned t=0;t<90;++t){keep_alive(g);g.advance(FixedClock::step,{});}
        return true;
    }
    return false;
}
}
int main(int argc,char** argv) {
    if(argc<2){std::cerr<<"Usage: spyro_script_quests <retail data> [slot ...]\n";return 2;}
    try {
        AssetStore assets(argv[1]);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25");
        std::vector<unsigned> slots;for(int i=2;i<argc;++i)slots.push_back(std::stoul(argv[i]));
        if(slots.empty())for(unsigned s=15;s<=69;s+=2)slots.push_back(s);
        for(const auto slot:slots) {
            if(!retail_level_id(slot))continue;
            const auto archive=wad.read(slot);
            Level level=decode_level(archive,slot);decode_travel(archive,exe,level);auto models=decode_object_models(archive,exe);
            initialize_retail_objects(level,models);level.script_image=make_retail_script_image(wad,exe,slot);
            auto speakers=decode_npcs(archive,slot);
            // Script-owned speakers (class 110: Zoe and talk triggers) open
            // their own conversations through 8003a98c / 80015be4.
            for(unsigned i=0;i<level.objects.size();++i)if(level.objects[i].type==0x110)
                if(auto npc=decode_npc_record(archive,slot,i)){npc->object=i;speakers.push_back(*npc);}
            Scene scene;scene.wad_slot=slot;scene.spawn=level.spawn;Game g(level,scene,{},&models,nullptr);
            g.player.gems=2000;   // purchases and Moneybags payments can be accepted
            for(unsigned t=0;t<4;++t){keep_alive(g);g.advance(FixedClock::step,{});}
            std::cout<<"slot "<<slot<<" speakers "<<speakers.size()<<" bridge "<<g.overlay_scripts.dialogue_bridge()<<"\n";
            unsigned opened=0,failed=0;
            for(const auto& npc:speakers) {
                if(npc.object>=g.retail_objects.size() || g.retail_objects[npc.object].state<0)continue;
                const auto state=[&](unsigned i){return static_cast<std::int8_t>(g.overlay_scripts.peek(g.overlay_scripts.moby_address(i)+0x48,1));};
                std::vector<int> before;for(unsigned i=0;i<level.objects.size();++i)before.push_back(state(i));
                const auto shots=g.overlay_scripts.camera_shots,warps=g.overlay_scripts.warps,faults=g.overlay_scripts.faults,checkpoints=g.overlay_scripts.checkpoints;
                const auto orbs=g.player.orbs,dialogues=g.overlay_scripts.dialogues;
                // Class 110 keeps its state block at param+18; byte 2 is the talk mode
                // (0 silent checkpoint, 1 triangle, 2 automatic), read before the visit.
                const auto mode=npc.type==0x110?g.overlay_scripts.peek(g.overlay_scripts.peek(g.overlay_scripts.moby_address(npc.object))+0x1a,1):0u;
                if(npc.type==0x110)approach_from_afar(g,g.retail_objects[npc.object]);
                bool open=false,near_seen=false;
                for(unsigned t=0;t<240 && !g.dialogue.active();++t) {
                    if(t%30==0 && !in_front(g,g.retail_objects[npc.object],0.9f))break;
                    keep_alive(g);InputState in;in.dialogue.interact=(t%8)==0;g.advance(FixedClock::step,in);
                    near_seen|=g.dialogue.nearby();
                }
                open=g.dialogue.active();
                const bool retail=open && g.dialogue.retail_driven();
                unsigned lines=0;
                for(unsigned step=0;step<60 && g.dialogue.active();++step) {
                    for(unsigned t=0;t<14;++t){keep_alive(g);g.advance(FixedClock::step,{});}
                    InputState in;const auto* line=g.dialogue.line();
                    if(line && line->yes>=0 && line->no>=0)in.dialogue.choice=0;else in.dialogue.next=true;
                    keep_alive(g);g.advance(FixedClock::step,in);++lines;
                }
                const bool stuck=g.dialogue.active();

                for(unsigned t=0;t<360;++t){keep_alive(g);g.advance(FixedClock::step,{});}
                unsigned woke=0;for(unsigned i=0;i<level.objects.size();++i)if(i!=npc.object && before[i]!=state(i) && g.retail_objects[i].retail_script)++woke;
                open?++opened:++failed;
                std::cout<<"  "<<(open?(retail?"talk  ":"NATIVE"):"NOTALK")<<(stuck?" STUCK":"")<<" object "<<npc.object<<" type "<<std::hex<<npc.type<<std::dec
                    <<" '"<<npc.name<<"'"<<(npc.type==0x110?" mode "+std::to_string(mode):"")<<" lines "<<lines<<" shots +"<<g.overlay_scripts.camera_shots-shots<<" warps +"<<g.overlay_scripts.warps-warps
                    <<" checkpoints +"<<g.overlay_scripts.checkpoints-checkpoints<<" woke "<<woke<<" orbs +"<<g.player.orbs-orbs<<" script-dialogues +"<<g.overlay_scripts.dialogues-dialogues
                    <<(g.overlay_scripts.faults>faults?" FAULT "+g.overlay_scripts.fault:"")
                    <<(g.overlay_scripts.rejected_dialogues?" rejected "+std::to_string(g.overlay_scripts.rejected_dialogues)+" (speaker "+std::to_string(g.overlay_scripts.rejected_speaker)+" line "+std::to_string(g.overlay_scripts.rejected_line)+")":"")<<"\n";
                g.overlay_scripts.rejected_dialogues=0;
                if(!open && std::getenv("QUEST_TRACE")) {
                    const auto& o=g.retail_objects[npc.object];
                    const bool native=std::any_of(level.npcs.begin(),level.npcs.end(),[&](const auto& n){return n.object==npc.object;});
                    std::cout<<"    native "<<native<<" script "<<o.retail_script<<" state "<<int(o.state)<<" nearby-seen "<<near_seen
                        <<" spyro "<<g.player.position.x<<','<<g.player.position.y<<','<<g.player.position.z<<" npc "<<o.position.x<<','<<o.position.y<<','<<o.position.z
                        <<" ram-state "<<int(static_cast<std::int8_t>(g.overlay_scripts.peek(g.overlay_scripts.moby_address(npc.object)+0x48,1)))
                        <<" rec56 "<<static_cast<std::int16_t>(o.record[56]|(o.record[57]<<8))<<" grounded "<<g.player.grounded<<" scene-locked "<<g.scene_locked<<" dialogue-active "<<g.dialogue.active();
                    const auto under=g.level.raycast(o.position+Vec3{0,1,0},{0,-1,0},6);
                    std::cout<<" floor-under-npc "<<(under?under->point.y:-999.0f)<<"\n";
                    {   // Retry by hand: place, press triangle, report what the dialogue system sees.
                        const bool placed=in_front(g,g.retail_objects[npc.object],0.9f);
                        for(unsigned t=0;t<6;++t){InputState in;in.dialogue.interact=true;g.advance(FixedClock::step,in);
                            std::cout<<"    retry t"<<t<<" placed "<<placed<<" grounded "<<g.player.grounded<<" nearby "<<g.dialogue.nearby()<<" active "<<g.dialogue.active()
                                     <<" spyro "<<g.player.position.x<<","<<g.player.position.y<<","<<g.player.position.z<<"\n";}
                    }
                    {   // The native speaker test (NpcInteractions): distance, feet band and line of sight.
                        const auto centre=g.player.position+retail::origin_above_feet;const auto d=o.position-centre;
                        const float hi=std::max(std::abs(d.x),std::abs(d.z)),lo=std::min(std::abs(d.x),std::abs(d.z));
                        std::cout<<"    native test: distance "<<hi+lo*.375f<<" centre y "<<centre.y<<" npc y "<<o.position.y;
                        for(const auto& [from,to]:{std::pair{centre,o.position+Vec3{0,.6f,0}},std::pair{centre+Vec3{0,.5f,0},o.position+Vec3{0,1.1f,0}}}) {
                            const auto arm=to-from;const auto hit=g.level.raycast(from,arm,length(arm));
                            std::cout<<" | line "<<length(arm)<<" hit "<<(hit?hit->distance:-1.0f);
                            if(hit)std::cout<<" at "<<hit->point.x<<","<<hit->point.y<<","<<hit->point.z;
                        }
                        std::cout<<"\n";
                    }
                    for(std::size_t k=0;k<npc.lines.size() && k<4;++k)std::cout<<"    says "<<k<<": "<<npc.lines[k].text.substr(0,140)<<"\n";
                    std::cout<<"    floor ring (r: y/normal.y):";
                    for(const float r:{0.6f,1.0f,1.5f,2.0f}) {
                        std::cout<<" r"<<r<<":";
                        for(unsigned k=0;k<8;++k){const float a=k*0.785f;const auto at=o.position+Vec3{std::sin(a)*r,0,std::cos(a)*r};
                            const auto f=g.level.raycast(at+Vec3{0,2,0},{0,-1,0},6);
                            if(f)std::cout<<' '<<std::lround((f->point.y-o.position.y)*100)<<'/'<<std::lround(f->normal.y*100);else std::cout<<" -";}
                    }
                    std::cout<<"\n";
                }
                if(!open && g.overlay_scripts.dialogues>dialogues && std::getenv("QUEST_TRACE")) {
                    const auto& v=g.overlay_scripts;const auto m=v.moby_address(npc.object);const auto h=v.peek(m);
                    std::cout<<std::hex<<"    moby "<<m<<" header "<<h<<" +2 "<<v.peek(h+2,1)<<" +4 "<<v.peek(h+4,1)
                        <<" dialogue moby 80066f78="<<v.peek(0x80066f78)<<" game state "<<v.peek(0x800681c8)<<std::dec<<"\n";
                    std::cout<<std::hex<<"    header bytes:";for(unsigned k=0;k<24;++k)std::cout<<' '<<v.peek(h+k,1);std::cout<<"\n";
                    const auto l0=v.peek(h+16);std::cout<<"    line0 @"<<l0<<":";for(unsigned k=0;k<48;++k)std::cout<<' '<<v.peek(l0+k,1);std::cout<<std::dec<<"\n";
                    for(std::size_t k=0;k<npc.lines.size() && k<6;++k)std::cout<<"    line "<<k<<" ["<<npc.lines[k].text.size()<<"] "<<npc.lines[k].text.substr(0,90)
                        <<" next "<<npc.lines[k].next<<" repeat "<<npc.lines[k].repeat<<"\n";
                }
                while(g.dialogue.active()){InputState in;in.dialogue.next=true;g.advance(FixedClock::step,in);if(++lines>400)break;}
            }
            std::cout<<"  opened "<<opened<<" / "<<opened+failed<<"\n"<<std::flush;
        }
        return 0;
    } catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}
