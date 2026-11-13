#include "spyro/custom_level.hpp"
#include "spyro/universe.hpp"
#include "spyro/travel.hpp"
#include "spyro/savegame.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <bit>
using namespace spyro;
namespace {
void check(bool yes,const std::string& text){if(!yes)throw FormatError(text);}
void ticks(Game& g,unsigned n,InputState input={}){for(unsigned i=0;i<n;++i)g.advance(FixedClock::step,input);}
std::string pos(Vec3 p){return std::to_string(p.x)+","+std::to_string(p.y)+","+std::to_string(p.z);}
}
int main(int argc,char** argv) {
    try {
        check(argc>=3,"Usage: spyro_custom_homeworld_tests retail-path world.s2world [courses]");
        AssetStore store(argv[1]);WadArchive wad(store);const auto exe=store.read_all("SCUS_944.25");
        const auto slot=custom_level_bank(argv[2]);const auto archive=wad.read(slot);
        auto source=decode_level(archive,slot);decode_travel(archive,exe,source);
        const auto models=decode_object_models(archive,exe);
        const auto portal=decode_return_portal(archive,exe);const auto spyro=decode_spyro(wad.read(0),archive,exe,wad.read(29),29);
        const auto level=load_custom_level(argv[2],wad,source,models,portal);
        check(level.custom_name=="Springtide Castle" && level.npcs.size()==8 && level.custom_objectives.size()==7,"Incomplete homeworld content");
        check(!level.sky_vertices.empty(),"Retail sky missing");
        check(level.homeworld_pieces.size()==10 && level.homeworld_trials.size()==1,"Missing authored interactions");
        Scene scene;scene.wad_slot=slot;scene.spawn=level.spawn;
        unsigned failures=0,samples=0;
        for(unsigned i=1;i<level.custom_route.size();++i) {
            auto a=level.custom_route[i-1],b=level.custom_route[i];const unsigned steps=unsigned(length(b-a)*2)+1;
            bool bad=false;
            for(unsigned k=0;k<=steps;++k) {
                const auto p=lerp(a,b,float(k)/steps);const auto hit=level.raycast(p+Vec3{0,1,0},{0,-1,0},2);
                if(!hit || hit->normal.y<.78f || std::abs(hit->point.y-p.y)>.65f){if(!bad)std::cerr<<"Floor at "<<pos(p)<<" hit "<<(hit?pos(hit->point):"none")<<'\n';bad=true;}++samples;
            }
            if(bad){++failures;std::cerr<<"Unsupported walking tour segment "<<i<<" "<<pos(a)<<" -> "<<pos(b)<<'\n';}
        }
        Game walk(level,scene,{},&models,&spyro);walk.player.invulnerable_ticks=100000;
        unsigned target=1,walk_ticks=0;Vec3 previous=walk.player.position;unsigned stagnant=0;
        for(;walk_ticks<22000 && target<level.custom_route.size();++walk_ticks) {
            auto d=level.custom_route[target]-walk.player.position;
            if(std::hypot(d.x,d.z)<.55f){++target;stagnant=0;continue;}
            const auto dir=normalized(Vec3{d.x,0,d.z});InputState input;input.move_x=dir.x;input.move_z=dir.z;
            walk.advance(FixedClock::step,input);
            if(length(walk.player.position-previous)<.0005f)++stagnant;else stagnant=0;
            previous=walk.player.position;if(stagnant>180)break;
        }
        if(target!=level.custom_route.size()){++failures;std::cerr<<"Walking tour blocked at "<<target<<" / "<<pos(walk.player.position)<<'\n';}
        else std::cerr<<"Walking tour passed: "<<walk_ticks<<" ticks, "<<walk.player.gems<<" gems\n";
        unsigned conversations=0;
        for(const auto& npc:level.npcs) {
            const auto& o=level.objects[npc.object];const auto feet=o.position.y-std::bit_cast<std::int16_t>(read_u16(o.record,56))/1024.0f;
            Scene start=scene;start.spawn={o.position.x,feet+.03f,o.position.z-1.35f};Game g(level,start,{},&models,&spyro);ticks(g,20);
            InputState in;in.dialogue.interact=true;ticks(g,1,in);
            if(!g.dialogue.active() || g.dialogue.object()!=npc.object){++failures;std::cerr<<npc.name<<" conversation failed at "<<pos(g.player.position)<<'\n';continue;}
            check(g.dialogue.line() && !g.dialogue.line()->speech,"Custom dialogue borrowed an unrelated retail voice");
            ticks(g,14);in={};in.dialogue.cancel=true;ticks(g,1,in);check(!g.dialogue.active(),"Dialogue remained locked");++conversations;
        }
        unsigned courses=0,jumps=0,walking_routes=0;
        if(argc>4) {
            std::ifstream in(argv[4]);std::string magic;unsigned version{},count{};in>>magic>>version>>count;check(magic=="S2ROUTES" && version==1,"Invalid walking routes");
            for(unsigned r=0;r<count;++r) {
                std::string name;unsigned n{};in>>std::quoted(name)>>n;std::vector<Vec3> points(n);for(auto& p:points)in>>p.x>>p.y>>p.z;
                Scene start=scene;start.spawn=points[0]+Vec3{0,.03f,0};Game g(level,start,{},&models,&spyro);ticks(g,12);unsigned target=1,still=0;auto previous=g.player.position;
                for(unsigned frame=0;frame<12000 && target<n;++frame) {
                    auto d=points[target]-g.player.position;
                    if(std::hypot(d.x,d.z)<.25f && std::abs(d.y)<.4f){++target;still=0;continue;}
                    auto direction=normalized(Vec3{d.x,0,d.z});InputState input;input.move_x=direction.x;input.move_z=direction.z;ticks(g,1,input);
                    if(length(g.player.position-previous)<.0005f)++still;else still=0;previous=g.player.position;if(still>180)break;
                }
                if(target!=n){++failures;std::cerr<<name<<" walking blocked at "<<target<<" / "<<pos(g.player.position)<<'\n';}else{++walking_routes;std::cerr<<name<<" walking passed\n";}
            }
        }
        if(argc>3) {
            std::ifstream in(argv[3]);std::string magic;unsigned version{},count{};in>>magic>>version>>count;check(magic=="S2COURSES" && version==1,"Invalid courses");
            for(unsigned c=0;c<count;++c) {
                std::string name;unsigned n{};in>>std::quoted(name)>>n;struct P{Vec3 p;bool jump;};std::vector<P> points(n);
                for(auto& p:points)in>>p.p.x>>p.p.y>>p.p.z>>p.jump;
                Scene start=scene;start.spawn=points[0].p+Vec3{0,.04f,0};Game g(level,start,{},&models,&spyro);ticks(g,12);bool complete=true;
                for(unsigned j=1;j<n && complete;++j) {
                    bool arrived=false,launched=false;unsigned air=0;
                    for(unsigned tick=0;tick<300;++tick) {
                        const auto d=points[j].p-g.player.position;
                        if(std::hypot(d.x,d.z)<.18f && std::abs(d.y)<.3f && g.player.grounded){arrived=true;break;}
                        const auto dir=Vec3{d.x,0,d.z}/std::max(1.0f,std::hypot(d.x,d.z));InputState input;input.move_x=dir.x;input.move_z=dir.z;
                        input.jump_pressed=points[j].jump && !launched && g.player.grounded;
                        if(input.jump_pressed){launched=true;++jumps;}input.jump_held=launched && air<52;if(launched)++air;
                        ticks(g,1,input);if(g.player.position.y<points[j].p.y-3)break;
                    }
                    if(!arrived){complete=false;++failures;std::cerr<<name<<" failed at "<<j<<" / "<<pos(g.player.position)<<'\n';}
                }
                if(complete){++courses;std::cerr<<name<<" passed\n";}
            }
        }
        unsigned doors=0;
        for(const auto& p:level.portals) {
            check(!p.picture.empty(),"Missing retail portal picture");
            Vec3 c{};for(auto v:p.aperture)c+=v;c=c/5;const auto n=normalized(cross(p.aperture[1]-p.aperture[0],p.aperture[2]-p.aperture[0]));
            check(portal_crossed(p,c-n,c+n),"Portal aperture mismatch");bool entered=false;
            for(float sign:{-1.0f,1.0f}) {
                const auto from=c+n*sign*1.8f;const auto hit=level.raycast(from+Vec3{0,1,0},{0,-1,0},4);if(!hit)continue;
                Scene start=scene;start.spawn=hit->point+Vec3{0,.03f,0};Game g(level,start,{},&models,&spyro);ticks(g,20);g.portal_travel.cooldown=0;
                for(unsigned t=0;t<180 && g.portal_travel.phase!=PortalTravel::Phase::Loading;++t){InputState in;in.move_x=-n.x*sign;in.move_z=-n.z*sign;ticks(g,1,in);}
                if(g.portal_travel.phase==PortalTravel::Phase::Loading && g.portal_travel.request.destination==p.destination){entered=true;break;}
            }
            if(!entered){++failures;std::cerr<<"Portal "<<p.index<<" physically blocked\n";}else ++doors;
        }
        auto teleport=[&](Game& g,Vec3 p){g.player.position=g.player.previous_position=p;g.player.velocity={};g.player.grounded=false;g.player.swim={};g.player.air={};g.player.jump={};};
        unsigned mechanism_checks=0;
        for(const auto& piece:level.homeworld_pieces)if(piece.kind==HomeworldKind::PressureLift) {
            Scene start=scene;start.spawn=piece.center+Vec3{0,piece.extent.y+.025f,0};Game g(level,start,{},&models,&spyro);ticks(g,160);
            check(g.player.grounded && g.player.position.y>start.spawn.y+1.85f,"Pressure lift did not carry Spyro to its upper stop");
            check(std::abs(g.player.position.x-start.spawn.x)<.05f && std::abs(g.player.position.z-start.spawn.z)<.05f,"Pressure lift moved Spyro sideways");++mechanism_checks;
        }
        Game puzzle(level,scene,{},&models,&spyro);const auto goal=std::find_if(level.custom_objectives.begin(),level.custom_objectives.end(),[](auto g){return g.bit==5;});
        check(goal!=level.custom_objectives.end(),"Watchfire reward missing");const auto reward=level.objects[goal->reward].position;
        teleport(puzzle,reward-retail::origin_above_feet);ticks(puzzle,15);check(!(puzzle.guide_awards.orbs&(1u<<5)),"Watchfire reward bypassed unlit braziers");
        for(const auto& p:level.homeworld_pieces)if(p.kind==HomeworldKind::FlameRelay) {
            auto q=p.center+Vec3{0,0,-1.8f};const auto ground=level.raycast(q+Vec3{0,1,0},{0,-1,0},4);check(bool(ground),"Watchfire approach has no ground");
            teleport(puzzle,ground->point+Vec3{0,.03f,0});puzzle.player.yaw=0;ticks(puzzle,25);InputState in;in.flame_pressed=true;ticks(puzzle,1,in);ticks(puzzle,80);
        }
        check(std::all_of(puzzle.level.homeworld_pieces.begin(),puzzle.level.homeworld_pieces.end(),[](const auto& p){return p.kind!=HomeworldKind::FlameRelay || p.active;}),"Directed flame did not light all watchfires");
        teleport(puzzle,reward-retail::origin_above_feet);ticks(puzzle,20);check(puzzle.guide_awards.orbs&(1u<<5),"Lit watchfires did not unlock the orb");
        const auto orbs=puzzle.player.orbs;ticks(puzzle,120);check(puzzle.player.orbs==orbs && puzzle.retail_objects[goal->reward].collected,"Watchfire orb can be claimed twice");++mechanism_checks;
        Scene start=scene;start.spawn=level.homeworld_trials[0].start+Vec3{0,.04f,0};Game courier(level,start,{},&models,&spyro);ticks(courier,20);InputState begin;begin.dialogue.interact=true;ticks(courier,1,begin);
        check(courier.level.homeworld_trials[0].active,"Courier plate did not start the timed run");
        courier.level.homeworld_trials[0].remaining=2;ticks(courier,3);check(!courier.level.homeworld_trials[0].active,"Courier timer did not expire");ticks(courier,1,begin);
        check(courier.level.homeworld_trials[0].active && courier.level.homeworld_trials[0].remaining>5900,"Courier run cannot restart");
        const std::vector<Vec3> delivery{{0,-6,-57},{0,0,-34},{0,0,-9},{0,2,-.5f},{5,2,6},{5,2,17},{0,2,18},{-6,2,14},{-15,2,14},{-6,2,14},{0,2,18},{0,2,22},{0,2,32},{0,2,37},{0,2.75f,40.5f},{0,2.75f,43}};
        unsigned waypoint=0;
        for(unsigned frame=0;frame<6000 && waypoint<delivery.size();++frame) {
            auto d=delivery[waypoint]-courier.player.position;if(std::hypot(d.x,d.z)<.35f){++waypoint;continue;}
            const auto dir=normalized(Vec3{d.x,0,d.z});InputState input;input.move_x=dir.x;input.move_z=dir.z;ticks(courier,1,input);
        }
        if(!(courier.guide_awards.orbs&(1u<<6))){++failures;std::cerr<<"Courier run failed at "<<waypoint<<" / "<<pos(courier.player.position)<<'\n';}else{++mechanism_checks;std::cerr<<"Courier run completed with "<<courier.level.homeworld_trials[0].remaining/60<<" seconds left\n";}
        unsigned swim_routes=0;
        for(unsigned bit:{2u,3u}) {
            const auto objective=std::find_if(level.custom_objectives.begin(),level.custom_objectives.end(),[&](auto g){return g.bit==bit;});
            check(objective!=level.custom_objectives.end(),"Missing submerged route");const auto water=level.water_at(objective->checkpoints.front()+Vec3{0,1,0});check(bool(water),"Submerged route has no water");
            Scene start=scene;start.spawn=objective->checkpoints.front();start.spawn.y=water->height-.05f;Game g(level,start,{},&models,&spyro);ticks(g,35);
            check(g.player.swim.mode==WaterMode::Surface,"Pond entry did not select surface swim");InputState dive;dive.charge=true;ticks(g,32,dive);
            check(g.player.swim.mode==WaterMode::Underwater,"Charge did not dive beneath the surface");bool passed=true;unsigned index=0;
            for(const auto target:objective->checkpoints) {
                bool arrived=false;
                for(unsigned frame=0;frame<1400;++frame) {
                    const auto d=target-g.player.position;const auto distance=length(d);
                    if(distance<.62f){arrived=true;break;}
                    const auto yaw_error=std::remainder(std::atan2(d.x,d.z)-g.player.yaw,6.283185307f);
                    const auto pitch=std::asin(std::clamp(d.y/distance,-1.f,1.f));InputState in;
                    in.move_x=std::clamp(yaw_error*3,-1.f,1.f);in.move_z=std::clamp((g.player.pitch-pitch)*4,-1.f,1.f);
                    in.jump_held=std::abs(yaw_error)<.55f;ticks(g,1,in);
                }
                if(!arrived){passed=false;++failures;std::cerr<<objective->title<<" swim blocked at "<<index<<" / "<<pos(g.player.position)<<'\n';break;}++index;
            }
            if(passed){++swim_routes;std::cerr<<objective->title<<" swimming passed\n";}
        }
        bool isolated=false;try{GameSave save;save.capture(walk);}catch(const FormatError&){isolated=true;}check(isolated,"Custom progress can overwrite a retail save");
        std::cout<<"{\"floor_samples\":"<<samples<<",\"tour_ticks\":"<<walk_ticks<<",\"walking_routes\":"<<walking_routes<<",\"conversations\":"<<conversations<<",\"platform_courses\":"<<courses<<",\"jumps\":"<<jumps<<",\"portals_entered\":"<<doors<<",\"mechanism_checks\":"<<mechanism_checks<<",\"swimming_routes\":"<<swim_routes<<",\"failures\":"<<failures<<"}\n";
        return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
