#include "spyro/runtime.hpp"
#include "spyro/savegame.hpp"
#include <iostream>

using namespace spyro;
namespace {
unsigned checks=0;
void check(bool condition,const char* why){++checks;if(!condition)throw std::runtime_error(why);}
void ticks(Game& g,unsigned n,InputState input={}){for(unsigned i=0;i<n;++i)g.advance(FixedClock::step,input);}
Level flat() {
    Level level;level.wad_slot=15;level.spawn={0,5,0};
    level.triangles={{{-100,0,-100},{100,0,100},{100,0,-100}},{{-100,0,-100},{-100,0,100},{100,0,100}}};
    for(const auto& t:level.triangles){level.bounds.add(t.a);level.bounds.add(t.b);level.bounds.add(t.c);}return level;
}
void controller_checks() {
    auto level=flat();Scene scene;scene.spawn=level.spawn;
    Game g(level,scene);g.cheats.superfly=true;InputState input;input.jump_pressed=true;
    ticks(g,1,input);ticks(g,90);
    check(g.powers.flying && g.player.motion==Motion::Superfly && g.player.position.z>4,"Superfly did not maintain flight without forward input");
    const auto high=g.player.position.y;input={};input.move_z=-1;ticks(g,48,input);
    check(g.player.position.y>high && g.player.motion==Motion::SuperflyClimb,"Superfly climb animation or thrust failed");
    input.move_z=1;ticks(g,100,input);
    check(g.player.motion==Motion::SuperflyDive,"Superfly dive animation failed");
    check(std::abs(g.player.pitch)<=800*retail::angle_to_radians+0.001f,"Flight pitch exceeded retail clamp");
    g.cheats.superfly=false;ticks(g,1);check(!g.powers.flying,"Disabling flight cheat left flight latched");
    g.powers.flight={3,3};g.powers.flying=true;g.player.position.y=20;
    g.set_paused(true);ticks(g,10);check(g.powers.flight.remaining==3,"Pausing consumed flight timer");
    g.set_paused(false);ticks(g,3);check(!g.powers.flying && g.powers.flight.remaining==0,"Timed flight did not expire");
    g.cheats.green_firebomb=true;g.cheats.superflame=true;
    const auto color=g.powers.tint(g);check(color.x==1 && color.y==1 && color.z==1,"Green-bomb cheat changed Spyro's normal colours");
    g.player.swim={};input={};input.flame_pressed=true;
    check(g.powers.fire_tick(g,input,true,false),"Green bomb did not start its firing animation");
    for(unsigned i=0;i<3;++i)g.powers.fire_tick(g,{},true,false);
    check(!g.powers.shots_fired && g.powers.secondary_action()==6,"Powered shot bypassed four-VBlank windup");
    g.powers.fire_tick(g,{},true,false);
    check(g.powers.shots_fired==1 && g.retail_objects.back().type==0x2c2,"Green bomb did not select the original projectile class");
    for(unsigned i=0;i<20;++i)g.powers.fire_tick(g,{},true,false);
    check(!g.powers.fire_tick(g,input,true,false),"Green bomb ignored its longer cooldown");
    for(unsigned i=0;i<10;++i)g.powers.fire_tick(g,{},true,false);
    check(g.powers.fire_tick(g,input,true,false),"Green bomb did not recover after cooldown");
    RetailObject barrier;barrier.power_target=PowerTarget{3,true};
    check(attack_strength(green_bomb_attack)>attack_strength(superflame_attack),"Green bomb is not stronger than superflame");
    check(!power_target_accepts(barrier,superflame_attack) && power_target_accepts(barrier,green_bomb_attack),"Heavy barrier attack hierarchy failed");
    Game a(level,scene),b(level,scene);a.cheats.superfly=b.cheats.superfly=true;input={};input.jump_pressed=true;
    a.advance(1.0/60,input);b.advance(1.0/120,input);b.advance(1.0/120,{});
    for(unsigned i=0;i<60;++i)a.advance(1.0/60,{});for(unsigned i=0;i<120;++i)b.advance(1.0/120,{});
    check(length(a.player.position-b.player.position)<0.0001f,"Flight speed changed with presentation frame rate");
}
void gate_checks() {
    for(const unsigned kind:{2,3,10}) {
        auto level=flat();RetailObject gate;gate.position={0,5,0};gate.rotation[2]=64;
        gate.powerup_gate=PowerupGate{kind,120,1};level.objects.push_back(gate);
        RetailObject spirit;spirit.id=1;spirit.power_target.emplace();spirit.record[83]=1;level.objects.push_back(spirit);
        Scene scene;scene.spawn=level.spawn;Game g(level,scene);
        g.player.previous_position={0,5,-0.1f};g.player.position={0,5,0.1f};g.player.gems=10000;g.player.orbs=64;
        g.powers.gates_tick(g);check(g.powers.gates_activated==0,"Locked gate granted a power");
        g.retail_objects[1].collected=true;g.powers.gates_tick(g);check(g.powers.gates_activated==1,"Crossing an unlocked gate failed");
        check(kind==2?g.powers.flight.remaining==120:kind==3?g.powers.fire.remaining==120:g.powers.permanent_fire,"Gate granted the wrong power");
        g.player.previous_position=g.player.position;g.powers.gates_tick(g);check(g.powers.gates_activated==1,"Standing at a gate continually refilled it");
    }
}
void explosion_checks(const ObjectModels& models) {
    for(const auto kind:{PowerShot::GreenBomb,PowerShot::Superflame,PowerShot::Underwater}) {
        const bool bomb=kind==PowerShot::GreenBomb,water=kind==PowerShot::Underwater;
        auto level=flat();level.spawn={0,0,0};
        level.triangles.push_back({{-10,0,3},{10,10,3},{10,0,3}});
        level.triangles.push_back({{-10,0,3},{-10,10,3},{10,10,3}});
        Scene scene;scene.spawn=level.spawn;Game g(level,scene,{},&models);
        g.cheats.green_firebomb=bomb;g.cheats.superflame=!bomb;
        InputState input;input.flame_pressed=true;
        if(water) {
            // Select the underwater breath at the input boundary, then use
            // the same deterministic wall fixture for its flight and impact.
            g.player.swim.mode=WaterMode::Underwater;g.powers.fire_tick(g,input,true,false);g.player.swim.mode=WaterMode::Dry;
        } else ticks(g,1,input);
        unsigned guard=0;
        while(!g.powers.bombs_exploded && !g.powers.flame_explosions && !g.powers.water_explosions && ++guard<120)ticks(g,1);
        check(guard<120 && g.powers.shots_fired==1,"Launched powered shot failed to hit terrain and detonate");
        check(!g.powers.visuals().empty() && !g.powers.particles().empty() && (water || !g.powers.sparks().empty()),"Explosion omitted retail meshes, particles or spark streaks");
        const auto blast=g.powers.projectiles().front();
        check(g.retail_objects[blast.object].collected,"Detonation left the projectile mesh visible");
        if(bomb)check(std::abs(g.retail_objects[blast.object].position.y-700/retail::units_per_world)<0.001f,"Green blast did not sit at the original ground offset");
        else if(!water) {
            check(std::count_if(g.powers.visuals().begin(),g.powers.visuals().end(),[](const auto& v){return v.kind==PowerVisualKind::FireSpoke;})==5,"Superflame impact omitted its five radial mesh spokes");
            check(!blast.trail.empty(),"Superflame omitted its continuous projectile trail");
        } else {
            check(std::all_of(g.powers.particles().begin(),g.powers.particles().end(),[](const auto& p){return p.kind==PowerParticleKind::Bubble;}),"Underwater superflame emitted fire smoke instead of retail bubbles");
            for(const auto type:{0x1f8,0x1e1,0x211})
                check(std::any_of(g.powers.visuals().begin(),g.powers.visuals().end(),[&](const auto& v){return v.object.type==type;}),"Underwater shot omitted its trail or expanding impact mesh");
        }
        const auto initial_scale=g.powers.visuals().front().scale;
        ticks(g,6);
        check(g.powers.visuals().front().scale>initial_scale,"Explosion mesh did not expand");
        const auto count=g.powers.visuals_spawned;g.set_paused(true);ticks(g,20);
        check(g.powers.visuals_spawned==count,"Paused explosion continued emitting");g.set_paused(false);
        ticks(g,130);
        check(g.powers.projectiles().empty() && g.powers.visuals().empty() && g.powers.particles().empty() && g.powers.sparks().empty(),"Finished explosion leaked live effects");
        if(bomb)check(g.powers.visuals_spawned==60,"Green bomb did not emit two burst mobys per original script update");
    }
}
void retail_checks(const std::filesystem::path& path) {
    AssetStore store(path);WadArchive wad(store);const auto exe=store.read_all("SCUS_944.25");
    auto ripto=decode_object_models(wad.read(71),exe);const auto forest=decode_object_models(wad.read(15),exe);
    const unsigned bomb[]{0x2c2,0x146,0x1e2,0x21d},fire[]{0x1e3,0x1f8,0x1e1,0x211};unsigned flame_gates=0,flight_gates=0;
    import_object_models(ripto,forest,fire);explosion_checks(ripto);
    for(unsigned slot=15;slot<=95;slot+=2) {
        const auto archive=wad.read(slot);auto models=decode_object_models(archive,exe);auto level=decode_level(archive,slot);
        import_object_models(models,ripto,bomb);import_object_models(models,forest,fire);initialize_retail_objects(level,models);
        check(models.models.contains(0x2c2) && models.models.contains(0x1e3) && models.models.contains(0x1f8) && models.models.contains(0x146) && models.models.contains(0x1e2) && models.models.contains(0x21d) && models.models.contains(0x1e1) && models.models.contains(0x211),"A level lacks an original projectile or explosion bank");
        for(const auto& [id,bank]:models.models)for(const auto& mesh:bank.meshes)for(const auto& face:mesh.faces)if(face.textured)
            for(const auto& uv:face.uv)if(uv[0]<0 || uv[1]<0 || uv[0]>1 || uv[1]>1)throw std::runtime_error("Imported material UV outside atlas");
        const auto fx=decode_powerup_assets(archive);check(slot==95 || (fx.wing_trail.width==8 && fx.wing_trail.height==32),"Original wing texture unavailable");
        for(const auto& object:level.objects)if(object.powerup_gate) {
            const auto& gate=*object.powerup_gate;if(gate.kind==2)++flight_gates;else if(gate.kind==3)++flame_gates;
        }
        if(slot==65) {
            check(level.objects[52].type==0x282 && level.objects[59].type==0x282,"Robotica heavy-door records changed");
            for(const unsigned id:{52,59}) {
                const auto& door=level.objects[id];const auto& model=models.models.at(door.type);
                const auto pose=sample_object_animation(model,door.animation,models.animation_blends);
                const auto& f=model.meshes.at(pose.mesh).faces.front();Vec3 center{};
                for(unsigned i=0;i<3;++i)center+=door.position+object_rotation(pose.vertices.at(f.vertices[i])*object_model_scale(door),door);
                center=center/3;Scene scene;scene.wad_slot=65;scene.spawn=level.spawn;Game g(level,scene,{},&models);
                check(container_contact(door,models,center,0.5f).has_value(),"Door lacks physical collision at its original mesh");
                g.interactions.attack_sphere(g.retail_objects,models,center,0.5f,superflame_attack);g.powers.tick(g,&models,nullptr);
                check(!g.retail_objects[id].collected,"Superflame incorrectly broke a heavy Robotica door");
                g.interactions.attack_sphere(g.retail_objects,models,center,0.5f,green_bomb_attack);g.powers.tick(g,&models,nullptr);
                check(g.retail_objects[id].collected && !container_contact(g.retail_objects[id],models,center,0.5f),"Green blast did not remove door mesh and collision");
                GameSave saved;saved.capture(g);Game loaded(level,scene,{},&models);saved.apply(loaded,&models);
                check(loaded.retail_objects[id].collected,"Loading resurrected the destroyed heavy door");
            }
        }
    }
    check(flame_gates==4 && flight_gates==5,"Incomplete native superflame/superfly gate inventory");
}
}
int main(int argc,char** argv) {
    try{controller_checks();gate_checks();if(argc>1)retail_checks(std::filesystem::u8path(argv[1]));std::cout<<checks<<" power-up checks passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
