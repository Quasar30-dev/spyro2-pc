#include "spyro/runtime.hpp"
#include "spyro/savegame.hpp"
#include <chrono>
#include <fstream>
#include <sstream>
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
    for(unsigned i=0;i<256;++i)m.animation_blends[i]=static_cast<std::uint8_t>((i>>4)?std::min(255u,(i&15)*256/((i>>4)+1)):0);
    for(unsigned type:{0x78,0x10,0xd3,0x12c,0xaa}) {
        auto& model=m.models[type];model.animated=true;model.meshes.resize(1);
        model.animation_count=type==0x78?2:1;model.attachment_vertices=type==0x78?1:0;
        if(type==0x12c)model.cylinder=ObjectCylinder{2016,340,340,1,0,1000};
        for(unsigned clip=0;clip<model.animation_count;++clip) {
            ObjectAnimation a;a.radius=1;
            for(unsigned frame=0;frame<(clip?15u:20u);++frame) {
                ObjectFrame f;f.ticks=4;f.metadata=((clip==1 && frame==5?1:255)<<16)|(3<<9)|(frame*2);
                f.vertices={{0,0,0}};f.collision_vertices={{0,0.1f,0}};a.frames.push_back(f);
            }
            model.animations.push_back(std::move(a));
        }
    }
    return m;
}
RetailObject food(const ObjectModels& m,unsigned type);
void controller_checks() {
    const auto models=fixture();const auto level=floor_level();const auto origin=level.spawn+retail::origin_above_feet;
    std::vector<RetailObject> objects;SparxCompanion companion;companion.reset(objects,&models,origin,0,3);
    CompanionContext context;context.idle=true;context.camera_eye={0,9,-3};context.camera_forward=normalized(origin-context.camera_eye);
    std::set<unsigned> states;
    for(unsigned t=0;t<18000;++t) {
        companion.tick(objects,&models,level,origin,0,{},false,false,3,nullptr,3,context);
        states.insert(companion.idle_state());const auto& sparx=objects[*companion.object_index()];
        check(finite(sparx.position) && length(sparx.position-origin)<8,"Idle companion escaped its player/camera neighborhood");
        check(sparx.position.y>=8,"Idle stunt crossed the ground");
        if(t<118)check(companion.idle_state()==0,"Idle stunt started before original delay");
    }
    check(states==std::set<unsigned>{0,1,2,3,4,5,6},"Not all seven recovered idle substates execute");
    context.idle=false;for(unsigned t=0;t<12;++t)companion.tick(objects,&models,level,origin,0,{0,0,4},false,false,3,nullptr,3,context);
    check(companion.idle_state()==0,"Movement failed to interrupt an idle stunt");
    RetailObject gem;gem.id=100;gem.type=1;gem.state=1;gem.gem_value=1;gem.held_gem=255;gem.position={0,8.5f,7};objects.push_back(gem);
    auto basket=gem;basket.id=101;basket.type=0xc8;basket.held_gem=5;basket.position={4,8.5f,0};objects.push_back(basket);
    auto decoy=basket;decoy.id=102;decoy.held_gem=255;decoy.position={0,8.5f,3};objects.push_back(decoy);
    context.gem_hint=true;
    for(unsigned t=0;t<120;++t)companion.tick(objects,&models,level,origin,0,{},false,false,3,nullptr,3,context);
    check(companion.hint_id()==101,"Gem hint missed nearer intact container or selected empty scenery");
    check(objects[*companion.object_index()].position.x>origin.x+0.8f,"Sparx did not point towards hinted gems");
    for(auto& o:objects)if(o.id==101)o.collected=true;
    for(unsigned t=0;t<120;++t)companion.tick(objects,&models,level,origin,0,{},false,false,3,nullptr,3,context);
    check(companion.hint_id()==100 && objects[*companion.object_index()].position.z>origin.z+0.8f,"Hint did not retarget after removal");
    context.gem_hint=false;context.underwater=true;context.pitch=0.6f;context.roll=-0.3f;
    for(unsigned t=0;t<60;++t)companion.tick(objects,&models,level,origin,0,{},false,false,3,nullptr,3,context);
    check(!companion.hint_id() && companion.bubble_index().has_value(),"Underwater Sparx did not release hint/create retail bubble");
    const auto bubble_index=*companion.bubble_index();const auto& bubble=objects[bubble_index];
    check(bubble.type==0xaa && bubble.size==26 && bubble.animation.active,"Bubble did not use original class, growth and animation");
    check(length(bubble.position-objects[*companion.object_index()].position-Vec3{0,28/1024.0f,0})<0.00001f,"Underwater bubble lost attachment");
    context.underwater=false;for(unsigned t=0;t<2;++t)companion.tick(objects,&models,level,origin,0,{},false,false,3,nullptr,3,context);
    check(objects[bubble_index].state<0,"Bubble survived surfacing");const auto count=objects.size();
    context.underwater=true;for(unsigned t=0;t<2;++t)companion.tick(objects,&models,level,origin,0,{},false,false,3,nullptr,3,context);
    check(objects.size()==count && objects[bubble_index].state==0,"Repeated dives leaked companion bubbles");
    for(unsigned t=0;t<2;++t)companion.tick(objects,&models,level,origin,0,{},false,false,0,nullptr,3,context);
    check(objects[bubble_index].state<0,"Absent Sparx retained underwater bubble");
    // 8007bf18 resets the interval after a life fly; using total modulo fails
    // when the life-dependent interval changes after the first award.
    std::vector<RetailObject> flock;
    for(unsigned i=0;i<16;++i){auto sheep=food(models,0x12c);sheep.id=i;sheep.position={50+float(i),8,0};if(i<7)sheep.attack_flags=flame_attack;flock.push_back(sheep);}
    SparxCompanion feeder;feeder.reset(flock,&models,origin,0,3);
    feeder.tick(flock,&models,level,origin,0,{},false,false,3,nullptr,3);
    check(feeder.butterfly_drops()==0,"Extra-life interval did not reset");
    for(auto& o:flock)if(o.type==0x12c && o.id>=7 && o.id<14)o.attack_flags=flame_attack;
    for(unsigned t=0;t<2;++t)feeder.tick(flock,&models,level,origin,0,{},false,false,3,nullptr,4);
    check(feeder.butterfly_drops()==7 && std::count_if(flock.begin(),flock.end(),[](const auto& o){return o.type==0xd3;})==1,"Life threshold changed the already-reset interval");
    for(auto& o:flock)if(o.type==0x12c && o.id>=14)o.attack_flags=flame_attack;
    for(unsigned t=0;t<2;++t)feeder.tick(flock,&models,level,origin,0,{},false,false,3,nullptr,4);
    check(feeder.butterfly_drops()==0 && std::count_if(flock.begin(),flock.end(),[](const auto& o){return o.type==0xd3;})==2,"Nine-drop interval after extra life is incorrect");
    auto wander_level=level;auto fly=food(models,0x10);fly.position={8,8.6f,0};wander_level.objects={fly};Scene scene;scene.spawn=level.spawn;
    Game wandering(wander_level,scene,{},&models);const auto start=fly.position;
    for(unsigned t=0;t<600;++t)wandering.advance(FixedClock::step,{});
    const auto travelled=wandering.retail_objects[0].position-start;
    check(std::hypot(travelled.x,travelled.z)>0.2f && length(travelled)<3,"Butterfly did not roam within its original home neighborhood");
    for(unsigned t=600;t<1800;++t)wandering.advance(FixedClock::step,{});
    check(wandering.retail_objects[0].collected,"Untargeted butterfly outlived its 1800-VBlank lifetime");
}
RetailObject food(const ObjectModels& m,unsigned type=0x10) {
    RetailObject o;o.id=41;o.type=static_cast<std::uint16_t>(type);o.position=o.previous_position={0.25f,8.7f,0};o.held_gem=255;
    o.animation=o.previous_animation=start_object_animation(m.models.at(type));return o;
}
unsigned event_count(const Game& g,unsigned type,unsigned id) {
    return static_cast<unsigned>(std::count_if(g.sound_events.begin(),g.sound_events.end(),[&](const auto& e){return e.scope==SoundScope::Object && e.object_type==type && e.id==id;}));
}
void synthetic() {
    auto m=fixture();auto l=floor_level();Scene scene;scene.spawn=l.spawn;
    for(unsigned health:{0,1,2,3}) {
        Game g(l,scene,{},&m);g.player.health=health;std::set<unsigned> colors;
        for(unsigned t=0;t<120;++t) {
            g.advance(FixedClock::step,{});const auto sparks=g.sparx.sparkles();check(sparks.size()<=48,"Sparkle lifetime/ring buffer overflow");
            for(const auto& p:sparks){check(finite(p.position),"Non-finite sparkle");colors.insert(static_cast<unsigned>(p.color.x*255)<<16|static_cast<unsigned>(p.color.y*255)<<8|static_cast<unsigned>(p.color.z*255));}
        }
        check(g.sparx.emitted_sparkles()==(health>=2?240:0),"Health/cadence changed retail sparkle emissions");
        if(health==2)check(colors.size()==8,"Blue Sparx lost the original multicolour trail");
        if(health==3)check(colors==std::set<unsigned>{0xffff00},"Gold trail tint changed");
        check(event_count(g,0x78,0)==0 && event_count(g,0x78,1)==0,"Idle wings emitted invented sound events");
        g.player.health=0;for(unsigned t=0;t<24;++t)g.advance(FixedClock::step,{});
        check(g.sparx.sparkles().empty(),"Expired trail survives missing Sparx");g.respawn();check(g.sparx.sparkles().empty(),"Respawn retained previous trail");
    }
    Vec3 end;std::vector<SparxCompanion::SparkPose> trail;
    for(unsigned rate:{30,60,144}) {
        Game g(l,scene,{},&m);for(unsigned t=0;t<rate*3;++t)g.advance(1.0/rate,{});
        const auto now=g.sparx.sparkles();const auto position=g.retail_objects[*g.sparx.object_index()].position;
        if(rate==30){trail=now;end=position;}else {
            check(length(end-position)<0.00001f && now.size()==trail.size(),"Presentation rate changed companion simulation");
            for(unsigned i=0;i<now.size();++i)check(length(now[i].position-trail[i].position)<0.00001f,"Presentation rate changed sparkle path");
        }
        g.set_paused(true);const auto emitted=g.sparx.emitted_sparkles();g.advance(0.2,{});
        check(emitted==g.sparx.emitted_sparkles(),"Paused companion emitted particles");
        for(const auto& p:g.sparx.sparkles(0))check(finite(p.position),"Pause reset damaged trail");
    }
    for(unsigned health:{0,1,2,3})for(unsigned type:{0x10,0xd3}) {
        auto arena=l;arena.objects={food(m,type)};Game g(arena,scene,{},&m);g.player.health=health;g.player.lives=98;
        bool started=false;for(unsigned t=0;t<240;++t) {
            g.advance(FixedClock::step,{});
            if(g.sparx.feeding_starts() && !started) {
                started=true;if(health)check(g.player.health==health,"Feeding healed before swallowing frame");
            }
        }
        check(started && g.sparx.butterflies_eaten()==1,"Butterfly was not consumed exactly once");
        check(g.player.health==(type==0xd3?3:std::min(3u,health+1)),"Butterfly health award wrong");
        check(g.player.lives==(type==0xd3?99:98),"Extra-life fly award wrong");
        check(event_count(g,0x78,0)==1 && event_count(g,0x78,1)==(health?1u:0u),"Dedicated Sparx sounds missing or duplicated");
        const auto& sparx=g.retail_objects[*g.sparx.object_index()];
        check(sparx.state>=0 && sparx.animation.clip==0 && sparx.animation.next_clip==0,"Eating did not return to wing loop");
        check(sparx.material_color==m.sparx_colors[g.player.health],"Feeding did not update health material");
    }
    auto blocked=l;blocked.objects={food(m)};blocked.objects[0].position={2,8.7f,0};
    blocked.triangles.push_back({{1,7,-4},{1,12,4},{1,7,4}});blocked.triangles.push_back({{1,7,-4},{1,12,-4},{1,12,4}});
    Game obstructed(blocked,scene,{},&m);for(unsigned t=0;t<180;++t)obstructed.advance(FixedClock::step,{});
    check(obstructed.sparx.butterflies_eaten()==0,"Sparx ate through terrain");
    auto arena=l;arena.objects={food(m)};Game interrupted(arena,scene,{},&m);interrupted.advance(FixedClock::step,{});
    check(interrupted.retail_objects[0].state==1,"Feeding pursuit did not start");interrupted.respawn();
    check(interrupted.retail_objects[0].state==0,"Respawn left butterfly permanently targeted");
    for(unsigned attack:{flame_attack,charge_attack}) {
        auto sheep=food(m,0x12c);std::vector<RetailObject> objects{sheep};SparxCompanion sparx;
        sparx.reset(objects,&m,l.spawn+retail::origin_above_feet,0,3);ObjectInteractions interaction;
        interaction.attack_sphere(objects,m,sheep.position+Vec3{0,0.4f,0},0.3f,attack);
        std::vector<SoundEvent> sounds;sparx.tick(objects,&m,l,l.spawn+retail::origin_above_feet,0,{},false,false,3,&sounds);
        check(objects[0].collected && objects.size()==3 && objects.back().type==0x10,"Sheep hit did not release a native butterfly");
        check(sounds.size()==1 && sounds[0].object_type==0x12c,"Sheep release lost its sound");
        for(unsigned t=0;t<8;++t)sparx.tick(objects,&m,l,l.spawn+retail::origin_above_feet,0,{},false,false,3,&sounds);
        check(objects.size()==3,"Sheep spawned repeated butterflies");
    }
}
void retail_checks(const std::filesystem::path& path) {
    AssetStore store(path);WadArchive wad(store);const auto exe=store.read_all("SCUS_944.25");unsigned banks=0,bubble_banks=0;
    for(unsigned slot=15;slot<=95;slot+=2) {
        const auto archive=wad.read(slot);const auto m=decode_object_models(archive,exe);
        if(!m.models.contains(0x78))continue;
        const auto& sparx=m.models.at(0x78);const auto sounds=decode_sound_bank(archive,exe,slot);++banks;
        check(sparx.attachment_vertices==1 && sparx.animations.size()==2,"Retail companion attachment or clips missing");
        if(m.models.contains(0xaa)){++bubble_banks;check(m.models.at(0xaa).animated && !m.models.at(0xaa).animations.empty(),"Original underwater bubble animation missing");}
        const auto mapping=sounds.object_sounds.at(0x78);
        check(mapping.size()>=2 && std::count_if(mapping.begin(),mapping.end(),[](auto id){return id!=255;})==2,"Sparx dedicated sound mapping changed");
        for(auto sample:mapping)if(sample!=255)check(sample<sounds.sounds.size() && sounds.sounds[sample].pcm.frames()>0 && !sounds.sounds[sample].looping,"Sparx sound is missing/incorrectly looping");
        for(unsigned clip=0;clip<2;++clip)for(unsigned f=0;f<sparx.animations[clip].frames.size();++f) {
            const auto& frame=sparx.animations[clip].frames[f];
            check(frame.collision_vertices.size()>=1 && finite(frame.collision_vertices[0]) && length(frame.collision_vertices[0])<1,"Sparx glow attachment outside model");
            check(((frame.metadata>>16)&255)==(clip==1 && f==5?1u:255u),"Sparx animation sound event differs from decoded retail metadata");
        }
        float extent=0;for(const auto& p:m.sparx_glows[0].outline)extent=std::max(extent,length(p));
        check(extent>20 && extent<128,"Glow outline was not decoded from executable");
        if(slot==15) {
            auto level=decode_level(archive,slot);initialize_retail_objects(level,m);
            auto sheep=std::find_if(level.objects.begin(),level.objects.end(),[](const auto& o){return is_butterfly_source(o.type);});
            check(sheep!=level.objects.end(),"No retail sheep for feeding integration");
            auto arena=floor_level();auto original=*sheep;original.position={0,8,0};arena.objects={original};Scene scene;scene.spawn={-0.7f,8,0};
            Game game(arena,scene,{},&m);game.player.health=1;
            game.interactions.attack_sphere(game.retail_objects,m,original.position+Vec3{0,0.3f,0},0.5f,flame_attack);
            for(unsigned t=0;t<360;++t)game.advance(FixedClock::step,{});
            check(game.sparx.butterflies_eaten()==1 && game.player.health==2,"Retail sheep/butterfly/Sparx integration failed");
            check(event_count(game,0x78,0)==1 && event_count(game,0x78,1)==1,"Retail feeding sounds did not fire once each");
            check(game.retail_objects[*game.sparx.object_index()].animation.next_clip==0,"Retail feeding loop did not finish");
            check(m.models.contains(0x104) && !m.models.at(0x104).animated && m.models.at(0x104).meshes.size()==10,"Retail gem digit bank was not decoded");
            for(unsigned value:{1,2,5,10,25}) {
                auto empty=floor_level();RetailObject gem;gem.type=1;gem.id=0;gem.state=1;gem.gem_value=value;gem.gem_age=29;gem.position={0,8.4f,0};empty.objects={gem};
                Scene start;start.spawn=empty.spawn;Game pickup(empty,start,{},&m);pickup.advance(FixedClock::step,{});
                unsigned numbers=0;std::vector<unsigned> digits;
                for(const auto& o:pickup.retail_objects)if(o.type==0x104 && o.runtime_spawned){++numbers;digits.push_back(o.model);check(o.material_color==2,"Gem feedback retained the empty black material");}
                check(numbers==(value<10?1u:2u) && digits[0]==value%10 && (value<10 || digits[1]==value/10),"Pickup feedback did not use the original denomination meshes");
                const auto size=pickup.retail_objects.size();
                for(unsigned t=0;t<198;++t)pickup.advance(FixedClock::step,{});
                check(pickup.player.gems==value && pickup.retail_objects.size()==size,"Number popups spawned repeatedly or awarded extra gems");
                check(std::none_of(pickup.retail_objects.begin(),pickup.retail_objects.end(),[](const auto& o){return o.type==0x104 && o.runtime_spawned && o.state>=0;}),"Expired gem number remained visible");
                GameSave saved;saved.capture(pickup);Game reload(empty,start,{},&m);saved.apply(reload,&m);reload.advance(FixedClock::step,{});
                check(reload.player.gems==value && std::none_of(reload.retail_objects.begin(),reload.retail_objects.end(),[](const auto& o){return o.type==0x104;}),"Loading old pickups replayed gem feedback or awards");
            }
        }
    }
    check(banks==40 && bubble_banks==11,"Retail Sparx / underwater bubble bank coverage changed");std::cout<<banks<<" retail Sparx banks and "<<bubble_banks<<" underwater bubble banks verified\n";
}
void save_checks() {
    auto m=fixture();auto l=floor_level();auto sheep=food(m,0x12c);sheep.id=0;l.objects={sheep};Scene scene;scene.spawn=l.spawn;
    Game g(l,scene,{},&m);g.player.health=1;g.retail_objects[0].attack_flags=flame_attack;
    g.advance(FixedClock::step,{});check(g.sparx.butterfly_drops()==1 && g.sparx.butterflies_eaten()==0,"Save fixture consumed butterfly before checkpoint");
    GameSave save;save.capture(g);check(save.levels.at(15).butterflies.size()==1,"Released butterfly missing from checkpoint");
    const auto path=std::filesystem::temp_directory_path()/("spyro-companion-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".s2save");
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove(path,e);}} cleanup{path};
    save.write(path);const auto restored=GameSave::read(path);Game loaded(l,scene,{},&m);restored.apply(loaded,&m);
    check(loaded.sparx.butterfly_drops()==1 && loaded.retail_objects[0].collected,"Load lost the animal or extra-life interval");
    const auto before=loaded.retail_objects.size();
    for(unsigned i=0;i<240;++i)loaded.advance(FixedClock::step,{});
    check(loaded.player.health==2 && loaded.sparx.butterflies_eaten()==1 && loaded.retail_objects.size()==before,"Restored butterfly was lost or duplicated");
    GameSave consumed;consumed.capture(loaded);consumed.write(path);Game again(l,scene,{},&m);GameSave::read(path).apply(again,&m);
    for(unsigned i=0;i<240;++i)again.advance(FixedClock::step,{});
    check(again.player.health==2 && again.sparx.butterflies_eaten()==0,"Loading swallowed food awarded health again");
    auto corrupt=save;corrupt.levels.at(15).butterflies[0].source=99;bool rejected=false;
    try{corrupt.apply(loaded,&m);}catch(const FormatError&){rejected=true;}
    check(rejected && loaded.player.health==2 && loaded.retail_objects.size()==before,"Invalid butterfly save mutated gameplay");
    std::ostringstream legacy;legacy<<0xcdc0917cu<<" 15 7 3 2 0 0 1 0 0 0 0 0 0\n0 1 0 0 0\n1\n15 0 8 0 0 0 8 0\n0\n0\n0\n0\n0\n1 40 1 2 1 1 1 2\n";
    const auto body=legacy.str();{std::ofstream out(path,std::ios::binary);out<<"S2SAVE 5 "<<crc32({reinterpret_cast<const std::uint8_t*>(body.data()),body.size()})<<' '<<body.size()<<'\n'<<body;}
    const auto old=GameSave::read(path);check(old.health==2 && old.gems==7 && old.upgrades.glide==2 && old.butterfly_drops==0 && old.levels.at(15).butterflies.empty(),"Version-5 save compatibility changed");
    std::ostringstream version6;version6<<0xcdc0917cu<<" 15 7 3 2 0 0 1 0 0 0 0 0 0\n0 1 0 0 0\n1\n15 0 8 0 0 0 8 0\n0\n1\n0 18\n0\n0\n1\n0 16 30 64 0 8 1 0 8 0\n0\n1 40 1 2 1 1 1 2\n5\n";
    const auto v6body=version6.str();{std::ofstream out(path,std::ios::binary);out<<"S2SAVE 6 "<<crc32({reinterpret_cast<const std::uint8_t*>(v6body.data()),v6body.size()})<<' '<<v6body.size()<<'\n'<<v6body;}
    const auto v6=GameSave::read(path);const auto& v6level=v6.levels.at(15);
    check(v6.butterfly_drops==5 && v6level.butterflies.size()==1 && v6level.butterflies[0].age==30 && v6level.bodies.empty() && !v6level.powers.flying,"Version-6 butterfly save compatibility changed");
}
}
int main(int argc,char** argv){try{synthetic();save_checks();controller_checks();if(argc>1)retail_checks(std::filesystem::u8path(argv[1]));std::cout<<checks<<" companion checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
