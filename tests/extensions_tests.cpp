#include "spyro/runtime.hpp"
#include "spyro/savegame.hpp"
#include "spyro/multiplayer.hpp"
#include <iostream>
#include <fstream>
#include <limits>
using namespace spyro;
namespace {
unsigned checks=0;
void check(bool yes,const char* message){++checks;if(!yes)throw std::runtime_error(message);}
Level floor_level() {
    Level l;l.wad_slot=17;l.spawn={0,8,0};l.bounds={{-200,0,-200},{200,100,200}};
    l.triangles={{{-200,8,-200},{200,8,200},{200,8,-200}},{{-200,8,-200},{-200,8,200},{200,8,200}}};return l;
}
Scene scene(const Level& l){Scene s;s.wad_slot=l.wad_slot;s.spawn=l.spawn;return s;}
void basic() {
    const auto level=floor_level();Game base(level,scene(level)),off(level,scene(level)),fast(level,scene(level));
    base.cheats.supercharge=off.cheats.supercharge=fast.cheats.supercharge=true;
    off.upgrades.charge=2;fast.upgrades.enabled=true;fast.upgrades.charge=2;
    for(unsigned i=0;i<10;++i){base.advance(FixedClock::step,{});off.advance(FixedClock::step,{});fast.advance(FixedClock::step,{});}
    InputState charge;charge.charge=true;
    for(unsigned i=0;i<180;++i){base.advance(FixedClock::step,charge);off.advance(FixedClock::step,charge);fast.advance(FixedClock::step,charge);}
    check(length(base.player.position-off.player.position)<1e-6f,"Disabled upgrades changed retail movement");
    check(std::abs(length(fast.player.velocity)/length(base.player.velocity)-1.6f)<0.001f,"Red supercharge has the wrong speed");
    check(fast.powers.upgrade_flames>200 && !base.powers.upgrade_flames,"Supercharge tier flames missing or enabled by default");
    check(std::any_of(fast.powers.particles().begin(),fast.powers.particles().end(),[](const auto& p){return p.kind==PowerParticleKind::ChargeFlame && p.color.x>p.color.y*10;}),
        "Red tier must retain its own particle colour instead of the retail fire cooling curve");
    auto air=level;air.spawn.y=30;Game fly(air,scene(air)),boost(air,scene(air));
    fly.cheats.superfly=boost.cheats.superfly=true;fly.powers.flying=boost.powers.flying=true;boost.upgrades.enabled=true;boost.upgrades.flight=2;
    for(unsigned i=0;i<120;++i){fly.advance(FixedClock::step,{});boost.advance(FixedClock::step,{});}
    check(std::abs(length(boost.player.velocity)/length(fly.player.velocity)-1.5f)<0.001f,"Upgraded flight has the wrong speed");
    UpgradeProgress u;u.points=100;check(!u.buy(Upgrade::Supercharge),"Disabled upgrades could be purchased");u.enabled=true;
    check(u.buy(Upgrade::Supercharge) && u.points==75 && u.charge==1,"First upgrade purchase incorrect");
    check(u.buy(Upgrade::Supercharge) && u.points==25 && u.charge==2 && !u.buy(Upgrade::Supercharge),"Second upgrade or cap incorrect");
    check(!u.buy(Upgrade::Carry),"Insufficient points were spent");
    Game earn(level,scene(level));earn.upgrades.enabled=true;earn.play_scene.entities.push_back({1,EntityKind::Gem,{0,8.4f,0},5});
    for(unsigned i=0;i<20;++i)earn.advance(FixedClock::step,{});
    check(earn.upgrades.points==5 && earn.player.gems==5,"Upgrade points replace or duplicate gems");
    GameSave save;save.capture(fast);const auto file=std::filesystem::temp_directory_path()/"spyro-pc-extensions-test.s2save";
    save.write(file);const auto loaded=GameSave::read(file);std::filesystem::remove(file);
    Game restored(level,scene(level));loaded.apply(restored,nullptr);
    check(restored.upgrades.enabled && restored.upgrades.charge==2,"Upgrade save did not restore tiers");
    auto invalid=save;invalid.upgrades.flight=3;bool rejected=false;try{invalid.validate();}catch(const FormatError&){rejected=true;}
    check(rejected,"Out-of-range saved upgrade accepted");
    for(const auto appearance:{SuperchargeAppearance::Retail,SuperchargeAppearance::MainModel,SuperchargeAppearance::MainModelChargeWings}) {
        save.cheats.supercharge_appearance=appearance;save.write(file);
        const auto roundtrip=GameSave::read(file);roundtrip.apply(restored,nullptr);
        check(restored.cheats.supercharge_appearance==appearance,"Save changed the selected supercharge wing variant");
    }
    std::filesystem::remove(file);
    invalid=save;invalid.cheats.supercharge_appearance=static_cast<SuperchargeAppearance>(3);rejected=false;
    try{invalid.validate();}catch(const FormatError&){rejected=true;}
    check(rejected,"Unknown saved supercharge appearance accepted");
    OnlineState net;net.world=online_world(level);net.sequence=99;net.position={1,2,3};net.frame=3.25f;
    for(unsigned appearance=0;appearance<3;++appearance) {
        net.appearance=appearance;std::istringstream packet(encode_online_state(net));
        check(decode_online_state(packet).appearance==appearance,"Online snapshot changed the supercharge wing variant");
    }
    net.appearance=3;check(!net.valid(),"Unknown online supercharge appearance accepted");net.appearance=0;
    std::istringstream bytes(encode_online_state(net));const auto decoded=decode_online_state(bytes);
    check(decoded.sequence==99 && decoded.frame==3.25f && decoded.world==net.world,"Online snapshot round trip failed");
    net.position.x=std::numeric_limits<float>::infinity();check(!net.valid(),"Non-finite network position accepted");
    OnlineGhosts ghosts;OnlineStatus roster;roster.connected=true;roster.self=0;auto remote=decoded;remote.peer=1;roster.players.push_back(remote);
    check(ghosts.update(roster,decoded.world,0.05).size()==1,"Remote player missing");
    roster.players[0].position.x=11;++roster.players[0].sequence;
    const auto smooth=ghosts.update(roster,decoded.world,0.05);
    check(smooth[0].position.x>1 && smooth[0].position.x<11,"Remote player snaps instead of interpolating");
    roster.connected=false;check(ghosts.update(roster,decoded.world,0.05).empty(),"Disconnected player remains visible");
    Game whiff(level,scene(level));whiff.upgrades.enabled=whiff.upgrades.melee=true;
    InputState swipe;swipe.melee_pressed=true;swipe.charge=true;
    InputState held_charge;held_charge.charge=true;
    whiff.advance(FixedClock::step/4,swipe);for(unsigned i=0;i<3;++i)whiff.advance(FixedClock::step/4,held_charge);
    check(whiff.abilities.attacking() && whiff.abilities.melee_swings==1 && whiff.abilities.melee_hits==0,"J was lost between render ticks or blocked by held charge / no target");
    for(unsigned i=0;i<30;++i)whiff.advance(FixedClock::step,{});
    check(!whiff.abilities.attacking() && whiff.abilities.melee_swings==1,"Empty-space melee did not finish exactly once");
    Game airborne(air,scene(air));airborne.upgrades.enabled=airborne.upgrades.melee=true;
    airborne.advance(FixedClock::step,swipe);
    check(airborne.abilities.attacking(),"J was silently rejected while airborne");
    airborne.upgrades.enabled=false;airborne.advance(FixedClock::step,{});
    check(!airborne.abilities.attacking(),"Optional melee stayed active after upgrades were disabled");
}
void retail_checks(const std::filesystem::path& path) {
    AssetStore store(path);WadArchive wad(store);const auto exe=store.read_all("SCUS_944.25"),archive=wad.read(17);
    const auto source=decode_level(archive,17);auto models=decode_object_models(archive,exe);
    const auto ripto=decode_object_models(wad.read(71),exe),forest=decode_object_models(wad.read(15),exe);
    const unsigned extras[]{0x146,0x1e2,0x21d},fire[]{0x1e3,0x1f8,0x1e1,0x211};
    import_object_models(models,ripto,extras);import_object_models(models,forest,fire);
    const auto spyro=decode_spyro(wad.read(0),archive,exe,wad.read(29),29);
    auto arena=floor_level();
    auto enemy=*std::find_if(source.objects.begin(),source.objects.end(),[](const auto& o){return o.enemy.has_value();});
    enemy.id=0;enemy.position=enemy.previous_position={1.8f,8,7};enemy.enemy->route.clear();arena.objects={enemy};
    Game homing(arena,scene(arena),{},&models,&spyro);homing.cheats.superflame=true;homing.upgrades={true,0,0,2,0,false,false};
    for(unsigned i=0;i<10;++i)homing.advance(FixedClock::step,{});
    InputState fire_input;fire_input.flame_pressed=true;homing.advance(FixedClock::step,fire_input);
    for(unsigned i=0;i<160;++i)homing.advance(FixedClock::step,{});
    check(homing.powers.homing_ticks>5,"Superflame did not steer toward the offset enemy");
    check(homing.retail_objects[0].enemy->defeated,"Homing superflame missed its enemy");
    check(homing.powers.split_flames==3,"Impact did not split once into exactly three child flames");
    for(unsigned i=0;i<240;++i)homing.advance(FixedClock::step,{});
    check(homing.powers.projectiles().empty() && homing.powers.split_flames==3,"Split flames recurse or leak projectiles");
    auto wall=floor_level();wall.triangles.push_back({{-5,8,3},{5,13,3},{5,8,3}});wall.triangles.push_back({{-5,8,3},{-5,13,3},{5,13,3}});
    wall.objects={enemy};Game blocked(wall,scene(wall),{},&models,&spyro);blocked.cheats.superflame=true;blocked.upgrades=homing.upgrades;
    for(unsigned i=0;i<10;++i)blocked.advance(FixedClock::step,{});blocked.advance(FixedClock::step,fire_input);
    for(unsigned i=0;i<90;++i)blocked.advance(FixedClock::step,{});
    check(!blocked.retail_objects[0].enemy->defeated,"Upgraded shot damaged an enemy through a wall");
    check(blocked.powers.split_flames==3,"A terrain impact did not split the flame");
    arena.objects[0].position=arena.objects[0].previous_position={0,8,1};Game melee(arena,scene(arena),{},&models,&spyro);
    melee.upgrades.enabled=melee.upgrades.melee=true;
    for(unsigned i=0;i<5;++i)melee.advance(FixedClock::step,{});
    InputState attack;attack.melee_pressed=true;melee.advance(FixedClock::step,attack);
    for(unsigned i=0;i<16;++i)melee.advance(FixedClock::step,{});
    check(melee.abilities.melee_hits>0 && melee.retail_objects[0].enemy->defeated,"Melee does not damage a nearby enemy");
    melee.advance(FixedClock::step,attack);for(unsigned i=0;i<15;++i)melee.advance(FixedClock::step,{});
    check(melee.abilities.combo==1,"Second strike was not buffered");
    for(unsigned i=0;i<4;++i)melee.advance(FixedClock::step,{});
    melee.advance(FixedClock::step,attack);for(unsigned i=0;i<22;++i)melee.advance(FixedClock::step,{});
    check(melee.abilities.combo==2,"Combo did not reach the spin finisher");
    auto basket=*std::find_if(source.objects.begin(),source.objects.end(),[](const auto& o){return o.type==0xc8;});
    basket.id=0;basket.position=basket.previous_position={0,8,0.45f};basket.container->rest_position=basket.position;
    auto carrying=floor_level();carrying.spawn.y=9.2f;carrying.objects={basket};Game lift(carrying,scene(carrying),{},&models,&spyro);
    lift.upgrades.enabled=lift.upgrades.carry=true;lift.cheats.superfly=true;lift.powers.flying=true;
    InputState grab;grab.carry_pressed=true;lift.advance(FixedClock::step,grab);
    check(lift.abilities.carried==0 && lift.retail_objects[0].pc_carried,"Flying Spyro cannot pick up a basket");
    for(unsigned i=0;i<20;++i)lift.advance(FixedClock::step,{});
    check(lift.retail_objects[0].position.z>basket.position.z+0.5f,"Carried basket does not follow Spyro");
    {
        const auto checkpoint_path=std::filesystem::temp_directory_path()/"spyro-carry-checkpoint-test.s2save";
        struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove(path,error);}} cleanup{checkpoint_path};
        GameSave checkpoint;checkpoint.capture(lift);checkpoint.write(checkpoint_path);
        const auto decoded_checkpoint=GameSave::read(checkpoint_path);
        check(decoded_checkpoint.levels.at(carrying.wad_slot).bodies.size()==1,"Carried basket is missing from checkpoint");
        Game loaded(carrying,scene(carrying),{},&models,&spyro);decoded_checkpoint.apply(loaded,&models);
        check(loaded.abilities.carried==0 && loaded.powers.flying && loaded.retail_objects[0].pc_carried,"Checkpoint did not restore flying Spyro with his held basket");
        check(loaded.animation.state().clip==motion_animation(loaded.player.motion),"Loaded flight uses the falling pose until the next tick");
        check(length(loaded.retail_objects[0].position-lift.retail_objects[0].position)<0.00001f,"Loading moved the carried basket");
        const auto held_position=lift.retail_objects[0].position;decoded_checkpoint.apply(lift,&models);
        check(length(lift.retail_objects[0].position-held_position)<0.00001f,"Loading over an existing carry reset the new checkpoint body to its home");
        for(unsigned t=0;t<12;++t) {
            InputState steer;steer.move_x=0.4f;lift.advance(FixedClock::step,steer);loaded.advance(FixedClock::step,steer);
            check(length(lift.player.position-loaded.player.position)<0.00001f && length(lift.retail_objects[0].position-loaded.retail_objects[0].position)<0.00001f,"Flight/body diverged after checkpoint restoration");
        }
        auto corrupt=decoded_checkpoint;corrupt.levels.at(carrying.wad_slot).bodies[0].object=999;
        bool rejected=false;const auto before=loaded.retail_objects[0].position;
        try{corrupt.apply(loaded,&models);}catch(const FormatError&){rejected=true;}
        check(rejected && loaded.abilities.carried==0 && length(before-loaded.retail_objects[0].position)<0.00001f,"Invalid body reference mutated the active game");
        InputState release;release.throw_pressed=true;loaded.advance(FixedClock::step,release);
        for(unsigned t=0;t<4;++t)loaded.advance(FixedClock::step,{});
        GameSave airborne;airborne.capture(loaded);airborne.write(checkpoint_path);
        const auto airborne_records=airborne.levels.at(carrying.wad_slot).bodies;
        check(airborne_records.size()==1 && !airborne_records[0].held && airborne_records[0].age>0,"Thrown basket lost its flight state in save");
        Game resumed(carrying,scene(carrying),{},&models,&spyro);GameSave::read(checkpoint_path).apply(resumed,&models);
        for(unsigned t=0;t<180;++t){loaded.advance(FixedClock::step,{});resumed.advance(FixedClock::step,{});}
        check(loaded.abilities.smashes==1 && resumed.abilities.smashes==1 && resumed.interactions.released_gems()==1,"Restored thrown basket did not smash/release exactly once");
        check(resumed.retail_objects[0].state<0 && resumed.abilities.checkpoint(resumed).empty(),"Smashed body remained in subsequent checkpoints");
    }
    InputState throw_input;throw_input.throw_pressed=true;lift.advance(FixedClock::step,throw_input);
    check(lift.abilities.carried==UINT32_MAX && lift.abilities.throws==1,"Basket cannot be thrown");
    for(unsigned i=0;i<150;++i)lift.advance(FixedClock::step,{});
    check(lift.retail_objects[0].state<0 && lift.interactions.released_gems()==1,"Thrown basket did not break and release its gem once");
    Game safe(carrying,scene(carrying),{},&models,&spyro);safe.upgrades.enabled=safe.upgrades.carry=true;safe.cheats.superfly=true;safe.powers.flying=true;
    safe.advance(FixedClock::step,grab);safe.upgrades.enabled=false;safe.advance(FixedClock::step,{});
    check(!safe.retail_objects[0].pc_carried && length(safe.retail_objects[0].position-basket.position)<0.001f,"Disabling abilities leaves a suspended basket");
    const auto beach=decode_level(wad.read(19),19);const auto beach_models=decode_object_models(wad.read(19),exe);
    auto chest=*std::find_if(beach.objects.begin(),beach.objects.end(),[](const auto& o){return o.type==0xcb;});
    // This authored chest contains separate level pickups (held_gem=255).
    // Also exercise the shared release path with an explicitly filled fixture.
    chest.held_gem=5;
    chest.id=0;chest.position=chest.previous_position={0,8,0.45f};carrying.objects={chest};carrying.spawn.y=9.4f;
    {
        auto approach=carrying;approach.spawn={0,9.4f,-3.8f};
        Game homing(approach,scene(approach),{},&beach_models,&spyro);
        homing.upgrades.enabled=homing.upgrades.carry=true;homing.cheats.superfly=homing.powers.flying=true;
        homing.advance(FixedClock::step,grab);
        check(homing.abilities.pickup_target==0 && homing.abilities.carried==UINT32_MAX,"G did not begin a distant grab approach");
        for(unsigned t=0;t<120 && homing.abilities.carried==UINT32_MAX;++t) {
            const auto before=homing.player.position;homing.advance(FixedClock::step,{});
            check(length(homing.player.position-before)<0.12f,"Assisted grab teleported Spyro");
        }
        check(homing.abilities.carried==0 && homing.abilities.assisted_pickups==1,"Assisted flight failed to reach and grab the heavy chest");
        approach.spawn={0,9.1f,-1.8f};
        Game slowed(approach,scene(approach),{},&beach_models,&spyro),regular(approach,scene(approach),{},&beach_models,&spyro);
        slowed.upgrades.enabled=slowed.upgrades.carry=true;slowed.cheats.superfly=slowed.powers.flying=regular.cheats.superfly=regular.powers.flying=true;
        slowed.advance(FixedClock::step,{});regular.advance(FixedClock::step,{});
        check(length(slowed.player.velocity)<length(regular.player.velocity)*0.95f,"Flying past a heavy chest did not slow down for grabbing");
        approach.spawn={0,9.4f,-3.8f};
        approach.triangles.push_back({{-4,8,-2},{4,8,-2},{4,14,-2}});approach.triangles.push_back({{-4,8,-2},{4,14,-2},{-4,14,-2}});
        Game blocked(approach,scene(approach),{},&beach_models,&spyro);blocked.upgrades.enabled=blocked.upgrades.carry=true;blocked.cheats.superfly=blocked.powers.flying=true;
        blocked.advance(FixedClock::step,grab);
        check(blocked.abilities.pickup_target==UINT32_MAX && blocked.abilities.carried==UINT32_MAX,"G locked onto a chest through solid terrain");
    }
    Game heavy(carrying,scene(carrying),{},&beach_models,&spyro);heavy.upgrades.enabled=heavy.upgrades.carry=true;heavy.cheats.superfly=true;heavy.powers.flying=true;
    heavy.advance(FixedClock::step,grab);check(heavy.abilities.carried==0 && heavy.abilities.carry_speed()<0.7f,"Heavy chest cannot be carried or has no extra weight");
    for(unsigned i=0;i<15;++i)heavy.advance(FixedClock::step,{});
    const std::array<std::vector<unsigned>,4> feet{{{33,34,35,36,37,38,39,40,41,42},{78,79,80,81,82,83,84,85,86,87},{0,1,2,3},{15,16,17,18}}};
    constexpr std::array<std::array<unsigned,2>,4> roots{{{30,49},{75,74},{112,114},{25,26}}},joints{{{31,44},{76,89},{8,9},{23,24}}},ankles{{{33,34},{78,79},{2,3},{15,16}}};
    const auto inspect_grip=[&](float frame,float alpha) {
        auto pose=spyro.frame(33,frame);const auto original=pose;heavy.abilities.pose(pose,alpha,&heavy);
        const auto contacts=heavy.abilities.grip_contacts(heavy,alpha);const auto& p=heavy.player;
        const auto origin=lerp(p.previous_position,p.position,alpha)+retail::origin_above_feet;
        const float yaw=p.previous_yaw+std::remainder(p.yaw-p.previous_yaw,2*std::numbers::pi_v<float>)*alpha;
        const auto world=[&](Vec3 v){return origin+retail::rotate_model(v,yaw,std::lerp(p.previous_pitch,p.pitch,alpha),std::lerp(p.previous_roll,p.roll,alpha));};
        const auto middle=[](const SpyroPose& v,const auto& pair){return (v.vertices[pair[0]]+v.vertices[pair[1]])*0.5f;};
        for(unsigned i=0;i<4;++i) {
            float minimum=1e30f;
            for(auto v:feet[i]) {
                const float gap=dot(world(pose.vertices[v])-contacts[i].point,contacts[i].normal);minimum=std::min(minimum,gap);
                check(gap>=carry_grip_clearance-0.0001f,"A carrying heel / claw penetrates the object's supporting plane");
                check(std::abs(length(pose.vertices[v]-pose.vertices[feet[i][0]])-length(original.vertices[v]-original.vertices[feet[i][0]]))<0.00001f,"A carrying paw was distorted");
            }
            if(minimum>=0.055f)throw std::runtime_error("Carrying paw float "+std::to_string(minimum)+" at limb "+std::to_string(i)+" frame "+std::to_string(frame)+" alpha "+std::to_string(alpha));
            check(true,"A carrying paw floats too far from the lid");
            const auto& held=heavy.retail_objects[0];const auto basis=object_basis(held,alpha);const auto location=object_position(held,alpha);
            const auto& bank=beach_models.models.at(held.type);const float scale=bank.animated?object_model_scale(held):1;float surface_gap=1e30f;
            const auto measure_surface=[&](Vec3 paw_point) {
                const auto delta=world(paw_point)-location;const Vec3 local{dot(delta,basis.x),dot(delta,basis.y),dot(delta,basis.z)};
                for(const auto& mesh:bank.meshes)for(const auto& face:mesh.faces) {
                    constexpr unsigned corner[]{0,1,2,1,3,2};
                    for(unsigned t=0;t<(face.quad?2u:1u);++t) {
                        const Triangle tri{mesh.vertices[face.vertices[corner[t*3]]]*scale,mesh.vertices[face.vertices[corner[t*3+1]]]*scale,mesh.vertices[face.vertices[corner[t*3+2]]]*scale};
                        surface_gap=std::min(surface_gap,length(local-closest_point(local,tri)));
                    }
                }
            };
            for(auto v:feet[i])measure_surface(pose.vertices[v]);
            // A narrow lid rib can support the middle of a sole while every
            // corner lies beyond the rib. Check points on the actual paw
            // faces too; a corner-only distance would falsely call it floating.
            for(const auto& face:spyro.animations.at(33).faces) {
                bool paw_face=true;for(unsigned k=0;k<(face.quad?4u:3u);++k)
                    paw_face&=std::find(feet[i].begin(),feet[i].end(),face.vertices[k])!=feet[i].end();
                if(!paw_face)continue;
                constexpr unsigned corner[]{0,1,2,1,3,2};
                for(unsigned t=0;t<(face.quad?2u:1u);++t) {
                    const auto a=pose.vertices[face.vertices[corner[t*3]]],b=pose.vertices[face.vertices[corner[t*3+1]]],c=pose.vertices[face.vertices[corner[t*3+2]]];
                    for(unsigned u=0;u<=4;++u)for(unsigned v=0;v<=4-u;++v)measure_surface((a*float(4-u-v)+b*float(u)+c*float(v))/4);
                }
            }
            if(surface_gap>0.055f)throw std::runtime_error("Paw has no nearby lid surface: "+std::to_string(surface_gap)+" at limb "+std::to_string(i)+" frame "+std::to_string(frame));
            check(true,"A carrying paw has no nearby visible lid surface");
            const auto upper=length(middle(pose,joints[i])-middle(pose,roots[i])),lower=length(middle(pose,ankles[i])-middle(pose,joints[i]));
            check(std::abs(upper-length(middle(original,joints[i])-middle(original,roots[i])))<0.0001f,"A carrying upper leg was stretched");
            if(std::abs(lower-length(middle(original,ankles[i])-middle(original,joints[i])))>=0.0001f)
                throw std::runtime_error("Carrying lower leg stretched at limb "+std::to_string(i)+" / frame "+std::to_string(frame)+" / alpha "+std::to_string(alpha));
            check(true,"A carrying lower leg was stretched");
        }
        check(length(middle(pose,ankles[0])-middle(pose,ankles[1]))>0.13f && length(middle(pose,ankles[2])-middle(pose,ankles[3]))>0.13f,"Carrying limbs collapsed onto the same point");
        for(unsigned i=58;i<74;++i)check(length(pose.vertices[i]-original.vertices[i])<1e-7f,"Carrying changed Spyro's wing length / flap");
        for(unsigned i=115;i<230;++i)check(length(pose.vertices[i]-original.vertices[i])<1e-7f,"Carrying distorted Spyro's head");
    };
    for(unsigned frame=0;frame<8;++frame)inspect_grip(float(frame),1);
    const auto straight_flight=heavy.player;const auto straight_object=heavy.retail_objects[0];
    heavy.player.position.y+=12;heavy.player.previous_position=heavy.player.position;
    heavy.retail_objects[0].position.y+=12;heavy.retail_objects[0].previous_position=heavy.retail_objects[0].position;
    bool wrapped_heading=false;
    for(unsigned tick=0;tick<300;++tick) {
        InputState turn;turn.move_x=0.8f;turn.move_z=std::clamp((heavy.player.pitch-0.3f*std::sin(tick/40.0f))*4,-1.0f,1.0f);
        heavy.advance(FixedClock::step,turn);check(heavy.abilities.carried==0,"Banking lost the held object");
        wrapped_heading|=std::abs(heavy.player.yaw-heavy.player.previous_yaw)>3;
        if(tick%17==0)for(float alpha:{0.0f,0.37f,1.0f})inspect_grip(float(tick%8),alpha);
    }
    check(wrapped_heading,"Carrying test did not cross a heading wrap");
    heavy.player=straight_flight;heavy.retail_objects[0]=straight_object;
    heavy.advance(FixedClock::step,grab);check(heavy.abilities.drops==1 && heavy.abilities.throws==0,"Drop incorrectly used throw velocity");
    for(unsigned i=0;i<120 && heavy.retail_objects[0].state>=0;++i)heavy.advance(FixedClock::step,{});
    check(!heavy.retail_objects[0].pc_carried && heavy.retail_objects[0].state<0 && heavy.abilities.smashes==1,"Dropped heavy chest did not smash on impact");
    check(std::count_if(heavy.retail_objects.begin(),heavy.retail_objects.end(),[](const auto& o){return o.type==0x134 && o.source_id==0 && o.state>=0;})==16,"Heavy chest did not release its sixteen retail metal shards");
    check(heavy.interactions.released_gems()==1,"Smashed chest lost or duplicated its gem");
    GameSave smashed;smashed.capture(heavy);Game reloaded(carrying,scene(carrying),{},&beach_models,&spyro);smashed.apply(reloaded,&beach_models);
    check(reloaded.retail_objects[0].collected || reloaded.retail_objects[0].state<0,"Save/load resurrected the smashed chest");
    check(std::count_if(reloaded.retail_objects.begin(),reloaded.retail_objects.end(),[](const auto& o){return o.runtime_spawned && o.type==1 && o.source_id==0 && o.gem_value==5;})==1,"Save/load lost or duplicated a heavy chest's gem");
    for(unsigned i=0;i<240;++i)heavy.advance(FixedClock::step,{});
    check(heavy.interactions.released_gems()==1 && std::none_of(heavy.retail_objects.begin(),heavy.retail_objects.end(),[](const auto& o){return o.type==0x134 && o.state>=0;}),"Chest shards leak or duplicate gems");
    carrying.objects[0].held_gem=255;
    Game empty(carrying,scene(carrying),{},&beach_models,&spyro);empty.upgrades.enabled=empty.upgrades.carry=true;empty.cheats.superfly=true;empty.powers.flying=true;
    empty.advance(FixedClock::step,grab);for(unsigned i=0;i<15;++i)empty.advance(FixedClock::step,{});
    empty.advance(FixedClock::step,throw_input);
    for(unsigned i=0;i<120 && empty.retail_objects[0].state>=0;++i)empty.advance(FixedClock::step,{});
    check(empty.abilities.throws==1 && empty.abilities.drops==0 && empty.abilities.smashes==1,"Heavy chest throw did not smash on impact");
    check(empty.interactions.released_gems()==0,"A chest with separate authored pickups invented another gem");
    GameSave empty_save;empty_save.capture(empty);Game empty_loaded(carrying,scene(carrying),{},&beach_models,&spyro);empty_save.apply(empty_loaded,&beach_models);
    check(empty_loaded.retail_objects[0].state<0 && std::none_of(empty_loaded.retail_objects.begin(),empty_loaded.retail_objects.end(),[](const auto& o){return o.runtime_spawned && o.type==1;}),"Empty heavy chest save resurrected it or invented a gem");
}
}
int main(int argc,char** argv){try{basic();if(argc>1)retail_checks(argv[1]);std::cout<<checks<<" PC extension checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"Extension check "<<checks<<": "<<e.what()<<'\n';return 1;}}
