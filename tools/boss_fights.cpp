// Crush (31) and Gulp (55) played through their original overlay scripts with
// ordinary inputs: Spyro is teleported near the boss (teleports stand in for
// walking) and kept alive, then
//   Crush: faces him and breathes fire (flame bit 0x10000 hurts him);
//   Gulp:  picks up an item lying in the arena (see pickable) and spits
//          it into him (his state 15 takes 1 health per projectile).
// Reports every health change (80066fec), the boss HUD and the level's end.
// Usage: spyro_boss_fights <retail data> [31] [55]    (SPAWNS=1 lists spawns)
#include "spyro/assets.hpp"
#include "spyro/retail_scripts.hpp"
#include "spyro/runtime.hpp"
#include "spyro/travel.hpp"
#include "spyro/campaign.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <numbers>

using namespace spyro;
namespace {
void keep_alive(Game& g){g.player.health=std::max(g.player.health,3u);g.player.lives=std::max(g.player.lives,4u);g.player.invulnerable_ticks=std::max(g.player.invulnerable_ticks,2u);}
unsigned health(const Game& g){return g.overlay_scripts.peek(0x80066fec,4);}
// Arena objects Spyro can take into his mouth in Gulp's fight (PICKUP=hex,...
// overrides): the dropped items (196, 19E, 1AE); spat, they hit as 197/198.
bool pickable(unsigned type) {
    if(const char* list=std::getenv("PICKUP")) {
        for(const char* at=list;*at;){char* end=nullptr;const auto v=std::strtoul(at,&end,16);if(end==at)break;if(v==type)return true;at=*end==','?end+1:end;}
        return false;
    }
    return type==0x197;
}
void face(Game& g,Vec3 at){const auto v=at-g.player.position;g.player.yaw=g.player.previous_yaw=std::atan2(v.x,v.z);}
// Spyro on real floor `distance` from `at`, facing it.
bool park(Game& g,Vec3 at,float distance,float bias=0) {
    for(unsigned k=0;k<16;++k) {
        const float a=bias+k*std::numbers::pi_v<float>/8;const Vec3 p=at+Vec3{std::sin(a)*distance,0,std::cos(a)*distance};
        if(const auto hit=g.level.raycast(p+Vec3{0,3,0},{0,-1,0},8);hit && hit->normal.y>.6f && std::abs(hit->point.y-at.y)<2.5f) {
            g.player.position=g.player.previous_position=hit->point+Vec3{0,.01f,0};g.player.velocity={};face(g,at);
            g.camera.reset(g.level,g.player.position,g.player.yaw);return true;
        }
    }
    return false;
}
}
int main(int argc,char** argv) {
    if(argc<2){std::cerr<<"Usage: spyro_boss_fights <retail data> [31] [55]\n";return 2;}
    try {
        AssetStore assets(argv[1]);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25");
        std::vector<unsigned> slots;for(int i=2;i<argc;++i)slots.push_back(std::stoul(argv[i]));
        if(slots.empty())slots={31,55};
        bool all_ok=true;
        for(const auto slot:slots) {
            const auto archive=wad.read(slot);
            Level level=decode_level(archive,slot);decode_travel(archive,exe,level);auto models=decode_object_models(archive,exe);
            initialize_retail_objects(level,models);level.script_image=make_retail_script_image(wad,exe,slot);
            Scene scene;scene.wad_slot=slot;scene.spawn=level.spawn;Game g(level,scene,{},&models,nullptr);
            const unsigned boss=0;   // Crush (8F) and Gulp (191) are record 0
            std::cout<<"slot "<<slot<<" boss class "<<std::hex<<g.retail_objects[boss].type<<std::dec<<"\n";
            unsigned last=UINT32_MAX,flames=0,spits=0,pickups=0,kicks=0;int kick=0;std::map<unsigned,unsigned> spawns;
            bool hud_seen=false,defeated=false;unsigned end_tick=0;
            for(unsigned t=0;t<36000 && !end_tick;++t) {
                keep_alive(g);InputState in;
                if(g.dialogue.active() && t%15==0)in.dialogue.next=true;
                const auto& b=g.retail_objects[boss];
                if(slot==31) {
                    // Crush: stay 2.2 units from him, face him, breathe fire twice a second.
                    if(t%40==0 && length(b.position-g.player.position)>3.2f)park(g,b.position,2.2f,float(t%7));
                    face(g,b.position);
                    if(t%30==0){in.flame_pressed=true;++flames;}
                } else {
                    // Gulp. His drops (199 -> 19E -> by phase, 8007792c): bombs (196)
                    // that a charge kicks along Spyro's heading (800745ec), and in
                    // later phases items (197) to pick up and spit into him.
                    const auto& s=g.overlay_scripts;
                    unsigned bomb=UINT32_MAX;float bomb_near=1e9f;
                    for(unsigned i=level.objects.size();i<g.retail_objects.size();++i) {
                        const auto& o=g.retail_objects[i];
                        if(o.state!=0 || o.collected || o.type!=0x196)continue;
                        const float d=length(o.position-g.player.position);if(d<bomb_near){bomb_near=d;bomb=i;}
                    }
                    if(!s.carrying() && bomb!=UINT32_MAX && kick<=0) {
                        // Line up 1.6 behind the bomb (away from Gulp), facing Gulp, and charge through it.
                        const auto at=g.retail_objects[bomb].position;auto away=at-b.position;away.y=0;away=normalized(away);
                        const auto from=at+away*1.6f;
                        if(const auto f=g.level.raycast(from+Vec3{0,2,0},{0,-1,0},5)) {
                            g.player.position=g.player.previous_position=f->point+Vec3{0,.01f,0};g.player.velocity={};face(g,b.position);
                            g.camera.reset(g.level,g.player.position,g.player.yaw);kick=45;++kicks;
                        }
                    }
                    if(kick>0) {
                        --kick;auto v=b.position-g.player.position;v.y=0;v=normalized(v);
                        in.charge=true;in.move_x=v.x;in.move_z=v.z;in.camera_relative=false;
                    }
                    else if(!s.carrying()) {
                        unsigned best=UINT32_MAX;float near=1e9f;
                        for(unsigned i=level.objects.size();i<g.retail_objects.size();++i) {
                            const auto& o=g.retail_objects[i];
                            if(o.state<0 || o.collected || !pickable(o.type))continue;
                            const float d=length(o.position-g.player.position);if(d<near){near=d;best=i;}
                        }
                        if(best==UINT32_MAX) {
                            // Nothing to pick up yet: run circles (the fight waits for a moving Spyro).
                            const float a=t*.02f;in.move_x=std::sin(a);in.move_z=std::cos(a);
                        } else {
                            const auto at=g.retail_objects[best].position;
                            if(near>2.5f)park(g,at,1.4f,float(t%5));
                            const auto v=at-g.player.position;const float n=std::hypot(v.x,v.z);
                            if(n>.6f){in.move_x=v.x/n*.4f;in.move_z=v.z/n*.4f;}
                            face(g,at);
                            if(std::getenv("TRACE") && t%60==0) {
                                const auto& o=g.retail_objects[best];
                                std::printf("  t %u item %u class %x state %d dist %.2f y %.2f spyro y %.2f head %x request %x\n",t,best,o.type,int(o.state),near,
                                    o.position.y,g.player.position.y,s.peek(0x8006a15c,4),s.peek(0x8006a1fc,4));
                            }
                        }
                    } else {
                        if(length(b.position-g.player.position)>6.5f || t%240==0)park(g,b.position,4.5f,float(t%9));
                        face(g,b.position);
                        if(t%20==0){in.flame_pressed=true;++spits;}
                    }
                }
                const auto before=g.overlay_scripts.pickups;
                g.advance(FixedClock::step,in);
                if(g.overlay_scripts.pickups>before)++pickups;
                if(std::getenv("SPAWNS"))for(unsigned i=level.objects.size();i<g.retail_objects.size();++i)
                    if(g.retail_objects[i].state>=0 && spawns.try_emplace(g.retail_objects[i].type,t).second)
                        std::cout<<"  spawn class "<<std::hex<<g.retail_objects[i].type<<std::dec<<" at tick "<<t<<"\n";
                hud_seen|=g.hud.boss_visible();
                const auto h=health(g);
                if(h!=last){std::cout<<"  tick "<<t<<" health "<<h<<" (hud "<<g.hud.boss_counter().value<<")\n";last=h;}
                if(!defeated && last==0 && t>60){defeated=true;std::cout<<"  boss health exhausted at tick "<<t<<"\n";break;}
            }
            // Aftermath: the original death sequence plays out and requests its
            // outro cutscene (80019230: Crush 7, Gulp 11); the engine then
            // travels to the home the original continues in. Arrival is
            // checked by loading that slot as the workbench does.
            for(unsigned t=0;defeated && t<6000 && !end_tick;++t) {
                keep_alive(g);InputState in;
                if(g.dialogue.active() && t%15==0)in.dialogue.next=true;
                g.advance(FixedClock::step,in);
                if(t%600==0)std::cout<<"  after +"<<t<<": boss state "<<int(g.retail_objects[boss].state)<<", dialogue "<<g.dialogue.active()<<", locked "<<g.scene_locked<<"\n";
                if(g.portal_travel.phase==PortalTravel::Phase::Loading)end_tick=t+1;
            }
            bool arrived=false;
            if(end_tick) {
                const auto request=g.portal_travel.request;
                std::cout<<"  outro cutscene "<<g.overlay_scripts.last_cutscene<<" requested; boss award "<<g.guide_awards.boss
                         <<"; departure took "<<end_tick<<" ticks after the defeat; destination slot "<<request.destination<<"\n";
                const auto next_archive=wad.read(request.destination);
                Level next=decode_level(next_archive,request.destination);decode_travel(next_archive,exe,next);auto next_models=decode_object_models(next_archive,exe);
                initialize_retail_objects(next,next_models);next.script_image=make_retail_script_image(wad,exe,request.destination);
                const auto at=campaign_arrival(next_archive,request.source_scene,next);
                Scene next_scene;next_scene.wad_slot=request.destination;next_scene.spawn=at.position;
                Game h(next,next_scene,{},&next_models,nullptr);
                h.player.yaw=h.player.previous_yaw=at.yaw;
                begin_portal_arrival(h,request);
                for(unsigned t=0;t<240;++t)h.advance(FixedClock::step,{});
                arrived=!h.portal_travel.active() && h.player.grounded && h.player.health>0;
                std::cout<<"  arrival in slot "<<request.destination<<": travel done "<<!h.portal_travel.active()<<", grounded "<<h.player.grounded
                         <<", position "<<h.player.position.x<<" "<<h.player.position.y<<" "<<h.player.position.z<<"\n";
            }
            const bool ok=hud_seen && defeated && end_tick && g.guide_awards.boss && arrived;
            std::cout<<"slot "<<slot<<(ok?" OK":" INCOMPLETE")<<": hud "<<hud_seen<<", flames "<<flames<<", pickups "<<pickups<<", spits "<<spits<<", charges "<<kicks
                     <<", final health "<<last<<", ended "<<(end_tick?"yes":"no")<<", arrived "<<(arrived?"yes":"no")<<"\n";
            all_ok&=ok;
        }
        return all_ok?0:1;
    } catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}
