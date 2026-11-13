#include "spyro/runtime.hpp"
#include "spyro/savegame.hpp"
#include <iostream>
#include <chrono>
#include <fstream>

using namespace spyro;
namespace {
unsigned checks{};
void check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
void ticks(Game& g,unsigned n){for(unsigned i=0;i<n;++i)g.advance(FixedClock::step,{});}
Level floor_level() {
    Level l;l.wad_slot=17;l.spawn={-12,8,-12};l.bounds={{-15,0,-15},{15,20,15}};
    l.triangles={{{-15,8,-15},{15,8,15},{15,8,-15},0},{{-15,8,-15},{-15,8,15},{15,8,15},1}};
    for(const auto& t:l.triangles)for(auto p:{t.a,t.b,t.c})l.visual_vertices.push_back({p,{.8f,.7f,.6f},p.x/30,p.z/30,0});
    l.terrain_materials.push_back({});return l;
}
Game game(const Level& l,unsigned tier=2) {
    Scene scene;scene.wad_slot=l.wad_slot;scene.spawn=l.spawn;Game g(l,scene);
    g.player.position=g.player.previous_position={0,8,0};g.player.grounded=true;
    g.upgrades.enabled=true;g.upgrades.headbash=tier;g.upgrades.headbash_mode=tier==2?HeadbashMode::Lava:HeadbashMode::Crater;return g;
}
float height(const Level& l,float x,float z) {
    const auto h=l.raycast({x,15,z},{0,-1,0},30);check(bool(h),"Deformation opened a collision crack");return h->point.y;
}
void basic() {
    const auto original=floor_level();
    auto base=game(original,0);check(!base.headbash.impact(base,nullptr) && base.level.triangles.size()==original.triangles.size(),"Locked headbash altered terrain");
    base.upgrades.headbash=2;base.upgrades.enabled=false;check(!base.headbash.impact(base,nullptr),"Disabled progression altered terrain");
    base.upgrades.enabled=true;base.upgrades.headbash_mode=HeadbashMode::Normal;check(!base.headbash.impact(base,nullptr),"Normal headbash altered terrain");
    for(unsigned tier:{1u,2u}) {
        auto g=game(original,tier);InputState jump;jump.jump_pressed=true;g.advance(FixedClock::step,jump);ticks(g,10);
        InputState bash;bash.headbash_pressed=true;g.advance(FixedClock::step,bash);ticks(g,150);
        check(g.player.headbash_impacts==1 && g.headbash.stamps().size()==1,"Real headbash input did not carve once on landing");
        check(g.level.triangles.size()>100 && g.level.visual_vertices.size()>300,"Large source polygons were not tessellated");
        check(g.player.grounded && std::abs(g.player.position.y-8)<.005f && g.player.health==3,"Impact damaged or lowered Spyro's safe island");
        std::cout<<"tier "<<tier<<" center "<<g.headbash.stamps()[0].center.x<<","<<g.headbash.stamps()[0].center.y<<","<<g.headbash.stamps()[0].center.z<<" floor "<<height(g.level,2.52f,0)<<"\n";
        check(height(g.level,2.52f,0)<8-(tier==2?1.3f:.7f),"Crater has no usable depth");
        check(std::abs(height(g.level,6,0)-8)<.005f,"Blast changed terrain outside its footprint");
        for(unsigned n=0;n<48;++n){const float angle=n*6.2831853f/48;
            const auto p=Vec3{std::sin(angle)*HeadbashTerrain::safe_radius,8,std::cos(angle)*HeadbashTerrain::safe_radius};
            check(std::abs(height(g.level,p.x,p.z)-8)<.005f && !g.headbash.burning(p),"Safe circular island edge was damaged");}
        Level visual;for(std::size_t i=0;i<g.level.visual_vertices.size();i+=3)visual.triangles.push_back({g.level.visual_vertices[i].position,g.level.visual_vertices[i+1].position,g.level.visual_vertices[i+2].position});
        for(float x=-4.8f;x<4.8f;x+=.217f)for(float z=-4.8f;z<4.8f;z+=.233f)
            check(std::abs(height(g.level,x,z)-height(visual,x,z))<.004f,"Visible crater and collision disagree");
        const auto count=g.level.triangles.size();g.headbash.impact(g,nullptr);
        check(g.headbash.stamps().size()==1 && g.level.triangles.size()==count,"Repeated impact kept excavating the same island");
        ticks(g,60);
        if(tier==2) {
            check(!g.headbash.lava().empty(),"Lava variant has no floor surface");
            const auto& f=g.headbash.lava().front();auto p=(f.vertices[0]+f.vertices[1]+f.vertices[2])/3;
            check(g.headbash.burning(p) && !g.headbash.burning(p+Vec3{0,1,0}),"Lava damage does not respect floor height");
            g.player.position=g.player.previous_position=p;g.player.velocity={};g.player.grounded=true;
            ticks(g,2);check(g.player.health==2,"Lava floor failed to damage Spyro");
        } else check(g.headbash.lava().empty(),"Crater-only tier created lava");
        GameSave save;save.capture(g);const auto path=std::filesystem::temp_directory_path()/"spyro-headbash-test.s2save";save.write(path);
        auto loaded=GameSave::read(path);check(loaded.upgrades.headbash==tier && loaded.levels.at(17).craters.size()==1,"Save lost headbash tier or crater history");
        auto restored=game(original);loaded.apply(restored,nullptr);
        check(restored.level.triangles.size()==g.level.triangles.size() && std::abs(height(restored.level,2.52f,0)-height(g.level,2.52f,0))<.001f,"Loading did not reconstruct the crater");
        loaded.apply(restored,nullptr);check(restored.level.triangles.size()==g.level.triangles.size(),"Loading the same save twice compounded deformation");
        std::filesystem::remove(path);
    }
    UpgradeProgress u;u.enabled=true;u.points=75;
    check(u.buy(Upgrade::Headbash) && u.headbash_mode==HeadbashMode::Crater,"First purchase did not equip crater mode");
    check(u.buy(Upgrade::Headbash) && u.headbash_mode==HeadbashMode::Lava && !u.points && !u.buy(Upgrade::Headbash),"Second purchase/cap failed");
    auto promoted=game(original,1);promoted.headbash.impact(promoted,nullptr);
    promoted.upgrades.headbash=2;promoted.upgrades.headbash_mode=HeadbashMode::Lava;
    check(promoted.headbash.impact(promoted,nullptr) && promoted.headbash.stamps().size()==1 && !promoted.headbash.lava().empty() && height(promoted.level,2.52f,0)<6.7f,"Lava cannot replace a crater made with the first tier");
    ticks(promoted,60);const auto molten=promoted.headbash.lava().front();const auto floor_point=(molten.vertices[0]+molten.vertices[1]+molten.vertices[2])/3;
    promoted.headbash.impact(promoted,nullptr);check(promoted.headbash.burning(floor_point),"Repeating a headbash temporarily extinguished existing lava");
}
void protection() {
    auto level=floor_level();
    level.triangles.push_back({{1.8f,8,-.6f},{3,8,.6f},{3,8,-.6f},2,false,3});
    level.triangles.push_back({{1.8f,8,-.6f},{1.8f,8,.6f},{3,8,.6f},3,false,3});
    level.triangles.push_back({{-3,8,-.6f},{-1.8f,8,.6f},{-1.8f,8,-.6f},4});
    TerrainTranslation platform;platform.triangles={4};platform.group=1;level.terrain_translations.push_back(platform);
    auto g=game(level);check(g.headbash.impact(g,nullptr),"Protected contacts suppressed every unrelated floor");
    check(std::abs(height(g.level,2.4f,0)-8)<.001f,"Special headbash puzzle surface moved");
    check(length(g.level.triangles[4].a-level.triangles[4].a)<.001f && g.level.triangles[2].surface_type==3,"Animated indices or surface tags changed");
    check(!g.headbash.burning({2.4f,8,0}),"Lava covered a protected gameplay surface");
    g.player.position={0,20,0};check(!g.headbash.impact(g,nullptr),"Airborne headbash carved a remote floor");
    auto water=floor_level();for(auto& t:water.triangles)t.water=true;
    auto wet=game(water);check(!wet.headbash.impact(wet,nullptr) && wet.level.triangles.size()==water.triangles.size(),"Water was deformed into lava");
}
void retail_levels(const std::filesystem::path& path) {
    AssetStore assets(path);WadArchive wad(assets);unsigned deformed=0;double worst=0;
    for(unsigned slot=15;slot<=95;slot+=2) {
        const auto level=decode_level(wad.read(slot),slot);std::vector<std::pair<float,Vec3>> candidates;
        for(const auto& t:level.triangles)if(!t.water && t.surface_type==UINT32_MAX && t.portal==UINT32_MAX && std::abs(normalized(cross(t.b-t.a,t.c-t.a)).y)>.9f)
            candidates.push_back({length(cross(t.b-t.a,t.c-t.a)),(t.a+t.b+t.c)/3});
        std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.first>b.first;});
        auto g=game(level);bool changed=false;
        for(unsigned i=0;i<std::min<std::size_t>(candidates.size(),12);++i) {
            g.player.position=candidates[i].second;
            const auto start=std::chrono::steady_clock::now();changed=g.headbash.impact(g,nullptr);
            const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();worst=std::max(worst,ms);
            if(changed)break;
        }
        if(changed)++deformed;else std::cout<<"No eligible sampled floor in slot "<<slot<<'\n';
        for(unsigned i=0;i<level.triangles.size();++i)if(level.triangles[i].water || level.triangles[i].surface_type!=UINT32_MAX || level.triangles[i].portal!=UINT32_MAX)
            check(length(g.level.triangles[i].a-level.triangles[i].a)<.0001f && length(g.level.triangles[i].b-level.triangles[i].b)<.0001f && length(g.level.triangles[i].c-level.triangles[i].c)<.0001f,"Retail special surface changed");
        for(const auto& t:g.level.triangles)check(finite(t.a) && finite(t.b) && finite(t.c),"Deformed retail geometry is not finite");
    }
    std::cout<<"Retail scenes with an eligible crater: "<<deformed<<"/41; worst impact CPU "<<worst<<" ms\n";
    check(deformed>=30,"Most retail scenes rejected ordinary floors");
    const auto archive=wad.read(17),exe=assets.read_all("SCUS_944.25");
    const auto source=decode_level(archive,17);const auto models=decode_object_models(archive,exe);
    const auto spyro=decode_spyro(wad.read(0),archive,exe,wad.read(29),29);
    for(unsigned type:{0xc8u,0xc9u,0xd1u}) {
        const auto found=std::find_if(source.objects.begin(),source.objects.end(),[&](const auto& o){return o.type==type;});
        if(found==source.objects.end())continue;
        auto arena=floor_level();auto object=*found;object.id=0;object.position=object.previous_position={0,8,0};
        object.container->rest_position=object.position;arena.objects={object};
        Scene scene;scene.wad_slot=17;scene.spawn={0,12,0};Game strike(arena,scene,{},&models,&spyro);
        strike.upgrades.enabled=true;strike.upgrades.headbash=2;strike.upgrades.headbash_mode=HeadbashMode::Lava;
        InputState bash;bash.headbash_pressed=true;strike.advance(FixedClock::step,bash);ticks(strike,150);
        check(strike.retail_objects[0].state<0 && strike.player.headbash_impacts==1,"Upgraded headbash lost its original retail container hit");
    }
}
}
int main(int argc,char** argv){try{basic();protection();if(argc>1)retail_levels(argv[1]);std::cout<<checks<<" headbash checks passed\n";return 0;}
catch(const std::exception& e){std::cerr<<"Headbash check "<<checks<<": "<<e.what()<<'\n';return 1;}}
