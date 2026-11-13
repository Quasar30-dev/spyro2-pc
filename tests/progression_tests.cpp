#include "spyro/savegame.hpp"
#include "spyro/hud.hpp"
#include <chrono>
#include <fstream>
#include <iostream>

using namespace spyro;
namespace {
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F> void rejects(F f,const char* why){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,why);}
void ticks(Game& game,unsigned count,InputState input={}){for(unsigned i=0;i<count;++i)game.advance(FixedClock::step,input);}
Level floor_level() {
    Level level;level.spawn={0,8,0};
    level.triangles={{{-40,8,-40},{40,8,40},{40,8,-40}},{{-40,8,-40},{-40,8,40},{40,8,40}}};
    for(const auto& t:level.triangles){level.bounds.add(t.a);level.bounds.add(t.b);level.bounds.add(t.c);}return level;
}
ObjectModels models() {
    ObjectModels m;CameraSettings camera;m.sine=camera.sine;m.cosine=camera.cosine;
    m.gem_values={1,2,5,10,25};m.gem_meshes={0,1,2,3,4};m.models[1].meshes.resize(5);
    for(const unsigned type:{0x1f0,0x22b,0x78}) {
        auto& model=m.models[type];model.animated=true;model.animation_count=7;
        model.cylinder=ObjectCylinder{1824,800,800,3,128,1000};
        for(unsigned c=0;c<7;++c) {
            ObjectAnimation clip;for(unsigned f=0;f<(c==3?25u:10u);++f) {
                ObjectFrame frame;frame.ticks=4;frame.metadata=(3<<9)|(f*2);clip.frames.push_back(frame);
            }
            model.animations.push_back(std::move(clip));
        }
    }
    return m;
}
RetailObject enemy(const ObjectModels& m,bool small=false) {
    RetailObject o;o.type=small?0x1f0:0x22b;o.position=o.previous_position={0,8,2.2f};o.held_gem=255;o.record[83]=1;
    o.rotation[2]=retail::original_heading(std::numbers::pi_v<float>)/16;o.previous_rotation=o.rotation;
    auto& e=o.enemy.emplace();e.kind=small?EnemyKind::SmallLizard:EnemyKind::ClubLizard;e.notice=8;e.reach=3;
    if(small)e.route={{{0,8,4},0},{{0,8,10},0},{{8,8,20},0}};
    o.animation=start_object_animation(m.models.at(o.type));o.previous_animation=o.animation;return o;
}
void save_checks(const std::filesystem::path& file) {
    auto m=models();auto level=floor_level();Scene scene;scene.spawn=level.spawn;
    scene.entities.push_back({4,EntityKind::Gem,{15,8,15},10,true});
    RetailObject gem;gem.id=0;gem.type=1;gem.gem_value=2;gem.held_gem=2;gem.position={12,8,10};gem.state=1;
    RetailObject basket;basket.id=1;basket.type=0xc8;basket.held_gem=5;basket.position={10,8,10};basket.container.emplace();
    RetailObject hunter;hunter.id=2;hunter.type=0x1bc;hunter.position={20,8,20};
    auto lizard=enemy(m);lizard.id=3;level.objects={gem,basket,hunter,lizard};
    NpcDefinition npc;npc.object=2;npc.lines.resize(3);level.npcs.push_back(npc);
    Game game(level,scene,{},&m);game.player.position={2,8,1};game.player.yaw=0.75f;
    game.player.gems=12;game.player.lives=6;game.player.health=1;game.player.orbs=3;game.player.talismans=5;
    game.cheats.superflame=game.cheats.superfly=game.cheats.green_firebomb=true;game.powers.permanent_fire=true;
    game.retail_objects[0].collected=true;game.retail_objects[1].state=-1;game.retail_objects[3].enemy->defeated=true;
    auto pending=make_saved_gem({1,5,{10,10,10}},m,static_cast<unsigned>(game.retail_objects.size()));game.retail_objects.push_back(pending);
    const std::vector<NpcProgress> dialogue{{2,1,true}};game.dialogue.restore(dialogue);game.dialogue.active_camera=false;
    GameSave save;save.capture(game);save.write(file);const auto read=GameSave::read(file);
    check(read.gems==12 && read.health==1 && read.orbs==3 && read.talismans==5,"Save lost campaign inventory");
    check(read.cheats.superflame && read.cheats.superfly && read.cheats.green_firebomb && read.permanent_superflame,"PC save lost power-up cheats or permanent superflame");
    check(read.levels.at(15).removed.size()==3 && read.levels.at(15).pending_gems.size()==1,"Save lost a removed object or pending gem");
    Game loaded(level,scene,{},&m);read.apply(loaded,&m);
    check(length(loaded.player.position-game.player.position)<0.001f && loaded.player.yaw==game.player.yaw,"Saved location changed");
    check(loaded.player.health==1 && loaded.player.lives==6 && loaded.player.gems==12,"Loading reset inventory/health");
    check(loaded.dialogue.progress()[0].node==1 && !loaded.dialogue.active_camera,"Conversation progress/camera preference lost");
    check(loaded.spirits()==1 && loaded.retail_objects[3].state<0,"Saving during a death animation resurrected its enemy");
    check(loaded.level_gems()==12 && loaded.play_scene.entities[0].collected,"HUD total or editor pickup progress changed on load");
    check(!loaded.hud.visible(HudPart::Gems) && loaded.hud.counter(HudPart::Gems).value==12,"Loading replayed already collected gems through the HUD");
    const auto released=std::find_if(loaded.retail_objects.begin(),loaded.retail_objects.end(),[](const auto& o){return o.runtime_spawned && o.type==1;});
    check(released!=loaded.retail_objects.end() && released->source_id==1 && released->state==2,"Sparx overwrote the released gem on load");
    unsigned total=loaded.player.gems;ObjectInteractions physics;
    for(unsigned i=0;i<150;++i)physics.tick(loaded.retail_objects,&m,level,{0,8,0},{},0,{},false,total);
    check(released->state==1 && std::abs(released->position.y-8)<0.02f && total==12,"Saved midair gem did not settle without awarding itself");
    auto bad=read;bad.levels[15].removed[0].type=999;
    rejects([&]{bad.apply(loaded,&m);},"Mismatched object accepted");
    check(loaded.player.gems==12 && length(loaded.player.position-game.player.position)<0.001f,"Rejected save changed the current game");
    bad=read;bad.levels[15].pending_gems[0].source=2;rejects([&]{bad.apply(loaded,&m);},"Released gem accepted an unrelated parent");
    bad=read;bad.health=4;rejects([&]{bad.write(file);},"Invalid save replaced a valid file");
    check(GameSave::read(file).gems==12,"Failed save destroyed previous file");
    save.gems=42;save.write(file);check(GameSave::read(file).gems==42,"Saving twice did not replace the PC save");
    Scene other_scene;other_scene.wad_slot=17;other_scene.spawn=level.spawn;Game other(level,other_scene,{},&m);
    save.apply(other,&m);other.player.gems+=5;save.capture(other);save.write(file);
    const auto multi=GameSave::read(file);multi.apply(loaded,&m);
    check(multi.levels.size()==2 && loaded.player.gems==47 && loaded.retail_objects[0].collected && loaded.spirits()==1,"Cross-level progress was reset or counted twice");
    {std::ofstream damage(file,std::ios::binary|std::ios::app);damage<<'x';}
    rejects([&]{GameSave::read(file);},"Damaged save was accepted");
    save.write(file);std::filesystem::resize_file(file,12);rejects([&]{GameSave::read(file);},"Truncated save accepted");
    save.write(file);bad=save;bad.retail_crc=1;rejects([&]{bad.apply(loaded,&m);},"Wrong retail profile accepted");
    auto wrong=file;wrong.replace_extension(".WAD");rejects([&]{save.write(wrong);},"Retail file extension accepted as a save destination");
}
void enemy_checks() {
    auto m=models();m.models.erase(0x78);auto world=floor_level();Scene scene;scene.spawn=world.spawn;scene.wad_slot=17;
    world.objects={enemy(m)};Game game(world,scene,{},&m);ticks(game,180);
    check(game.player.health==2 && game.player.invulnerable_ticks>0,"Club swing did not damage Spyro in its animation hit window");
    const auto health=game.player.health;check(!game.hurt(world.objects[0].position) && game.player.health==health,"Invulnerability did not prevent repeated damage");
    check(!enemy_accepts_attack(game.retail_objects[0],charge_attack),"Large lizard accepted an ordinary charge");
    game.interactions.attack_sphere(game.retail_objects,m,game.retail_objects[0].position+Vec3{0,0.5f,0},1,flame_attack);ticks(game,1);
    check(game.retail_objects[0].enemy->defeated && game.spirits()==0 && game.player.gems==0,"Flame did not start death without prematurely awarding spirits or inventing gem loot");
    ticks(game,100);check(game.retail_objects[0].state<0 && game.spirits()==1,"Death clip looped or spirit was awarded twice");
    Game charged(world,scene,{},&m);charged.retail_objects[0].attack_flags=0x160000;ticks(charged,1);
    check(charged.retail_objects[0].enemy->defeated,"Supercharge failed to defeat a large lizard");
    auto small=floor_level();small.objects={enemy(m,true)};Game runner(small,scene,{},&m);ticks(runner,180);
    check(length(runner.retail_objects[0].position-small.objects[0].position)>2 && runner.retail_objects[0].animation.clip==2,"Small lizard did not follow its authored route with its run animation");
    runner.retail_objects[0].attack_flags=charge_attack;ticks(runner,1);check(runner.retail_objects[0].enemy->defeated,"Small lizard survived a normal charge");
    auto cliff=small;cliff.objects[0].enemy->route={{{0,8,60},0}};Game ledge(cliff,scene,{},&m);ticks(ledge,1000);
    check(ledge.retail_objects[0].position.z<40 && std::abs(ledge.retail_objects[0].position.y-8)<0.01f,"Enemy walked off a ledge without an authored jump");
    auto wall=world;wall.triangles.push_back({{-4,7,1},{4,7,1},{4,12,1}});wall.triangles.push_back({{-4,7,1},{4,12,1},{-4,12,1}});
    Game blocked(wall,scene,{},&m);ticks(blocked,400);check(blocked.player.health==3 && blocked.retail_objects[0].enemy->phase==EnemyPhase::Idle,"Enemy attacked through a wall");
    auto empty=floor_level();Game hurt(empty,scene);check(hurt.hurt({0,8,1}),"Initial hit rejected");ticks(hurt,120);
    check(hurt.hurt({0,8,1}) && hurt.player.health==1,"Damage failed after invulnerability expired");ticks(hurt,120);
    hurt.hurt({0,8,1});check(hurt.player.health==0,"Last Sparx health point not consumed");ticks(hurt,120);
    const auto clock=hurt.clock.ticks();hurt.hurt({0,8,1});check(hurt.player.lives==2 && hurt.player.health==3 && hurt.clock.ticks()==clock,"Death failed to consume a life and restore Spyro without resetting simulation time");
    Vec3 reference;
    for(const int rate:{30,60,144}) {
        Game paced(small,scene,{},&m);for(int i=0;i<rate*3;++i)paced.advance(1.0/rate,{});
        const auto position=paced.retail_objects[0].position;
        if(rate==30)reference=position;else check(length(position-reference)<0.001f,"Enemy movement depends on display frame rate");
    }
}
void hud_checks() {
    HudController hud;hud.reset(10,2,3);
    const auto run=[&](unsigned n,unsigned gems,unsigned spirits,unsigned lives,bool recall=false,bool suppressed=false){
        for(unsigned i=0;i<n;++i)hud.tick(gems,spirits,lives,recall,suppressed);};
    run(120,10,2,3);check(!hud.visible(HudPart::Gems) && !hud.visible(HudPart::Lives),"HUD stayed permanently visible while idle");
    run(2,11,2,3);check(hud.visible(HudPart::Gems) && !hud.visible(HudPart::Spirits) && !hud.visible(HudPart::Lives),"Gem event revealed unrelated HUD parts");
    run(30,11,2,3);check(hud.counter(HudPart::Gems).value==10,"Gem total changed before the retail entry animation finished");
    run(36,11,2,3);check(hud.counter(HudPart::Gems).value==11 && hud.counter(HudPart::Gems).phase==HudPhase::Holding,"Gem counter did not count up and hold");
    run(210,11,2,3);check(!hud.visible(HudPart::Gems),"Gem counter did not leave after the retail hold window");
    run(2,11,3,3);check(hud.visible(HudPart::Spirits) && !hud.visible(HudPart::Gems),"Spirit event failed to reveal its own counter");
    run(280,11,3,3);check(!hud.visible(HudPart::Spirits),"Spirit counter stayed visible indefinitely");
    run(2,11,3,2);check(hud.visible(HudPart::Lives) && !hud.visible(HudPart::Spirits),"Lost life failed to reveal the life display alone");
    run(65,11,3,2);check(hud.counter(HudPart::Lives).value==2,"Life counter did not count down");
    run(260,11,3,2);run(2,11,3,2,true);
    check(hud.visible(HudPart::Gems) && hud.visible(HudPart::Spirits) && hud.visible(HudPart::Lives),"HUD recall did not reveal all three counters");
    run(300,11,3,2,true);check(hud.counter(HudPart::Gems).phase==HudPhase::Holding,"Holding recall let the HUD disappear");
    run(300,11,3,2);check(!hud.visible(HudPart::Lives),"HUD failed to hide after recall was released");
    run(60,11,3,2,true);run(60,11,3,2,false,true);
    check(!hud.visible(HudPart::Gems) && !hud.visible(HudPart::Spirits) && !hud.visible(HudPart::Lives),"Conversation failed to suppress the HUD");
    hud.reset(0,0,3);run(198,1,0,3); // Counter is partway through leaving.
    run(300,5,0,3);check(hud.counter(HudPart::Gems).value==5,"Pickup during HUD exit was lost");
    const auto world=floor_level();Scene scene;scene.spawn=world.spawn;Game game(world,scene);
    InputState press;press.recall_hud=true;game.advance(1.0/144,press);game.advance(1.0/144,{});game.advance(1.0/144,{});
    check(game.hud.visible(HudPart::Lives),"A short HUD recall was dropped between simulation ticks");
}
void retail_checks(const std::filesystem::path& source,const std::filesystem::path& save_file) {
    AssetStore assets(source);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25");const auto archive=wad.read(17);
    auto level=decode_level(archive,17);auto m=decode_object_models(archive,exe);initialize_retail_objects(level,m);
    check(std::count_if(level.objects.begin(),level.objects.end(),[](const auto& o){return o.enemy.has_value();})==14,"Glimmer enemy mapping does not contain 14 enemies");
    check(std::none_of(level.objects.begin(),level.objects.end(),[](const auto& o){return o.type==0x22a && o.enemy.has_value();}),"Friendly Gemcutter was marked hostile");
    check(level.objects[0].enemy->route.size()==8 && level.objects[177].enemy->loop,"Authored escape routes were not decoded");
    const auto hud=decode_hud(archive,exe);check(hud.sprites[31].width==48 && std::any_of(hud.rgba.begin(),hud.rgba.end(),[](auto c){return c!=0;}),"Original HUD sprites failed to decode");
    for(unsigned slot=15;slot<=95;slot+=2) {
        const auto bank=wad.read(slot);const auto sprites=decode_hud(bank,exe);const auto object_models=decode_object_models(bank,exe);
        check(sprites.sprites[0].width==24 && sprites.sprites[19].height==20 && (slot==95 || !object_models.models.empty()),"HUD or collision bank failed in another scene");
    }
    Scene scene;scene.wad_slot=17;const auto& target=level.objects[8];bool found=false;
    for(unsigned i=0;i<16 && !found;++i) {
        const auto angle=i*std::numbers::pi_v<float>/8;auto position=target.position+Vec3{std::sin(angle)*2.2f,1,std::cos(angle)*2.2f};
        if(const auto floor=level.raycast(position,{0,-1,0},2))if(std::abs(floor->point.y-target.position.y)<0.3f){scene.spawn=floor->point;found=true;}
    }
    check(found,"No standing place for retail enemy probe");Game game(level,scene,{},&m);ticks(game,240);
    check(game.player.health<3,"Retail club animation never hit Spyro");
    game.interactions.attack_sphere(game.retail_objects,m,target.position+Vec3{0,0.6f,0},1,flame_attack);ticks(game,1);
    check(game.retail_objects[8].enemy->defeated && game.spirits()==0,"Retail enemy flame collision/death timing failed");
    GameSave saved;saved.capture(game);saved.write(save_file);Game loaded(level,scene,{},&m);GameSave::read(save_file).apply(loaded,&m);
    check(loaded.retail_objects[8].state<0 && loaded.spirits()==1 && loaded.player.health==game.player.health,"Retail enemy did not stay defeated after save/load");
    std::cout<<"Retail Glimmer combat/HUD/save checks passed; save: "<<save_file.string()<<'\n';
}
}
int main(int argc,char** argv) {
    const auto file=std::filesystem::temp_directory_path()/("spyro-progression-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".s2save");
    try {
        save_checks(file);enemy_checks();hud_checks();
        if(argc>1)retail_checks(std::filesystem::u8path(argv[1]),argc>2?std::filesystem::u8path(argv[2]):file);
        std::filesystem::remove(file);std::cout<<checks<<" progression checks passed\n";return 0;
    }catch(const std::exception& e){std::error_code ignored;std::filesystem::remove(file,ignored);std::cerr<<e.what()<<'\n';return 1;}
}
