#include "spyro/custom_level.hpp"
#include "spyro/travel.hpp"
#include "spyro/universe.hpp"
#include "../tools/crownworks_swim_route.hpp"
#include <iostream>
using namespace spyro;
namespace {
unsigned checks{};
void check(bool yes,const std::string& message){++checks;if(!yes)throw FormatError(message);}
void step(Game& g,unsigned count=1,InputState input={}){for(unsigned i=0;i<count;++i)g.advance(FixedClock::step,input);}
}
int main(int argc,char** argv){try {
    if(argc!=3)throw FormatError("Usage: spyro_crownworks_tunnel_tests retail-path crownworks.s2world");
    AssetStore assets(argv[1]);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25"),archive=wad.read(17);
    auto models=decode_object_models(archive,exe);auto source=decode_level(archive,17);decode_travel(archive,exe,source);
    const auto level=load_custom_level(argv[2],wad,source,models,decode_return_portal(archive,exe));
    const auto model=decode_spyro(wad.read(0),archive,exe,wad.read(29),29,wad.read(15),wad.read(45),wad.read(59));
    check(level.water_volumes.size()==48,"Unexpected flooded network volume count");
    check(level.water_triangles==8,"Four entry pools must have four visible water surfaces");
    check(model.animations.at(57).imported_from_slot==59,"Missing verified underwater hurt bank");
    for(float x:{-26.f,26.f})for(float z:{-12.f,12.f}) {
        const auto floor=level.raycast({x,4,z},{0,-1,0},20);
        check(floor && std::abs(floor->point.y+10)<.01f,"A pool entry is still obstructed by the arena floor or a tunnel roof");
        check(bool(level.water_at({x,-8,z})) && !level.water_at({x,-11,z}),"Shaft volume escapes its floor");
        check(!level.water_at({x+2.5f,3.8f,z}),"Water leaks onto the arena beside a pool");
    }
    for(auto p:{Vec3{0,.3f,0},Vec3{-14,.3f,-12},Vec3{-18,-6,18}})check(!level.water_at(p),"Tunnel water leaked onto the arena or into a death shaft");
    Scene scene;scene.wad_slot=17;scene.spawn={-26,3.52f,-15.3f};Game swim(level,scene,decode_camera_settings(exe),&models,&model);
    CrownworksSwimRoute pilot;unsigned wet_ticks=0;bool surface=false,underwater=false;
    // Safe traversal proves topology and ordinary entry/exit controls; hazards
    // are tested separately below without invincibility or synthetic damage.
    swim.level.custom_arena->cooling=10000;
    for(unsigned i=0;i<7000 && pilot.stage!=4;++i) {
        step(swim,1,pilot.input(swim));
        surface|=swim.player.swim.mode==WaterMode::Surface;underwater|=swim.player.swim.mode==WaterMode::Underwater;
        wet_ticks+=swim.player.swim.mode==WaterMode::Underwater;
        check(swim.player.lives==3,"Swimming through a sealed tunnel triggered the old death plane");
        if(i%600==0)std::cout<<"Route tick "<<i<<" stage "<<pilot.stage<<" waypoint "<<pilot.waypoint<<" position "<<swim.player.position.x<<","<<swim.player.position.y<<","<<swim.player.position.z<<'\n';
    }
    check(pilot.stage==4 && surface && underwater && wet_ticks>300,"Input-only route failed to swim in and emerge at the opposite pool");
    check(swim.player.health==3 && swim.player.grounded,"Cooling did not make traversal safe or the exit failed to land");
    std::cout<<"Entry, centre shortcut and opposite exit passed\n";
    scene.spawn={26,3.52f,-15.3f};Game outer(level,scene,decode_camera_settings(exe),&models,&model);
    outer.level.custom_arena->cooling=10000;CrownworksSwimRoute perimeter;
    perimeter.route={{26,-8.5f,-12},{26,-8.5f,0},{26,-8.5f,12},{10,-8.5f,12},{-26,-8.5f,12},{-26,2.9f,12}};
    for(unsigned i=0;i<7000 && perimeter.stage!=4;++i) {
        step(outer,1,perimeter.input(outer));check(outer.player.lives==3,"Perimeter route hit a sealed connection or death plane");
    }
    check(perimeter.stage==4 && outer.player.health==3,"The other two pool entrances do not connect through the perimeter gallery");
    std::cout<<"Other two shafts and outer gallery passed\n";
    unsigned jets=0,crushers=0;
    for(unsigned index=0;index<level.custom_arena->pieces.size();++index) {
        const auto& p=level.custom_arena->pieces[index];
        if(p.kind!=ArenaKind::UnderwaterJet && p.kind!=ArenaKind::UnderwaterCrusher)continue;
        const bool jet=p.kind==ArenaKind::UnderwaterJet;jet?++jets:++crushers;
        scene.spawn=p.center+(jet?Vec3{0,1.7f,0}:Vec3{0,-2.15f,0})-retail::origin_above_feet;
        Game hit(level,scene,decode_camera_settings(exe),&models,&model);
        hit.level.custom_arena->ticks=(190+300-unsigned(p.parameter))%300;
        const auto y=hit.player.position.y;step(hit);
        check(hit.player.health==2 && hit.player.hurt_ticks==25,"Active submerged trap failed to inflict underwater damage");
        bool hurt_clip=false;
        for(unsigned i=0;i<25;++i){step(hit);hurt_clip|=hit.animation.state().clip==57;
            for(auto v:hit.animation.sample(.5f).vertices)check(finite(v),"Underwater hurt produced invalid render geometry");}
        check(hurt_clip && hit.player.health==2,"Retail hurt clip was not played, or invulnerability allowed repeat damage");
        check(std::abs(hit.player.position.y-y)<.03f,"Underwater damage applied land gravity or upward launch");
        step(hit,20);check(hit.player.motion==Motion::UnderwaterIdle,"Underwater hurt failed to recover into swim idle");
        Game safe(level,scene,{},&models,&model);safe.level.custom_arena->ticks=(140+300-unsigned(p.parameter))%300;
        step(safe,12);check(safe.player.health==3,"Warning phase inflicted damage");
        safe.level.custom_arena->cooling=300;step(safe,180);check(safe.player.health==3,"Cooling did not disable a submerged trap");
        const auto clock=safe.level.custom_arena->ticks;safe.set_paused(true);safe.advance(.2,{});
        check(safe.level.custom_arena->ticks==clock,"Paused trap timer advanced");
    }
    check(jets==4 && crushers==2,"Missing authored traps");
    scene.spawn={18,-6,18};Game fallen(level,scene,{},&models,&model);const auto lives=fallen.player.lives;
    step(fallen);check(fallen.player.lives+1==lives,"Original arena death shafts no longer kill the player");
    std::cout<<checks<<" Crownworks swimming, collision, trap and animation checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
