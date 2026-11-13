#include "spyro/runtime.hpp"
#include "spyro/custom_level.hpp"
#include "spyro/travel.hpp"
#include "spyro/universe.hpp"
#include "spyro/savegame.hpp"
#include <iostream>
#include <set>
using namespace spyro;
namespace {
unsigned checks{};
void check(bool yes,const std::string& message){++checks;if(!yes)throw FormatError(message);}
void step(Game& g,const ObjectModels& models,unsigned count=1){for(unsigned i=0;i<count;++i){tick_object_animations(g.retail_objects,models);g.ripto.tick(g,&models);}}
// The fight checks start without the Crownworks cutscenes; cinematic_checks()
// below turns them back on.
void put(Game& g,Vec3 p){g.player.position=g.player.previous_position=p;g.player.velocity={};g.player.grounded=true;g.cheats.boss_cinematics=false;}
void hit(Game& g,const ObjectModels& m,unsigned flags){g.ripto.damage_cooldown=0;g.retail_objects[g.ripto.actor].attack_flags|=flags;step(g,m);}
}
int main(int argc,char** argv){try {
    if(argc!=3)throw FormatError("Usage: spyro_ripto_tests retail-path crownworks.s2world");
    AssetStore assets(argv[1]);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25"),archive=wad.read(17),boss_archive=wad.read(71);
    auto models=decode_object_models(archive,exe);const auto donor=decode_object_models(boss_archive,exe);
    auto source=decode_level(archive,17);decode_travel(archive,exe,source);
    auto level=load_custom_level(argv[2],wad,source,models,decode_return_portal(archive,exe));
    auto retail=decode_level(boss_archive,71);initialize_retail_objects(retail,donor);
    install_ripto_encounter(level,models,retail,donor);
    const auto crush_archive=wad.read(31);auto crush_models=decode_object_models(crush_archive,exe);
    auto crush_level=decode_level(crush_archive,31);initialize_retail_objects(crush_level,crush_models);
    std::ifstream texture_file(std::filesystem::path(argv[2]).parent_path().parent_path().parent_path().parent_path()/"expansions/backrealms/mecha-crush.rgba",std::ios::binary);
    const Bytes texture{std::istreambuf_iterator<char>(texture_file),std::istreambuf_iterator<char>()};
    install_mecha_crush(level,models,crush_level,crush_models,texture);
    check(models.models.at(0x8f).animations.size()==10,"Crush lost retail animation clips");
    for(const auto& mesh:models.models.at(0x8f).meshes)for(const auto& f:mesh.faces)if(f.textured)for(unsigned k=0;k<(f.quad?4u:3u);++k)
        check(f.uv[k][0]>=0 && f.uv[k][0]<=1 && f.uv[k][1]>=0 && f.uv[k][1]<=1,"Mecha-Crush texture escaped atlas");
    const auto spyro=decode_spyro(wad.read(0),archive,exe,wad.read(29),29,wad.read(15),wad.read(45));
    Scene scene;scene.wad_slot=17;scene.spawn=level.spawn;
    Game g(level,scene,{},&models,&spyro);
    if(std::getenv("CROWN_HEIGHTS")) {
        // Floor heights over the arena (the tiers), bucketed by 0.25.
        std::map<int,unsigned> heights;const auto& b=level.bounds;
        for(float x=b.min.x;x<=b.max.x;x+=.5f)for(float z=b.min.z;z<=b.max.z;z+=.5f)
            if(const auto f=level.raycast({x,b.max.y+1,z},{0,-1,0},b.max.y-b.min.y+2);f && f->normal.y>.8f)++heights[int(std::floor(f->point.y*4))];
        std::cout<<"bounds "<<b.min.x<<","<<b.min.y<<","<<b.min.z<<" .. "<<b.max.x<<","<<b.max.y<<","<<b.max.z<<"\n";
        for(const auto& [h,n]:heights)if(n>20)std::cout<<"  floor "<<h/4.0f<<": "<<n<<"\n";
        if(level.custom_arena)for(const auto& p:level.custom_arena->pieces)std::cout<<"  piece "<<int(p.kind)<<" "<<p.name<<" at "<<p.center.x<<","<<p.center.y<<","<<p.center.z<<" r "<<p.radius<<"\n";
        return 0;
    }
    check(g.ripto.enabled && !g.ripto.engaged,"Boss failed to initialize");
    check(g.retail_objects[g.ripto.actor].boss_body.has_value(),"Ripto lacks solid collision");
    // Elemental attacks reach the bosses. Their bodies are scripted-contact
    // actors, which element_target used to reject before it saw boss_body,
    // so no breath, shot, wave or Fury could touch Ripto, the mechs or the bird.
    {
        Game s(level,scene,{},&models,&spyro);s.player.position=s.player.previous_position={0,0,-7};
        s.ripto.prepare_capture_stage(s,models,RiptoStage::Foot);
        check(element_target(s.retail_objects[s.ripto.actor]),"Ripto's body is not an elemental target");
        s.upgrades.enabled=true;for(auto& t:s.upgrades.elements.tiers)t=2;s.upgrades.elements.energy=100;s.player.invulnerable_ticks=100000;
        const auto health=s.ripto.health;bool landed=false;
        for(unsigned t=0;t<240 && !landed;++t) {
            // Face Ripto and fire the equipped (Fire) shot every half second.
            const auto d=s.retail_objects[s.ripto.actor].position-s.player.position;
            InputState in;in.camera_relative=false;in.move_x=d.x*.01f;in.move_z=d.z*.01f;in.shot_pressed=t%30==5;
            s.advance(FixedClock::step,in);landed=s.ripto.health<health;
        }
        check(landed && s.elements.shots_fired>0,"An element shot fired at Ripto never landed");
    }
    // Offline phase selection must use the real transformation, then allow
    // ordinary movement on the arena floor without resetting feet each tick.
    for(auto stage:{RiptoStage::Foot,RiptoStage::MechaGulp,RiptoStage::Bird}) {
        Game captured(level,scene,{},&models,&spyro);
        captured.player.position=captured.player.previous_position={0,0,-7};
        captured.ripto.prepare_capture_stage(captured,models,stage);
        check(captured.ripto.stage==stage && captured.ripto.engaged,"Capture selected an inert or incorrect boss phase");
        if(stage==RiptoStage::Foot)check(captured.ripto.action==RiptoAction::Seek,"Foot phase capture left the boss waiting instead of fighting");
        check(captured.clock.ticks()==0,"Phase setup consumed gameplay input time");
        if(stage==RiptoStage::MechaGulp)check(captured.ripto.health==80 && captured.ripto.crush_health==60 &&
            !captured.retail_objects[captured.ripto.gulp].hidden && !captured.retail_objects[captured.ripto.crush].hidden,
            "Capture bypassed the two-mech transformation");
        if(stage!=RiptoStage::Bird) {
            unsigned grounded=0,falling=0;InputState input;input.move_x=.4f;
            for(unsigned i=0;i<90;++i){captured.advance(FixedClock::step,input);if(i>=5){grounded+=captured.player.grounded;falling+=captured.player.motion==Motion::Fall;}}
            check(grounded>=80 && falling<=5,"Grounded Crownworks gameplay keeps falling");
            check(captured.player.position.x>1,"Crownworks capture does not use normal movement");
        }
    }
    // The optic must stay attached to the true eye across every shipped pose.
    const auto& r=donor.models.at(0x2af);
    for(const auto& animation:r.animations)for(const auto& f:animation.frames){
        const auto e=ripto_eye_socket(f.vertices);
        check(finite(e.center) && finite(e.normal) && length(e.normal)>.99f && e.width>.01f && e.height>.005f,"Degenerate eye socket in a retail pose");
        const auto inner=(f.vertices[157]+f.vertices[161])*.5f,outer=(f.vertices[168]+f.vertices[165])*.5f;
        check(length(e.center-lerp(inner,outer,.61f))<.0001f,"Optic escaped the eye quad");
    }
    // Original saddle/bird attachment streams must exist for all used poses.
    for(const unsigned type:{0x2b0u,0x2adu})for(unsigned c=0;c<(type==0x2b0?18u:4u);++c){
        auto a=start_object_animation(models.models.at(type),c);
        for(unsigned n=0;n<240;++n){tick_object_animation(models.models.at(type),a);
            check(finite(sample_object_attachment(models.models.at(type),a,models.animation_blends,type==0x2b0?14:0)),"Nonfinite mount socket");}
    }
    put(g,{0,.03f,-7});g.player.invulnerable_ticks=100000;step(g,models);
    check(g.ripto.engaged,"Approach did not start boss fight");
    const auto old=g.retail_objects[g.ripto.actor].position;step(g,models,30);
    check(length(old-g.retail_objects[g.ripto.actor].position)>.2f,"Ripto did not move");
    std::set<RiptoAction> seen;
    for(unsigned n=0;n<6000;++n){put(g,{0,.03f,-7});step(g,models);seen.insert(g.ripto.action);check(g.ripto.projected_shots()<=96,"Unbounded projectiles");}
    for(const auto a:{RiptoAction::Cast,RiptoAction::Shockwaves,RiptoAction::FireSpiral,RiptoAction::ShieldRush})check(seen.contains(a),std::string("Missing attack: ")+std::to_string(int(a)));
    check(g.ripto.attack_counts[0] && g.ripto.attack_counts[1] && g.ripto.attack_counts[2] && g.ripto.attack_counts[3],"Ground attack emitted no hazard");
    check(g.ripto.attack_counts[6] && g.ripto.attack_counts[7],"Cyborg-specific attack choreography never ran");
    check(!g.ripto.orbs().empty(),"Hunter never supplied power orbs");
    // Exercise both walking and charging through the real solid resolver.
    auto& actor=g.retail_objects[g.ripto.actor];actor.position={0,.02f,0};
    for(bool charge:{false,true}){
        Vec3 center{.2f,.55f,0},velocity{-5,0,0};bool grounded=true;
        const auto out=g.interactions.resolve_player(g.retail_objects,&models,center,g.tuning.radius,velocity,grounded,charge,charge_attack);
        check(out.x>.5f && std::abs(velocity.x)<.001f,"Spyro passed through Ripto");
        if(charge)check(actor.attack_flags&charge_attack,"Solid charge did not register an attack");
    }
    actor.attack_flags=0;g.level.custom_arena->boss_shield=false;const auto hp=g.ripto.health;
    hit(g,models,flame_attack);check(g.ripto.health==hp-1,"Normal flame cannot damage Ripto");
    hit(g,models,charge_attack);check(g.ripto.health==hp-3,"Normal charge cannot damage Ripto");
    hit(g,models,0x400000);check(g.ripto.health==hp-18,"Green bomb damage is not stronger");
    while(g.ripto.action!=RiptoAction::Transform)hit(g,models,0x400000);
    step(g,models,151);check(g.ripto.stage==RiptoStage::MechaGulp && g.ripto.health==80,"Mecha-Gulp phase did not begin");
    check(g.ripto.crush_health==60 && !g.retail_objects[g.ripto.crush].hidden && !g.retail_objects[g.ripto.gulp].hidden,"Both mechs must be present together");
    bool fire_train=false,flame_stream=false,mortar=false,combo_pose=false;
    const auto crush_start=g.retail_objects[g.ripto.crush].position;
    seen.clear();for(unsigned n=0;n<6000;++n){put(g,{0,.03f,-7});step(g,models);seen.insert(g.ripto.action);
        for(const auto& shot:g.ripto.shots())mortar|=shot.kind==RiptoShotKind::Mortar;
        combo_pose|=g.ripto.action==RiptoAction::GulpPincer && g.ripto.action_ticks>60 && g.ripto.action_ticks<110 && g.retail_objects[g.ripto.crush].animation.next_clip==9;
        for(const auto& shot:g.ripto.shots()){fire_train|=shot.kind==RiptoShotKind::FireTrain;flame_stream|=shot.kind==RiptoShotKind::GulpFlame;}
        for(const auto& v:g.ripto.visuals())if(v.animation.active)check(!sample_object_animation(models.models.at(v.type),v.animation,models.animation_blends).vertices.empty(),"Invalid boss effect pose");
    }
    check(fire_train && flame_stream,"Mecha-Gulp did not emit his original fire stream/train assets");
    for(const auto a:{RiptoAction::GulpStomp,RiptoAction::GulpLasers,RiptoAction::GulpFire,RiptoAction::GulpFireTrain})check(seen.contains(a),std::string("Missing Gulp attack: ")+std::to_string(int(a)));
    check(g.ripto.crush_attacks && length(crush_start-g.retail_objects[g.ripto.crush].position)>1,"Mecha-Crush is idle");
    check(mortar && seen.contains(RiptoAction::GulpBombard),"Gulp bombardment did not launch");
    check(combo_pose && g.ripto.attack_counts[10],"Coordinated Crush/Gulp animation sequence never ran");
    for(unsigned n=0;n<15 && g.ripto.health;++n)hit(g,models,0x400000);
    check(g.ripto.health==0 && g.ripto.stage==RiptoStage::MechaGulp && g.ripto.crush_health==60,"Gulp defeat bypassed the living Crush");
    check(g.retail_objects[g.ripto.gulp].hidden && !g.retail_objects[g.ripto.crush].hidden,"Independent Gulp defeat hid both bosses");
    const auto crush_attacks=g.ripto.crush_attacks;step(g,models,700);
    check(g.ripto.crush_attacks>crush_attacks,"Crush stopped fighting after Gulp died");
    for(unsigned n=0;n<10 && g.ripto.crush_health;++n){g.retail_objects[g.ripto.crush].attack_flags|=green_bomb_attack;step(g,models,71);}
    check(!g.ripto.crush_health && g.ripto.action==RiptoAction::Transform,"Defeating both mechs failed to advance");
    step(g,models,151);check(g.ripto.stage==RiptoStage::Bird,"Bird phase did not begin");
    step(g,models,1100);check(g.ripto.attack_counts[5]>0 && g.ripto.attack_counts[9]>0 && g.powers.flight.remaining && g.powers.fire.remaining,"Aerial phase has no volleys, dive or player powers");
    // Crownworks finale (6.3): the bird falls and the crown core is exposed;
    // a Fury ends the fight.
    while(g.ripto.stage!=RiptoStage::Defeated && g.ripto.action!=RiptoAction::Finale)hit(g,models,0x400000);
    check(g.ripto.action==RiptoAction::Finale && g.ripto.finale_ticks+1>=RiptoBoss::finale_window,"Knocking the bird down did not expose the crown core");
    g.ripto.finale_fury(g,Element::Fire);step(g,models);
    check(g.ripto.stage==RiptoStage::Defeated && g.ripto.action==RiptoAction::Death,"A Fury did not end the finale");
    step(g,models,151);check(g.ripto.defeated() && g.level.custom_arena->complete,"Boss defeat did not complete arena");
    const auto orbs=g.player.orbs;step(g,models,300);check(g.player.orbs==orbs && orbs==1,"Victory reward duplicated");
    // Reverse defeat order, collider, and lethal-hit reset of both actors.
    Game pair(level,scene,{},&models,&spyro);put(pair,{0,.03f,-7});pair.player.invulnerable_ticks=100000;step(pair,models);
    pair.level.custom_arena->boss_shield=false;pair.level.custom_arena->boss_repair=false;
    pair.ripto.health=1;hit(pair,models,green_bomb_attack);step(pair,models,151);
    auto& crush=pair.retail_objects[pair.ripto.crush];
    Vec3 contact=crush.position+Vec3{.1f,.6f,0},velocity{-5,0,0};bool grounded=true;
    const auto resolved=pair.interactions.resolve_player(pair.retail_objects,&models,contact,pair.tuning.radius,velocity,grounded,false,0);
    check(length(resolved-contact)>.5f,"Mecha-Crush is not solid relative to Spyro");
    for(unsigned n=0;n<5000 && pair.ripto.action!=RiptoAction::GulpPincer;++n)step(pair,models);
    check(pair.ripto.action==RiptoAction::GulpPincer,"No team attack to validate interruption");
    hit(pair,models,green_bomb_attack);
    check(pair.ripto.action==RiptoAction::Hit && pair.ripto.crush_action==CrushAction::Recover,"Interrupting Gulp stranded Crush in the team animation");
    for(unsigned n=0;n<8 && pair.ripto.crush_health;++n){crush.attack_flags|=green_bomb_attack;step(pair,models,71);}
    check(!pair.ripto.crush_health && pair.ripto.health>0 && pair.ripto.stage==RiptoStage::MechaGulp,"Crush defeat bypassed living Gulp");
    step(pair,models,120);check(crush.hidden && !pair.retail_objects[pair.ripto.gulp].hidden,"Independent Crush defeat hid Gulp or left Crush visible");
    for(unsigned n=0;n<15 && pair.ripto.health;++n)hit(pair,models,green_bomb_attack);
    step(pair,models,151);check(pair.ripto.stage==RiptoStage::Bird,"Reverse mech defeat order failed");
    pair.ripto.reset(pair,&models);check(crush.hidden && !crush.boss_body && pair.ripto.crush_health==0 && pair.ripto.crush_action==CrushAction::Dormant,"Reset retained the second boss");
    check(pair.ripto.stage==RiptoStage::Bird && pair.ripto.health==80 && pair.ripto.phase_restarts==1,"A lost life in the bird phase did not restart that phase");
    // Crownworks failure flow: a lost life restarts the current phase from
    // its own start (both mechs at full health), not the whole fight.
    {
        Game retry(level,scene,{},&models,&spyro);retry.player.position=retry.player.previous_position={0,0,-7};
        retry.ripto.prepare_capture_stage(retry,models,RiptoStage::MechaGulp);
        retry.level.custom_arena->boss_shield=false;hit(retry,models,green_bomb_attack);
        check(retry.ripto.health<80,"Mecha-Gulp took no damage before the retry");
        retry.player.health=0;retry.player.invulnerable_ticks=0;check(retry.hurt(retry.player.position+Vec3{1,0,0}),"Lethal hit was not taken");
        check(retry.ripto.stage==RiptoStage::MechaGulp && retry.ripto.engaged && retry.ripto.health==80 && retry.ripto.crush_health==60 &&
            !retry.retail_objects[retry.ripto.gulp].hidden && !retry.retail_objects[retry.ripto.crush].hidden && retry.ripto.phase_restarts==1,
            "A lost life did not restart the two-mech phase at full health");
        check(length(retry.player.position-retry.play_scene.spawn)<.01f,"Spyro did not return to the arena entrance");
    }
    // Elemental Furies (element_boss_flags -> 0x140000, marked fury_hit) deal
    // at most a third of the phase's health; ordinary attacks still count.
    {
        Game fury(level,scene,{},&models,&spyro);put(fury,{0,.03f,-7});fury.player.invulnerable_ticks=100000;step(fury,models);
        fury.level.custom_arena->boss_shield=false;fury.level.custom_arena->boss_repair=false;
        for(unsigned n=0;n<20;++n){fury.retail_objects[fury.ripto.actor].fury_hit=true;hit(fury,models,0x140000);}
        check(fury.ripto.health==40 && fury.ripto.stage==RiptoStage::Foot,"An elemental Fury exceeded a third of Cyborg Ripto's 60 HP");
        hit(fury,models,green_bomb_attack);check(fury.ripto.health==25,"Ordinary attacks stopped counting after the Fury cap");
        // Relays act in the two-mech phase only: the shield does not halve phase-one damage.
        fury.level.custom_arena->boss_shield=true;hit(fury,models,charge_attack);
        check(fury.ripto.health==23,"The shield relay weakened Cyborg Ripto outside the two-mech phase");
    }
    // 6.4: per-element damage comes from the element system's hit record, not
    // the attack masks: breath 1/2/3 by element, shot 3, wave 4, Fury 4.
    {
        check(boss_element_damage({Element::Fire,1,{},0,false,0})==1 && boss_element_damage({Element::Water,1,{},0,false,0})==1 &&
            boss_element_damage({Element::Ice,1,{},0,false,0})==1 && boss_element_damage({Element::Electric,1,{},0,false,0})==2 &&
            boss_element_damage({Element::Convexity,2,{},0,false,0})==3 && boss_element_damage({Element::Water,2,{},0,false,0,HitSource::Shot})==3 &&
            boss_element_damage({Element::Fire,2,{},0,false,0,HitSource::Wave})==4 && boss_element_damage({Element::Electric,2,{},0,false,0,HitSource::Fury})==4 &&
            boss_element_damage({Element::Ice,0,{},0,false,0,HitSource::Fury})==0,"The Crownworks element damage table is wrong");
        Game e(level,scene,{},&models,&spyro);put(e,{0,.03f,-7});e.player.invulnerable_ticks=100000;step(e,models);
        e.level.custom_arena->boss_shield=false;e.level.custom_arena->boss_repair=false;
        for(const unsigned damage:{1u,2u,3u,4u}) {
            const auto hp=e.ripto.health;e.retail_objects[e.ripto.actor].element_damage=damage;hit(e,models,flame_attack);
            check(e.ripto.health==hp-damage,"Elemental hit of "+std::to_string(damage)+" dealt "+std::to_string(hp-e.ripto.health));
        }
        check(!e.ripto.blocks_dark(),"Dark Spyro is blocked during ordinary fighting");
    }
    // 6.4: Ice freezes a mech for at most 1.5 s, once per 10 s; an Electric
    // breath on one mech chains 1 HP to the other.
    {
        Game m(level,scene,{},&models,&spyro);put(m,{0,.03f,-7});m.player.invulnerable_ticks=100000;
        m.ripto.prepare_capture_stage(m,models,RiptoStage::MechaGulp);
        m.retail_objects[m.ripto.gulp].element_freeze=true;step(m,models,2);
        check(m.ripto.gulp_frozen>0 && m.retail_objects[m.ripto.gulp].frozen_ticks>0 && m.ripto.mech_freezes==1,"Ice did not freeze Mecha-Gulp");
        const auto action=m.ripto.action;const auto ticks=m.ripto.action_ticks;step(m,models,30);
        check(m.ripto.action==action && m.ripto.action_ticks==ticks,"A frozen Mecha-Gulp kept acting");
        step(m,models,RiptoBoss::mech_freeze);check(!m.ripto.gulp_frozen,"Mecha-Gulp stayed frozen past 1.5 s");
        m.retail_objects[m.ripto.gulp].element_freeze=true;step(m,models);
        check(!m.ripto.gulp_frozen && m.ripto.mech_freezes==1,"Ice froze Mecha-Gulp again within 10 s");
        m.level.custom_arena->boss_shield=false;
        const auto crush=m.ripto.crush_health;auto& gulp=m.retail_objects[m.ripto.gulp];
        gulp.element_chain=true;gulp.element_damage=2;gulp.attack_flags|=flame_attack;m.ripto.damage_cooldown=0;step(m,models);
        check(m.ripto.crush_health==crush-1 && m.ripto.chain_hits>=1,"An Electric breath on Mecha-Gulp did not chain to Mecha-Crush");
    }
    // 6.3 finale: ordinary hits do nothing, the window closes after 20 s with
    // Ripto back in the air at a quarter health, and no Dark Spyro meanwhile.
    {
        Game f(level,scene,{},&models,&spyro);put(f,{0,.03f,-7});f.player.invulnerable_ticks=100000;
        f.ripto.prepare_capture_stage(f,models,RiptoStage::Bird);
        while(f.ripto.action!=RiptoAction::Finale && f.ripto.stage==RiptoStage::Bird)hit(f,models,0x400000);
        check(f.ripto.action==RiptoAction::Finale && f.ripto.blocks_dark(),"The finale did not start or allowed Dark Spyro");
        hit(f,models,flame_attack);check(f.ripto.action==RiptoAction::Finale,"An ordinary hit ended the finale");
        for(unsigned n=0;n<RiptoBoss::finale_window+2 && f.ripto.action==RiptoAction::Finale;++n)step(f,models);
        check(f.ripto.stage==RiptoStage::Bird && f.ripto.action==RiptoAction::BirdFlight && f.ripto.health==20 && f.ripto.finales_missed==1,
            "A missed finale did not send Ripto back into the air at a quarter health");
    }
    // 6.5.6 difficulty: assist stretches the optic lance's warning by half;
    // hard keeps the overdrive's faster attack rhythm in phase one.
    {
        const auto lance_warning=[&](BossDifficulty difficulty) {
            Game d(level,scene,{},&models,&spyro);d.cheats.boss_difficulty=difficulty;put(d,{0,.03f,-7});d.player.invulnerable_ticks=100000;
            for(unsigned n=0;n<6000 && d.ripto.action!=RiptoAction::CyborgLance;++n)step(d,models);
            unsigned frames=0;for(;frames<400 && d.ripto.action==RiptoAction::CyborgLance && d.ripto.action_ticks<72;++frames)step(d,models);
            return frames;
        };
        const auto normal=lance_warning(BossDifficulty::Normal),assist=lance_warning(BossDifficulty::Assist);
        check(normal>=70 && normal<=72 && assist>=normal*3/2-3,"Assist did not stretch the lance warning ("+std::to_string(normal)+" -> "+std::to_string(assist)+")");
        const auto first_attack=[&](BossDifficulty difficulty) {
            Game d(level,scene,{},&models,&spyro);d.cheats.boss_difficulty=difficulty;put(d,{0,.03f,-7});d.player.invulnerable_ticks=100000;
            unsigned n=0;for(;n<600 && (d.ripto.action==RiptoAction::Seek || !d.ripto.engaged);++n)step(d,models);
            return n;
        };
        check(first_attack(BossDifficulty::Hard)<first_attack(BossDifficulty::Normal),"Hard difficulty did not speed up Ripto's attacks");
    }
    // The visible optic sightline is harmless until release; its locked aim
    // must damage a stationary target and allow a sideways dodge.
    for(bool dodge:{false,true}) {
        Game optic(level,scene,{},&models,&spyro);put(optic,{0,.03f,-7});optic.player.invulnerable_ticks=100000;
        for(unsigned n=0;n<6000 && !(optic.ripto.action==RiptoAction::CyborgLance && optic.ripto.action_ticks==65);++n)step(optic,models);
        check(optic.ripto.action==RiptoAction::CyborgLance && optic.ripto.action_ticks==65,"Optic lance test never reached the locked warning");
        if(dodge)put(optic,optic.player.position+Vec3{5,0,0});
        optic.player.invulnerable_ticks=0;const auto hp=optic.player.health;
        step(optic,models,5);check(optic.player.health==hp,"Warning sightline dealt damage");
        step(optic,models,10);check(dodge?optic.player.health==hp:optic.player.health<hp,"Optic release hit/dodge does not match its locked sightline");
    }
    // 6.5.4 cutscenes and 6.5.2 phase light. The intro holds the arena and the
    // controls and wakes the lower ring blue; the summon shows the mechs arrive
    // as the terrace turns amber; the flight turns the sky violet; the outro
    // raises the exit gate. Each can be skipped once it has been seen.
    {
        const auto tier=[&](const Game& g,ArenaKind kind){for(const auto& p:g.level.custom_arena->pieces)if(p.kind==kind)return p.value;return -1000.f;};
        Game c(level,scene,{},&models,&spyro);put(c,{0,.03f,-7});c.cheats.boss_cinematics=true;c.player.invulnerable_ticks=100000;
        step(c,models);const auto dormant=c.ripto.light(1);   // the first tick sets the dormant light
        check(c.ripto.cinematic==CrownCinematic::Intro && c.scene_locked && c.ripto.action==RiptoAction::Waiting,"Engaging Cyborg Ripto did not start the intro");
        c.ripto.skip_requested=true;step(c,models,2);
        check(c.ripto.cinematic==CrownCinematic::Intro && !c.ripto.cinematics_skipped,"The intro could be skipped before it had been seen");
        check(c.camera.scripted_view.has_value() && c.camera.scripted_exact,"The intro does not drive the camera");
        const auto eye=c.camera.scripted_view->eye;step(c,models,200);
        check(c.ripto.action==RiptoAction::Waiting && c.ripto.shots().empty() && length(c.camera.scripted_view->eye-eye)>3,"The intro released the fight or held one shot");
        step(c,models,RiptoBoss::cinematic_length(CrownCinematic::Intro)-202);
        check(c.ripto.cinematic==CrownCinematic::None && !c.scene_locked && !c.camera.scripted_view && c.ripto.action==RiptoAction::Seek &&
            (c.ripto.cinematics_seen>>static_cast<unsigned>(CrownCinematic::Intro)&1),"The intro did not hand over to the fight");
        step(c,models,121);const auto ring=c.ripto.light(1);
        check(ring.lower.z>ring.lower.x+.2f && ring.lower.z>dormant.lower.z+.4f && ring.lower_cast.z>.2f && ring.terrace.z<ring.lower.z,"Phase one does not light the lower ring blue");
        c.level.custom_arena->boss_shield=c.level.custom_arena->boss_repair=false;
        while(c.ripto.action!=RiptoAction::Transform)hit(c,models,0x400000);
        check(c.ripto.cinematic==CrownCinematic::Summon && c.scene_locked,"The mech summon has no cutscene");
        step(c,models,151);
        check(c.ripto.stage==RiptoStage::MechaGulp && c.ripto.cinematic==CrownCinematic::Summon,"The summon cutscene did not show the mechs arriving");
        const auto gulp=c.retail_objects[c.ripto.gulp].position,crush=c.retail_objects[c.ripto.crush].position;step(c,models,40);
        check(length(c.retail_objects[c.ripto.gulp].position-gulp)<.01f && length(c.retail_objects[c.ripto.crush].position-crush)<.01f,"The mechs moved during the summon cutscene");
        step(c,models,RiptoBoss::cinematic_length(CrownCinematic::Summon)-190);
        check(c.ripto.cinematic==CrownCinematic::None && !c.scene_locked,"The summon cutscene did not end");
        step(c,models,121);const auto amber=c.ripto.light(1);
        check(amber.terrace_cast.x>amber.terrace_cast.z+.4f && amber.terrace.x>amber.terrace.z+.3f && ring.terrace_cast.x<.01f,"Phase two does not light the terrace amber");
        for(unsigned n=0;n<15 && c.ripto.health;++n)hit(c,models,green_bomb_attack);
        for(unsigned n=0;n<10 && c.ripto.crush_health;++n){c.retail_objects[c.ripto.crush].attack_flags|=green_bomb_attack;step(c,models,71);}
        check(c.ripto.cinematic==CrownCinematic::Flight,"Ripto took flight without a cutscene");
        step(c,models,RiptoBoss::cinematic_length(CrownCinematic::Flight)+122);
        const auto violet=c.ripto.light(1);
        check(c.ripto.cinematic==CrownCinematic::None && c.ripto.stage==RiptoStage::Bird && violet.sky_cast.z>violet.sky_cast.y*3 && violet.sky_cast.x>violet.sky_cast.y*2 &&
            violet.sky.z>violet.sky.y,"Phase three does not turn the sky violet");
        while(c.ripto.stage!=RiptoStage::Defeated && c.ripto.action!=RiptoAction::Finale)hit(c,models,0x400000);
        step(c,models,121);check(c.ripto.light(1).core.x>.5f,"The exposed crown core casts no light");
        c.ripto.finale_fury(c,Element::Fire);step(c,models);
        check(c.ripto.cinematic==CrownCinematic::Outro && c.ripto.action==RiptoAction::Death,"The finale has no outro");
        for(unsigned n=0;n<200;++n){tick_custom_level(c,{});step(c,models);}
        check(c.level.custom_arena->complete && tier(c,ArenaKind::ExitGate)<.01f,"The outro opened the gate before its shot");
        for(unsigned n=0;n<100;++n){tick_custom_level(c,{});step(c,models);}
        check(tier(c,ArenaKind::ExitGate)>3,"The exit gate did not rise in the outro");
        for(unsigned n=0;n<80;++n){tick_custom_level(c,{});step(c,models);}
        check(c.ripto.cinematic==CrownCinematic::None && !c.scene_locked && c.ripto.cinematics_played==4 &&
            std::any_of(c.level.portals.begin(),c.level.portals.end(),[](const auto& p){return p.return_home && p.enabled;}),"The outro did not end with Return Home open");
        // Seen cutscenes skip on jump; phase setup never plays them.
        Game d(level,scene,{},&models,&spyro);put(d,{0,.03f,-7});d.cheats.boss_cinematics=true;d.player.invulnerable_ticks=100000;
        step(d,models,RiptoBoss::cinematic_length(CrownCinematic::Intro)+2);d.ripto.reset(d,&models);put(d,{0,.03f,-7});d.cheats.boss_cinematics=true;step(d,models);
        check(d.ripto.cinematic==CrownCinematic::Intro && d.ripto.cinematic_skippable(),"A seen intro is not offered as skippable");
        InputState jump;jump.jump_pressed=true;d.advance(FixedClock::step,jump);
        check(d.ripto.cinematic==CrownCinematic::None && d.ripto.cinematics_skipped==1 && d.ripto.action==RiptoAction::Seek,"Jump did not skip a seen intro");
        Game q(level,scene,{},&models,&spyro);q.player.position=q.player.previous_position={0,0,-7};
        q.ripto.prepare_capture_stage(q,models,RiptoStage::Bird);
        check(q.ripto.cinematic==CrownCinematic::None && !q.scene_locked && !q.ripto.cinematics_played,"Phase setup played a cutscene");
        std::cout<<"Crownworks cutscenes: "<<c.ripto.cinematics_played<<" played, light changes "<<c.ripto.light_changes<<"\n";
    }
    // 6.5.1: the north and south dry crossings are ring sectors about 7 units
    // wide: walking across near their edges (2.9 off the axis) stays dry.
    {
        Game w(level,scene,{},&models,&spyro);
        for(const float sign:{1.f,-1.f})for(const float lateral:{-2.9f,0.f,2.9f}) {
            put(w,{lateral,.03f,sign*9.f});w.player.invulnerable_ticks=100000;w.ripto.enabled=false;
            bool lava=false,crossed=false;
            for(unsigned t=0;t<300 && !crossed;++t) {
                InputState in;in.move_z=sign;w.advance(FixedClock::step,in);
                lava|=w.player.position.y<-.4f;crossed=w.player.grounded && std::hypot(w.player.position.x,w.player.position.z)>16.6f;
            }
            check(crossed && !lava,"The widened crossing is not walkable at "+std::to_string(lateral)+" off its axis");
        }
        for(const float sign:{1.f,-1.f})for(const float lateral:{-3.2f,3.2f}) {
            const auto f=w.level.raycast({lateral,2,sign*14.f},{0,-1,0},4);
            check(f && f->point.y>-.05f,"The crossing deck does not reach 3.2 off its axis");
        }
    }
    // 6.4 slicing: in the Crownworks the Void Beam cuts only the four pylons
    // (SLICEABLE volumes); a cut pylon topples inward across the molten ring
    // (radius 12..16) and lands as a walkable crossing.
    {
        Game s(level,scene,{},&models,&spyro);put(s,{0,.03f,-7});
        check(s.level.sliceable.size()==4,"The Crownworks has no sliceable pylons");
        check(!s.destruction.slice(s,{0,.6f,-6},{0,.05f,-8},{0,-.3f,-1},{},&models,{0,1,0}) && !s.destruction.slices,"The Void Beam cut the Crownworks floor");
        check(!s.destruction.slice(s,{0,4.1f,-26},{0,4.1f,-31.9f},{0,0,-1},{},&models),"The Void Beam cut a Crownworks wall");
        const float d=17.4f*std::sqrt(.5f);
        const Vec3 foot{d,1.1f,d},mouth=foot+Vec3{-2.2f,0,0};
        check(s.destruction.slice(s,mouth,foot-Vec3{.75f,0,0},Vec3{1,0,0},{},&models),"The Void Beam could not cut a pylon");
        check(s.destruction.slices==1 && s.destruction.slabs().size()==1,"A pylon cut did not come away as one piece");
        for(unsigned t=0;t<900 && !s.destruction.slabs().empty();++t)s.destruction.tick(s);
        check(s.destruction.slabs().empty() && s.destruction.slabs_landed==1,"The cut pylon never landed");
        unsigned bridged=0;std::string heights;
        for(const float r:{12.6f,13.4f,14.2f,15.0f,15.8f}) {
            const float k=r*std::sqrt(.5f);
            const auto f=s.level.raycast({k,6,k},{0,-1,0},10);
            heights+=" "+(f?std::to_string(f->point.y).substr(0,5):std::string("none"));
            bridged+=f && f->point.y>-.35f && f->normal.y>.5f;
        }
        const auto& cut=s.destruction.damage().back();
        std::cout<<"Pylon crossing: floor heights across the molten ring"<<heights<<"; cut at "<<cut.center.x<<","<<cut.center.y<<","<<cut.center.z
                 <<" r "<<cut.slice_radius<<", landed offset "<<cut.slice_offset.x<<","<<cut.slice_offset.y<<","<<cut.slice_offset.z
                 <<" rotation "<<cut.slice_rotation.x<<","<<cut.slice_rotation.y<<","<<cut.slice_rotation.z<<"\n";
        check(bridged>=4,"The cut pylon did not fall across the molten ring:"+heights);
        // Cross on it with the real controller: from the island, jump onto the
        // fallen pylon's low end, run along it over the lava, drop to the outer floor.
        const Vec3 out{std::sqrt(.5f),0,std::sqrt(.5f)};float tip=12.6f;
        for(float r=12.6f;r>5;r-=.1f){const auto f=s.level.raycast(out*r+Vec3{0,6,0},{0,-1,0},10);if(f && f->point.y>.4f)tip=r;else break;}
        put(s,out*(tip-2.4f)+Vec3{0,.03f,0});s.player.yaw=s.player.previous_yaw=std::atan2(out.x,out.z);s.player.invulnerable_ticks=100000;
        bool on=false,crossed=false,lava=false;float peak=0;
        for(unsigned t=0;t<420 && !crossed;++t) {
            InputState in;in.move_x=out.x;in.move_z=out.z;in.jump_pressed=t==4;in.jump_held=t>=4 && t<24;s.advance(FixedClock::step,in);
            const float r=std::hypot(s.player.position.x,s.player.position.z);
            if(s.player.grounded && r>12 && r<16){on|=s.player.position.y>.8f;peak=std::max(peak,s.player.position.y);}
            lava|=s.player.position.y<-.6f;
            crossed=s.player.grounded && r>16.6f;
        }
        std::cout<<"Pylon crossing walked: tip at radius "<<tip<<", on the beam "<<on<<" (up to "<<peak<<"), crossed "<<crossed<<", lava "<<lava<<"\n";
        check(on && crossed && !lava,"Spyro cannot cross the molten ring on the fallen pylon");
    }
    std::cout<<"Three phases and all attack families completed; validating real gameplay input\n";
    Game live(level,scene,{},&models,&spyro);put(live,{0,.03f,-7});live.player.yaw=live.player.previous_yaw=0;live.player.invulnerable_ticks=100000;
    for(unsigned i=0;i<400 && live.ripto.action!=RiptoAction::Cast;++i)live.advance(FixedClock::step,{});
    put(live,live.retail_objects[live.ripto.actor].position+Vec3{0,.01f,-1.15f});
    InputState fire;fire.flame_pressed=true;live.advance(FixedClock::step,fire);
    for(unsigned n=0;n<75;++n)live.advance(FixedClock::step,{});
    if(!live.ripto.hits){const auto p=live.player.position,b=live.retail_objects[live.ripto.actor].position;std::cerr<<"Flame miss player="<<p.x<<","<<p.y<<","<<p.z<<" boss="<<b.x<<","<<b.y<<","<<b.z<<" action="<<live.ripto.action_name()<<" yaw="<<live.player.yaw<<"\n";}
    check(live.ripto.hits>0,"Real flame input failed to damage the boss");
    const auto time=live.ripto.ticks;live.set_paused(true);live.advance(1,{});check(live.ripto.ticks==time,"Paused encounter kept running");live.set_paused(false);
    // Force a real incoming projectile contact, then a lethal contact while
    // the controller is iterating its shot vector (respawn invalidation).
    live.ripto.reset(live,&models);put(live,{0,.03f,-7});live.player.invulnerable_ticks=100000;
    for(unsigned n=0;n<800 && live.ripto.shots().empty();++n)step(live,models);
    check(!live.ripto.shots().empty(),"No incoming shot for damage test");
    auto shot=live.ripto.shots().front();put(live,shot.visual.position+shot.velocity/60-retail::origin_above_feet);live.player.invulnerable_ticks=0;
    const auto before=live.player.health;step(live,models);check(live.player.health<before,"Boss projectile cannot damage Spyro");
    live.ripto.reset(live,&models);put(live,{0,.03f,-7});live.player.invulnerable_ticks=100000;
    for(unsigned n=0;n<800 && live.ripto.shots().empty();++n)step(live,models);
    shot=live.ripto.shots().front();put(live,shot.visual.position+shot.velocity/60-retail::origin_above_feet);live.player.invulnerable_ticks=0;live.player.health=0;
    step(live,models);check(!live.ripto.engaged && live.ripto.shots().empty() && live.ripto.health==60,"Lethal boss hit did not safely reset the encounter");
    // Relays weaken Ripto; they must no longer bypass the actual boss.
    live.level.custom_arena->relay_breaks=3;tick_custom_level(live,{});
    check(!live.level.custom_arena->complete,"Relays bypassed the boss fight");
    // Retail must activate without the Backrealms flag or a custom arena.
    decode_travel(boss_archive,exe,retail);
    Scene retail_scene;retail_scene.wad_slot=71;retail_scene.spawn=retail.spawn;
    const auto retail_spyro=decode_spyro(wad.read(0),boss_archive,exe,wad.read(29),29,wad.read(15),wad.read(45));
    auto retail_models=donor;const auto forest_models=decode_object_models(wad.read(15),exe);
    const unsigned fire_types[]{0x1e3,0x1f8,0x1e1,0x211};import_object_models(retail_models,forest_models,fire_types);
    Game original(retail,retail_scene,{},&retail_models,&retail_spyro);
    check(original.ripto.enabled && original.ripto.retail_arena && !original.level.custom_arena,"Retail arena still has an idle boss");
    check(length(original.ripto.center-retail.ripto_arena->center)<1 && original.ripto.center.x>40,"Retail arena used Crownworks origin");
    original.player.invulnerable_ticks=100000;step(original,retail_models,2);
    const auto foot_start=original.retail_objects[original.ripto.actor].position;step(original,retail_models,60);
    check(length(foot_start-original.retail_objects[original.ripto.actor].position)>.1f,"Retail Ripto never pursues Spyro");
    hit(original,retail_models,flame_attack);check(original.ripto.health==60,"Normal flame bypassed the retail power-orb mechanic");
    step(original,retail_models,130);hit(original,retail_models,charge_attack);check(original.ripto.health==60,"Normal charge bypassed the retail power-orb mechanic");
    check(original.retail_objects[original.ripto.actor].animation.next_clip==15,"Normal hit did not play retail flinch");
    // Collect real Hunter drops by touching them; exercise all three colours.
    std::set<unsigned> powers;
    for(unsigned n=0;n<8000 && powers.size()<3;++n) {
        original.player.invulnerable_ticks=100000;
        std::optional<unsigned> pickup;const auto count=original.ripto.spyro_orbs;
        for(const auto& orb:original.ripto.orbs())if(orb.resting && !original.ripto.player_power_ticks && (original.ripto.spyro_orbs<2 || orb.color==powers.size())){put(original,orb.visual.position-Vec3{0,.15f,0});pickup=orb.color;break;}
        step(original,retail_models);
        if(pickup && original.ripto.spyro_orbs==count+1)check(original.ripto.player_power==*pickup,"HUD did not update to the latest collected orb colour");
        if(original.ripto.player_power_ticks){powers.insert(original.ripto.player_power);
            check(original.ripto.player_power_ticks<=original.ripto.power_duration(),"Power timer exceeds original VBlank duration");
            if(original.ripto.player_power==2)check(original.powers.charge_enabled(original),"Blue orbs do not grant Supercharge in retail arena");}
    }
    check(powers.size()==3,"Hunter drops failed to grant every retail power");
    check(!original.ripto.attack_counts[6] && !original.ripto.attack_counts[7],"Custom cyborg attacks leaked into retail rules");
    for(unsigned phase=0;phase<3;++phase) {
        check(unsigned(original.ripto.stage)==phase,"Unexpected retail phase");
        const unsigned hp=phase?80:60;check(original.ripto.max_health==hp,"Wrong retail phase HP");
        const unsigned before_hp=original.ripto.health;hit(original,retail_models,superflame_attack);
        check(original.ripto.health==before_hp-10,"Powered retail hit does not damage boss");
        for(unsigned n=0;n<12 && original.ripto.action!=RiptoAction::Transform && original.ripto.stage!=RiptoStage::Defeated;++n)hit(original,retail_models,superflame_attack);
        step(original,retail_models,151);
        if(phase==0){check(!original.retail_objects[original.ripto.gulp].hidden,"Retail Mecha-Gulp never appeared");step(original,retail_models,350);check(original.ripto.attack_counts[4]>0,"Retail Mecha-Gulp never attacks");}
        if(phase==1){check(original.powers.flying,"Retail final phase did not launch Spyro into Superfly");
            const auto bird_start=original.retail_objects[original.ripto.bird].position;step(original,retail_models,400);
            check(length(bird_start-original.retail_objects[original.ripto.bird].position)>5 && original.ripto.attack_counts[5],"Retail bird route/volleys failed");}
    }
    check(original.ripto.defeated() && original.guide_awards.boss && original.ripto_defeated,"Retail victory was not awarded");
    check(original.player.orbs==0,"Boss victory incorrectly awarded an ordinary level orb");
    GameSave save;save.capture(original);
    Game restored(retail,retail_scene,{},&retail_models,&retail_spyro);save.apply(restored,&retail_models);
    check(restored.ripto.defeated() && restored.retail_objects[restored.ripto.actor].hidden,"Loading victory resurrected an idle Ripto");
    step(restored,retail_models,241);
    check(restored.portal_travel.phase==PortalTravel::Phase::Loading && restored.portal_travel.request.destination==57,"Retail victory did not return to Winter Tundra");
    Game attempt(retail,retail_scene,{},&retail_models,&retail_spyro);step(attempt,retail_models,10);hit(attempt,retail_models,superflame_attack);
    GameSave midway;midway.capture(attempt);midway.apply(attempt,&retail_models);
    check(attempt.ripto.health==60 && !attempt.ripto.engaged && attempt.retail_objects[attempt.ripto.actor].boss_body.has_value(),"Loading an unfinished attempt lost the boss collider/reset");
    check(length(attempt.player.position-retail.spawn)<.001f,"Unfinished save restored Spyro into an incompatible boss phase");
    // 6.4 floods: a Water Fury in the Crownworks floods only the middle
    // terrace (and its pools); the lower ring and the crown walk stay dry.
    {
        Game flooded(level,scene,{},&models,&spyro);
        // Elemental water only exists with the upgrade system on (the
        // Crownworks launcher passes --upgrade-tier 2); otherwise it is cleared.
        flooded.upgrades.enabled=true;
        put(flooded,{14.4f,3.52f,22.5f});flooded.advance(FixedClock::step,{});
        flooded.elements.flood(flooded);
        for(unsigned t=0;t<240;++t){flooded.player.invulnerable_ticks=600;flooded.advance(FixedClock::step,{});}
        const auto terrace=flooded.level.water_at({14.4f,3.8f,22.5f});
        check(terrace && terrace->height>4.6f && terrace->height<5.0f,"The Crownworks flood does not stand on the middle terrace");
        check(!flooded.level.water_at({0,.4f,-8}) && !flooded.level.water_at({12,.4f,0}),"The Crownworks flood reached the lower ring");
        check(!flooded.level.water_at({33,7.3f,19.1f}),"The Crownworks flood reached the crown walk");
    }
    std::cout<<"PASS "<<checks<<" Ripto checks\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
