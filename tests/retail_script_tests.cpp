// Original overlay moby scripts running in the interpreter (RetailScripts):
// every campaign level boots, owns its non-native mobys, and the bridges
// work both ways (engine attacks reach the scripts; scripts hurt Spyro).
#include "spyro/assets.hpp"
#include "spyro/game_build.hpp"
#include "spyro/overlay_vm.hpp"
#include "spyro/retail_scripts.hpp"
#include "spyro/runtime.hpp"
#include "spyro/spyro_model.hpp"
#include "spyro/travel.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <optional>

using namespace spyro;
namespace {
unsigned checks{};
void check(bool condition,const std::string& message){++checks;if(!condition)throw std::runtime_error(message);}
void ticks(Game& g,unsigned n,InputState in={}){for(unsigned i=0;i<n;++i)g.advance(FixedClock::step,in);}
struct Retail {
    Level level;ObjectModels models;
    Retail(AssetStore& assets,WadArchive& wad,unsigned slot) {
        const auto archive=wad.read(slot),exe=assets.read_all("SCUS_944.25");
        level=decode_level(archive,slot);decode_travel(archive,exe,level);models=decode_object_models(archive,exe);
        initialize_retail_objects(level,models);
        level.script_image=make_retail_script_image(wad,exe,slot);
    }
    Game game()const{Scene s;s.wad_slot=level.wad_slot;s.spawn=level.spawn;return Game(level,s,{},&models,nullptr);}
};
// First authored spirit-bearing moby of `type`.
unsigned record(const Level& level,unsigned type) {
    for(unsigned i=0;i<level.objects.size();++i)if(level.objects[i].type==type && level.objects[i].state>=0 && (level.objects[i].record[83]&127))return i;
    throw std::runtime_error("No authored record of class "+std::to_string(type));
}
// Park Spyro in front of a speaker (8003a98c needs him inside its facing arc).
void in_front(Game& g,const RetailObject& o,float distance) {
    const float yaw=retail::native_heading(o.rotation[2]*16);const Vec3 f{std::sin(yaw),0,std::cos(yaw)};
    for(float spread:{0.0f,0.4f,-0.4f,0.8f,-0.8f}) {
        const Vec3 d{std::sin(yaw+spread),0,std::cos(yaw+spread)};
        const auto floor=g.level.raycast(o.position+d*distance+Vec3{0,1.5f,0},{0,-1,0},4);
        if(!floor)continue;
        g.player.position=g.player.previous_position=floor->point+Vec3{0,0.01f,0};g.player.velocity={};
        g.player.yaw=g.player.previous_yaw=std::atan2(-d.x,-d.z);
        // Walking up, the camera follows: the speaker is drawn (+4D), which
        // activates it and lets 8003a98c open its conversation.
        g.camera.reset(g.level,g.player.position,g.player.yaw);return;
    }
    (void)f;throw std::runtime_error("No floor in front of the speaker");
}
// Park Spyro on the floor beside an object, facing it.
void beside(Game& g,const RetailObject& o,float distance) {
    for(unsigned i=0;i<16;++i) {
        const float a=i*std::numbers::pi_v<float>/8;const Vec3 d{std::sin(a),0,std::cos(a)};
        const auto floor=g.level.raycast(o.position-d*distance+Vec3{0,1.5f,0},{0,-1,0},4);
        if(!floor)continue;
        g.player.position=g.player.previous_position=floor->point+Vec3{0,0.01f,0};g.player.velocity={};
        g.player.yaw=g.player.previous_yaw=std::atan2(d.x,d.z);return;
    }
    throw std::runtime_error("No floor beside the target");
}
}
int main(int argc,char** argv) {
    try {
        AssetStore assets(argc>1?argv[1]:"data/disc");WadArchive wad(assets);
        const auto exe=assets.read_all("SCUS_944.25");
        // Execute the original reward's update 150 directly. Only its final
        // camera / outer game-loop calls are stubbed; the flag and count
        // writes are the unmodified SCUS_944.25 instructions.
        for(const unsigned owned:{0u,1u}) {
            OverlayVm vm;const auto image=make_retail_script_image(wad,exe,17);
            const auto boot=boot_retail_level(vm,*image);check(boot.fault.empty(),"Retail reward oracle boot failed");
            vm.hle[0x8001f1e4]=[](OverlayVm&){};vm.hle[0x8001b050]=[](OverlayVm&){};
            vm.write32(0x80067120,0);vm.write32(0x800670dc,0);vm.write32(0x800681c8,0);
            vm.write32(0x80067134,4);vm.write32(0x80067130,149);vm.write32(0x80066f54,1);
            vm.write8(0x8006b265,static_cast<std::uint8_t>(owned));vm.write32(0x80067108,owned);vm.write8(0x80066ff4,0);
            vm.r[29]=retail_stack_top;vm.call(0x80016328,{},100'000);
            check(vm.fault.empty() && vm.read8(0x80066ff4)==1 && vm.read8(0x8006b265)==1 && vm.read32(0x80067108)==1,
                "Original reward did not set the visit flag / count exactly once: "+vm.fault);
            std::cout<<"Retail 80016328 state 4 update 150: owned "<<owned<<", visit flag 1, talisman count 1\n";
        }
        // Twitchy's command-1 / command-2B conversation must release Spyro
        // both on the first visit and with the talisman already in the save.
        // The retail reward writes 80066ff4 independently of its award count.
        {
            const Retail retail(assets,wad,17);
            for(const bool revisit:{false,true}) {
                auto g=retail.game();ticks(g,4);
                if(revisit){check(g.award_talisman(),"Revisit fixture did not own Glimmer's talisman");g.reward_this_visit=false;}
                const auto count=g.player.talismans;
                in_front(g,g.retail_objects[26],1.2f);g.camera.reset(g.level,g.player.position,g.player.yaw);
                for(unsigned t=0;t<300 && !g.dialogue.active();++t){InputState in;in.dialogue.interact=t%8==1;ticks(g,1,in);}
                check(g.dialogue.active() && g.dialogue.object()==26,"Twitchy did not open his conversation");
                bool shown=false;
                for(unsigned t=0;t<1600 && (g.dialogue.active() || g.reward_show.active());++t) {
                    InputState in;in.dialogue.next=t%24==23;ticks(g,1,in);shown|=g.reward_show.active();
                }
                check(shown && !g.dialogue.active() && !g.reward_show.active(),"Twitchy's reward did not play and close");
                check(g.reward_this_visit && g.overlay_scripts.peek(0x80066ff4,1)==1,"Twitchy's visit flag missing");
                check(g.guide_awards.talisman && (!revisit || g.player.talismans==count),"Revisit changed the talisman count");
                // Command 2B may open the following ordinary line once; read
                // that too, then leave the stick held away from the speaker.
                for(unsigned t=0;t<600;++t){InputState in;in.dialogue.next=t%24==23;ticks(g,1,in);}
                check(!g.dialogue.active() && !g.reward_show.active(),"Twitchy repeats his reward indefinitely");
                const auto conversations=g.dialogue.conversations();const auto before=g.player.position;
                auto away=g.player.position-g.retail_objects[26].position;away.y=0;away=normalized(away);
                InputState walk;walk.move_x=away.x;walk.move_z=away.z;ticks(g,90,walk);
                check(length(g.player.position-before)>.3f && !g.dialogue.active(),"Twitchy did not return movement control");
                check(g.dialogue.conversations()==conversations && g.overlay_scripts.faults==0,"Twitchy reopened or faulted while leaving");
                std::cout<<"Twitchy "<<(revisit?"revisit":"first visit")<<": reward shown, visit flag 1, control returned, talisman counted once\n";
            }
        }
        // 1. Every campaign scene boots the retail way and runs its dispatcher.
        for(unsigned slot=15;slot<=69;slot+=2) {
            const auto image=make_retail_script_image(wad,exe,slot);
            check(image!=nullptr,"No retail runtime image for slot "+std::to_string(slot));
            OverlayVm vm;const auto boot=boot_retail_level(vm,*image);
            check(boot.fault.empty() && boot.dispatcher && boot.count==decode_object_instances(image->archive,slot).size(),
                "Slot "+std::to_string(slot)+" boot: "+boot.fault);
            vm.r[29]=retail_stack_top;vm.write32(0x80067018,2);
            for(unsigned t=0;t<30;++t){vm.call(boot.dispatcher,{},4'000'000);check(vm.fault.empty(),"Slot "+std::to_string(slot)+" dispatcher fault: "+vm.fault);}
        }
        // 2. Engine-side: ownership, flame/charge bits, hurting Spyro, gates' spirits.
        struct Case {unsigned slot,type;};
        for(const auto [slot,type]:{Case{21,0x111},Case{45,0x1a3},Case{37,0x4e},Case{61,0x1c9}}) {
            const Retail retail(assets,wad,slot);const auto index=record(retail.level,type);
            auto g=retail.game();ticks(g,2);
            const auto label="Slot "+std::to_string(slot)+" class "+std::to_string(type)+": ";
            check(g.overlay_scripts.active() && g.overlay_scripts.owned()>10 && g.retail_objects[index].retail_script,label+"scripts did not take ownership "+g.overlay_scripts.fault);
            beside(g,g.retail_objects[index],1.4f);
            const auto start=g.retail_objects[index].position;
            for(unsigned t=0;t<120;++t){g.player.invulnerable_ticks=std::max(g.player.invulnerable_ticks,2u);ticks(g,1);}
            check(g.overlay_scripts.faults==0,label+"interpreter faults: "+g.overlay_scripts.fault);
            check(length(g.retail_objects[index].position-start)>0.05f || g.retail_objects[index].animation.frame!=0,label+"script did not move or animate its moby");
            // The original flame mask, delivered through the +18 attack word, kills it.
            const auto spirits=g.spirits();
            g.retail_objects[index].attack_flags=flame_attack;
            for(unsigned t=0;t<240 && g.retail_objects[index].state>=0;++t){g.player.invulnerable_ticks=std::max(g.player.invulnerable_ticks,2u);ticks(g,1);}
            check(g.retail_objects[index].state<0,label+"flame bit did not defeat the scripted enemy");
            check(g.spirits()>spirits,label+"defeat did not count towards the level's spirits");
        }
        // 3. Scripts hurt Spyro (Colossus rams write the hurt word themselves).
        {
            const Retail retail(assets,wad,21);auto g=retail.game();ticks(g,2);
            beside(g,g.retail_objects[record(retail.level,0x111)],1.4f);
            const auto health=g.player.health;ticks(g,600);
            check(g.overlay_scripts.hits>0 && (g.player.health<health || g.player.lives<3),"Colossus ram never hurt an idle Spyro");
        }
        // 4. Native classes stay native.
        {
            const Retail retail(assets,wad,23);auto g=retail.game();ticks(g,2);
            for(const auto& o:g.retail_objects)if(o.enemy || o.type==1 || is_breakable(o.type))check(!o.retail_script,"A natively reconstructed moby was handed to the interpreter");
        }
        // 5. Conversations and progression run on the original logic. Magma
        // Cone's party host (13b, subtype 3) is script owned: its script sees
        // triangle (8003a98c), opens the conversation (80015be4), and accepting
        // the Party Crashers challenge raises 8006abf8, which wakes the lava
        // creatures (128). Defeating them and talking again hands out the orb
        // through 8003a0d8, into the save's per-level orb bits.
        {
            const Retail retail(assets,wad,49);
            const auto archive=wad.read(49);
            unsigned host=UINT32_MAX;
            for(unsigned i=0;i<retail.level.objects.size() && host==UINT32_MAX;++i)
                if(retail.level.objects[i].type==0x13b)if(const auto npc=decode_npc_record(archive,49,i);npc && npc->subtype==3)host=i;
            check(host!=UINT32_MAX,"Magma Cone party host not found");
            auto g=retail.game();ticks(g,2);
            const auto talk=[&](int answer,unsigned limit) {
                // Face the host and press triangle until the script opens the conversation.
                for(unsigned t=0;t<240 && !g.dialogue.active();++t) {
                    in_front(g,g.retail_objects[host],1.2f);g.player.invulnerable_ticks=600;
                    InputState in;in.dialogue.interact=(t%8)==0;ticks(g,1,in);
                }
                check(g.dialogue.active() && g.dialogue.object()==host,"The party host's script did not open a conversation");
                check(g.dialogue.retail_driven(),"Conversation is not driven by the original logic");
                for(unsigned step=0;step<limit && g.dialogue.active();++step) {
                    ticks(g,14);InputState in;
                    const auto* line=g.dialogue.line();
                    if(line && line->yes>=0 && line->no>=0)in.dialogue.choice=answer;else in.dialogue.next=true;
                    ticks(g,1,in);
                }
                check(!g.dialogue.active(),"Conversation did not finish");
            };
            std::vector<unsigned> crashers;for(unsigned i=0;i<retail.level.objects.size();++i)if(retail.level.objects[i].type==0x128)crashers.push_back(i);
            check(crashers.size()==12,"Expected Magma Cone's twelve party crashers");
            // The host only offers the challenge once the Superfly gate (2 spirits) is open.
            for(unsigned i=0,defeated=0;i<retail.level.objects.size() && defeated<2;++i)if(retail.level.objects[i].type==0x13e) {
                for(unsigned t=0;t<240 && g.retail_objects[i].state>=0;++t){g.retail_objects[i].attack_flags|=charge_attack;g.player.invulnerable_ticks=600;ticks(g,1);}
                if(g.retail_objects[i].state<0)++defeated;
            }
            // Each defeat launches a class-68 spirit; the gate opens when they arrive (80066efc).
            check(g.powers.spirits_launched>=2 && g.retail_objects[33].powerup_gate && !g.retail_objects[33].powerup_gate->unlocked,
                "Defeats did not launch spirits, or the gate opened before they arrived");
            unsigned flight=0;
            for(;flight<900 && !g.retail_objects[33].powerup_gate->unlocked;++flight)ticks(g,1);
            check(g.retail_objects[33].powerup_gate->unlocked && g.powers.spirits_arrived>=2,"Superfly gate did not open after two spirits arrived");
            ticks(g,1);
            check(std::any_of(g.powers.held_sounds().begin(),g.powers.held_sounds().end(),[](const auto& h){return h.key==33 && h.event.object_type==0x1cc;}),
                "The open gate does not keep its ambient sound 0 playing");
            std::cout<<"Magma Cone spirits reached the gate in "<<flight<<" ticks\n";
            talk(0,40);
            for(unsigned t=0;t<240;++t){g.player.invulnerable_ticks=600;ticks(g,1);}
            check(g.overlay_scripts.camera_shots>=1,"Accepting did not run the host's camera shot (8001f24c)");
            unsigned awake=0;for(const auto i:crashers)if(g.retail_objects[i].state!=0)++awake;
            check(awake>0,"Accepting Party Crashers did not wake the lava creatures");
            // Defeat them with the original flame bit, then report back.
            const auto orbs=g.player.orbs;
            for(unsigned round=0;round<40;++round) {
                for(const auto i:crashers)if(g.retail_objects[i].state>=0)g.retail_objects[i].attack_flags|=superflame_attack;
                for(unsigned t=0;t<30;++t){g.player.invulnerable_ticks=600;ticks(g,1);}
            }
            unsigned defeated=0;for(const auto i:crashers)if(g.retail_objects[i].state<0)++defeated;
            check(defeated==crashers.size(),"Not every party crasher could be defeated");
            // The last defeat ends the shot; the host then places Spyro in front
            // of itself (8001f524, camera mode 11) and opens the reward line.
            check(g.overlay_scripts.peek(0x80067088)==0,"The crasher counter 80067088 did not reach zero");
            for(unsigned t=0;t<600 && !g.dialogue.active();++t){g.player.invulnerable_ticks=600;ticks(g,1);}
            // Mode 11 fades out before it places Spyro (8001fa58 case B); the
            // host may already talk during the fade when Spyro is in range.
            for(unsigned t=0;t<60 && !g.overlay_scripts.warps;++t){g.player.invulnerable_ticks=600;ticks(g,1);}
            check(g.overlay_scripts.warps>=1 && length(g.player.position-g.retail_objects[host].position)<2.5f,"The host did not place Spyro in front of it");
            if(!g.dialogue.active())talk(0,40);
            else {
                check(g.dialogue.retail_driven() && g.dialogue.object()==host,"The reward conversation is not the host's original logic");
                for(unsigned step=0;step<40 && g.dialogue.active();++step){ticks(g,14);InputState in;in.dialogue.next=true;ticks(g,1,in);}
                check(!g.dialogue.active(),"Reward conversation did not finish");
            }
            for(unsigned t=0;t<60;++t)ticks(g,1);
            std::cerr<<"Party Crashers: orbs "<<orbs<<" -> "<<g.player.orbs<<", level bits "<<g.guide_awards.orbs<<"\n";
            check(g.player.orbs==orbs+1 && g.guide_awards.orbs!=0,"Reporting the finished task did not award the orb");
            check(!g.overlay_scripts.memory().empty(),"No persistent script state was registered");
        }
        // 6. Zoe (class 110) is script owned: Glimmer's first Zoe wakes as Spyro
        // arrives, opens her conversation through 8003a98c / 80015be4, stores
        // her checkpoint (8004c0c8 -> 80067414), and a lost life returns there.
        {
            const Retail retail(assets,wad,17);
            const auto archive=wad.read(17);
            unsigned zoe=UINT32_MAX;
            for(unsigned i=0;i<retail.level.objects.size() && zoe==UINT32_MAX;++i)if(retail.level.objects[i].type==0x110)
                if(const auto npc=decode_npc_record(archive,17,i);npc && !npc->lines.empty() && npc->lines[0].text.find("remember your progress")!=std::string::npos)zoe=i;
            check(zoe!=UINT32_MAX,"Glimmer's first Zoe not found");
            check(std::none_of(retail.level.npcs.begin(),retail.level.npcs.end(),[&](const auto& n){return n.object==zoe;}),"Zoe is still a native speaker");
            auto g=retail.game();ticks(g,2);
            check(g.retail_objects[zoe].retail_script,"Zoe's class 110 is not script owned");
            // She materialises as Spyro walks up (within 24, then 9 units) but not
            // if he is already within 6 when she first draws: approach first.
            for(unsigned t=0;t<300 && !g.dialogue.active();++t) {
                if(t%30==0)in_front(g,g.retail_objects[zoe],t<60?8.0f:1.0f);
                g.player.invulnerable_ticks=600;InputState in;in.dialogue.interact=(t%8)==0;ticks(g,1,in);
            }
            check(g.dialogue.active() && g.dialogue.object()==zoe && g.dialogue.retail_driven(),"Zoe's script did not open her conversation");
            // Line one, her zap (the checkpoint), then line two.
            for(unsigned t=0;t<1200 && (g.dialogue.active() || !g.overlay_scripts.checkpoints || t<400);++t) {
                g.player.invulnerable_ticks=600;InputState in;in.dialogue.next=g.dialogue.active() && t%30==29;ticks(g,1,in);
            }
            check(!g.dialogue.active(),"Zoe's conversation did not finish");
            check(g.overlay_scripts.checkpoints==1 && g.respawn_yaw,"Zoe did not store a checkpoint");
            auto apart=g.play_scene.spawn-g.retail_objects[zoe].position;apart.y=0;
            check(length(apart)<2.0f,"The checkpoint is not at Zoe");
            // A lost life now returns Spyro to Zoe, not to the level start.
            g.player.position=g.player.previous_position=retail.level.spawn;g.player.health=0;g.player.invulnerable_ticks=0;
            check(g.hurt(g.player.position+Vec3{1,0,0}),"Lethal hit was not taken");
            check(length(g.player.position-g.play_scene.spawn)<0.01f && std::abs(g.player.yaw-*g.respawn_yaw)<0.001f,"Spyro did not return to Zoe's checkpoint");
        }
        // 7. Colossus 11a is an area-triggered sequence, not an enemy: Spyro
        // inside its oriented box (8003873c, centre at param+4) starts a camera
        // look (8001f24c); the sequence ends by setting 80066f44 and placing
        // Spyro (8001f524), and the next state 0 awards its spirit (8004c3d4).
        {
            const Retail retail(assets,wad,21);auto g=retail.game();ticks(g,2);
            const auto index=record(retail.level,0x11a);
            check(g.retail_objects[index].retail_script,"Colossus 11a is not script owned");
            const auto param=g.overlay_scripts.peek(g.overlay_scripts.moby_address(index));
            const auto centre=retail::to_world({static_cast<std::int32_t>(g.overlay_scripts.peek(param+4)),
                static_cast<std::int32_t>(g.overlay_scripts.peek(param+8)),static_cast<std::int32_t>(g.overlay_scripts.peek(param+12))});
            const auto floor=g.level.raycast(centre+Vec3{0,2,0},{0,-1,0},8);
            check(floor.has_value(),"No floor under Colossus 11a's trigger area");
            g.player.position=g.player.previous_position=floor->point+Vec3{0,.01f,0};g.player.velocity={};
            const auto spirits=g.spirits();const auto shots=g.overlay_scripts.camera_shots;
            for(unsigned t=0;t<3600 && g.spirits()==spirits;++t){g.player.invulnerable_ticks=600;ticks(g,1);}
            check(g.overlay_scripts.camera_shots>shots,"Entering 11a's area did not start its camera sequence");
            check(g.overlay_scripts.peek(0x80066f44,1)==1 && g.spirits()>spirits,"11a's sequence did not finish and award its spirit");
            // Winning the sequence opens the gate beside its arena (terrain group 2).
            for(unsigned t=0;t<600;++t){g.player.invulnerable_ticks=600;ticks(g,1);}
            bool gate=false;
            for(std::size_t k=0;k<g.level.terrain_morphs.size();++k)
                if(const auto& m=g.level.terrain_morphs[k];m.scripted && m.collision && m.group==2 && m.node!=retail.level.terrain_morphs[k].node)gate=true;
            check(gate,"Finishing 11a's sequence did not open Colossus' gate (terrain group 2)");
        }
        // 8. Shady Oasis: 187s join a list on object 0 (8007ca78). A hit 233
        // resets the 187 it holds to state 0 (80076f4c) without the state-99
        // unlink, so it joins again; that must not close the list into a cycle
        // (the walks at 8007caf4 / 80081634 would never end).
        {
            const Retail retail(assets,wad,47);auto g=retail.game();ticks(g,2);
            const auto& v=g.overlay_scripts;const auto header=v.peek(v.moby_address(0));
            const auto valid=[](std::uint32_t a){return a>=0x80000000u && a<0x80800000u;};
            // Visit the script mobys in turn (as the engagement census does) until 187s are thrown.
            for(unsigned i=1;i<retail.level.objects.size() && !valid(v.peek(header+8));++i) {
                if(!g.retail_objects[i].retail_script || g.retail_objects[i].state<0)continue;
                beside(g,g.retail_objects[i],3.0f);
                for(unsigned t=0;t<240 && !valid(v.peek(header+8));++t) {
                    if(t>=120 && t%10==0 && g.retail_objects[i].state>=0)g.retail_objects[i].attack_flags|=flame_attack;
                    g.player.invulnerable_ticks=600;ticks(g,1);
                }
            }
            const auto head=v.peek(header+8);
            check(valid(head) && v.peek(head+0x36,2)==0x187,"Shady Oasis 187s did not join object 0's list");
            g.overlay_scripts.poke(head+0x48,0,1);   // what a hit 233 does to its 187
            for(unsigned t=0;t<300;++t){g.player.invulnerable_ticks=600;ticks(g,1);}
            check(v.faults==0 && v.active(),"A re-joining 187 faulted Shady Oasis' scripts: "+v.fault);
            auto at=v.peek(header+8);unsigned members=0;
            while(valid(at) && members<300){at=v.peek(v.peek(at)+0x14);++members;}
            check(members<300,"Object 0's 187 list became a cycle");
        }
        // 9. Chest gems: class 80 gem riders follow a host (param[0]) and burst
        // out (80039334, flying in state 2 with only their variant at +49) once
        // the host's state has bit 80. Breaking Idol Springs' native cb chest
        // must show it removed (FE) to the scripts and yield all 42 gems.
        {
            const Retail retail(assets,wad,19);auto g=retail.game();ticks(g,2);
            unsigned chest=UINT32_MAX,riders=0,value=0;
            for(unsigned i=0;i<retail.level.objects.size();++i)if(retail.level.objects[i].type==0x80) {
                const auto host=g.overlay_scripts.peek(g.overlay_scripts.peek(g.overlay_scripts.moby_address(i)));
                if(host<retail.level.objects.size() && retail.level.objects[host].type==0xcb){chest=host;++riders;value+=retail.level.objects[i].held_gem;}
            }
            check(chest!=UINT32_MAX && riders==7 && value==42,"Idol Springs' chest gem riders not found");
            const auto before=g.level_gems();
            beside(g,g.retail_objects[chest],1.6f);
            for(unsigned t=0;t<600 && !(g.retail_objects[chest].collected || g.retail_objects[chest].state<0);++t) {
                if(t%10==0)g.retail_objects[chest].attack_flags|=0x560000u;   // supercharge opens it
                g.player.invulnerable_ticks=600;ticks(g,1);
            }
            check(g.retail_objects[chest].collected || g.retail_objects[chest].state<0,"The chest did not open");
            for(unsigned t=0;t<240;++t){g.player.invulnerable_ticks=600;ticks(g,1);}
            check(g.overlay_scripts.peek(g.overlay_scripts.moby_address(chest)+0x48,1)==0xfe,"The broken chest still looks alive to the scripts");
            check(g.overlay_scripts.gems==7,"The chest's gem riders did not burst into native gems");
            for(std::size_t i=0;i<g.retail_objects.size();++i)if(g.retail_objects[i].script_gem && !g.retail_objects[i].collected) {
                const auto at=g.retail_objects[i].position;
                g.player.position=g.player.previous_position=at-retail::origin_above_feet*.5f;g.player.velocity={};
                for(unsigned t=0;t<8 && !g.retail_objects[i].collected;++t){g.player.invulnerable_ticks=600;ticks(g,1);}
            }
            check(g.level_gems()==before+42,"The chest's 42 gems did not count towards the level");
        }
        // Walk Spyro along +dir from `from` for `frames`; how far he got.
        const auto walk=[](Game& g,Vec3 from,Vec3 dir,unsigned frames,std::uint32_t charge_frames=0) {
            g.player.position=g.player.previous_position=from;g.player.velocity={};g.player.yaw=g.player.previous_yaw=std::atan2(dir.x,dir.z);
            g.camera.reset(g.level,g.player.position,g.player.yaw);   // as if he had walked there
            float reach=-1e9f;
            for(unsigned t=0;t<frames;++t) {
                InputState in;in.move_x=dir.x;in.move_z=dir.z;in.charge=t<charge_frames;g.player.invulnerable_ticks=600;ticks(g,1,in);
                reach=std::max(reach,dot(g.player.position-from,dir));
            }
            return reach;
        };
        const auto morph=[](const Game& g,unsigned kind,unsigned group)->const TerrainMorph& {
            for(const auto& m:g.level.terrain_morphs)if(m.kind==kind && m.group==group)return m;
            throw std::runtime_error("Terrain group "+std::to_string(group)+" is not decoded");
        };
        const auto collision_bottom=[](const Game& g,const TerrainMorph& m) {
            float y=1e9f;for(unsigned i=0;i<m.frames.front().size()/3;++i){const auto& t=g.level.triangles[m.first_triangle+i];y=std::min({y,t.a.y,t.b.y,t.c.y});}
            return y;
        };
        // 10. Terrain doors: every scene's animated groups are decoded with
        // their collision, and the scripts' timelines (8002365c starts a group,
        // 800225f8 advances it) pose them. Summer Forest's corridor gates are a
        // toggle: charging switch 145 (record 166) raises gate 2 (z 59) and
        // lowers gate 3 (z 51). Closed gates stop Spyro; open ones let him by.
        {
            const Retail retail(assets,wad,15);auto g=retail.game();ticks(g,4);
            check(g.level.terrain_morphs.size()==25 && g.level.skipped_morphs==0,"Summer Forest's terrain groups were not all decoded");
            const auto& gate=morph(g,4,2);const auto& other=morph(g,4,3);
            check(gate.scripted && collision_bottom(g,gate)<42.5f && collision_bottom(g,other)>45,"Summer Forest's corridor gates did not start in their authored poses");
            // Gate 2 spans z 59..60, gate 3 z 51..52; Spyro starts between them.
            const auto shut=56+walk(g,{51,42.05f,56},{0,0,1},200),open=56-walk(g,{51,42.05f,56},{0,0,-1},200);
            check(shut<59,"Spyro walked through the closed gate (z "+std::to_string(shut)+")");
            check(open<50.5f,"The open gate blocked Spyro (z "+std::to_string(open)+")");
            check(g.retail_objects[166].type==0x145,"Summer Forest record 166 is not the gate switch");
            walk(g,{54.3f,42.05f,67},{0,0,-1},60,60);
            for(unsigned t=0;t<120;++t){g.player.invulnerable_ticks=600;ticks(g,1);}
            check(g.overlay_scripts.terrain_changes>0 && gate.node==15 && (gate.flags&2),"Charging the switch did not run the gates' timeline");
            check(collision_bottom(g,gate)>45 && collision_bottom(g,other)<42.5f,"The gates did not swap");
            const auto raised=56+walk(g,{51,42.05f,56},{0,0,1},200),lowered=56-walk(g,{51,42.05f,56},{0,0,-1},200);
            check(raised>60.5f,"The raised gate still blocked Spyro (z "+std::to_string(raised)+")");
            check(lowered>52,"Spyro walked through the lowered gate (z "+std::to_string(lowered)+")");
        }
        // 11. Doors that native classes open: Moneybags' paid doors are set to
        // their open node at load (80023710); Summer Forest's group 4 region is
        // then hidden (step byte 7), as the original 80024168 flags it.
        {
            const Retail retail(assets,wad,15);auto g=retail.game();g.purchases=(1u<<19)-1;ticks(g,60);
            check(g.overlay_scripts.door_commands>=4,"Paid Moneybags doors were not opened at load");
            const auto& door=morph(g,2,4);
            check(door.node==30 && door.steps[door.node].hidden,"Summer Forest's paid door did not reach its open, hidden node");
            auto unpaid=retail.game();ticks(unpaid,60);
            check(morph(unpaid,2,4).node==0,"An unpaid Moneybags door opened");
            // Elora's door on to Crush (16D subtype 5, group 5) needs 6 talismans.
            auto five=retail.game();five.player.talismans=0x1f;ticks(five,300);
            auto six=retail.game();six.player.talismans=0x3f;ticks(six,300);
            check(morph(five,4,5).node==0 && collision_bottom(five,morph(five,4,5))<40.5f,"Elora's door opened with 5 talismans");
            check(morph(six,4,5).node==15 && collision_bottom(six,morph(six,4,5))>43.5f,"Elora's door did not open with 6 talismans");
        }
        // 12. Solid mobys: collision-bank triangles (8003dc64's moby half) are
        // part of the level's collision. Colossus' class 21 doors stop Spyro at
        // their faces; Hurricos' class 4D blocks as well.
        {
            for(const auto [slot,object]:{std::pair{21u,69u},std::pair{23u,161u}}) {
                const Retail retail(assets,wad,slot);auto g=retail.game();ticks(g,4);
                // Approach first: once Spyro is within its activation radius
                // (+4A) or the door is drawn, its script places it (Colossus'
                // 21 rise about a unit from their authored spots).
                for(const auto& c:g.level.object_colliders)if(c.object==object) {
                    for(unsigned k=0;k<16;++k) {
                        const float a=k*std::numbers::pi_v<float>/8;
                        const auto floor=g.level.raycast(c.bounds.center()+Vec3{std::sin(a)*6,3,std::cos(a)*6},{0,-1,0},8);
                        if(!floor || floor->normal.y<.7f || (floor->triangle&Level::object_triangle))continue;
                        g.player.position=g.player.previous_position=floor->point+Vec3{0,.02f,0};g.player.velocity={};
                        g.player.yaw=g.player.previous_yaw=a+std::numbers::pi_v<float>;g.camera.reset(g.level,g.player.position,g.player.yaw);break;
                    }
                    break;
                }
                ticks(g,40);
                const auto* collider=[&]()->const Level::ObjectCollider*{for(const auto& c:g.level.object_colliders)if(c.object==object)return &c;return nullptr;}();
                check(collider && collider->count>=20,"Slot "+std::to_string(slot)+" record "+std::to_string(object)+" has no collision triangles");
                const auto centre=collider->bounds.center();const float half=(collider->bounds.max.x-collider->bounds.min.x)*.5f;
                std::optional<float> blocked;
                for(unsigned k=0;k<16 && !blocked;++k) {
                    const float a=k*std::numbers::pi_v<float>/8;const Vec3 d{std::sin(a),0,std::cos(a)};
                    const auto at=centre-d*(half+1.6f);
                    const auto floor=g.level.raycast(at+Vec3{0,3,0},{0,-1,0},6);
                    if(!floor || floor->normal.y<.7f || (floor->triangle&Level::object_triangle) || floor->point.y>collider->bounds.max.y-.3f || floor->point.y<collider->bounds.min.y-1.5f)continue;
                    const auto before=g.overlay_scripts.solid_contacts;
                    const auto reach=walk(g,floor->point+Vec3{0,.05f,0},d,90);
                    if(g.overlay_scripts.solid_contacts>before)blocked=reach-(half+1.6f);
                }
                check(blocked && *blocked<-.3f,"Spyro was not stopped by slot "+std::to_string(slot)+" record "+std::to_string(object)+"'s triangles");
            }
        }
        // 13. Crystal Glacier's class 50 creatures are one group (list 7). Its
        // controller c6 (subtype 7) arms the challenge only while the flight
        // gate (record 23) is open and Spyro flies in movement mode 6 (the
        // level overlay's flight states 33-35), and awards every spirit once all
        // eighteen are knocked down together.
        {
            const Retail retail(assets,wad,37);auto g=retail.game();ticks(g,4);
            std::vector<unsigned> members;
            for(unsigned i=0;i<retail.level.objects.size();++i)if(g.retail_objects[i].type==0x50)members.push_back(i);
            check(members.size()==18 && g.retail_objects[23].powerup_gate && g.retail_objects[23].powerup_gate->kind==2,"Crystal Glacier's class 50 group or flight gate not found");
            const auto fly=[&]{
                g.retail_objects[23].powerup_gate->requirement=0;g.cheats.superfly=true;g.powers.flight={2400,2400};
                if(!g.powers.flying){g.player.position.y+=1.5f;g.player.previous_position=g.player.position;g.player.grounded=false;g.powers.flying=true;}
            };
            std::vector<std::vector<unsigned>> clusters;
            for(const auto i:members) {
                auto c=std::find_if(clusters.begin(),clusters.end(),[&](const auto& k){return length(g.retail_objects[k.front()].position-g.retail_objects[i].position)<16;});
                if(c==clusters.end())clusters.push_back({i});else c->push_back(i);
            }
            for(const auto& cluster:clusters) {
                Vec3 centre{};for(const auto i:cluster)centre+=g.retail_objects[i].position;centre=centre/static_cast<float>(cluster.size());
                if(const auto floor=g.level.raycast(centre+Vec3{0,3,0},{0,-1,0},10))g.player.position=g.player.previous_position=floor->point+Vec3{0,.02f,0};
                g.player.velocity={};g.camera.reset(g.level,g.player.position,g.player.yaw);
                for(unsigned t=0;t<90;++t) {
                    if(t%12==0)for(const auto i:cluster)if(g.retail_objects[i].state>=0)g.retail_objects[i].attack_flags|=flame_attack;
                    fly();g.player.invulnerable_ticks=600;ticks(g,1);
                }
            }
            for(unsigned t=0;t<300;++t){fly();g.player.invulnerable_ticks=600;ticks(g,1);}
            check(g.player.motion==Motion::Superfly,"Spyro was not flying for the challenge");
            check(std::all_of(members.begin(),members.end(),[&](unsigned i){return g.retail_objects[i].state<0 || g.retail_objects[i].collected;}) && g.spirits()>=18,
                "Knocking every class 50 down while flying did not award their spirits ("+std::to_string(g.spirits())+")");
        }
        // 14. Idol Springs' first Foreman Bud puzzle: standing on a 1E5 tile
        // (8006a134) toggles it and its two linked tiles; once all eight are lit
        // (+49 reaches 99) Bud (124 subtype 3, record 268) sends Spyro to the
        // pond. Once he is undrawn (+4D) while Spyro swims underwater (movement
        // mode 11/12), he hides and reveals the pond Bud (record 267).
        {
            const Retail retail(assets,wad,19);auto g=retail.game();ticks(g,4);
            const auto& s=g.overlay_scripts;
            const auto moby=[&](unsigned i){return s.moby_address(i);};
            const auto start=g.player.position;
            for(const unsigned tile:{166u,167u,176u,185u}) {
                const auto c=std::find_if(g.level.object_colliders.begin(),g.level.object_colliders.end(),[&](const auto& x){return x.object==tile;});
                check(c!=g.level.object_colliders.end(),"Idol Springs tile "+std::to_string(tile)+" has no collision");
                g.player.position=g.player.previous_position={c->bounds.center().x,c->bounds.max.y+.05f,c->bounds.center().z};g.player.velocity={};
                for(unsigned t=0;t<30;++t){g.player.invulnerable_ticks=600;ticks(g,1);}
                g.player.position=g.player.previous_position=start;g.player.velocity={};
                for(unsigned t=0;t<40;++t){g.player.invulnerable_ticks=600;ticks(g,1);}
            }
            for(unsigned t=0;t<60;++t)ticks(g,1);
            for(const unsigned tile:{166u,167u,168u,176u,184u,185u,186u,187u})
                check(s.peek(s.peek(moby(tile)),2)==1 && s.peek(moby(tile)+0x49,1)==99,"Idol Springs tile "+std::to_string(tile)+" is not lit and solved");
            in_front(g,g.retail_objects[268],1.2f);
            std::string said;
            for(unsigned t=0;t<600 && said.empty();++t) {
                InputState in;in.dialogue.interact=t%30==2 && !g.dialogue.active();
                if(g.dialogue.active() && g.dialogue.line())said=g.dialogue.line()->text;
                g.player.invulnerable_ticks=600;ticks(g,1,in);
            }
            check(said.find("meet me at the pond")!=std::string::npos,"Foreman Bud did not acknowledge the solved puzzle ("+said+")");
            for(unsigned step=0;step<20 && g.dialogue.active();++step){ticks(g,14);InputState in;in.dialogue.next=true;ticks(g,1,in);}
            check(s.peek(0x8006abf8,1)==1,"The solved puzzle did not set Idol Springs' progress byte 8006abf8");
            check(static_cast<std::int8_t>(s.peek(moby(267)+0x48,1))==0x60,"The pond Bud is visible before Spyro leaves");
            // Standing by the pond does not move him; diving into it, with Bud
            // behind the camera, does.
            beside(g,g.retail_objects[267],3.0f);g.camera.reset(g.level,g.player.position,g.player.yaw);
            for(unsigned t=0;t<120;++t){g.player.invulnerable_ticks=600;ticks(g,1);}
            check(s.peek(moby(268)+0x48,1)==0,"Foreman Bud left before Spyro swam");
            g.player.position=g.player.previous_position=g.retail_objects[216].position;g.player.velocity={};   // a pond fish's spot
            g.camera.reset(g.level,g.player.position,g.player.yaw);
            for(unsigned t=0;t<120;++t){g.player.invulnerable_ticks=600;g.player.health=std::max(g.player.health,3u);ticks(g,1);}
            check(g.player.swim.mode==WaterMode::Underwater,"Spyro is not underwater in Idol Springs' pond");
            check(s.peek(moby(268)+0x48,1)==0x60 && s.peek(moby(267)+0x48,1)==0 && s.peek(moby(267)+0x4c,1)>0,
                "Foreman Bud did not move to the pond ("+std::to_string(s.peek(moby(268)+0x48,1))+", "+std::to_string(s.peek(moby(267)+0x48,1))+")");
        }
        // 15. Hurricos' diodes. The frames-since-load counter 80067008 runs; the
        // power bolt (8B, record 257) is hidden by its script (+4C = 0) and not
        // drawn. Once robots 254/255 stop juggling diode 178, Spyro walking up
        // facing it takes it in his mouth (head state 8006a15c 2 -> 3, diode
        // state 1, head clip 45); Circle spits it (head 4 -> 0, diode state 2)
        // and it homes in on socket 188, which takes it (params+10).
        {
            const Retail retail(assets,wad,23);
            // With Spyro's model, so his head partition animates (8002eda0).
            const auto spyro=decode_spyro(wad.read(0),wad.read(23),exe);
            Scene scene;scene.wad_slot=retail.level.wad_slot;scene.spawn=retail.level.spawn;
            Game g(retail.level,scene,{},&retail.models,&spyro);ticks(g,4);
            const auto& s=g.overlay_scripts;
            const auto frames=s.peek(0x80067008);ticks(g,20);
            check(s.peek(0x80067008)>=frames+8,"Hurricos: the frames-since-load counter 80067008 does not run");
            check(g.retail_objects[257].type==0x8b && g.retail_objects[257].script_undrawn,"Hurricos: the unpowered bolt (257) is drawn");
            const auto park=[&](Vec3 at,Vec3 face) {
                if(const auto f=g.level.raycast(at+Vec3{0,3,0},{0,-1,0},8))at=f->point+Vec3{0,.02f,0};
                g.player.position=g.player.previous_position=at;g.player.velocity={};
                g.player.yaw=g.player.previous_yaw=std::atan2(face.x-at.x,face.z-at.z);g.camera.reset(g.level,g.player.position,g.player.yaw);
            };
            for(const unsigned robot:{254u,255u})
                for(unsigned t=0;t<300 && g.retail_objects[robot].state>=0;++t) {
                    if(t%20==0)park(g.retail_objects[robot].position+Vec3{0,0,-3},g.retail_objects[robot].position);
                    if(t>=10 && t%10==0)g.retail_objects[robot].attack_flags|=superflame_attack;
                    g.player.invulnerable_ticks=600;ticks(g,1);
                }
            check(g.retail_objects[254].state<0 && g.retail_objects[255].state<0,"Hurricos: the robots juggling diode 178 were not defeated");
            for(unsigned t=0;t<90;++t){g.player.invulnerable_ticks=600;ticks(g,1);}
            const auto diode=s.moby_address(178);const auto socket=g.retail_objects[188].position;
            check(s.peek(diode+0x48,1)==0,"Hurricos: diode 178 did not settle");
            {auto away=g.retail_objects[178].position-socket;away.y=0;park(g.retail_objects[178].position+normalized(away)*1.6f,g.retail_objects[178].position);}
            const auto pickups=s.pickups;
            for(unsigned t=0;t<120 && s.pickups==pickups;++t) {
                InputState in;const auto v=g.retail_objects[178].position-g.player.position;const float n=std::hypot(v.x,v.z);
                if(n>.75f){in.move_x=v.x/n*.4f;in.move_z=v.z/n*.4f;}
                g.player.invulnerable_ticks=600;ticks(g,1,in);
            }
            check(s.pickups==pickups+1 && s.carrying() && s.head_state()==3 && s.peek(diode+0x48,1)==1,"Hurricos: Spyro did not take diode 178 in his mouth");
            // The pickup clip (53) plays out before the head holds clip 45.
            for(unsigned t=0;t<90 && g.secondary.state().pose.clip!=45;++t)ticks(g,1);
            check(!g.retail_objects[178].script_undrawn && length(g.retail_objects[178].position-g.elements.mouth())<.8f,"Hurricos: the carried diode is not shown at Spyro's mouth");
            check(g.secondary.state().pose.clip==45 || g.secondary.state().pose.next_clip==45,"Hurricos: Spyro's head does not hold the carrying clip (45): clip "+std::to_string(g.secondary.state().pose.clip)+" next "+
                std::to_string(g.secondary.state().pose.next_clip)+" mode "+std::to_string(g.secondary.state().mode)+" action "+std::to_string(g.secondary.state().action)+" head "+std::to_string(s.head_state())+
                " motion "+std::to_string(int(g.player.motion)));
            park(socket+normalized(Vec3{g.retail_objects[178].position.x-socket.x,0,g.retail_objects[178].position.z-socket.z})*2.0f,socket);
            ticks(g,4);
            {InputState in;in.flame_pressed=true;ticks(g,1,in);}
            check(!g.interactions.flame_active(),"Hurricos: Circle breathed fire instead of spitting the diode");
            for(unsigned t=0;t<150;++t){g.player.invulnerable_ticks=600;ticks(g,1);}
            check(s.spits==1 && !s.carrying(),"Hurricos: the diode was not spat out");
            check(s.peek(s.peek(s.moby_address(188))+0x10)!=0,"Hurricos: socket 188 did not take the spat diode");
        }
        std::cout<<checks<<" retail overlay script checks passed\n";return 0;
    } catch(const std::exception& e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}
}
