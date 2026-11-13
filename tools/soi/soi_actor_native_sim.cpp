// Full native Game simulation with retained core library and original scripts.
// Starting health, source approaches and named quest RAM hits are fixtures.
// Physical-input UI checks and original PS1 rendering are separate evidence.
#include "spyro/assets.hpp"
#include "spyro/game_build.hpp"
#include "spyro/retail_scripts.hpp"
#include "spyro/runtime.hpp"
#include "spyro/savegame.hpp"
#include "spyro/travel.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <set>
using namespace spyro;
Bytes bytes(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);if(!f)throw FormatError("Cannot read "+p.string());return Bytes(std::istreambuf_iterator<char>(f),{});}
void need(bool b,const std::string&s){if(!b)throw FormatError(s);}
void ticks(Game&g,unsigned n,InputState in={}){for(unsigned i=0;i<n;++i)g.advance(FixedClock::step,in);}
struct Content{
    Level level;ObjectModels models;SpyroModel spyro;SoundBank sounds;
    Content(const char*arc,const char*ov){
        AssetStore store("data/disc");WadArchive wad(store);const auto exe=store.read_all(find_game_executable(store)),a=bytes(arc);
        level=decode_level(a,17);decode_travel(a,exe,level);models=decode_object_models(a,exe);sounds=decode_sound_bank(a,exe,17);spyro=decode_spyro(wad.read(0),a,exe);initialize_retail_objects(level,models);
        auto image=std::make_shared<RetailScriptImage>();image->executable=exe;image->loader=wad.read(1);image->overlay=bytes(ov);image->archive=a;image->slot=17;image->level_id=11;level.script_image=image;
    }
    Game game()const{Scene s;s.wad_slot=17;s.spawn=level.spawn;return Game(level,s,{},&models,&spyro);}
};
void beside(Game&g,unsigned index,float distance=.8f){
    const auto o=g.retail_objects.at(index).position;
    for(unsigned k=0;k<16;++k){const float a=k*std::numbers::pi_v<float>/8;const Vec3 at=o+Vec3{std::cos(a)*distance,0,std::sin(a)*distance};
        if(const auto hit=g.level.raycast(at+Vec3{0,1.5f,0},{0,-1,0},3);hit && hit->normal.y>.7f && std::abs(hit->point.y-o.y)<1.3f){
            g.player.position=g.player.previous_position=hit->point+Vec3{0,.02f,0};g.player.velocity={};g.player.yaw=g.player.previous_yaw=std::atan2(o.x-at.x,o.z-at.z);g.camera.reset(g.level,g.player.position,g.player.yaw);return;
        }
    }
    std::cerr<<"APPROACH DIAGNOSTIC index "<<index<<" origin "<<o.x<<' '<<o.y<<' '<<o.z<<'\n';
    for(unsigned k=0;k<4;++k){const float a=k*std::numbers::pi_v<float>/2;const Vec3 at=o+Vec3{std::cos(a)*distance,0,std::sin(a)*distance};
        const auto hit=g.level.raycast(at+Vec3{0,1.5f,0},{0,-1,0},3);
        if(hit)std::cerr<<"APPROACH RAY "<<k<<" height "<<hit->point.y<<" normal "<<hit->normal.y<<'\n';else std::cerr<<"APPROACH RAY "<<k<<" no hit\n";
    }
    throw FormatError("No supported native approach "+std::to_string(index));
}
int main(int argc,char**argv){try{
    need(argc==4 || argc==5,"usage: soi_actor_native_sim ARCHIVE OVERLAY OUTPUT_DIR [BELL_SOUND_DIR]");Content content(argv[1],argv[2]);const std::filesystem::path out=argv[3];std::filesystem::create_directories(out);
    if(argc==5){
        const auto& ids=content.sounds.object_sounds.at(0x2e6);need(ids.size()>=3,"Bell local sound slots");
        for(unsigned i=0;i<3;++i){const auto raw=bytes(std::filesystem::path(argv[4])/("bell-"+std::to_string(i)+"-decoded.pcm"));const auto& sample=content.sounds.sounds.at(ids[i]);
            need(raw.size()==sample.pcm.samples.size()*2,"Source bell native PCM length");
            for(unsigned k=0;k<sample.pcm.samples.size();++k)need(static_cast<std::uint16_t>(sample.pcm.samples[k])==std::uint16_t(raw[k*2]|raw[k*2+1]<<8),"Source bell native SPU PCM mismatch");
            need(sample.pitch_min==2048 && sample.pitch_max==2048 && !sample.looping,"Source bell descriptor pitch/loop");
            std::cout<<"NATIVE SOURCE BELL PCM ordinal "<<i<<" sample "<<unsigned(ids[i])<<" exact signed16 samples "<<sample.pcm.samples.size()<<" pitch2048 (22050Hz), one-shot\n";
        }
    }
    for(unsigned index:{246u,247u,248u})for(unsigned health:{0u,2u}){
        auto g=content.game();beside(g,index);g.player.health=health;
        // A scene-start fixture must initialize Sparx at that start too, as
        // the real S2SHOT/scene constructor does. Teleporting only Spyro left
        // Sparx on the entrance terrace behind intervening walls.
        g.sparx.reset(g.retail_objects,&content.models,g.player.position+retail::origin_above_feet,g.player.yaw,health);ticks(g,2);
        std::set<unsigned> frames;unsigned food=0,flaming=0,queries=0,dialogue=0;
        for(unsigned t=0;t<1000;++t){InputState input;input.flame_pressed=t==8 || t==36 || t==72;ticks(g,1,input);
            frames.insert(g.retail_objects[index].animation.frame);flaming+=g.interactions.flame_active();queries+=g.interactions.flame_query_points().size();dialogue+=g.dialogue.active();
            for(const auto&o:g.retail_objects)if(o.runtime_spawned && o.type==0x10)food=std::max(food,1u);
        }
        need(g.overlay_scripts.faults==0,"Native critter script fault "+g.overlay_scripts.fault);
        if(!(food && g.sparx.butterflies_eaten()==1 && g.player.health==health+1)){
            const auto m=g.overlay_scripts.moby_address(index);std::cerr<<"CRITTER DIAGNOSTIC food "<<food<<" state "<<g.overlay_scripts.peek(m+72,1)<<" body "<<std::hex<<g.overlay_scripts.peek(m+8)<<" hit "<<g.overlay_scripts.peek(m+24)<<std::dec<<" faults "<<g.overlay_scripts.faults<<" frames "<<frames.size()<<" flame ticks "<<flaming<<" queries "<<queries<<" dialogue "<<dialogue<<" player "<<g.player.position.x<<' '<<g.player.position.y<<' '<<g.player.position.z<<" actor "<<g.retail_objects[index].position.x<<' '<<g.retail_objects[index].position.y<<' '<<g.retail_objects[index].position.z<<'\n';
            std::cerr<<"SPARX feeding starts "<<g.sparx.feeding_starts()<<" clips "<<content.models.models.at(0x78).animations.size()<<'\n';
            for(const auto&o:g.retail_objects)if(o.runtime_spawned && (o.type==0x10 || o.type==0xd3 || o.type==0x78))std::cerr<<"FOOD/SPARX type "<<o.type<<" state "<<int(o.state)<<" collected "<<o.collected<<" animation "<<int(o.animation.clip)<<' '<<int(o.animation.frame)<<" pos "<<o.position.x<<' '<<o.position.y<<' '<<o.position.z<<'\n';
        }
        need(food && g.sparx.butterflies_eaten()==1 && g.player.health==health+1,"Native critter healing source "+std::to_string(index)+" start "+std::to_string(health)+" final "+std::to_string(g.player.health)+" eaten "+std::to_string(g.sparx.butterflies_eaten()));
        need(g.overlay_scripts.gems==0,"Critter released a gem");
        const auto m=g.overlay_scripts.moby_address(index);need(g.overlay_scripts.peek(m+72,1)==7 && g.retail_objects[index].script_undrawn,"Critter did not enter hidden cooldown");
        std::cout<<"NATIVE CRITTER index "<<index<<" initial-health-fixture "<<health<<" final "<<g.player.health<<" actual flame input; class10 spawned; eaten exactly1; script gems0; hidden cooldown; animation frames "<<frames.size()<<'\n';
    }
    // Source bell sequence through the actual native bridge. Hits are explicit
    // post-collision RAM fixtures; the state, animation and save paths are real.
    {
        auto g=content.game();beside(g,263,1.1f);ticks(g,2);const unsigned controller=264,portal=265;
        auto& scripts=g.overlay_scripts;const auto cm=scripts.moby_address(controller),q=scripts.peek(cm);
        std::array<unsigned,3> bell_events{};
        const auto ring=[&](unsigned n){const auto index=261+n;beside(g,index,1.1f);ticks(g,2);g.sound_events.clear();scripts.poke(scripts.moby_address(index)+24,0x10000);
            for(unsigned t=0;t<40;++t){ticks(g,1);for(const auto&e:g.sound_events)if(e.scope==SoundScope::Object && e.object_type==0x2e6){need(e.id==n,"Bell requested wrong local pitch slot");++bell_events[n];}g.sound_events.clear();}
        };
        ring(0);need(scripts.peek(q+28)==0 && scripts.peek(q+32)==0,"Wrong bell did not reset native quest");
        for(unsigned n:{2u,1u,0u,2u,1u,0u})ring(n);
        need(scripts.peek(q+32)==1,"Native source quest not completed");beside(g,portal,1.1f);ticks(g,4);
        need(!g.retail_objects[portal].script_undrawn,"Native saved gate portal visual closed");
        GameSave save;save.capture(g);const auto file=out/"source-quest-complete.s2save";save.write(file);const auto restored=GameSave::read(file);
        auto next=content.game();restored.apply(next,&content.models);ticks(next,4);beside(next,portal,1.1f);ticks(next,4);
        const auto nq=next.overlay_scripts.peek(next.overlay_scripts.moby_address(controller));
        need(next.overlay_scripts.peek(nq+32)==1 && !next.retail_objects[portal].script_undrawn && next.overlay_scripts.faults==0,"Native saved source portal gate did not restore");
        need(bell_events==std::array<unsigned,3>{3,2,2},"Bell sound emitted more/less than once per accepted ring");
        std::cout<<"NATIVE SOURCE QUEST actual GameSave write/read/apply restored completion and open portal visual; file "<<file.string()<<"; named bell RAM hits; source-pitched sound events3/2/2 exactly once per ring; parent travel binding separate\n";
    }
    if(argc==5){
        auto g=content.game();ticks(g,2);const auto q=g.overlay_scripts.moby_address(264);need(q!=0,"Physical bell bridge not initialized");
        for(unsigned n:{2u,1u,0u,2u,1u,0u}){
            beside(g,261+n,1.1f);g.player.invulnerable_ticks=600;ticks(g,4);
            // The source bells hang 976 units above this terrace. Jump to
            // their real height before flaming; a ground-only breath passes
            // below their authored collision envelope.
            for(unsigned t=0;t<90;++t){InputState in;in.jump_pressed=t==0;in.jump_held=t<12;in.flame_pressed=t==10;ticks(g,1,in);
            }
            need(g.overlay_scripts.faults==0,"Physical source bell script fault");
            std::cout<<"NATIVE BELL JUMP/FLAME ordinal "<<n<<" progress "<<g.overlay_scripts.peek(g.overlay_scripts.peek(q)+28)<<" complete "<<g.overlay_scripts.peek(g.overlay_scripts.peek(q)+32)<<'\n';
        }
        need(g.overlay_scripts.peek(g.overlay_scripts.peek(q)+32)==1,"Actual native flame input did not finish source bell sequence");
        std::cout<<"NATIVE SOURCE QUEST PHYSICAL JUMP/FLAME six source-position approaches, 2/1/0/2/1/0 accepted; no hit-mailbox writes; explicit spawn/invulnerability fixtures\n";
    }
    for(unsigned index:{225u,234u,245u}){
        auto g=content.game();beside(g,index);g.player.invulnerable_ticks=600;ticks(g,2);
        const float yaw=g.player.yaw;
        for(unsigned t=0;t<240;++t){InputState in;in.charge=t>=12 && t<52;if(in.charge){in.move_x=std::sin(yaw);in.move_z=std::cos(yaw);}ticks(g,1,in);}
        const auto m=g.overlay_scripts.moby_address(index);
        need(g.overlay_scripts.faults==0 && g.overlay_scripts.peek(m+72,1)==0xfd && g.overlay_scripts.gems==1,"Native physical charge death/reward failed "+std::to_string(index));
        std::cout<<"NATIVE ENEMY CHARGE index "<<index<<" actual controller movement/charge, original adapter stateFD and one gem release; no hit-mailbox writes; supported spawn/invulnerability fixtures\n";
    }
    // Reproduce the unowned engine's pending script-gem save restriction at
    // the first release frame, before native Sparx can collect the reward.
    {
        auto g=content.game();beside(g,225);ticks(g,2);g.retail_objects[225].attack_flags=flame_attack;
        for(unsigned t=0;t<150 && !g.overlay_scripts.gems;++t)ticks(g,1);
        need(g.overlay_scripts.gems==1,"Pending script-gem fixture did not release");
        bool pending=false;for(const auto&o:g.retail_objects)if(o.script_gem && !o.collected && o.state>=0){pending=true;std::cout<<"PENDING SCRIPT GEM source_id "<<o.source_id<<" value "<<o.gem_value<<" parent225; native bridge assigned UINT32_MAX\n";}
        need(pending,"Pending reward already collected");
        try{GameSave save;save.capture(g);std::cout<<"PENDING SCRIPT GEM SAVE accepted\n";}
        catch(const std::exception&e){std::cout<<"UNOWNED ENGINE REPRO GameSave::capture: "<<e.what()<<"; src/retail_scripts.cpp:1232 source_id UINT32_MAX; src/savegame.cpp:73 pending-source validation\n";}
    }
    std::cout<<"NATIVE CONTENT SIMULATION CHECK COMPLETE; runtime Game/controller/save tests; fixture boundaries above; no manual acceptance\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
