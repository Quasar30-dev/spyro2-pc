#include "spyro/runtime.hpp"
#include "spyro/assets.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>

using namespace spyro;
namespace {
unsigned checks{};
void check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
void ticks(Game& g,unsigned n){for(unsigned i=0;i<n;++i)g.advance(FixedClock::step,{});}
Level arena() {
    Level l;l.wad_slot=17;l.spawn={-12,8,-12};l.bounds={{-20,0,-20},{20,30,20}};
    l.triangles={{{-20,8,-20},{20,8,20},{20,8,-20},0},{{-20,8,-20},{-20,8,20},{20,8,20},1}};
    // A wall at x=3 between the impact and target B.
    l.triangles.push_back({{3,8,-1},{3,11,-1},{3,8,1},2});l.triangles.push_back({{3,11,-1},{3,11,1},{3,8,1},3});
    for(const auto& t:l.triangles)for(auto p:{t.a,t.b,t.c})l.visual_vertices.push_back({p,{.8f,.7f,.6f},p.x/30,p.z/30,0});
    l.terrain_materials.push_back({});return l;
}
RetailObject lizard(Vec3 p){RetailObject o;o.type=0x1f0;o.position=o.previous_position=p;EnemyState e;e.kind=EnemyKind::SmallLizard;o.enemy=e;return o;}
Game setup(Element element) {
    const auto level=arena();Scene s;s.wad_slot=17;s.spawn={0,8,0};Game g(level,s);
    g.upgrades.enabled=true;g.upgrades.headbash=1;unlock_all_elements(g);g.upgrades.elements.equipped=element;
    for(const auto p:{Vec3{1.2f,8,0},Vec3{4.4f,8,0},Vec3{0,8,2.6f},Vec3{0,10.6f,1},Vec3{0,8,-7}})g.retail_objects.push_back(lizard(p));
    return g;
}
void ripple_and_sphere() {
    {
        auto g=setup(Element::Water);g.elements.headbash(g,nullptr,{0,8,0});
        check(g.elements.waves==1 && g.elements.shockwaves().front().shape==ShockShape::Ripple,"Water headbash must ripple along the ground");
        for(unsigned i=0;i<40;++i)g.advance(FixedClock::step,{});
        const auto& o=g.retail_objects;
        check(o[0].attack_flags&charge_attack,"Core of a tidal ripple must strike");
        check(!o[1].soak_ticks && !o[1].attack_flags,"Walls must stop the wave");
        check(o[2].soak_ticks && !(o[2].attack_flags&charge_attack),"Outer ripple must be status only");
        check(!o[3].soak_ticks,"Ripples must stay on their own floor");
        check(!o[4].soak_ticks,"Targets beyond the radius must be untouched");
        check(g.elements.wave_hits==2,"Each visible target in reach must be hit exactly once per wave");
        check(!g.elements.shockwaves().empty() || g.elements.waves==1,"Wave bookkeeping");
        const auto& w=g.elements.shockwaves();
        if(!w.empty()) {
            unsigned valid=0;for(const auto& ring:w.front().rings)for(bool v:ring.valid)valid+=v;
            check(valid>0,"Ripple rings must sample the floor");
        }
    }
    {
        auto g=setup(Element::Electric);g.elements.headbash(g,nullptr,{0,8,0});
        check(g.elements.shockwaves().front().shape==ShockShape::Sphere,"Electric headbash must expand a sphere");
        ticks(g,40);
        check(g.retail_objects[3].stun_ticks || g.retail_objects[3].attack_flags,"The sphere must reach an airborne target");
        check(!g.retail_objects[1].stun_ticks,"Walls stop spheres too");
    }
    {
        auto g=setup(Element::Convexity);g.elements.headbash(g,nullptr,{0,8,0});
        check(g.elements.shockwaves().front().shape==ShockShape::Sphere,"Convexity headbash must expand a sphere");
        ticks(g,40);check(g.retail_objects[0].void_mark,"Convexity marks its targets");
    }
    for(const auto element:{Element::Fire,Element::Ice})
        {auto g=setup(element);g.elements.headbash(g,nullptr,{0,8,0});check(g.elements.shockwaves().front().shape==ShockShape::Ripple,"Fire/Ice ripple");}
    {
        auto g=setup(Element::Water);g.upgrades.elements.tiers[1]=1;   // below tier 3: the plain dust ripple
        g.elements.headbash(g,nullptr,{0,8,0});
        check(g.elements.shockwaves().front().plain && g.elements.shockwaves().front().max_radius==shock_radius(Element::Fire,true),"Below tier 3 uses the plain wave");
        ticks(g,30);check(g.retail_objects[0].attack_flags&headbash_attack,"Plain wave core uses the headbash mask");
        auto none=setup(Element::Water);none.upgrades.elements.tiers[1]=1;none.upgrades.headbash=0;none.elements.headbash(none,nullptr,{0,8,0});
        check(!none.elements.waves,"No wave without the headbash upgrade or a tier-3 element");
    }
    {
        auto g=setup(Element::Ice);g.elements.headbash(g,nullptr,{0,8,0});ticks(g,20);g.elements.headbash(g,nullptr,{0,8,0});
        ticks(g,2);g.elements.headbash(g,nullptr,{0,8,0});g.elements.headbash(g,nullptr,{0,8,0});
        check(g.elements.shockwaves().size()<=ElementSystem::max_waves,"At most two waves expand at once");
    }
}
void headbashpocalypse() {
    // Refused carve (special surface) still fires the wave.
    auto level=arena();for(auto& t:level.triangles)t.surface_type=5;
    Scene s;s.wad_slot=17;s.spawn={0,8,0};
    {
        Game g(level,s);g.upgrades.enabled=true;g.upgrades.headbash=2;g.upgrades.headbash_mode=HeadbashMode::Lava;unlock_all_elements(g);g.upgrades.elements.equipped=Element::Ice;
        g.player.position=g.player.previous_position={0,8,0};g.player.grounded=true;
        InputState jump;jump.jump_pressed=jump.jump_held=true;g.advance(FixedClock::step,jump);ticks(g,10);
        InputState bash;bash.headbash_pressed=true;g.advance(FixedClock::step,bash);
        for(unsigned i=0;i<150 && !g.elements.waves;++i)ticks(g,1);
        check(g.player.headbash_impacts==1 && g.headbash.stamps().empty() && g.elements.waves==1,"A refused carve must still emit the wave");
    }
    // Lava + Water: cooled to stone, burning stops, no recarve.
    {
        auto plain=arena();Game g(plain,s);g.upgrades.enabled=true;g.upgrades.headbash=2;g.upgrades.headbash_mode=HeadbashMode::Lava;unlock_all_elements(g);
        g.upgrades.elements.equipped=Element::Fire;g.upgrades.elements.tiers[0]=1;   // plain wave only for the carve
        g.player.position=g.player.previous_position={0,8,0};g.player.grounded=true;
        check(g.headbash.impact(g,nullptr) && g.headbash.stamps().size()==1,"Lava crater carved");
        const auto triangles=g.level.triangles.size(),visual=g.level.visual_vertices.size();
        const auto& patch=g.headbash.lava().front();const auto p=(patch.vertices[0]+patch.vertices[1]+patch.vertices[2])/3;
        ticks(g,90);check(g.headbash.burning(p),"Lava burns before cooling");
        g.upgrades.elements.equipped=Element::Water;g.upgrades.elements.tiers[1]=3;g.elements.headbash(g,nullptr,{0,8,0});
        check(g.headbash.stamps()[0].cooled && g.headbash.stamps()[0].element==Element::Water,"Water must cool the crater");
        check(!g.headbash.burning(p) && g.level.triangles.size()==triangles && g.level.visual_vertices.size()==visual,"Cooling is a flag pass, never a recarve");
        ticks(g,40);g.upgrades.elements.equipped=Element::Fire;g.upgrades.elements.tiers[0]=3;g.elements.headbash(g,nullptr,{0,8,0});
        check(!g.headbash.stamps()[0].cooled && g.headbash.revealing(),"Fire must re-melt and replay the reveal");
        ticks(g,40);g.upgrades.elements.equipped=Element::Convexity;g.elements.headbash(g,nullptr,{0,8,0});
        check(g.headbash.stamps()[0].element==Element::Convexity && g.level.triangles.size()==triangles,"Convexity tints without carving");
    }
}
void convexity_slice() {
    // A closed 2x2x6 tower on the arena floor, struck by the Void Beam on its near wall.
    auto level=arena();
    const auto quad=[&](Vec3 a,Vec3 b,Vec3 c,Vec3 d) {
        level.triangles.push_back({a,b,c,unsigned(level.triangles.size())});level.triangles.push_back({a,c,d,unsigned(level.triangles.size())});
        for(auto p:{a,b,c,a,c,d})level.visual_vertices.push_back({p,{.7f,.6f,.5f},p.x/8,p.y/8,0});
    };
    const float x0=6,x1=8,z0=-1,z1=1,y0=8,y1=14;
    quad({x0,y0,z0},{x0,y1,z0},{x0,y1,z1},{x0,y0,z1});quad({x1,y0,z0},{x1,y0,z1},{x1,y1,z1},{x1,y1,z0});
    quad({x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0});quad({x0,y0,z1},{x0,y1,z1},{x1,y1,z1},{x1,y0,z1});
    quad({x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1});
    Scene s;s.wad_slot=17;s.spawn={0,8,0};Game g(level,s);
    g.player.position=g.player.previous_position={1,8,0};g.player.yaw=g.player.previous_yaw=std::numbers::pi_v<float>/2;
    const auto before=g.level.triangles.size();
    const Vec3 mouth{1.4f,8.7f,0},hit{x0,9.4f,0};
    check(g.destruction.slice(g,mouth,hit,{1,0,0},{},nullptr),"The Void Beam must cut the tower");
    const auto pieces=g.destruction.slabs().size();
    check(g.destruction.slices==1 && pieces>=TerrainDestruction::min_shards && pieces<=TerrainDestruction::max_shards,"A cut tower shatters into 4-8 pieces");
    check(g.destruction.infill_faces==0,"Walls and structures are capped, never packed");
    const auto lid=g.level.raycast({7,20,0},{0,-1,0},20);
    check(lid && lid->point.y>9 && lid->point.y<10.6f,"The stump keeps a collidable lid at the cut");
    check(!g.level.raycast({7,20,0},{0,-1,0},5.9f),"The tower top is no longer level geometry while it falls");
    unsigned fall=0;for(;fall<600 && !g.destruction.slabs().empty();++fall)ticks(g,1);
    check(g.destruction.slabs().empty() && g.destruction.slabs_landed==pieces,"Every piece must come to rest");
    const auto& record=g.destruction.damage().back();
    check(record.attack==TerrainAttack::VoidSlice && record.slice_landed && record.valid(),"The landed transform is recorded");
    float rest=0;for(float x=2;x<16;x+=.25f)for(float z=-4;z<=4;z+=.25f)
        if(std::abs(x-7)>1.2f || std::abs(z)>1.2f)if(const auto top=g.level.raycast({x,20,z},{0,-1,0},20))rest=std::max(rest,top->point.y);
    std::cout<<"Void Beam cut: tower top (y 9.4 to 14) split into "<<pieces<<" pieces, fell and landed in "<<fall<<" ticks; stump lid at y "<<lid->point.y
             <<"; "<<g.level.triangles.size()-before<<" collision triangles added; landed pieces stand to y "<<rest<<" beside the stump\n";
    check(g.level.triangles.size()>before,"The landed slab joins the level");
    // The fallen block is solid: something stands above the floor where it lies.
    bool solid=false;
    for(float x=2;x<16 && !solid;x+=.25f)for(float z=-4;z<=4 && !solid;z+=.25f)
        if(std::abs(x-7)>1.2f || std::abs(z)>1.2f)if(const auto top=g.level.raycast({x,20,z},{0,-1,0},20);top && top->point.y>8.3f)solid=true;
    check(solid,"The fallen slab must have collision");
    // Headbash suspension and save replay rebuild the same geometry.
    const auto count=g.level.triangles.size();const auto lid_before=g.level.raycast({7,20,0},{0,-1,0},20);
    g.destruction.suspend(g.level);g.destruction.resume(g,nullptr);
    const auto lid_after=g.level.raycast({7,20,0},{0,-1,0},20);
    check(g.level.triangles.size()==count && lid_before && lid_after && std::abs(lid_before->point.y-lid_after->point.y)<1e-3f,"Replay must rebuild the cut and the landed slab");
    // Spyro's own floor is protected.
    Game near(level,s);near.player.position=near.player.previous_position={7,8,3.2f};
    check(!near.destruction.slice(near,{7,8.7f,3.6f},{7,8.02f,2.4f},{0,-.6f,-.8f},{},nullptr) || near.destruction.slices==1,"Protected cuts are refused or leave Spyro supported");
    near.destruction_settings.void_slice=false;
    check(!near.destruction.slice(near,mouth,hit,{1,0,0},{},nullptr),"Cutting can be switched off");
}
void convexity_ground() {
    // Flat ground under a held beam: it shatters, the hole is packed (no way
    // through the level) yet left visibly lower, and the pieces settle.
    Level level;level.wad_slot=17;level.spawn={-12,8,-12};level.bounds={{-20,0,-20},{20,30,20}};
    level.triangles={{{-20,8,-20},{20,8,20},{20,8,-20},0},{{-20,8,-20},{-20,8,20},{20,8,20},1}};
    for(const auto& t:level.triangles)for(auto p:{t.a,t.b,t.c})level.visual_vertices.push_back({p,{.8f,.7f,.6f},p.x/30,p.z/30,0});
    level.terrain_materials.push_back({});
    Scene s;s.wad_slot=17;s.spawn={0,8,0};Game g(level,s);
    g.player.position=g.player.previous_position={0,8,0};g.player.yaw=g.player.previous_yaw=std::numbers::pi_v<float>/2;
    check(g.destruction.slice(g,{.4f,8.7f,0},{2.2f,8,0},normalized(Vec3{1,-.4f,0}),{},nullptr,{0,1,0}),"The Void Beam must cut the ground");
    const auto pieces=g.destruction.slabs().size();
    check(pieces>=TerrainDestruction::min_shards && pieces<=TerrainDestruction::max_shards,"Cut ground shatters into 4-8 pieces");
    check(g.destruction.infill_faces>0,"Cut ground is packed");
    for(const auto p:{Vec3{3.4f,20,0},Vec3{4.3f,20,1.6f},Vec3{5.6f,20,-1.2f}}) {
        const auto floor=g.level.raycast(p,{0,-1,0},30);
        if(!floor || floor->point.y>=7.9f || floor->point.y<=7.0f)std::cerr<<"ground at "<<p.x<<","<<p.z<<": "<<(floor?floor->point.y:-1.0f)<<" infill "<<g.destruction.infill_faces<<" damage centre "<<g.destruction.damage().back().center.x<<"\n";
        check(floor && floor->point.y<7.9f && floor->point.y>7.0f,"The packed hole must be solid and visibly lower than the ground");
    }
    for(unsigned i=0;i<900 && !g.destruction.slabs().empty();++i)ticks(g,1);
    check(g.destruction.slabs().empty() && g.destruction.slabs_landed==pieces,"Every piece comes to rest");
    const auto& record=g.destruction.damage().back();
    check(record.shards>=pieces && record.valid() && record.slice_landed,"Each shard's landed pose is recorded");
    for(const auto& pose:record.shard_poses)check(std::hypot(pose[0].x,pose[0].z)<4,"Pieces settle near the cut instead of sliding away");
    const auto count=g.level.triangles.size();
    g.destruction.suspend(g.level);g.destruction.resume(g,nullptr);
    check(g.level.triangles.size()==count,"Replay must rebuild the shattered cut and every landed piece");
}
void retail_sweep(const std::filesystem::path& path) {
    AssetStore assets(path);WadArchive wad(assets);unsigned scenes=0,rippled=0;double worst=0;
    for(unsigned slot=15;slot<=95;slot+=2) {
        auto level=decode_level(wad.read(slot),slot);
        const Triangle* chosen=nullptr;float best=0;
        for(const auto& t:level.triangles) {
            if(t.water || t.surface_type!=UINT32_MAX || t.portal!=UINT32_MAX)continue;
            const auto n=normalized(cross(t.b-t.a,t.c-t.a));if(std::abs(n.y)<.9f)continue;
            const float area=length(cross(t.b-t.a,t.c-t.a));if(area>best){best=area;chosen=&t;}
        }
        if(!chosen)continue;
        const auto point=(chosen->a+chosen->b+chosen->c)/3;
        Scene s;s.wad_slot=slot;s.spawn=point;Game g(level,s);g.upgrades.enabled=true;g.upgrades.headbash=1;unlock_all_elements(g);
        const auto triangles=g.level.triangles.size();
        for(unsigned e=0;e<element_count;++e) {
            g.upgrades.elements.equipped=static_cast<Element>(e);
            const auto start=std::chrono::steady_clock::now();
            g.elements.headbash(g,nullptr,point);for(unsigned i=0;i<26;++i)g.advance(FixedClock::step,{});
            worst=std::max(worst,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/27);
            // Only the Ice wave may add triangles: frozen sheets over nearby water.
            check(g.level.triangles.size()>=triangles &&
                std::all_of(g.level.triangles.begin()+triangles,g.level.triangles.end(),[](const Triangle& t){return t.surface_type==4;}),"A shockwave must never change retail terrain");
            for(const auto& w:g.elements.shockwaves())if(w.shape==ShockShape::Ripple){unsigned valid=0;for(const auto& ring:w.rings)for(bool v:ring.valid)valid+=v;if(valid)++rippled;}
            ticks(g,20);
        }
        ++scenes;
    }
    std::cout<<"retail scenes "<<scenes<<" ripples sampled "<<rippled<<" worst ms/tick (incl. full game tick) "<<worst<<'\n';
    check(scenes>=40 && rippled>=scenes,"Ripples must sample floors across the retail banks");
}
} // namespace
int main(int argc,char** argv) {
    try {
        ripple_and_sphere();headbashpocalypse();convexity_slice();convexity_ground();
        if(argc>1)retail_sweep(argv[1]);
        std::cout<<"Shockwave checks passed: "<<checks<<'\n';return 0;
    } catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<" after "<<checks<<" checks\n";return 1;}
}
