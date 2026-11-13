#include "spyro/runtime.hpp"
#include "spyro/savegame.hpp"
#include <bit>
#include <fstream>
#include <iostream>
#include <set>
using namespace spyro;
namespace {
unsigned checks{};
void check(bool ok,const char* message){++checks;if(!ok)throw FormatError(message);}
void ticks(Game& g,unsigned n,InputState input={}){for(unsigned i=0;i<n;++i)g.advance(FixedClock::step,input);}
void touch(Game& g,const ScriptDefinition& s,const ObjectModels* models) {
    auto& o=g.retail_objects.at(s.object);
    if(s.pass_through) {
        const float correction=std::bit_cast<std::int16_t>(read_u16(o.record,56))/retail::units_per_world;
        const auto middle=o.position-Vec3{0,correction,0}-retail::origin_above_feet;
        const auto normal=retail::rotate_model({0,0,1},retail::native_heading(o.rotation[2]*16));
        g.player.previous_position=middle-normal;g.player.position=middle+normal;
    }else {o.attack_flags=flame_attack;g.player.position=g.player.previous_position=o.position+Vec3{0,s.role==ScriptRole::FlightTarget?2.0f:0.0f,0};}
    g.systems.after_move(g,{},models);
}
void flight(Level l,const ObjectModels& models) {
    Scene scene;scene.wad_slot=l.wad_slot;scene.spawn=l.spawn;Game g(l,scene,{},&models);
    check(g.systems.mode==ChallengeMode::Trial && g.powers.flying && g.systems.remaining==1800,"Speedway did not start a timed flight");
    unsigned categories[4]{};for(const auto& s:l.scripts)if(s.role==ScriptRole::FlightTarget)++categories[s.category];
    for(auto n:categories)check(n==8,"Speedway category does not have eight targets");
    ticks(g,1);const auto prior=g.systems.remaining;g.set_paused(true);ticks(g,10);check(g.systems.remaining==prior,"Pause consumed flight time");g.set_paused(false);
    for(const auto& s:l.scripts)if(s.role==ScriptRole::FlightTarget)touch(g,s,&models);
    if(!g.systems.success)std::cerr<<"Flight result slot="<<l.wad_slot<<" mode="<<int(g.systems.mode)<<" counts="<<g.systems.counts[0]<<','<<g.systems.counts[1]<<','<<g.systems.counts[2]<<','<<g.systems.counts[3]<<'\n';
    check(g.systems.success && g.systems.mode==ChallengeMode::Results,"32 targets did not finish the speedway");
    check(g.player.gems==400 && g.level_gems()==400 && g.guide_awards.flight_tasks==31,"Speedway did not award 400 gems and five flags");
    g.systems.start_trial(g,&models);for(const auto& s:l.scripts)if(s.role==ScriptRole::FlightTarget)touch(g,s,&models);
    check(g.player.gems==400,"Retry duplicated gems");
    GameSave save;save.capture(g);check(save.levels.at(l.wad_slot).removed.empty(),"Flight targets leaked into permanent removed objects");
    const auto file=std::filesystem::temp_directory_path()/("spyro-flight-"+std::to_string(l.wad_slot)+".s2save");save.write(file);
    Game restored(l,scene,{},&models);GameSave::read(file).apply(restored,&models);std::filesystem::remove(file);
    check(restored.player.gems==400 && restored.systems.mode==ChallengeMode::Trial && restored.systems.counts==std::array<unsigned,4>{},"Saved speedway failed to restart while retaining rewards");
    restored.systems.remaining=1;ticks(restored,1);check(restored.systems.results() && !restored.systems.success,"Timeout did not produce retry results");
    restored.systems.start_trial(restored,&models,true);check(restored.systems.mode==ChallengeMode::FreeFlight,"Free-flight choice failed");
    restored.systems.start_hunter(restored,&models);check(restored.systems.mode==ChallengeMode::Hunter && restored.systems.hunter_total>0,"Hunter challenge did not start");
    // Exercise authored activation deadlines and all reward transitions. The
    // pilot is placed at each reachable target; this is not a manual replay.
    for(unsigned step=0;step<30000 && !restored.systems.results();++step) {
        InputState in;restored.systems.before_move(restored,in,&models);restored.systems.after_move(restored,{},&models);
        for(const auto& target:l.scripts)if(target.role==ScriptRole::HunterTarget && !restored.retail_objects[target.object].hidden && !restored.retail_objects[target.object].collected)touch(restored,target,&models);
    }
    if(!restored.systems.success)std::cerr<<"Hunter "<<l.wad_slot<<" count="<<restored.systems.hunter_count<<'/'<<restored.systems.hunter_total<<" elapsed="<<restored.systems.elapsed<<'\n';
    check(restored.systems.success && restored.systems.mode==ChallengeMode::HunterResults && restored.player.orbs==1,"Hunter objective failed to reach its reward/results state");
    const auto earned=restored.player.orbs;restored.systems.after_move(restored,{},&models);check(restored.player.orbs==earned,"Hunter objective repeated its reward");
    restored.systems.leave_challenge(restored);check(restored.portal_travel.phase==PortalTravel::Phase::Loading,"Speedway exit did not request home");
    std::cout<<"Flight "<<l.wad_slot<<": categories, completion, repeat rewards, save, timeout, Hunter start, exit passed\n";
}
void breeze(Level l,const ObjectModels& models) {
    Scene scene;scene.wad_slot=39;scene.spawn=l.spawn;Game g(l,scene,{},&models);
    check(!l.terrain_translations.empty() && l.terrain_morphs.size()>10,"Breeze moving terrain/colour tracks were not decoded");
    for(const auto& s:l.scripts)if(s.role==ScriptRole::Burner)g.retail_objects[s.object].attack_flags=flame_attack;
    g.systems.after_move(g,{},&models);check(g.systems.activations>=6,"Burners did not accept flame");
    for(unsigned n=0;n<260;++n)g.systems.after_move(g,{},&models);
    check(g.retail_objects[4].state==1,"Lit burner did not spin fan and activate linked whirlwind");
    bool moved=false;for(const auto& t:g.level.terrain_translations)moved|=t.offset.y>1;check(moved,"Burners did not raise visible terrain");
    for(const auto& s:l.scripts)if(s.role==ScriptRole::CannonTarget)g.retail_objects[s.object].attack_flags=0x10000000;
    g.systems.after_move(g,{},&models);check(g.guide_awards.orbs&(1u<<1),"All eight mines did not award their objective");
    const auto orbs=g.player.orbs;g.systems.after_move(g,{},&models);check(g.player.orbs==orbs,"Cannon reward repeated");
    GameSave save;save.capture(g);Game restored(l,scene,{},&models);save.apply(restored,&models);
    check(restored.retail_objects[2].variant==1 && restored.retail_objects[7].collected,"Machine/mine save lost progress");
    check(std::any_of(restored.level.terrain_morphs.begin(),restored.level.terrain_morphs.end(),[](const auto& m){return m.color && m.group==1 && !(m.flags&2);}),"Saved burner did not resume its lit colour track");
    // Aim and shoot from an authored cannon at a real mine. This checks the
    // projectile sweep rather than injecting the target's attack flag.
    Game aiming(l,scene,{},&models);bool fired=false;
    for(const auto& gun:l.scripts)if(gun.role==ScriptRole::Cannon && !fired)for(const auto& mine:l.scripts)if(mine.role==ScriptRole::CannonTarget && !fired) {
        const auto muzzle=aiming.retail_objects[gun.object].position+Vec3{0,0.65f,0};
        const auto d=aiming.retail_objects[mine.object].position-muzzle;const float distance=length(d);
        const float pitch=std::asin(d.y/distance);
        if(pitch<-.2f || pitch>.9f || distance>60 || aiming.level.raycast(muzzle,d,std::max(0.0f,distance-1)))continue;
        aiming.player.position=aiming.retail_objects[gun.object].position;InputState mount;mount.dialogue.interact=true;aiming.systems.before_move(aiming,mount,&models);
        aiming.player.yaw=std::atan2(d.x,d.z);aiming.player.pitch=pitch;InputState shot;shot.flame_pressed=true;ticks(aiming,1,shot);ticks(aiming,180);
        fired=aiming.systems.cannon_hits>0 && aiming.retail_objects[mine.object].collected;
        aiming.systems.leave_challenge(aiming);
    }
    check(fired,"No authored cannon could hit its visible mine with a real projectile");
    g.systems.start_trolley(g,&models);check(g.systems.mode==ChallengeMode::Trolley,"Trolley did not start");
    const auto start=g.player.position;InputState jump;jump.jump_pressed=true;ticks(g,1,jump);ticks(g,12);
    check(g.systems.trolley_jumps==1 && length(g.player.position-start)>0.2f,"Cart failed to move or jump");
    InputState fire;fire.flame_pressed=true;ticks(g,1,fire);check(g.systems.cannon_shots>0,"Trolley cannon did not fire");
    InputState leave;leave.dialogue.interact=true;ticks(g,1,leave);check(g.systems.mode==ChallengeMode::None,"Cannot leave trolley");
    const auto cannon=std::find_if(l.scripts.begin(),l.scripts.end(),[](const auto& s){return s.role==ScriptRole::Cannon;});
    g.player.position=g.retail_objects[cannon->object].position;g.systems.before_move(g,leave,&models);check(g.systems.cannon==cannon->object,"Cannot mount cannon");
    ticks(g,1,fire);check(g.systems.cannon_shots>=2,"Mounted cannon did not fire");
    g.systems.leave_challenge(g);check(g.systems.cannon==UINT32_MAX,"Leaving challenge retained the mounted cannon");g.systems.start_trolley(g,&models);
    for(const auto& s:l.scripts)if(s.role==ScriptRole::TrackObstacle)g.retail_objects[s.object].collected=true;
    const auto cart=std::find_if(l.scripts.begin(),l.scripts.end(),[](const auto& s){return s.role==ScriptRole::Trolley;});
    for(const auto& s:l.scripts)if(s.role==ScriptRole::Gear){g.retail_objects[cart->object].position=g.retail_objects[s.object].position;ticks(g,1);}
    check(g.systems.success && g.systems.gears==50 && g.systems.mode==ChallengeMode::TrolleyResults && g.guide_awards.orbs==3,"Fifty gears did not finish the trolley and award its own orb bit");
    g.systems.start_trolley(g,&models);check(g.systems.gears==0 && std::none_of(l.scripts.begin(),l.scripts.end(),[&](const auto& s){return s.role==ScriptRole::Gear && g.retail_objects[s.object].collected;}),"Trolley retry did not restore all fifty gears");
    std::cout<<"Breeze burners, terrain, fan/lift, mines, saves, trolley controls and cannon passed\n";
}
void gates(Level l) {
    Scene scene;scene.wad_slot=l.wad_slot;scene.spawn=l.spawn;Game g(l,scene);
    for(const auto& authored:l.objects)if(authored.powerup_gate && authored.powerup_gate->requirement) {
        auto& o=g.retail_objects[authored.id];const auto& p=*o.powerup_gate;
        const auto normal=retail::rotate_model({0,0,1},retail::native_heading(o.rotation[2]*16));
        const auto middle=o.position-Vec3{0,p.origin_offset,0}-retail::origin_above_feet;
        g.player.previous_position=middle-normal*.1f;g.player.position=middle+normal*.1f;
        const auto before=g.powers.gates_activated;g.powers.gates_tick(g);
        check(g.powers.gates_activated==before && !o.powerup_gate->unlocked,"Locked gate granted a power before the spirit requirement");
    }
    for(auto& o:g.retail_objects)if(o.power_target || o.enemy)o.collected=true;
    g.player.gems=10000;g.player.orbs=64;
    for(auto& o:g.retail_objects)if(o.powerup_gate) {
        auto& p=*o.powerup_gate;o.collected=false;p.requirement=0;p.cooldown=0;
        const auto normal=retail::rotate_model({0,0,1},retail::native_heading(o.rotation[2]*16));
        const auto middle=o.position-Vec3{0,p.origin_offset,0}-retail::origin_above_feet;
        g.player.previous_position=middle-normal*0.1f;g.player.position=middle+normal*0.1f;
        const auto before=g.powers.gates_activated;g.powers.gates_tick(g);check(g.powers.gates_activated==before+1,"Authored gate did not grant its power on crossing");
        g.powers.gates_tick(g);check(g.powers.gates_activated==before+1,"Gate granted the same crossing twice during cooldown");
        if(p.kind==0)check(g.powers.invincibility.remaining==p.duration,"Invincibility gate timer mismatch");
        if(p.kind==5)check(std::abs(g.player.velocity.y-(p.duration?p.duration:0x1900)*MovementTuning::velocity_unit)<0.001f && !g.player.grounded,"Superjump gate did not use its authored launch velocity");
        if(p.kind==7)check(g.powers.ice.remaining==p.duration,"Freeze gate timer mismatch");
    }
}
void ice(Level l,const ObjectModels& models) {
    Scene s;s.spawn=l.spawn;s.wad_slot=l.wad_slot;Game g(l,s,{},&models);g.powers.ice={2000,2000};
    const auto target=std::find_if(g.retail_objects.begin(),g.retail_objects.end(),[](const auto& o){return o.type==0x1ee;});
    check(target!=g.retail_objects.end(),"Cloud Temples has no decoded troll");target->attack_flags=freeze_attack;   // the freeze shot's bit (class 270)
    g.powers.tick(g,&models,nullptr);check(target->frozen_ticks==300 && target->animation.clip==4,"Freeze breath did not select the troll's ice pose");
    check(std::any_of(g.powers.visuals().begin(),g.powers.visuals().end(),[](const auto& v){return v.kind==PowerVisualKind::IceShell && v.object.type==0x271;}),"Freeze omitted the original ice shell");
    GameSave save;save.capture(g);Game loaded(l,s,{},&models);save.apply(loaded,&models);check(loaded.powers.ice.remaining==2000,"Ice gate power was lost on save/load");
    for(unsigned t=0;t<300;++t)g.powers.tick(g,&models,nullptr);
    check(!target->frozen_ticks && target->animation.clip==0,"Frozen troll did not thaw");
    // 80030738: with the freeze timer set, Circle fires the level's freeze shot (class 270).
    check(g.powers.freeze_enabled(g),"Cloud Temples' superfreeze has no freeze shot model (270)");
    InputState press;press.flame_pressed=true;g.powers.fire_tick(g,press,true,false);
    for(unsigned t=0;t<5;++t)g.powers.fire_tick(g,{},true,false);
    check(!g.powers.projectiles().empty() && g.powers.projectiles().back().kind==PowerShot::Freeze &&
        g.retail_objects[g.powers.projectiles().back().object].type==0x270,"The superfreeze did not fire the freeze shot");
}
}
int main(int argc,char** argv) {try {
    if(argc!=2)throw FormatError("Pass retail source path");AssetStore store(argv[1]);WadArchive wad(store);auto exe=store.read_all("SCUS_944.25");
    HudController hud;hud.reset(0,0,3);for(unsigned i=0;i<47;++i)hud.power_tick(1);check(hud.power_visible() && hud.power_fill()==1,"Power meter did not enter");
    for(unsigned i=0;i<80;++i)hud.power_tick(0,true);check(hud.power_fill()==1,"Suppressed HUD consumed meter animation");
    for(unsigned i=0;i<48;++i)hud.power_tick(0);check(!hud.power_visible(),"Expired power meter did not slide out");
    unsigned objects=0,gates_total=0,failures=0;
    for(unsigned slot=15;slot<=71;slot+=2) {
      try {
        auto b=wad.read(slot);auto l=decode_level(b,slot);objects+=static_cast<unsigned>(l.objects.size());
        unsigned combat=0,unmapped=0;std::set<unsigned> missing;
        for(const auto& o:l.objects){combat+=o.enemy.has_value();if(o.power_target && !o.power_target->scenery && !o.enemy && !o.scripted_contact){++unmapped;missing.insert(o.type);}}
        std::cout<<"AUDIT "<<slot<<" objects="<<l.objects.size()<<" native_combat="<<combat<<" scripts="<<l.scripts.size()<<" remaining_combat="<<unmapped<<" classes=";for(auto type:missing)std::cout<<std::hex<<type<<',';std::cout<<std::dec<<'\n';
        for(const auto& o:l.objects)gates_total+=o.powerup_gate.has_value();gates(l);
        if(slot==29 || slot==39 || slot==43 || slot==53 || slot==63) {
            auto models=decode_object_models(b,exe);initialize_retail_objects(l,models);
            if(slot==39)breeze(std::move(l),models);else flight(std::move(l),models);
        }
        if(slot==61){auto models=decode_object_models(b,exe);initialize_retail_objects(l,models);ice(std::move(l),models);}
      }catch(const std::exception& e){++failures;std::cerr<<"Scene "<<slot<<": "<<e.what()<<'\n';}
    }
    std::cout<<"All 29 scenes: "<<objects<<" objects, "<<gates_total<<" gates. "<<checks<<" checks, "<<failures<<" scene failures\n";return failures?1:0;
}catch(const std::exception& e){std::cerr<<"After "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
