#include "spyro/universe.hpp"
#include "spyro/savegame.hpp"
#include <iostream>
#include <set>
#include <chrono>
#include <fstream>
#include <sstream>
using namespace spyro;
namespace {
unsigned checks=0;
void check(bool yes,const char* message){++checks;if(!yes)throw std::runtime_error(message);}
void plan_checks() {
    std::set<unsigned> themes;
    for(std::uint64_t seed=0;seed<1000;++seed) {
        const UniverseAddress address{seed,seed*1907,false};const auto a=plan_universe(address),b=plan_universe(address);
        check(a.route.size()>=5 && a.route.size()==b.route.size(),"Missing/different deterministic route");
        check(a.portals.size()==2 && a.portals[0].destination.space,"No guaranteed world exit");
        for(std::size_t i=0;i<a.route.size();++i)check(length(a.route[i]-b.route[i])==0,"Unstable route");
        themes.insert(universe_source_slot(address));
        const auto space=plan_universe({seed,seed*1907,true});
        check(space.portals.size()==4 && space.stars.size()==280,"Missing space destinations/stars");
        check(space.portals[3].destination.space && space.portals[3].destination.world==address.world+3,"Space exploration has no continuation");
    }
    check(themes.size()==4,"Theme selection omits an asset bank");
}
void portal_checks() {
    Level level;level.wad_slot=17;level.universe=plan_universe({42,7,true});level.spawn=level.universe->landing;
    level.triangles.push_back({{-400,-200,-400},{400,-200,400},{400,-200,-400}});
    level.bounds={{-400,-200,-400},{400,350,400}};
    Scene scene;scene.wad_slot=17;scene.spawn=level.spawn;Game g(level,scene);
    for(unsigned i=0;i<120;++i)g.advance(FixedClock::step,{});
    check(g.powers.flying && g.player.position.z>10 && !g.cheats.superfly,"Space did not grant independent automatic flight");
    InputState boost;boost.charge=true;g.advance(FixedClock::step,boost);check(g.powers.flying,"Space boost cancelled flight");
    const auto portal=level.universe->portals[0];g.player.previous_position=portal.center-Vec3{0,0,5}-retail::origin_above_feet;
    g.player.position=portal.center+Vec3{0,0,5}-retail::origin_above_feet;tick_universe(g,false);
    check(g.universe_travel && *g.universe_travel==portal.destination,"Fast flight skipped a portal");
    g.respawn();tick_universe(g,true);check(!g.universe_travel,"Arrival cooldown failed");
    g.player.position.x=200;const auto tick=g.clock.ticks();g.advance(FixedClock::step,{});
    check(g.clock.ticks()==tick+1 && length(g.player.position-level.spawn)<0.01f,"Space recovery reset its simulation clock");
}
void save_checks() {
    UniverseAddress address{0xfedcba9876543210ULL,0xeedcba9876543210ULL,false};
    Level level;level.universe=plan_universe(address);level.wad_slot=universe_source_slot(address);level.spawn=level.universe->landing;
    RetailObject gem;gem.id=0;gem.type=1;gem.position=level.spawn+Vec3{0,0,4};level.objects.push_back(gem);
    Scene scene;scene.wad_slot=level.wad_slot;scene.spawn=level.spawn;Game first(level,scene);
    first.retail_objects[0].collected=true;first.player.gems=1;GameSave save;save.capture(first);
    auto another=level;another.universe=plan_universe({address.seed,address.world+1,false});another.wad_slot=universe_source_slot(another.universe->address);
    scene.wad_slot=another.wad_slot;Game second(another,scene);save.apply(second,nullptr);
    check(second.player.gems==1 && !second.retail_objects[0].collected,"A new world inherited another world's pickups");
    save.capture(second);Game restored(level,first.play_scene);save.apply(restored,nullptr);
    check(restored.player.gems==1 && restored.retail_objects[0].collected,"Revisiting a world reset pickups or duplicated inventory");
    const auto file=std::filesystem::temp_directory_path()/("spyro-universe-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".s2save");
    save.write(file);const auto loaded=GameSave::read(file);std::filesystem::remove(file);
    check(loaded.universe->seed==address.seed && loaded.universe_worlds.size()==2 && loaded.universe_worlds.contains(address.world),"Universe save lost its 64-bit address/progress");
    loaded.apply(restored,nullptr);check(restored.retail_objects[0].collected,"Disk save lost collected gems");
    auto retail=level;retail.universe.reset();Game retail_game(retail,first.play_scene);
    bool rejected=false;try{loaded.apply(retail_game,nullptr);}catch(const FormatError&){rejected=true;}
    check(rejected,"A universe save was applied to the retail campaign");
    auto incompatible=loaded;++incompatible.generator_version;rejected=false;
    try{incompatible.validate();}catch(const FormatError&){rejected=true;}check(rejected,"Unsupported generator version accepted");
    std::ostringstream legacy;legacy<<0xcdc0917cu<<" 15 7 3 3 0 0 1 0 0 1 1 0 0\n1\n15 0 8 0 0 0 8 0\n0\n0\n0\n0\n";
    const auto body=legacy.str();
    {std::ofstream out(file,std::ios::binary);out<<"S2SAVE 2 "<<crc32({reinterpret_cast<const std::uint8_t*>(body.data()),body.size()})<<' '<<body.size()<<'\n'<<body;}
    const auto previous=GameSave::read(file);std::filesystem::remove(file);
    check(!previous.universe && previous.gems==7 && previous.cheats.superflame && previous.cheats.superfly,"Version 2 retail save compatibility regressed");
}
void retail_checks(const std::filesystem::path& path) {
    AssetStore store(path);WadArchive wad(store);const auto exe=store.read_all("SCUS_944.25");
    const auto archive=wad.read(17);auto actors=decode_level(archive,17);const auto models=decode_object_models(archive,exe);const auto portal=decode_return_portal(archive,exe);
    const auto spyro=decode_spyro(wad.read(0),archive,exe,wad.read(29),29);
    std::map<unsigned,Level> sources;
    for(unsigned slot:{17,33,35,37})sources.emplace(slot,decode_level(wad.read(slot),slot));
    unsigned samples=0,objects=0;
    check(portal.picture.size()>1000 && portal.aperture[2].y>1.6f,"Retail portal panorama/aperture was not decoded");
    for(std::uint64_t seed=0;seed<256;++seed) {
        UniverseAddress address{seed,seed*197,false};
        const auto level=generate_universe(address,sources.at(universe_source_slot(address)),actors,models,portal);
        const auto audit=audit_universe(level,&models);check(bool(audit),"Retail world failed audit");samples+=audit.route_samples;objects+=audit.grounded_objects;
        check(level.universe->enemies>=1 && level.universe->objective_gems.size()>10,"Generated world omitted encounters/objective");
        check(level.objects[level.universe->portals[0].object].type==0x1aa,"World exit is not the original Return Home Moby");
        if(seed<8) {
            auto duplicate=generate_universe(address,sources.at(universe_source_slot(address)),actors,models,portal);
            check(duplicate.objects.size()==level.objects.size() && duplicate.triangles.size()==level.triangles.size(),"Generated world changed across visits");
            Scene scene;scene.wad_slot=level.wad_slot;scene.spawn=level.spawn;Game game(level,scene,{},&models);
            // Traverse the complete generated centreline through the real controller.
            std::size_t target=1;
            for(unsigned tick=0;tick<3600 && target<level.universe->route.size();++tick) {
                const auto delta=level.universe->route[target]-game.player.position;
                if(std::hypot(delta.x,delta.z)<0.6f){++target;continue;}
                InputState input;input.camera_relative=false;input.move_x=delta.x;input.move_z=delta.z;
                const float d=std::hypot(input.move_x,input.move_z);input.move_x/=d;input.move_z/=d;
                game.advance(FixedClock::step,input);
                check(game.player.position.y>level.bounds.min.y+1,"Player fell out of a generated route");
            }
            check(target==level.universe->route.size(),"Controller could not traverse complete route");
            for(unsigned i=0;i<120 && !game.universe_travel;++i) {
                const auto goal=level.universe->portals.back().center+Vec3{0,0,2};
                const auto d=normalized(Vec3{goal.x-game.player.position.x,0,goal.z-game.player.position.z});
                InputState input;input.camera_relative=false;input.move_x=d.x;input.move_z=d.z;game.advance(FixedClock::step,input);
            }
            if(!game.universe_travel) {
                const auto p=game.player.position,c=level.universe->portals.back().center;
                std::cerr<<"Exit seed "<<seed<<" player "<<p.x<<','<<p.y<<','<<p.z<<" portal "<<c.x<<','<<c.y<<','<<c.z<<'\n';
                if(const auto h=level.raycast(p+retail::origin_above_feet,{0,0,1},3)) {
                    const auto& t=level.triangles.at(h->triangle);std::cerr<<"blocking triangle "<<h->triangle<<": "<<t.a.x<<','<<t.a.y<<','<<t.a.z<<" / "<<t.b.x<<','<<t.b.y<<','<<t.b.z<<" / "<<t.c.x<<','<<t.c.y<<','<<t.c.z<<'\n';
                }
            }
            check(game.universe_travel && game.universe_travel->space,"The retail arch blocks the exit route or fails to travel");
            check(universe_objective_progress(game)>10,"Walking the coherent route did not collect its objective gems");
            auto bad=level;bad.triangles.push_back({{-20,9,4},{20,9,4},{0,9,16}});
            check(!audit_universe(bad,&models),"Route audit accepted obstructing geometry");
            if(seed<4) {
                const auto target_enemy=std::find_if(level.objects.begin(),level.objects.end(),[](const auto& o){return o.enemy.has_value();});
                check(target_enemy!=level.objects.end(),"Generated encounter has no mapped enemy");
                Game encounter(level,scene,{},&models,&spyro);encounter.play_scene.spawn=target_enemy->position+Vec3{0,0,-1.6f};encounter.respawn();
                for(unsigned i=0;i<16;++i)encounter.advance(FixedClock::step,{});
                check(encounter.retail_objects[target_enemy->id].enemy->phase!=EnemyPhase::Idle,"Generated enemy never reacts to Spyro");
                encounter.retail_objects=level.objects;encounter.play_scene.spawn=target_enemy->position+Vec3{0,0,-1.25f};encounter.respawn();
                for(unsigned i=0;i<2;++i)encounter.advance(FixedClock::step,{});
                encounter.player.invulnerable_ticks=180;
                InputState flame;flame.flame_pressed=true;encounter.advance(FixedClock::step,flame);
                for(unsigned i=0;i<120;++i)encounter.advance(FixedClock::step,{});
                if(!encounter.retail_objects[target_enemy->id].enemy->defeated) {
                    const auto& enemy=encounter.retail_objects[target_enemy->id];
                    std::cerr<<"Enemy seed "<<seed<<" type "<<enemy.type<<" phase "<<int(enemy.enemy->phase)<<" target "<<enemy.position.x<<','<<enemy.position.y<<','<<enemy.position.z<<" player "<<encounter.player.position.x<<','<<encounter.player.position.y<<','<<encounter.player.position.z<<'\n';
                }
                check(encounter.retail_objects[target_enemy->id].enemy->defeated && encounter.spirits()>0,"Generated enemy cannot be flamed or award its spirit");
                GameSave defeated;defeated.capture(encounter);Game revisited(level,scene,{},&models);defeated.apply(revisited,&models);
                check(revisited.retail_objects[target_enemy->id].collected,"Revisiting resurrected a defeated generated enemy");
            }
        }
    }
    auto space=generate_universe({42,0,true},sources.at(17),actors,models,portal);check(space.universe->validated,"Space failed validation");
    auto blocked_space=space;blocked_space.universe->planets[0].center=lerp(space.spawn,space.universe->portals.back().center,0.5f);
    check(!audit_universe(blocked_space,&models),"Space audit accepted a planet across a flight approach");
    Scene flight_scene;flight_scene.wad_slot=space.wad_slot;flight_scene.spawn=space.spawn;Game flight(space,flight_scene,{},&models,&spyro);
    InputState boost;boost.charge=true;
    for(unsigned i=0;i<120;++i)flight.advance(FixedClock::step,boost);
    for(const auto& wing:flight.powers.wings()) {
        float span=0;for(std::size_t i=1;i<wing.size();++i)span+=length(wing[i]-wing[i-1]);
        check(span<=1.21f,"Boosted wing streak extends into the follow camera");
    }
    std::cout<<"256 retail worlds; "<<samples<<" route samples; "<<objects<<" grounded actors\n";
}
}
int main(int argc,char** argv) {try{plan_checks();portal_checks();save_checks();if(argc>1)retail_checks(argv[1]);std::cout<<checks<<" universe checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"Universe check failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
