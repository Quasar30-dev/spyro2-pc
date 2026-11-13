#include "spyro/runtime.hpp"
#include "spyro/savegame.hpp"
#include "spyro/powerup_import.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace spyro;
namespace {
unsigned checks{};
void check(bool ok,const char* reason){++checks;if(!ok)throw std::runtime_error(reason);}
void ticks(Game& g,unsigned n,InputState in={}){for(unsigned i=0;i<n;++i)g.advance(FixedClock::step,in);}
Level room() {
    Level l;l.wad_slot=17;l.spawn={-10,0,-10};l.bounds={{-16,0,-16},{16,8,16}};
    l.triangles={{{-16,0,-16},{16,0,16},{16,0,-16},0},{{-16,0,-16},{-16,0,16},{16,0,16},1},
        {{-8,0,4},{8,0,4},{8,8,4},2},{{-8,0,4},{8,8,4},{-8,8,4},3}};
    for(unsigned i=0;i<2;++i)for(auto p:{l.triangles[i].a,l.triangles[i].b,l.triangles[i].c})
        l.visual_vertices.push_back({p,{.8f,.7f,.6f},(p.x+16)/32,(p.z+16)/32,0});
    // Visible wall has the opposite diagonal to its collision mesh.
    for(auto p:{Vec3{-8,0,4},Vec3{8,0,4},Vec3{-8,8,4},Vec3{8,0,4},Vec3{8,8,4},Vec3{-8,8,4}})
        l.visual_vertices.push_back({p,{.8f,.7f,.6f},(p.x+8)/16,p.y/8,0});
    l.terrain_materials.push_back({});return l;
}
Game make_game(const Level& l,const ObjectModels* objects=nullptr,const SpyroModel* model=nullptr) {
    Scene s;s.wad_slot=l.wad_slot;s.spawn=l.spawn;Game g(l,s,{},objects,model);
    g.player.position=g.player.previous_position={0,0,0};g.player.grounded=true;
    g.destruction_settings.enabled=true;return g;
}
Level visible(const Level& l) {
    Level v;for(unsigned i=0;i+2<l.visual_vertices.size();i+=3)v.triangles.push_back({l.visual_vertices[i].position,l.visual_vertices[i+1].position,l.visual_vertices[i+2].position});return v;
}
void wall_agreement(const Level& l,float lo,float hi) {
    const auto v=visible(l);
    for(float x=-3;x<3;x+=.153f)for(float y=lo;y<hi;y+=.147f) {
        const auto a=l.raycast({x,y,0},{0,0,1},8),b=v.raycast({x,y,0},{0,0,1},8);
        check(bool(a)==bool(b),"Visible damage and collision have different openings");
        if(a && b)check(length(a->point-b->point)<.006f,"Dent or blast rim has a collision seam");
    }
}
void same_geometry(const Level& a,const Level& b) {
    check(a.triangles.size()==b.triangles.size() && a.visual_vertices.size()==b.visual_vertices.size(),"Restoring damage changed mesh counts");
    for(unsigned i=0;i<a.triangles.size();++i)check(length(a.triangles[i].a-b.triangles[i].a)<.0001f && length(a.triangles[i].b-b.triangles[i].b)<.0001f && length(a.triangles[i].c-b.triangles[i].c)<.0001f && a.triangles[i].disabled==b.triangles[i].disabled,"Restoring damage changed collision geometry");
    for(unsigned i=0;i<a.visual_vertices.size();++i)check(length(a.visual_vertices[i].position-b.visual_vertices[i].position)<.0001f && std::abs(a.visual_vertices[i].u-b.visual_vertices[i].u)<.0001f && std::abs(a.visual_vertices[i].v-b.visual_vertices[i].v)<.0001f,"Restoring damage changed visible geometry or UVs");
}
void geometry() {
    const auto base=room();auto g=make_game(base);g.destruction_settings.enabled=false;
    check(!g.destruction.impact(g,{0,3,4},{0,0,-1},TerrainAttack::GreenBomb,nullptr),"Destruction is not opt-in");same_geometry(g.level,base);
    g.destruction_settings.enabled=true;g.destruction_settings.bomb=false;
    check(!g.destruction.impact(g,{0,3,4},{0,0,-1},TerrainAttack::GreenBomb,nullptr),"Disabled bomb damage carved terrain");
    g.destruction_settings.bomb=true;
    check(g.destruction.impact(g,{0,3,4},{0,0,-1},TerrainAttack::Supercharge,nullptr),"Supercharge cannot deform a wall");
    const auto dent=g.level.raycast({0,3,0},{0,0,1},8);
    check(dent && dent->point.z>4.2f && !g.destruction.cracks().empty() && g.destruction.debris().size()==6,"Charge dent has no depth, cracks or chips");
    wall_agreement(g.level,1.5f,4.5f);
    for(unsigned i=6;i<g.level.visual_vertices.size();++i) {
        const auto& v=g.level.visual_vertices[i];
        if(v.position.z<3.5f)continue;
        check(std::abs(v.u-(v.position.x+8)/16)<.00001f && std::abs(v.v-v.position.y/8)<.00001f,"Wall dent broke shared texture scale/alignment");
    }
    g.destruction.clear(g.level);same_geometry(g.level,base);
    for(const auto attack:{TerrainAttack::Superflame,TerrainAttack::GreenBomb}) {
        auto blast=make_game(base);check(blast.destruction.impact(blast,{0,3,4},{0,0,-1},attack,nullptr),"Powered blast cannot shear a wall");
        check(!blast.level.raycast({0,3,0},{0,0,1},8),"Blast hole still has collision");
        check(bool(blast.level.raycast({3,3,0},{0,0,1},8)),"Blast deleted an entire building face");
        check(blast.destruction.debris().size()==24 && !blast.destruction.dust().empty(),"Blast emitted no textured chunks or dust");
        wall_agreement(blast.level,.5f,5.5f);
        for(const auto& d:blast.destruction.debris()) {
            check(d.vertices.size()==24 && d.radius<.8f,"Debris lacks a closed bounded mesh");
            for(const auto& v:d.vertices)check(finite(v.position) && length(v.position)<=d.radius+.001f && std::isfinite(v.u) && std::isfinite(v.v),"Debris mesh exceeds its collider or loses UVs");
        }
        Vec3 velocity{0,0,10};bool ground=false;
        const auto crossed=blast.level.resolve_sphere({0,3,4},.3f,velocity,ground);
        check(length(crossed-Vec3{0,3,4})<.0001f,"Spyro cannot pass through a blasted opening");
        const auto count=blast.destruction.debris().size();blast.set_paused(true);ticks(blast,20);
        check(blast.destruction.debris().size()==count && !blast.destruction.debris()[0].age,"Pause advances debris physics");
        blast.set_paused(false);ticks(blast,330);check(blast.destruction.debris().empty() && blast.destruction.dust().empty(),"Transient debris never retires");
    }
    auto flame=make_game(base),bomb=make_game(base);
    flame.destruction.impact(flame,{0,3,4},{0,0,-1},TerrainAttack::Superflame,nullptr);
    bomb.destruction.impact(bomb,{0,3,4},{0,0,-1},TerrainAttack::GreenBomb,nullptr);
    check(flame.level.raycast({1.6f,3,0},{0,0,1},8) && !bomb.level.raycast({1.6f,3,0},{0,0,1},8),"Green bomb does not create a larger hole than superflame");
    auto small=make_game(base);small.destruction.impact(small,{0,3,4},{0,0,-1},TerrainAttack::Superflame,nullptr,true);
    check(bool(small.level.raycast({.9f,3,0},{0,0,1},8)),"Split flames have full-size destructive blasts");
    auto join=make_game(base);join.destruction.impact(join,{0,.35f,4},{0,0,-1},TerrainAttack::Supercharge,nullptr);
    const auto foot_join=join.level.raycast({0,.001f,0},{0,0,1},8);
    check(foot_join && std::abs(foot_join->point.z-4)<.0001f,"Wall dent detached from its adjoining floor");wall_agreement(join.level,.01f,1.4f);
    auto cliff=base;cliff.triangles.erase(cliff.triangles.begin(),cliff.triangles.begin()+2);cliff.visual_vertices.erase(cliff.visual_vertices.begin(),cliff.visual_vertices.begin()+6);
    auto hollow=make_game(cliff);hollow.destruction.impact(hollow,{0,3,4},{0,0,-1},TerrainAttack::GreenBomb,nullptr);
    const auto inside=hollow.level.raycast({0,3,0},{0,0,1},8);
    check(inside && inside->point.z>5 && inside->point.z<6,"Destroying solid terrain exposed the sky instead of a textured cavity");wall_agreement(hollow.level,.5f,5.5f);
    for(auto attack:{TerrainAttack::Superflame,TerrainAttack::GreenBomb}) {
        auto low=make_game(base);low.destruction.impact(low,{0,.7f,4},{0,0,-1},attack,nullptr);
        const auto drawn=visible(low.level);
        for(float z=2;z<6;z+=.17f)for(float x=-1.5f;x<1.5f;x+=.19f) {
            const auto floor=low.level.raycast({x,.01f,z},{0,-1,0},.02f),image=drawn.raycast({x,.01f,z},{0,-1,0},.02f);
            check(floor && image && std::abs(floor->point.y)<.001f && std::abs(image->point.y)<.001f,
                "Wall blast removed or detached the floor beneath its passage");
        }
        check(!low.level.raycast({0,.7f,0},{0,0,1},8),"Preserving the floor sealed the wall opening");
    }
}
void protected_geometry() {
    for(unsigned kind=0;kind<4;++kind) {
        auto l=room();for(unsigned i=2;i<4;++i){if(kind==0)l.triangles[i].water=true;else if(kind==1)l.triangles[i].portal=0;else if(kind==2)l.triangles[i].surface_type=3;}
        if(kind==3){TerrainTranslation m;m.triangles={2,3};m.regions={0};l.terrain_translations.push_back(m);}
        auto g=make_game(l);check(!g.destruction.impact(g,{0,3,4},{0,0,-1},TerrainAttack::GreenBomb,nullptr),"Gameplay wall/animated surface was destroyed");same_geometry(g.level,l);
        g.destruction_settings.protect_gameplay=false;
        check(g.destruction.impact(g,{0,3,4},{0,0,-1},TerrainAttack::GreenBomb,nullptr)==(kind==2),"Surface protection override damaged water, portals or animated indices");
    }
    auto l=room();RetailObject o;o.position={0,3,4};o.powerup_gate=PowerupGate{3,120,0};l.objects.push_back(o);
    auto g=make_game(l);g.destruction.impact(g,{0,3,4},{0,0,-1},TerrainAttack::GreenBomb,nullptr);
    const auto gate=g.level.raycast({0,3,0},{0,0,1},8);check(gate && std::abs(gate->point.z-4)<.001f,"Blast destroyed a power gate support");wall_agreement(g.level,.5f,5.5f);
    auto floor=make_game(room());
    floor.destruction.impact(floor,{0,0,0},{0,1,0},TerrainAttack::GreenBomb,nullptr);
    check(bool(floor.level.raycast({0,2,0},{0,-1,0},4)),"Owner's safe footing was destroyed");
    auto platform=room();platform.triangles.resize(2);platform.visual_vertices.resize(6);
    RetailObject gem;gem.type=1;gem.gem_value=5;gem.position=gem.previous_position={5,0,-5};platform.objects.push_back(gem);
    auto excavation=make_game(platform);excavation.destruction.impact(excavation,{5,0,-5},{0,1,0},TerrainAttack::GreenBomb,nullptr);
    check(excavation.retail_objects[0].position.y< -1 && std::abs(excavation.retail_objects[0].position.y-excavation.retail_objects[0].previous_position.y)<.0001f,"Floor destruction left a supported gem floating");
    for(const auto& v:excavation.level.visual_vertices)
        check(std::abs(v.u-(v.position.x+16)/32)<.00001f && std::abs(v.v-(v.position.z+16)/32)<.00001f,"Excavated floor lost material scale/alignment across UV charts");
    GameSave supported_save;supported_save.capture(excavation);auto returned=make_game(platform);supported_save.apply(returned,nullptr);
    check(length(returned.retail_objects[0].position-excavation.retail_objects[0].position)<.0001f,"Loading terrain damage lost the relocated collectible");
    returned.destruction.clear(returned);check(std::abs(returned.retail_objects[0].position.y)<.0001f,"Restoring terrain buried the relocated collectible");same_geometry(returned.level,platform);
    ObjectModels support_models;
    excavation.player.position={1.5f,0,-5};excavation.upgrades.enabled=true;excavation.upgrades.headbash=2;excavation.upgrades.headbash_mode=HeadbashMode::Lava;
    check(excavation.headbash.impact(excavation,&support_models),"Headbash did not rebuild an excavated collectible floor");
    const auto support=excavation.level.raycast({5,2,-5},{0,-1,0},8);
    check(support && std::abs(excavation.retail_objects[0].position.y-support->point.y)<.001f,"Headbash replay left an excavated collectible below its floor");
    excavation.destruction.clear(excavation);const auto reset_support=excavation.level.raycast({5,2,-5},{0,-1,0},8);
    check(reset_support && excavation.headbash.stamps().size()==1 && std::abs(excavation.retail_objects[0].position.y-reset_support->point.y)<.001f,"Restoring destruction lost headbash support or its collectible");
    TerrainDamage bad;bad.normal={};check(!bad.valid(),"Checkpoint accepts an invalid impact normal");
}
void recoil(const SpyroModel* model=nullptr) {
    for(bool destructive:{false,true})for(unsigned rate:{30u,60u,144u}) {
        auto g=make_game(room(),nullptr,model);g.destruction_settings.enabled=destructive;g.cheats.supercharge=destructive;
        InputState in;in.charge=true;in.move_z=1;unsigned count=0;
        while(!g.player.charge_impacts && count++<rate*3)g.advance(1.0/rate,in);
        check(g.player.charge_impacts==1 && g.player.motion==Motion::ChargeRecoil && g.player.velocity.z<0,"Head-on charge did not recoil backwards");
        check(g.player.health==3 && g.player.recoil_ticks>0 && motion_animation(g.player.motion)==13,"Wall recoil uses the wrong clip or hurts Spyro");
        check(destructive?g.destruction.damage().size()==1:g.destruction.damage().empty(),"Optional supercharge deformation was not respected");
        if(model) {bool saw=false;for(unsigned i=0;i<12;++i){ticks(g,1);const auto& a=g.animation.state();saw|=a.clip==13 || a.next_clip==13;const auto pose=g.animation.sample(1);check(!pose.vertices.empty() && std::all_of(pose.vertices.begin(),pose.vertices.end(),[](auto p){return finite(p);}),"Retail recoil animation failed to sample");}check(saw,"Retail recoil never reached the visible animator");}
        ticks(g,55);check(!g.player.recoil_ticks && g.player.motion!=Motion::ChargeRecoil,"Charge collision recovery is stuck");
    }
    auto g=make_game(room());InputState in;in.charge=true;in.move_x=1;in.move_z=.05f;ticks(g,45,in);
    check(!g.player.charge_impacts,"Glancing ground traversal falsely triggered wall recoil");
}
void persistence() {
    auto base=room();auto g=make_game(base);
    g.destruction_settings.strength=2;g.destruction_settings.flame=false;
    g.destruction.impact(g,{0,3,4},{0,0,-1},TerrainAttack::Supercharge,nullptr);
    g.destruction.impact(g,{3,3,4},{0,0,-1},TerrainAttack::GreenBomb,nullptr);
    g.upgrades.enabled=true;g.upgrades.headbash=2;g.upgrades.headbash_mode=HeadbashMode::Lava;
    check(g.headbash.impact(g,nullptr) && g.destruction.damage().size()==2,"Headbash dropped existing wall destruction");
    GameSave save;save.capture(g);const auto path=std::filesystem::temp_directory_path()/("spyro-destruction-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".s2save");save.write(path);
    auto loaded=GameSave::read(path);check(loaded.destruction_settings==g.destruction_settings && loaded.levels.at(17).terrain_damage.size()==2,"Save lost impact history or settings");
    auto restored=make_game(base);loaded.apply(restored,nullptr);same_geometry(restored.level,g.level);
    loaded.apply(restored,nullptr);same_geometry(restored.level,g.level);
    check(restored.destruction.debris().empty() && restored.destruction.cracks().size()==g.destruction.cracks().size(),"Loading replayed particles or lost persistent cracks");
    restored.destruction.clear(restored.level);check(restored.headbash.stamps().size()==1 && !restored.headbash.lava().empty() && restored.level.raycast({3,3,0},{0,0,1},8),"Restoring walls removed the independent headbash layer");
    // Capture another level, then return to the first without losing its damage.
    auto next=room();next.wad_slot=19;auto next_game=make_game(next);next_game.destruction_settings=loaded.destruction_settings;
    loaded.capture(next_game);auto back=make_game(base);loaded.apply(back,nullptr);same_geometry(back.level,g.level);
    // A version 14 save is the same payload up to the new destruction footer.
    std::ifstream input(path,std::ios::binary);std::string header,payload;std::getline(input,header);payload.assign(std::istreambuf_iterator<char>(input),{});
    input.close();const auto footer=payload.rfind("1 1 0 1 1 2\n");check(footer!=std::string::npos,"Could not find new save extension for backward compatibility fixture");payload.resize(footer);
    const auto crc=crc32({reinterpret_cast<const std::uint8_t*>(payload.data()),payload.size()});
    {std::ofstream output(path,std::ios::binary|std::ios::trunc);output<<"S2SAVE 14 "<<crc<<' '<<payload.size()<<'\n'<<payload;}
    const auto legacy=GameSave::read(path);check(!legacy.destruction_settings.enabled && legacy.levels.at(17).terrain_damage.empty() && legacy.levels.at(17).craters.size()==1,"Existing saves lost compatibility");std::filesystem::remove(path);
}
bool capture_plan(const Level& level,const ObjectModels& models,const SpyroModel& spyro,const std::filesystem::path& folder,bool passage=false) {
    const auto drawn=visible(level);
    for(const auto& t:level.triangles) {
        auto n=normalized(cross(t.b-t.a,t.c-t.a));if(t.water || t.portal!=UINT32_MAX || t.surface_type!=UINT32_MAX || std::abs(n.y)>.15f || length(cross(t.b-t.a,t.c-t.a))<4)continue;
        const auto p=(t.a+t.b+t.c)/3;
        for(float side:{1.0f,-1.0f}) {
            const auto outward=n*side,approach=p+outward*2.8f;
            const auto floor=level.raycast(approach+Vec3{0,4,0},{0,-1,0},16);
            if(!floor || floor->normal.y<.7f)continue;
            const auto foot=floor->point+Vec3{0,.01f,0};
            const auto visible_floor=drawn.raycast(foot+Vec3{0,.2f,0},{0,-1,0},.4f);
            if(passage && (!visible_floor || std::abs(visible_floor->point.y-floor->point.y)>.05f))continue;
            if(passage && level.raycast(foot+Vec3{0,1.5f,0},outward,4.2f))continue;
            if(passage && std::max({t.a.y,t.b.y,t.c.y})<foot.y+2.8f)continue;
            const auto wall=level.raycast(foot+retail::origin_above_feet,outward*-1,3.8f);
            if(!wall || std::abs(wall->normal.y)>.15f)continue;
            if(passage && !drawn.raycast(wall->point+outward*.15f,outward*-1,.4f))continue;
            auto trial=make_game(level,&models,&spyro);trial.player.position=trial.player.previous_position=foot;
            trial.player.yaw=std::atan2(-outward.x,-outward.z);InputState input;
            if(passage) {
                trial.cheats.green_firebomb=true;input.flame_pressed=true;ticks(trial,1,input);ticks(trial,100);
                if(trial.destruction.damage().empty())continue;
                const auto hit=trial.destruction.damage().front().center;
                const auto inside=level.raycast(hit-outward*2.55f+Vec3{0,.2f,0},{0,-1,0},6);
                if(!inside || trial.level.raycast(hit+outward*.15f,outward*-1,2.1f) || visible(trial.level).raycast(hit+outward*.15f,outward*-1,2.1f))continue;
                InputState move;move.move_x=-outward.x*.5f;move.move_z=-outward.z*.5f;
                bool supported=true;
                for(unsigned tick=0;tick<120;++tick) {
                    move.jump_pressed=tick==0;move.jump_held=tick<30;ticks(trial,1,move);
                    if(trial.player.grounded) {
                        const auto shown=visible(trial.level).raycast(trial.player.position+Vec3{0,.2f,0},{0,-1,0},.4f);
                        if(!shown || std::abs(shown->point.y-trial.player.position.y)>.08f){supported=false;break;}
                    }
                }
                if(!supported)continue;
                if(dot(trial.player.position-wall->point,outward)>-1 || !trial.player.grounded)continue;
            } else {
                trial.cheats.supercharge=true;input.charge=true;ticks(trial,80,input);
                if(!trial.player.charge_impacts || trial.destruction.damage().empty())continue;
            }
            const auto tangent=cross(Vec3{0,1,0},outward),eye=foot+outward*3.6f+tangent*1.6f+Vec3{0,2.2f,0},target=wall->point+Vec3{0,.55f,0};
            std::filesystem::create_directories(folder);
            for(unsigned kind=0;kind<4;++kind) {
                const auto file=folder/((passage?std::string("building-"):std::string("wall-"))+(kind==0?"before.s2shot":kind==1?"charge.s2shot":kind==2?"superflame.s2shot":"bomb.s2shot"));
                std::ofstream out(file);out.precision(9);const unsigned frames=kind==0?8:180;
                out<<"S2SHOT 3 1280 720 "<<frames<<' '<<foot.x<<' '<<foot.y<<' '<<foot.z<<' '<<trial.player.yaw<<" 0 50 1\n0\n";
                if(kind==0)out<<"0\n";else if(kind==1)out<<"1\n0 60 1 0 0 0 0 0 0 0 0 0 0 0\n";else out<<"1\n15 16 0 0 1 0 0 0 0 0 0 0 0 0\n";
                out<<"2\n0 "<<eye.x<<' '<<eye.y<<' '<<eye.z<<' '<<target.x<<' '<<target.y<<' '<<target.z<<'\n';
                out<<frames<<' '<<eye.x<<' '<<eye.y<<' '<<eye.z<<' '<<target.x<<' '<<target.y<<' '<<target.z<<'\n';
            }
            std::cout<<"Retail capture plan: slot "<<level.wad_slot<<" wall "<<wall->point.x<<","<<wall->point.y<<","<<wall->point.z<<" spawn "<<foot.x<<","<<foot.y<<","<<foot.z<<'\n';return true;
        }
    }
    return false;
}
void retail_checks(const std::filesystem::path& path,const std::filesystem::path& capture={},bool passage=false,unsigned capture_slot=0) {
    AssetStore store(path);WadArchive wad(store);const auto exe=store.read_all("SCUS_944.25");
    auto model=decode_spyro(wad.read(0),wad.read(17),exe,wad.read(29),29);
    auto models=decode_object_models(wad.read(17),exe);PowerupAssets effects;
    const auto donor=read_retail_powerups(path);import_retail_powerups(model,models,effects,donor);recoil(&model);
    if(!capture.empty()) {
        bool found=false;
        for(unsigned slot:capture_slot?std::vector<unsigned>{capture_slot}:passage?std::vector<unsigned>{15,33,57,61,63,65,67}:std::vector<unsigned>{17}) {
            const auto archive=wad.read(slot);auto bank_models=decode_object_models(archive,exe);
            auto bank_spyro=decode_spyro(wad.read(0),archive,exe,wad.read(29),29);PowerupAssets bank_effects;
            import_retail_powerups(bank_spyro,bank_models,bank_effects,donor);
            if(capture_plan(decode_level(archive,slot),bank_models,bank_spyro,capture,passage)){found=true;break;}
        }
        check(found,"No reachable retail wall found for installed-render validation");
    }
    for(bool bomb:{false,true}) {
        auto g=make_game(room(),&models,&model);g.cheats.green_firebomb=bomb;g.cheats.superflame=!bomb;
        InputState fire;fire.flame_pressed=true;ticks(g,1,fire);unsigned age=0;
        while(g.destruction.damage().empty() && ++age<120)ticks(g,1);
        check(age<120 && g.powers.shots_fired==1 && g.destruction.damage().size()==1,"Real powered projectile did not trigger terrain destruction");
        const auto& hit=g.destruction.damage().front();std::cout<<"Real "<<(bomb?"bomb":"flame")<<" impact "<<hit.center.x<<","<<hit.center.y<<","<<hit.center.z<<" normal "<<hit.normal.x<<","<<hit.normal.y<<","<<hit.normal.z<<'\n';check(hit.attack==(bomb?TerrainAttack::GreenBomb:TerrainAttack::Superflame) && std::abs(hit.center.z-4)<.02f,"Destruction used an explosion's relocated position instead of its wall impact");
        check(!g.destruction.debris().empty() && (bomb?g.powers.bombs_exploded:g.powers.flame_explosions)>0,"Destructive shots lost their retail explosion");
    }
    unsigned scenes=0;double worst=0,physics_worst=0;
    for(unsigned slot=15;slot<=95;slot+=2) {
        auto l=decode_level(wad.read(slot),slot);std::vector<std::pair<float,Triangle>> faces;
        for(const auto& t:l.triangles)if(!t.water && t.surface_type==UINT32_MAX && t.portal==UINT32_MAX && std::abs(normalized(cross(t.b-t.a,t.c-t.a)).y)<.2f)faces.push_back({length(cross(t.b-t.a,t.c-t.a)),t});
        std::sort(faces.begin(),faces.end(),[](auto a,auto b){return a.first>b.first;});auto g=make_game(l);bool changed=false;
        for(unsigned i=0;i<std::min<std::size_t>(24,faces.size());++i) {
            const auto t=faces[i].second;const auto point=(t.a+t.b+t.c)/3;auto n=normalized(cross(t.b-t.a,t.c-t.a));
            g.player.position=point+n*3;const auto start=std::chrono::steady_clock::now();changed=g.destruction.impact(g,point,n,TerrainAttack::GreenBomb,nullptr);
            worst=std::max(worst,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());if(changed)break;
        }
        if(changed){++scenes;const auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<30;++i)g.destruction.tick(g);const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/30;physics_worst=std::max(physics_worst,ms);
            if(std::getenv("DESTRUCTION_TRACE")){const auto& grid=g.level.collision_index();std::cout<<"slot "<<slot<<" tris "<<g.level.triangles.size()<<" always "<<grid.always.size()<<" dynamic "<<g.level.dynamic_triangles.size()<<" cells "<<grid.nx<<"x"<<grid.nz<<" debris "<<g.destruction.debris().size()<<" ms/tick "<<ms<<std::endl;}}
        for(unsigned i=0;i<l.triangles.size();++i)if(l.triangles[i].water || l.triangles[i].surface_type!=UINT32_MAX || l.triangles[i].portal!=UINT32_MAX)
            check(length(l.triangles[i].a-g.level.triangles[i].a)<.0001f && length(l.triangles[i].b-g.level.triangles[i].b)<.0001f && length(l.triangles[i].c-g.level.triangles[i].c)<.0001f,"Destruction altered a retail water/portal/scripted surface");
        for(const auto& t:g.level.triangles)check(finite(t.a) && finite(t.b) && finite(t.c),"Destruction produced non-finite retail collision");
    }
    std::cout<<"Retail eligible scenes "<<scenes<<"/41; worst carve "<<worst<<" ms; worst 24-chunk physics "<<physics_worst<<" ms/tick\n";
    check(scenes>=32,"Most retail levels reject ordinary building damage");
}
}
int main(int argc,char** argv){try{geometry();protected_geometry();recoil();persistence();if(argc>1)retail_checks(argv[1],argc>2?argv[2]:std::filesystem::path{},argc>3 && std::string(argv[3])=="--passage",argc>4?unsigned(std::stoul(argv[4])):0);std::cout<<checks<<" destruction checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"Destruction check "<<checks<<": "<<e.what()<<'\n';return 1;}}
