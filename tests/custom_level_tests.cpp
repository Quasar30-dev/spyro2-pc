#include "spyro/custom_level.hpp"
#include "spyro/universe.hpp"
#include "spyro/travel.hpp"
#include "spyro/savegame.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
using namespace spyro;
int main(int argc,char** argv) {
    try {
        if(argc!=3 && argc!=4)throw FormatError("Usage: spyro_custom_level_tests retail-path world.s2world [platforming.s2courses]");
        AssetStore store(argv[1]);WadArchive wad(store);const auto exe=store.read_all("SCUS_944.25"),archive=wad.read(17);
        auto source=decode_level(archive,17);decode_travel(archive,exe,source);
        const auto models=decode_object_models(archive,exe);const auto portal=decode_return_portal(archive,exe);
        const auto spyro=decode_spyro(wad.read(0),archive,exe,wad.read(29),29);
        const auto level=load_custom_level(argv[2],wad,source,models,portal);
        auto check=[](bool yes,const std::string& why){if(!yes)throw FormatError(why);};
        unsigned samples=0,grounded=0,floor_failures=0;
        for(unsigned i=1;i<level.custom_route.size();++i) {
            bool reported=false;
            const auto a=level.custom_route[i-1],b=level.custom_route[i];const auto steps=unsigned(length(b-a)*3)+1;
            const auto side=normalized(Vec3{b.z-a.z,0,a.x-b.x});
            for(unsigned j=0;j<=steps;++j)for(float lateral:{-.65f,0.0f,.65f}) {
                auto p=lerp(a,b,float(j)/steps)+side*lateral;auto hit=level.raycast(p+Vec3{0,2,0},{0,-1,0},4);
                if(!(hit && hit->normal.y>.8f && std::abs(hit->point.y-p.y)<.7f)) {
                    ++floor_failures;if(!reported)std::cerr<<"Route floor/clearance failure at segment "<<i<<" point "<<p.x<<","<<p.y<<","<<p.z<<(hit?" hit "+std::to_string(hit->point.y):" no ground")<<'\n';reported=true;
                }++samples;
            }
        }
        if(floor_failures)std::cerr<<floor_failures<<" unsafe main-route floor samples\n";
        for(const auto& o:level.objects)if(o.type!=0x78) {
            const auto h=level.raycast(o.position+Vec3{0,.5f,0},{0,-1,0},1.5f);
            check(h && h->normal.y>.8f && std::abs(h->point.y-o.position.y)<.55f,"Unsupported object "+std::to_string(o.id));++grounded;
        }
        Scene scene;scene.wad_slot=17;scene.spawn=level.spawn;Game game(level,scene,{},&models);
        game.player.invulnerable_ticks=100000;
        float route_length=0;for(unsigned i=1;i<level.custom_route.size();++i)route_length+=length(level.custom_route[i]-level.custom_route[i-1]);
        const unsigned traversal_limit=1200+static_cast<unsigned>(route_length/game.tuning.run_speed*120);
        unsigned target=1,ticks=0;
        unsigned failed_routes=floor_failures?1:0;
        try {
        auto progress_position=game.player.position;
        for(;ticks<traversal_limit && target<level.custom_route.size();++ticks) {
            const auto d=level.custom_route[target]-game.player.position;
            if(std::hypot(d.x,d.z)<.6f){++target;continue;}
            const auto direction=Vec3{d.x,0,d.z}/std::max(1.0f,std::hypot(d.x,d.z));
            InputState input;input.move_x=direction.x;input.move_z=direction.z;input.flame_pressed=ticks%60==0;
            game.advance(FixedClock::step,input);
            if(ticks && ticks%300==0) {
                check(length(game.player.position-progress_position)>.2f,"Player blocked at route "+std::to_string(target)+" / position "+std::to_string(game.player.position.x)+","+std::to_string(game.player.position.y)+","+std::to_string(game.player.position.z));
                progress_position=game.player.position;
            }
        }
        check(target==level.custom_route.size(),"Player traversal stalled at route "+std::to_string(target)+" / position "+std::to_string(game.player.position.x)+","+std::to_string(game.player.position.y)+","+std::to_string(game.player.position.z));
        check(game.player.gems>20,"Full traversal did not collect the authored gem trail");
        std::cerr<<"Main route completed in "<<ticks<<" ticks; testing optional courses\n";
        } catch(const FormatError& e) {++failed_routes;std::cerr<<e.what()<<'\n';}
        unsigned courses=0,jumps=0;
        if(argc==4) {
            std::ifstream paths(argv[3]);std::string magic;unsigned version{},count{};paths>>magic>>version>>count;
            check(magic=="S2COURSES" && version==1 && count>0 && count<=32,"Invalid platform-course manifest");
            for(unsigned i=0;i<count;++i) {
                std::string name;unsigned n{};paths>>std::quoted(name)>>n;check(n>=2 && n<=256,"Invalid platform route size");
                struct Point{Vec3 position;bool jump;};std::vector<Point> route;
                for(unsigned j=0;j<n;++j){Point p{};paths>>p.position.x>>p.position.y>>p.position.z>>p.jump;check(bool(paths) && finite(p.position),"Invalid platform route position");route.push_back(p);}
                try {
                Scene start=scene;start.spawn=route[0].position+Vec3{0,.05f,0};Game branch(level,start,{},&models,&spyro);
                branch.player.invulnerable_ticks=100000;
                for(unsigned j=0;j<12;++j)branch.advance(FixedClock::step,{});
                check(branch.player.grounded,name+": no safe approach landing");
                for(unsigned j=1;j<n;++j) {
                    bool arrived=false,launched=false;unsigned air_ticks=0;
                    for(unsigned step=0;step<600;++step) {
                        const auto d=route[j].position-branch.player.position;
                        if(std::hypot(d.x,d.z)<.13f && std::abs(d.y)<.25f && branch.player.grounded){arrived=true;break;}
                        const auto direction=Vec3{d.x,0,d.z}/std::max(1.0f,std::hypot(d.x,d.z));
                        InputState input;input.move_x=direction.x;input.move_z=direction.z;
                        if(route[j].jump) {
                            input.jump_pressed=!launched && branch.player.grounded;
                            if(input.jump_pressed){launched=true;++jumps;}
                            input.jump_held=launched && air_ticks<52;
                            if(launched)++air_ticks;
                        }
                        branch.advance(FixedClock::step,input);
                        if(branch.player.position.y<route[j].position.y-4)break;
                    }
                    check(arrived,name+": traversal failed at waypoint "+std::to_string(j)+" / player "+std::to_string(branch.player.position.x)+","+std::to_string(branch.player.position.y)+","+std::to_string(branch.player.position.z));
                }
                ++courses;
                std::cerr<<name<<": passed\n";
                } catch(const FormatError& e) {++failed_routes;std::cerr<<e.what()<<'\n';}
            }
        }
        check(!failed_routes,std::to_string(failed_routes)+" traversal routes failed");
        unsigned reactions=0,broken_baskets=0;
        for(const auto& target_object:level.objects)if(target_object.enemy || (target_object.type==0xc8 && !broken_baskets)) {
            Game encounter(level,scene,{},&models,&spyro);encounter.play_scene.spawn=target_object.position+Vec3{0,0,-1.25f};encounter.respawn();
            encounter.player.invulnerable_ticks=600;
            for(unsigned i=0;i<2;++i)encounter.advance(FixedClock::step,{});
            InputState flame;flame.flame_pressed=true;encounter.advance(FixedClock::step,flame);
            for(unsigned i=0;i<120;++i)encounter.advance(FixedClock::step,{});
            const auto& o=encounter.retail_objects[target_object.id];
            if(o.enemy){check(o.enemy->defeated,"Authored enemy cannot be defeated: "+std::to_string(o.id));++reactions;}
            else {check(o.collected || o.state<0,"Authored basket does not break");++broken_baskets;}
        }
        check(level.portals.size()==1 && level.portals[0].destination==15,"Missing retail return-home destination");
        const auto& p=level.portals[0];Vec3 c{};for(auto v:p.aperture)c+=v;c=c/5;
        const auto n=normalized(cross(p.aperture[1]-p.aperture[0],p.aperture[2]-p.aperture[0]));
        check(portal_crossed(p,c-n,c+n),"Custom exit aperture cannot be crossed");
        Game exit(level,scene,{},&models);exit.player.position=exit.player.previous_position=c-n*1.5f-retail::origin_above_feet;
        exit.portal_travel.cooldown=0; // Teleported test setup is past arrival protection.
        for(unsigned i=0;i<180 && exit.portal_travel.phase!=PortalTravel::Phase::Loading;++i) {
            InputState walk;walk.move_x=n.x;walk.move_z=n.z;exit.advance(FixedClock::step,walk);
        }
        check(exit.portal_travel.phase==PortalTravel::Phase::Loading && exit.portal_travel.request.destination==15,"Walking into the exit did not request Summer Forest: phase "+std::to_string(unsigned(exit.portal_travel.phase))+" position "+std::to_string(exit.player.position.x)+","+std::to_string(exit.player.position.y)+","+std::to_string(exit.player.position.z)+" center "+std::to_string(c.x)+","+std::to_string(c.y)+","+std::to_string(c.z)+" normal "+std::to_string(n.x)+","+std::to_string(n.y)+","+std::to_string(n.z));
        bool rejected=false;try{GameSave save;save.capture(game);}catch(const FormatError&){rejected=true;}
        check(rejected,"Custom world can overwrite a retail save slot");
        std::cout<<"{\"route_samples\":"<<samples<<",\"grounded_objects\":"<<grounded<<",\"traversal_ticks\":"<<ticks<<",\"collected_gems\":"<<game.player.gems<<",\"platform_courses\":"<<courses<<",\"platform_jumps\":"<<jumps<<",\"enemies_defeated\":"<<reactions<<",\"baskets_broken\":"<<broken_baskets<<",\"portal\":true,\"save_isolation\":true}\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
