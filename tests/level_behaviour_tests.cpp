#include "spyro/runtime.hpp"
#include "spyro/savegame.hpp"
#include <iostream>
#include <set>
using namespace spyro;
namespace {
unsigned checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw FormatError(why);}
Level floor_level() {
    Level l;l.spawn={0,8,0};l.triangles={{{-100,8,-100},{100,8,100},{100,8,-100}},{{-100,8,-100},{-100,8,100},{100,8,100}}};
    for(const auto& t:l.triangles){l.bounds.add(t.a);l.bounds.add(t.b);l.bounds.add(t.c);}return l;
}
ObjectModels fixture() {
    ObjectModels m;CameraSettings c;m.sine=c.sine;m.cosine=c.cosine;
    m.gem_values={1,2,5,10,25};m.gem_meshes={0,1,2,3,4};for(unsigned i=0;i<5;++i)m.gem_variants[m.gem_values[i]]=i;
    m.models[1].meshes.resize(5);m.models[0x58].meshes.resize(5);m.models[0x134].meshes.resize(2);
    for(unsigned i=0;i<256;++i)m.animation_blends[i]=static_cast<std::uint8_t>((i>>4)?std::min(255u,(i&15)*256/((i>>4)+1)):0);
    for(unsigned type:{0x2c,0x30,0x116}) {
        auto& model=m.models[type];model.animated=true;model.animation_count=10;model.meshes.resize(1);
        model.cylinder=ObjectCylinder{2016,340,340,1,0,1000};
        model.damage_banks[0]=ObjectDamageBank{2016,1,{},{}};
        model.damage_banks[1]=ObjectDamageBank{2016,1,{{400,0,-1}},{}};
        for(unsigned clip=0;clip<10;++clip) {
            ObjectAnimation animation;
            for(unsigned f=0;f<24;++f) {
                ObjectFrame a;a.ticks=4;a.metadata=0xff0000|(3<<9)|(f*2)|((clip==4 && f>=8 && f<16)?0x2000:0);
                a.vertices={{0,0,0}};a.collision_vertices={{0,0.5f,0.7f}};animation.frames.push_back(a);
            }
            model.animations.push_back(std::move(animation));
        }
    }
    return m;
}
RetailObject actor(const ObjectModels& m,EnemyKind kind,unsigned id,Vec3 at) {
    RetailObject o;o.id=id;o.type=kind==EnemyKind::GearGrinder?0x2c:kind==EnemyKind::Robot?0x30:0x116;
    o.position=o.previous_position=at;o.rotation[2]=64;o.previous_rotation=o.rotation;o.held_gem=255;o.record[83]=1;
    auto& e=o.enemy.emplace();e.kind=kind;e.home=at;e.home_heading=64;e.notice=9;e.reach=3;e.maximum_speed=120;
    o.animation=o.previous_animation=start_object_animation(m.models.at(o.type));return o;
}
unsigned run(EnemySystem& ai,std::vector<RetailObject>& objects,const ObjectModels& models,const Level& l,Vec3 origin,unsigned ticks) {
    unsigned hits=0;
    for(unsigned i=0;i<ticks;++i){
        tick_object_animations(objects,models);if(ai.tick(objects,&models,l,origin,false))++hits;
        for(const auto& o:objects)if(!o.collected && o.state>=0) {
            const auto bank=models.models.find(o.type);if(bank==models.models.end() || !bank->second.animated)continue;
            try {sample_object_animation(bank->second,o.animation,models.animation_blends);sample_object_animation(bank->second,o.previous_animation,models.animation_blends);}
            catch(const FormatError& e){throw FormatError("Moby "+std::to_string(o.type)+" record "+std::to_string(o.id)+" clip "+std::to_string(o.animation.clip)+" -> "+std::to_string(o.animation.next_clip)+": "+e.what());}
        }
    }
    return hits;
}
void synthetic() {
    auto m=fixture();auto l=floor_level();
    auto g=actor(m,EnemyKind::GearGrinder,100,{0,8,0});auto r=actor(m,EnemyKind::Robot,200,{3,8,0});
    check(!enemy_accepts_attack(g,charge_attack) && enemy_accepts_attack(g,flame_attack),"Grinder attack immunity");
    check(!enemy_accepts_attack(r,flame_attack) && enemy_accepts_attack(r,charge_attack),"Robot attack immunity");
    auto idol=actor(m,EnemyKind::MetalIdol,300,{0,8,0});
    check(!enemy_accepts_attack(idol,flame_attack) && enemy_accepts_attack(idol,charge_attack),"Metal idol attack immunity");
    for(const auto& o:{g,r,idol})for(auto attack:{0x40000u,0x100000u,0x400000u})check(enemy_accepts_attack(o,attack),"Mapped enemy blocked a powered attack");
    g.enemy->linked=200;g.enemy->command_available=true;g.enemy->activation=20;g.enemy->command_delay=40;
    r.enemy->linked=100;r.enemy->assigned_waypoint=0;r.enemy->patrol_duration=300;
    r.enemy->route={{{0,8,4},0},{{0,8,9},0}};
    std::vector<RetailObject> pair{g,r};EnemySystem ai;
    initialize_enemy_objects(pair);
    check(pair[0].enemy->initialized && pair[1].enemy->initialized && length(pair[1].position-Vec3{0,8,2000.0f/1024})<0.001f,
        "Level preview and Play disagree about the linked robot's initial placement");
    run(ai,pair,m,l,{10,8.5f,0},1);
    check(length(pair[1].position-Vec3{0,8,2000.0f/1024})<0.001f,"Robot did not spawn in front of its linked grinder");
    run(ai,pair,m,l,{10,8.5f,0},280);
    check(pair[1].enemy->phase==EnemyPhase::Patrol && pair[1].position.z>3,"Grinder command did not release robot onto its authored patrol");
    pair[0].attack_flags=flame_attack;run(ai,pair,m,l,{10,8.5f,0},160);
    check(pair[0].enemy->defeated && pair[0].collected,"Grinder did not finish its actual death clip");
    run(ai,pair,m,l,{30,8.5f,0},800);
    check(pair[1].enemy->assigned_waypoint==-1 && pair[1].enemy->phase==EnemyPhase::Ready,"Robot did not become an independent guard after its leader died");
    const auto home=pair[1].enemy->home;
    run(ai,pair,m,l,home+Vec3{0,0.5f,2},40);
    check(pair[1].enemy->phase==EnemyPhase::Chase || pair[1].enemy->phase==EnemyPhase::Swing,"Free robot did not pursue Spyro");
    run(ai,pair,m,l,{80,8.5f,80},600);
    check(length(pair[1].position-home)<0.25f && pair[1].enemy->phase==EnemyPhase::Ready,"Robot failed its chase leash/return-home cycle");
    pair[1].attack_flags=charge_attack;run(ai,pair,m,l,home,1);
    check(pair[1].collected && pair.size()==12,"Robot did not break into ten retail pieces exactly once");
    std::set<unsigned> ids;for(const auto& o:pair)ids.insert(o.id);check(ids.size()==pair.size(),"Enemy fragments reused an authored record ID");
    check(pair[2].model==0 && pair[3].model==1 && pair[4].model==2 && pair[5].model==4 && pair[6].model==3,"Robot debris mesh sequence changed");
    run(ai,pair,m,l,home,300);check(pair.size()==12,"Defeated robot respawned its debris");
    // Retail collision bank switches with frame metadata, including blends.
    for(unsigned selector:{0u,1u}) {
        auto o=actor(m,EnemyKind::GearGrinder,0,{0,8,0});o.enemy->initialized=true;o.enemy->phase=EnemyPhase::Swing;
        o.animation.clip=o.animation.next_clip=4;o.animation.frame=o.animation.next_frame=selector?10:2;o.animation.blend=0;
        std::vector<RetailObject> objects{o};EnemySystem hit_ai;
        check(hit_ai.tick(objects,&m,l,{0,8.5f,0.7f},false).has_value()==(selector==1),"Enemy damaged Spyro outside its active retail collision bank");
    }
    auto chest=[&](unsigned type){RetailObject o;o.id=0;o.type=static_cast<std::uint16_t>(type);o.held_gem=5;
        o.position=o.previous_position={0,8,0};o.container.emplace();o.container->rest_position=o.position;return o;};
    for(unsigned type:{0xca,0xcb}) {
        const auto o=chest(type);std::vector<RetailObject> objects{o};
        ObjectInteractions held;unsigned held_gems=0;objects[0].pc_carried=true;begin_object_transform(objects[0],1);objects[0].position.x+=1;
        held.tick(objects,&m,l,{0,8,0},{},0,{},false,held_gems);
        check(objects[0].transform_phase==0 && std::abs(object_position(objects[0],0.37f).x-0.37f)<1e-5f,"Container script advanced a carried object's interpolation twice");
        objects={o};
        check(!container_accepts_attack(o,objects,flame_attack) && !container_accepts_attack(o,objects,charge_attack),"Heavy chest lost ordinary-attack immunity");
        check(container_accepts_attack(o,objects,headbash_attack)==(type==0xca),"Headbash chest classes share the wrong attack mask");
        ObjectInteractions interaction;objects[0].attack_flags=type==0xca?headbash_attack:0x40000;unsigned gems=0;
        interaction.tick(objects,&m,l,{0,8,0},{},0,{},false,gems);
        check(objects[0].state<0 && objects.size()==18 && interaction.released_gems()==1,"Heavy chest failed to release 16 fragments and one gem");
        interaction.tick(objects,&m,l,{0,8,0},{},0,{},false,gems);
        check(interaction.released_gems()==1,"Heavy chest repeated its gem reward");
        Scene scene;scene.wad_slot=15;l.objects={o};Game game(l,scene,{},&m);game.retail_objects[0].attack_flags=0x40000;
        game.advance(FixedClock::step,{});GameSave save;save.capture(game);Game loaded(l,scene,{},&m);save.apply(loaded,&m);
        check(loaded.retail_objects[0].state<0 && save.levels.at(15).pending_gems.size()==1,"Broken heavy chest / pending gem did not survive save/load");
    }
}
void retail_checks(const std::filesystem::path& path) {
    AssetStore store(path);WadArchive wad(store);const auto exe=store.read_all("SCUS_944.25");
    unsigned heavy=0,mapped=0,npcs=0,expanded=0;std::set<EnemyKind> families;
    for(unsigned slot=15;slot<=95;slot+=2) {
        const auto archive=wad.read(slot);auto l=decode_level(archive,slot);
        npcs+=static_cast<unsigned>(l.npcs.size());
        for(const auto& o:l.objects)if(is_heavy_container(o.type)){++heavy;check(o.container.has_value(),"Retail heavy chest has no container script");}
        if(slot==39 || slot==59) {
            auto m=decode_object_models(archive,exe);initialize_retail_objects(l,m);
            for(const auto& original:l.objects)if(original.enemy) {
                auto o=original;auto& e=*o.enemy;families.insert(e.kind);++expanded;
                const bool monkey=e.kind==EnemyKind::SpittingMonkey;
                const bool aquatic=e.kind==EnemyKind::Platypus;
                auto arena=floor_level();o.position=o.previous_position={0,monkey?10.0f:aquatic?8.5f:8.0f,0};e.home=o.position;
                // Isolate combat from route/quest activation, using the actual
                // model, metadata, attack banks and decoded class parameters.
                e.route.clear();o.rotation[2]=64;const auto yaw=retail::native_heading(1024);
                const auto origin=Vec3{std::sin(yaw)*0.8f,8.4f,std::cos(yaw)*0.8f};
                std::vector<RetailObject> objects{o};EnemySystem ai;unsigned hits=0;bool acted=false;std::set<unsigned> visited_clips;
                for(unsigned tick=0;tick<960;++tick) {
                    hits+=run(ai,objects,m,arena,origin,1);const auto& actor=objects[0];
                    acted|=actor.enemy->phase!=EnemyPhase::Idle;
                    visited_clips.insert(actor.animation.next_clip);
                    check(actor.animation.clip<m.models.at(actor.type).animations.size(),"Expanded enemy requested a missing retail animation");
                    if(aquatic && actor.animation.next_clip>=5)
                        check(!enemy_accepts_attack(actor,charge_attack|0x540000),"Submerged platypus accepted a surface attack");
                }
                if(!acted || !hits)throw FormatError("Expanded enemy failed combat: slot "+std::to_string(slot)+" record "+std::to_string(o.id)+" acted="+std::to_string(acted)+" hits="+std::to_string(hits));
                const auto attack=(e.kind==EnemyKind::FireBlubber || e.kind==EnemyKind::ElephantSnail || monkey)?flame_attack:charge_attack;
                if(aquatic) {
                    check(visited_clips==std::set<unsigned>{0,1,2,5,6,7},"Platypus skipped a surface/dive clip");
                    for(unsigned t=0;t<600 && !enemy_accepts_attack(objects[0],attack);++t)run(ai,objects,m,arena,origin,1);
                    check(enemy_accepts_attack(objects[0],attack),"Platypus did not resurface for a charge hit");
                }
                objects[0].attack_flags=attack;run(ai,objects,m,arena,origin,360);
                check(objects[0].enemy->defeated && objects[0].collected,"Expanded enemy failed its retail defeat animation");
                std::cout<<slot<<":"<<o.id<<" class="<<o.type<<" expanded-combat=passed hits="<<hits<<'\n';
            }
        }
        if(slot!=19 && slot!=23)continue;
        auto m=decode_object_models(archive,exe);initialize_retail_objects(l,m);
        const auto count=std::count_if(l.objects.begin(),l.objects.end(),[](const auto& o){return o.enemy.has_value();});
        check(count==(slot==23?22:6),"Retail enemy coverage count changed");mapped+=static_cast<unsigned>(count);
        for(const auto& original:l.objects)if(original.enemy) {
            auto o=original;o.enemy->linked=UINT32_MAX;o.enemy->command_available=false;o.enemy->patrol_initial=false;
            const auto yaw=retail::native_heading(o.rotation[2]*16);auto origin=o.position+Vec3{std::sin(yaw)*0.9f,0.2f,std::cos(yaw)*0.9f};
            std::vector<RetailObject> objects{o};EnemySystem ai;bool moved=false,attacked=false;unsigned hits=0;
            for(unsigned tick=0;tick<360;++tick) {
                hits+=run(ai,objects,m,l,origin,1);moved|=length(objects[0].position-o.position)>0.1f;attacked|=objects[0].enemy->phase==EnemyPhase::Swing;
            }
            check(attacked,"Retail enemy did not reach its attack state");
            if(!hits)throw FormatError("No retail enemy hit: slot "+std::to_string(slot)+" record "+std::to_string(o.id));
            check(hits>0,"Retail enemy attack never connected");
            if(o.enemy->kind==EnemyKind::Robot)check(moved,"Retail robot did not move");
            objects[0].attack_flags=o.enemy->kind==EnemyKind::GearGrinder?flame_attack:charge_attack;
            run(ai,objects,m,l,origin,300);check(objects[0].collected,"Retail enemy failed to complete defeat");
            check(objects[0].enemy->defeated,"Retail defeat persistence flag is missing");
            if(o.enemy->kind==EnemyKind::Robot) {
                auto arena=l;auto target=o;target.id=0;arena.objects={target};
                Scene scene;scene.wad_slot=slot;scene.spawn=o.position+Vec3{std::sin(yaw),0,std::cos(yaw)}*2.2f;
                if(const auto floor=l.raycast(scene.spawn+Vec3{0,1,0},{0,-1,0},3))scene.spawn.y=floor->point.y;
                Game game(arena,scene,{},&m);game.player.yaw=game.player.previous_yaw=yaw+std::numbers::pi_v<float>;
                InputState charge;charge.charge=true;
                for(unsigned tick=0;tick<120 && !game.retail_objects[0].enemy->defeated;++tick)game.advance(FixedClock::step,charge);
                if(!game.retail_objects[0].enemy->defeated)throw FormatError("Player charge missed retail robot "+std::to_string(o.id));
                check(game.spirits()==1,"Actual player charge did not award the robot's spirit exactly once");
            }
            std::cout<<slot<<":"<<o.id<<" class="<<o.type<<" combat=passed\n";
        }
    }
    check(heavy>0 && mapped==28,"Retail script coverage audit did not run");
    check(expanded==52 && families.size()==6,"Breeze Harbor / Mystic Marsh enemy coverage changed");
    check(npcs>100,"Cross-level NPC dialogue coverage is missing");
    std::cout<<"41 scene records checked; "<<heavy<<" heavy containers, "<<mapped<<" additional enemies\n";
    std::cout<<expanded<<" Breeze Harbor / Mystic Marsh enemies, "<<npcs<<" NPC conversation records\n";
}
}
int main(int argc,char** argv){try{synthetic();if(argc>1)retail_checks(std::filesystem::u8path(argv[1]));std::cout<<checks<<" level behaviour checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
