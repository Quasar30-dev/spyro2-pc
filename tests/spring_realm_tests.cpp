// Spring Realm (custom world built on the retail Dragon Realm terrain).
// Surface: walking tour, whirlwind lifts to the hall terraces, breakables,
// the level portals in the Dragon Realm's own blank arches (walked into).
// The Wellspring: the deep courtyard lake and its flooded passage (input-only
// swim, dive orb), the Root Tunnel from the hillside mouth to the root ledge,
// the grotto's glide chain over fixed pillars and moving slabs, the Root Lift
// and gallery whirlwind, Glimmer's lizards opening the Superfly gate, and the
// gallery gap that needs the glide upgrade. Routes come from routes.txt
// (written beside the world by tools/export_spring_realm_blender.py).
#include <cctype>
#include <sstream>
#include "spyro/custom_level.hpp"
#include "spyro/universe.hpp"
#include "spyro/travel.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <map>
using namespace spyro;
namespace {
unsigned failures{};
void check(bool yes,const std::string& text){if(!yes)throw FormatError(text);}
void soft(bool yes,const std::string& text){if(!yes){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
std::string pos(Vec3 p){return std::to_string(p.x)+","+std::to_string(p.y)+","+std::to_string(p.z);}
void ticks(Game& g,unsigned n,InputState input={}){for(unsigned i=0;i<n;++i)g.advance(FixedClock::step,input);}
std::map<std::string,std::vector<Vec3>> read_routes(const std::filesystem::path& path) {
    std::ifstream in(path);check(bool(in),"Missing routes.txt beside the world");
    std::map<std::string,std::vector<Vec3>> out;std::string tag,name;unsigned n{};
    while(in>>tag>>name>>n){auto& v=out[name];for(unsigned i=0;i<n;++i){Vec3 p;in>>p.x>>p.y>>p.z;v.push_back(p);}}
    return out;
}
// Steer the walking controller through waypoints; returns the number reached.
unsigned walk(Game& g,const std::vector<Vec3>& route,unsigned limit=6000,float reach=.6f) {
    unsigned target=0,stalled=0;auto previous=g.player.position;
    for(unsigned t=0;t<limit && target<route.size();++t) {
        const auto d=route[target]-g.player.position;
        if(std::hypot(d.x,d.z)<reach){++target;stalled=0;continue;}
        const auto dir=normalized(Vec3{d.x,0,d.z});InputState in;in.move_x=dir.x;in.move_z=dir.z;g.advance(FixedClock::step,in);
        if(length(g.player.position-previous)<.0005f)++stalled;else stalled=0;previous=g.player.position;
        if(stalled>180)break;
    }
    return target;
}
Vec3 piece_top(const HomeworldPiece& p,unsigned age) {
    float off=p.offset;
    if(p.kind==HomeworldKind::Shuttle)off=std::sin(float(age)*6.283185307f/p.period)*p.travel;
    return p.center+p.axis*off+Vec3{0,p.extent.y,0};
}
// One hop from the current footing to `to` (a platform top), played like a
// player would: wait for a moving slab's best phase, face the target, run to
// the takeoff edge, jump, glide from the apex when the gap needs it, spend
// glide boosts when sinking below the takeoff height, and cancel the glide
// (charge) just before the target so Spyro drops onto it.
bool hop(Game& g,Vec3 to,float from_radius,const HomeworldPiece* moving=nullptr,const HomeworldPiece* riding=nullptr,unsigned limit=600) {
    if(moving || riding) {
        const auto& p=moving?*moving:*riding;float best=1e9f;unsigned best_t=0;
        for(unsigned t=0;t<unsigned(p.period);++t) {
            const auto c=piece_top(p,p.age+t);
            const float d=moving?std::hypot(c.x-g.player.position.x,c.z-g.player.position.z):std::hypot(to.x-c.x,to.z-c.z);
            if(d<best){best=d;best_t=t;}
        }
        ticks(g,unsigned(std::max(0,int(best_t)-30)));
    }
    const auto start=g.player.position;
    {const auto d=(moving?piece_top(*moving,moving->age):to)-g.player.position;g.player.yaw=g.player.previous_yaw=std::atan2(d.x,d.z);}
    bool jumped=false,glided=false,dropped=false;
    for(unsigned t=0;t<limit;++t) {
        if(moving)to=piece_top(*moving,moving->age);
        const auto d=to-g.player.position;const float flat=std::hypot(d.x,d.z);
        const float run=std::hypot(g.player.position.x-start.x,g.player.position.z-start.z);
        InputState in;
        if(g.player.air.gliding) {
            // glide steering is relative: move_x turns, pulling back brakes
            const float err=std::remainder(std::atan2(d.x,d.z)-g.player.yaw,6.283185307f);
            in.move_x=std::clamp(err*2.5f,-1.f,1.f);in.move_z=flat>2.2f?1.f:-1.f;
        } else if(!dropped){const auto dir=normalized(Vec3{d.x,0,d.z});in.move_x=dir.x;in.move_z=dir.z;}
        if(!jumped && (run>=from_radius-0.35f || (riding && t>2)) && g.player.grounded){in.jump_pressed=in.jump_held=true;jumped=true;}
        else if(!jumped && !g.player.grounded){in.jump_pressed=true;jumped=true;}
        else if(jumped && !glided && !g.player.grounded && g.player.velocity.y<=0 && flat>1.0f){in.jump_pressed=true;glided=true;}
        else if(glided && !dropped && g.player.air.gliding && g.player.position.y<start.y-0.3f && flat>1.4f)in.jump_pressed=true;    // glide boost
        const float speed=std::hypot(g.player.velocity.x,g.player.velocity.z);
        if(jumped && !dropped && flat<0.3f+speed*0.1f && !g.player.grounded){in.charge=g.player.air.gliding;dropped=true;in.move_x=in.move_z=0;}
        g.advance(FixedClock::step,in);
        if(std::getenv("SR_TRACE") && t%6==0)std::cerr<<"    t"<<t<<" "<<pos(g.player.position)<<" yaw "<<g.player.yaw<<" grounded "<<g.player.grounded
            <<" jumped "<<jumped<<" glide "<<g.player.air.gliding<<" in "<<in.move_x<<","<<in.move_z<<"\n";
        if(jumped && t>6 && g.player.grounded) {
            const auto e=to-g.player.position;
            return std::hypot(e.x,e.z)<1.35f && std::abs(e.y)<0.35f;
        }
        if(g.player.position.y<to.y-3.0f)return false;
    }
    return false;
}
// Horizontal reach of a running jump + glide (boosts spent when sinking
// below the takeoff height) before Spyro sinks below `floor_y`.
float glide_reach(Game& g,Vec3 dir,float floor_y) {
    const auto start=g.player.position;g.player.yaw=g.player.previous_yaw=std::atan2(dir.x,dir.z);
    bool jumped=false,glided=false;float reach=0;
    const float heading=std::atan2(dir.x,dir.z);
    for(unsigned t=0;t<900;++t) {
        InputState in;in.move_x=dir.x;in.move_z=dir.z;
        if(g.player.air.gliding){in.move_x=std::clamp(std::remainder(heading-g.player.yaw,6.283185307f)*2.5f,-1.f,1.f);in.move_z=1;}
        if(!jumped && t>=6 && g.player.grounded){in.jump_pressed=in.jump_held=true;jumped=true;}
        else if(jumped && !glided && !g.player.grounded && g.player.velocity.y<=0){in.jump_pressed=true;glided=true;}
        else if(glided && g.player.air.gliding && g.player.position.y<start.y-0.3f)in.jump_pressed=true;
        g.advance(FixedClock::step,in);
        if(jumped && g.player.position.y>=floor_y)reach=std::hypot(g.player.position.x-start.x,g.player.position.z-start.z);
        if(jumped && t>8 && (g.player.grounded || g.player.position.y<floor_y-0.5f))break;
    }
    return reach;
}
}
int main(int argc,char** argv) {
    try {
        check(argc==3 || argc==4,"Usage: spyro_spring_realm_tests retail-path spring-realm.s2world [routes.txt]");
        AssetStore store(argv[1]);WadArchive wad(store);const auto exe=store.read_all("SCUS_944.25");
        const std::filesystem::path world(argv[2]);
        const auto routes=read_routes(argc==4?std::filesystem::path(argv[3]):world.parent_path()/"routes.txt");
        const auto slot=custom_level_bank(world);check(slot==15,"Spring Realm uses the Summer Forest actor bank");
        const auto archive=wad.read(slot);auto source=decode_level(archive,slot);decode_travel(archive,exe,source);
        auto models=decode_object_models(archive,exe);const auto portal=decode_return_portal(archive,exe);
        import_custom_level_models(world,wad,exe,models);
        const auto spyro=decode_spyro(wad.read(0),archive,exe,wad.read(29),29,wad.read(15),wad.read(45),wad.read(59));
        const auto level=load_custom_level(world,wad,source,models,portal);
        check(level.custom_name=="Spring Realm","Wrong world");
        {   // Original tiles (-2): the blossom meadow is loaded into the atlas, opaque.
            std::ifstream in(world);std::string line;unsigned index=0,found=UINT32_MAX;bool counting=false;
            for(unsigned n=0;std::getline(in,line) && n<400;++n) {
                if(!counting && line.find(' ')==std::string::npos && !line.empty() && std::isdigit(static_cast<unsigned char>(line[0])) && n>5){counting=true;continue;}
                if(counting){if(line.rfind("-2 \"textures/blossom-meadow.rgba\"",0)==0){found=index;break;}++index;}
            }
            check(found!=UINT32_MAX,"Spring Realm lists its original blossom meadow tile");
            Bytes tile(64*64*4);{std::ifstream f(world.parent_path()/"textures/blossom-meadow.rgba",std::ios::binary);f.read(reinterpret_cast<char*>(tile.data()),static_cast<std::streamsize>(tile.size()));}
            const auto at=[&](unsigned x,unsigned y){return ((found/16*64+y)*level.texture_width+found%16*64+x)*4;};
            unsigned same=0;for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){const auto a=at(x,y),b=(y*64+x)*4;
                same+=level.texture_rgba[a]==tile[b] && level.texture_rgba[a+1]==tile[b+1] && level.texture_rgba[a+2]==tile[b+2] && level.texture_rgba[a+3]==255;}
            check(same==64*64,"The blossom meadow tile reached the atlas unchanged and opaque");
            // Paths outside the world folder and non-tile files are refused.
            for(const char* bad:{"-2 \"../evil.rgba\" 0","-2 \"C:/Windows/evil.rgba\" 0","-2 \"textures/blossom-meadow.png\" 0"}) {
                std::ifstream original(world);std::stringstream text;text<<original.rdbuf();auto body=text.str();
                const auto line_at=body.find("-2 \"textures/blossom-meadow.rgba\" 0");check(line_at!=std::string::npos,"Missing tile line");
                body.replace(line_at,std::string("-2 \"textures/blossom-meadow.rgba\" 0").size(),bad);
                const auto copy=world.parent_path()/"spring-realm-bad-tile-test.s2world";{std::ofstream out(copy,std::ios::binary);out<<body;}
                bool rejected=false;try{(void)load_custom_level(copy,wad,source,models,portal);}catch(const FormatError&){rejected=true;}
                std::filesystem::remove(copy);check(rejected,std::string("Accepted a bad original tile: ")+bad);
            }
        }
        check(level.whirlwinds.size()==3,"Expected the two hall whirlwinds and the grotto whirlwind");
        check(level.portals.size()==4,"Expected the three retail-arch level portals and the crown gate");
        const auto gate=std::find_if(level.portals.begin(),level.portals.end(),[](const auto& p){return !p.world.empty();});
        check(gate!=level.portals.end() && gate->world=="cyborg-ripto-arena/pc/crownworks.s2world" && gate->world_emblems==4 && gate->world_flags==3,
            "The crown gate must be a world door to the Crownworks needing four emblems");
        check(level.homeworld_pieces.size()==5,"Expected five moving slabs in the grotto");
        check(level.custom_objectives.size()==3,"Expected three underground orbs");
        check(level.water_volumes.size()>=10,"Missing lake / passage / pool water volumes");
        check(level.sky_vertices.size()==source.sky_vertices.size() && !level.sky_vertices.empty(),"The world must use Summer Forest's sky");
        for(const unsigned type:{0x1cc,0x1f0,0x22b})check(models.models.contains(type),"Imported Glimmer model missing");
        Scene scene;scene.wad_slot=slot;scene.spawn=level.spawn;
        unsigned samples=0;
        // ---- surface: walking tour -----------------------------------------------------
        for(unsigned i=1;i<level.custom_route.size();++i) {
            const auto a=level.custom_route[i-1],b=level.custom_route[i];const unsigned steps=unsigned(length(b-a)*3)+1;bool bad=false;
            for(unsigned k=0;k<=steps;++k) {
                const auto p=lerp(a,b,float(k)/steps);const auto hit=level.raycast(p+Vec3{0,1.5f,0},{0,-1,0},3);++samples;
                if(!hit || hit->normal.y<.75f || std::abs(hit->point.y-p.y)>.7f){if(!bad)std::cerr<<"Route floor gap at "<<pos(p)<<" (segment "<<i<<")\n";bad=true;}
            }
            soft(!bad,"walking tour floor");
        }
        {
            Game g(level,scene,{},&models,&spyro);g.player.invulnerable_ticks=100000;
            const auto n=walk(g,level.custom_route,20000,.55f);soft(n==level.custom_route.size(),"walking tour blocked at point "+std::to_string(n));
            std::cerr<<"Walking tour: "<<n<<" / "<<level.custom_route.size()<<"\n";
        }
        // ---- whirlwinds: halls -> terraces (16.78), grotto -> gallery (9.0) -----------------
        unsigned rides=0;
        for(const auto& w:level.whirlwinds) {
            const auto& base=level.objects[w.object].position;const float landing=base.y<5?9.0f:16.78f;
            bool landed=false;
            for(const Vec3 off:{Vec3{0,0,2.2f},Vec3{2.2f,0,0},Vec3{-2.2f,0,0},Vec3{0,0,-2.2f}}) {
                Scene start=scene;start.spawn=base+off+Vec3{0,.05f,0};Game g(level,start,{},&models,&spyro);g.player.invulnerable_ticks=100000;ticks(g,10);
                if(!g.player.grounded)continue;
                bool lifted=false;
                for(unsigned t=0;t<900;++t) {
                    InputState in;if(!lifted){const auto d=base-g.player.position;const auto dir=normalized(Vec3{d.x,0,d.z});in.move_x=dir.x;in.move_z=dir.z;}
                    g.advance(FixedClock::step,in);
                    if(g.whirlwind.index!=UINT32_MAX)lifted=true;
                    if(lifted && g.whirlwind.index==UINT32_MAX && g.player.grounded)break;
                }
                landed=lifted && g.player.grounded && std::abs(g.player.position.y-landing)<.35f;
                std::cerr<<"Whirlwind at "<<pos(base)<<": lifted "<<lifted<<", ended "<<pos(g.player.position)<<"\n";
                break;
            }
            soft(landed,"whirlwind at "+pos(base));rides+=landed;
        }
        // ---- breakables (surface and grotto) ------------------------------------------------
        unsigned broken=0,breakables=0;
        for(const auto& o:level.objects)if(o.type==0xc8 || o.type==0xc9) {
            ++breakables;bool done=false;
            for(const Vec3 offset:{Vec3{0,0,-1.25f},Vec3{0,0,1.25f},Vec3{-1.25f,0,0},Vec3{1.25f,0,0}}) {
                if(o.type==0xc8) {
                    Scene start=scene;start.spawn=o.position+offset+Vec3{0,.05f,0};Game g(level,start,{},&models,&spyro);g.player.invulnerable_ticks=600;ticks(g,30);
                    if(!g.player.grounded)continue;
                    const auto d=o.position-g.player.position;g.player.yaw=std::atan2(d.x,d.z);
                    InputState flame;flame.flame_pressed=true;g.advance(FixedClock::step,flame);ticks(g,120);
                    const auto& r=g.retail_objects[o.id];if(r.collected || r.state<0){done=true;break;}
                } else {
                    Scene back=scene;back.spawn=o.position+offset*1.6f+Vec3{0,.05f,0};Game c(level,back,{},&models,&spyro);c.player.invulnerable_ticks=600;ticks(c,30);
                    if(!c.player.grounded)continue;
                    for(unsigned t=0;t<120;++t){const auto dd=o.position-c.player.position;const auto dir=normalized(Vec3{dd.x,0,dd.z});InputState in;in.move_x=dir.x;in.move_z=dir.z;in.charge=true;c.advance(FixedClock::step,in);}
                    ticks(c,60);const auto& r=c.retail_objects[o.id];if(r.collected || r.state<0){done=true;break;}
                }
            }
            if(done)++broken;else soft(false,"breakable "+std::to_string(o.id)+" at "+pos(o.position));
        }
        // ---- portals: walk into each retail arch from its front ---------------------------
        unsigned entered=0;
        for(const auto& p:level.portals) {
            Vec3 c{};for(auto v:p.aperture)c+=v;c=c/5;const auto n=normalized(cross(p.aperture[1]-p.aperture[0],p.aperture[2]-p.aperture[0]));
            const auto base=(p.aperture[0]+p.aperture[4])*.5f;
            const auto walk_in=[&](bool unlock) {
                Scene start=scene;start.spawn=base+Vec3{n.x,0,n.z}*2.6f+Vec3{0,.3f,0};Game g(level,start,{},&models,&spyro);
                g.player.invulnerable_ticks=600;g.portal_travel.unlock=unlock;ticks(g,20);
                bool travel=false;
                for(unsigned t=0;t<240 && !travel;++t){const auto d=base-g.player.position;const auto dir=normalized(Vec3{d.x,0,d.z});InputState in;in.move_x=dir.x;in.move_z=dir.z;g.advance(FixedClock::step,in);travel=g.portal_travel.active();}
                std::cerr<<"Portal "<<p.index<<" ("<<(p.world.empty()?"destination "+std::to_string(p.destination):p.world)<<(unlock?", unlocked":"")<<"): "
                         <<(travel?"entered":"NOT entered")<<" from "<<pos(start.spawn)<<"\n";
                return std::pair{travel,g.portal_travel.request};
            };
            if(!p.world.empty()) {
                // The crown gate stays sealed without the four emblems, and
                // leads to the Crownworks once opened.
                soft(!walk_in(false).first,"the sealed crown gate let Spyro through");
                const auto [travel,request]=walk_in(true);
                soft(travel && request.world==p.world && request.world_flags==3 && !request.world_return,"the opened crown gate does not lead to the Crownworks");
                entered+=travel;continue;
            }
            const bool travel=walk_in(false).first;
            soft(travel,"portal "+std::to_string(p.index)+" cannot be walked into");entered+=travel;
        }
        // ---- the lake: depth and swimmable volume ------------------------------------------
        const auto lake=level.water_at({57.5f,9.0f,14.0f});
        check(lake && std::abs(lake->height-9.406f)<.01f,"The courtyard lake has no water surface");
        check(bool(level.water_at({57.5f,4.0f,14.0f})),"The lake well is not water");
        check(!level.water_at({57.5f,10.6f,30.0f}),"Lake water leaked onto the lawn");
        {
            const auto bed=level.raycast({57.5f,9.3f,14.0f},{0,-1,0},10);check(bed && bed->point.y<3.3f,"The lake well is not deep");
            const auto shallow=level.raycast({49.5f,9.3f,15.0f},{0,-1,0},3);check(shallow && shallow->point.y>7.0f,"The retail pond shallows are missing");
        }
        // ---- dive: lake -> flooded passage -> spring pool (orb), then climb out ------------
        {
            const auto& route=routes.at("dive");const auto exit=routes.at("dive_exit").front();
            Scene start=scene;start.spawn={57.5f,9.2f,14.0f};Game g(level,start,{},&models,&spyro);g.player.invulnerable_ticks=100000;
            const auto orbs=g.player.orbs;unsigned stage=0,waypoint=0;bool surfaced=false,dived=false;
            for(unsigned t=0;t<9000 && stage<4;++t) {
                InputState in;
                if(stage==0){if(g.player.swim.mode==WaterMode::Surface)stage=1;}
                else if(stage==1){if(g.player.swim.mode==WaterMode::Underwater){stage=2;dived=true;}else in.charge=g.player.swim.surface_ticks>25;}
                else if(stage==2) {
                    if(waypoint>=route.size()){stage=3;}
                    else {
                        const auto d=route[waypoint]-g.player.position;const float flat=std::hypot(d.x,d.z);
                        if(length(d)<.7f){++waypoint;}
                        else {
                            const float yaw=flat>.4f?std::atan2(d.x,d.z):g.player.yaw;
                            const float pitch=std::clamp(std::atan2(d.y,std::max(.01f,flat)),-1.2f,1.2f);
                            const float heading=std::remainder(yaw-g.player.yaw,6.283185307f);
                            in.move_x=std::clamp(heading*3,-1.f,1.f);in.move_z=std::clamp((g.player.pitch-pitch)*4,-1.f,1.f);
                            in.jump_held=std::abs(heading)<.65f && std::abs(g.player.pitch-pitch)<.5f;
                        }
                        if(waypoint+1>=route.size() && g.player.swim.mode==WaterMode::Surface){surfaced=true;stage=3;}
                    }
                } else if(stage==3) {
                    surfaced|=g.player.swim.mode==WaterMode::Surface;
                    const auto d=exit-g.player.position;const auto dir=normalized(Vec3{d.x,0,d.z});in.move_x=dir.x;in.move_z=dir.z;
                    in.jump_pressed=g.player.swim.mode==WaterMode::Surface && (t%20)==0;in.jump_held=true;
                    if(g.player.swim.mode==WaterMode::Dry && g.player.grounded && g.player.position.y>-0.7f)stage=4;
                }
                g.advance(FixedClock::step,in);
                if(t%900==0)std::cerr<<"  dive t"<<t<<" stage "<<stage<<" wp "<<waypoint<<" "<<pos(g.player.position)<<" mode "<<int(g.player.swim.mode)<<"\n";
            }
            std::cerr<<"Dive: stage "<<stage<<" waypoint "<<waypoint<<"/"<<route.size()<<" orbs "<<orbs<<" -> "<<g.player.orbs<<" ended "<<pos(g.player.position)<<"\n";
            soft(dived && stage==4 && surfaced,"the lake -> passage -> pool swim and climb-out");
            soft(g.player.orbs==orbs+1 && (g.guide_awards.orbs&1),"the Wellspring Dive orb was not awarded");
        }
        // ---- the Root Tunnel: knoll mouth -> root ledge ---------------------------------------
        {
            const auto& route=routes.at("root_tunnel");
            Scene start=scene;start.spawn=route.front()+Vec3{0,.1f,0};Game g(level,start,{},&models,&spyro);g.player.invulnerable_ticks=100000;ticks(g,10);
            const auto n=walk(g,route,8000,.7f);
            std::cerr<<"Root Tunnel: "<<n<<" / "<<route.size()<<" ended "<<pos(g.player.position)<<"\n";
            soft(n==route.size() && g.player.grounded && std::abs(g.player.position.y-9.6f)<.3f,"the Root Tunnel walk to the root ledge");
        }
        // ---- the glide chain: root ledge -> pillars / shuttle / sinking slabs -> floor --------
        {
            const auto& hops=routes.at("hops");
            unsigned ok=0;
            for(unsigned i=1;i<hops.size();++i) {
                Scene start=scene;start.spawn=hops[i-1]+Vec3{0,.05f,0};Game g(level,start,{},&models,&spyro);g.player.invulnerable_ticks=100000;
                // a moving slab as the takeoff: spawn on its rest position at age 0
                ticks(g,8);
                const HomeworldPiece *target=nullptr,*riding=nullptr;
                for(const auto& piece:g.level.homeworld_pieces) {
                    if(piece.kind!=HomeworldKind::Shuttle)continue;
                    if(std::hypot(piece.center.x-hops[i].x,piece.center.z-hops[i].z)<.5f)target=&piece;
                    if(std::hypot(piece.center.x-hops[i-1].x,piece.center.z-hops[i-1].z)<.5f)riding=&piece;
                }
                const bool landed=hop(g,hops[i],i==1?1.6f:1.2f,target,riding);
                std::cerr<<"Hop "<<i<<" -> "<<pos(hops[i])<<": "<<(landed?"landed":"missed")<<" at "<<pos(g.player.position)<<"\n";
                soft(landed,"glide hop "+std::to_string(i));ok+=landed;
            }
            std::cerr<<"Glide chain: "<<ok<<" / "<<hops.size()-1<<" hops\n";
        }
        // ---- the Root Lift: floor -> root ledge ------------------------------------------------
        {
            const auto base=routes.at("root_lift").front();
            Scene start=scene;start.spawn=base+Vec3{0,1.0f,0};Game g(level,start,{},&models,&spyro);g.player.invulnerable_ticks=100000;
            float top=0;for(unsigned t=0;t<600;++t){g.advance(FixedClock::step,{});top=std::max(top,g.player.position.y);}
            std::cerr<<"Root Lift: rider peaked at "<<top<<"\n";
            soft(top>9.4f && g.player.grounded,"the Root Lift does not carry Spyro to the root ledge");
        }
        // ---- lizards open the Superfly gate ------------------------------------------------------
        {
            Game g(level,scene,{},&models,&spyro);g.player.invulnerable_ticks=100000;
            unsigned lizards=0,defeated=0;const RetailObject* gate=nullptr;
            for(const auto& o:g.retail_objects){if(o.type==0x1f0 || o.type==0x22b)++lizards;if(o.powerup_gate)gate=&o;}
            check(lizards==6 && gate,"Missing the grotto lizards or the Superfly gate");
            for(auto& o:g.retail_objects)if(o.type==0x1f0 || o.type==0x22b) {
                for(unsigned attempt=0;attempt<12 && !(o.collected || o.state<0);++attempt) {
                    g.player.position=g.player.previous_position=o.position+Vec3{1.1f,0.1f,0};g.player.velocity={};
                    const auto d=o.position-g.player.position;g.player.yaw=g.player.previous_yaw=std::atan2(d.x,d.z);
                    InputState flame;flame.flame_pressed=true;g.advance(FixedClock::step,flame);ticks(g,40);
                }
                defeated+=o.collected || o.state<0;
            }
            // The gate opens on spirits that have flown to it (80066efc), not on
            // the HUD count: let the last spirits finish their flight.
            unsigned waited=0;
            for(;waited<900 && !gate->powerup_gate->unlocked;++waited)ticks(g,1);
            std::cerr<<"Lizards defeated "<<defeated<<" / "<<lizards<<", spirits "<<g.spirits()<<" (launched "<<g.powers.spirits_launched<<", arrived "<<g.powers.spirits_arrived
                     <<" after "<<waited<<" ticks), gate "<<(gate->powerup_gate->unlocked?"open":"closed")<<"\n";
            for(const auto& s:g.powers.spirit_flights())std::cerr<<"  spirit in flight: state "<<s.state<<" route "<<s.route<<" waypoint "<<s.waypoint<<" counter "<<s.counter<<"/"<<s.frames
                <<" at "<<pos(s.body.position)<<" gate record "<<s.gate<<" routes "<<g.retail_objects[s.gate].powerup_gate->spirit_routes.size()<<"\n";
            soft(defeated==lizards && gate->powerup_gate->unlocked,"defeating the lizards does not open the Superfly gate");
            // cross the gate plane: flight begins
            const auto dir=retail::rotate_model({0,0,1},retail::native_heading(gate->rotation[2]*16));
            g.player.position=g.player.previous_position=gate->position-Vec3{dir.x,0,dir.z}*1.2f+Vec3{0,.1f,0};
            bool flying=false;
            for(unsigned t=0;t<120 && !flying;++t){InputState in;in.move_x=dir.x;in.move_z=dir.z;g.advance(FixedClock::step,in);flying=g.powers.flight.remaining>0;}
            soft(flying,"crossing the open Superfly gate does not grant flight");
        }
        // ---- the glide-upgrade gap: gallery -> Echo Alcove ----------------------------------------
        {
            const auto from=routes.at("glide_gap_start").front(),to=routes.at("glide_gap_target").front();
            bool reached[3]{};
            const auto dir=normalized(Vec3{to.x-from.x,0,to.z-from.z});
            for(unsigned tier=0;tier<3;++tier) {
                Scene start=scene;start.spawn=from+Vec3{0,.05f,0};
                Game m(level,start,{},&models,&spyro);m.player.invulnerable_ticks=100000;m.upgrades.enabled=true;m.upgrades.glide=tier;ticks(m,8);
                const float reach=glide_reach(m,dir,to.y);
                Game g(level,start,{},&models,&spyro);g.player.invulnerable_ticks=100000;
                g.upgrades.enabled=true;g.upgrades.glide=tier;ticks(g,8);
                reached[tier]=hop(g,to,1.0f);
                std::cerr<<"Echo gap with glide tier "<<tier<<": "<<(reached[tier]?"reached":"fell")<<" at "<<pos(g.player.position)
                         <<" (gap "<<std::hypot(to.x-from.x,to.z-from.z)<<", open-air reach "<<reach<<")\n";
            }
            soft(!reached[0] && reached[1],"the Echo Alcove gap must need exactly the first glide upgrade");
        }
        std::cout<<"{\"route_samples\":"<<samples<<",\"whirlwind_rides\":"<<rides<<",\"breakables\":"<<breakables<<",\"broken\":"<<broken
                 <<",\"portals_entered\":"<<entered<<",\"failures\":"<<failures<<"}\n";
        return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
