#include "spyro/runtime.hpp"
#include "spyro/pc_actors.hpp"
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>

using namespace spyro;
namespace {
unsigned checks{};
void check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
void ticks(Game& g,unsigned n){for(unsigned i=0;i<n;++i)g.advance(FixedClock::step,{});}
bool rejects(std::span<const std::uint8_t> bytes){try{decode_s2actor(bytes);}catch(const FormatError&){return true;}return false;}
Bytes reseal(Bytes body){body.resize(body.size()-4);return encode_s2actor_checksum(body);}
void actor_format() {
    check(embedded_pc_actor_count()==4,"Four demon actors must be embedded");
    for(unsigned i=0;i<embedded_pc_actor_count();++i) {
        const auto bytes=embedded_pc_actor(i);const auto actor=decode_s2actor(bytes);
        check(actor.kind==i+1 && actor.model.animated && actor.model.animations.size()==5,"Demon clips (erupt, move, attack, sink, idle)");
        check(actor.model.meshes.front().vertices.size()<=256,"Demon exceeds the retail 8-bit animated vertex limit");
        check(actor.texture_width==128 && actor.texture_height==128 && actor.texture_rgba.size()==128*128*4,"Demon texture");
        for(const auto& clip:actor.model.animations)for(const auto& f:clip.frames)check(f.vertices.size()==actor.model.meshes.front().vertices.size(),"Frame vertex count");
        Bytes copy(bytes.begin(),bytes.end());
        auto broken=copy;broken[60]^=0x5a;check(rejects(broken),"Corrupted actor accepted (checksum)");
        check(rejects(std::span(copy).first(copy.size()-9)),"Truncated actor accepted");
        auto many=copy;const std::uint32_t big=300;std::memcpy(&many[12],&big,4);check(rejects(reseal(many)),"Over-256-vertex actor accepted");
        auto bad_magic=copy;bad_magic[0]='X';check(rejects(reseal(bad_magic)),"Bad magic accepted");
        auto trailing=copy;trailing.insert(trailing.end()-4,std::uint8_t{0});check(rejects(reseal(trailing)),"Trailing bytes accepted");
    }
    // Install: one appended tile; existing faces rescaled; new UVs inside it.
    ObjectModels models;models.texture_width=4096;models.texture_height=256;models.texture_tiles=16;models.texture_rgba.resize(4096*256*4);
    ObjectModel dummy;ObjectMesh mesh;ObjectFace face;face.textured=true;face.uv={{{.5f,.5f},{.5f,.5f},{.5f,.5f},{.5f,.5f}}};mesh.faces.push_back(face);dummy.meshes.push_back(mesh);
    models.models.emplace(1,dummy);install_embedded_demons(models);
    check(models.texture_tiles==17 && models.texture_height==512,"Demons must append exactly one atlas tile");
    check(std::abs(models.models.at(1).meshes[0].faces[0].uv[0][1]-.25f)<1e-6f,"Existing atlas UVs must be rescaled");
    for(unsigned k=1;k<=demon_kind_count;++k) {
        const auto& m=models.models.at(demon_type(static_cast<DemonKind>(k)));
        for(const auto& f:m.meshes[0].faces)for(const auto& uv:f.uv)check(uv[1]>=.5f && uv[1]<=1 && uv[0]>=0 && uv[0]<=256.0f/4096,"Demon UV outside its tile");
    }
}
Level floor_level() {
    Level l;l.wad_slot=17;l.spawn={0,8,0};l.bounds={{-40,0,-40},{40,30,40}};
    l.triangles={{{-40,8,-40},{40,8,40},{40,8,-40}},{{-40,8,-40},{-40,8,40},{40,8,40}}};
    // Special-surface strip at x 14..18 and a pool at x -20..-14.
    l.triangles.push_back({{14,8.01f,-4},{18,8.01f,4},{18,8.01f,-4},7,false,10});l.triangles.push_back({{14,8.01f,-4},{14,8.01f,4},{18,8.01f,4},8,false,10});
    l.triangles.push_back({{-20,8.6f,-4},{-14,8.6f,4},{-14,8.6f,-4},9,true});l.triangles.push_back({{-20,8.6f,-4},{-20,8.6f,4},{-14,8.6f,4},10,true});
    return l;
}
RetailObject lizard(Vec3 p){RetailObject o;o.type=0x1f0;o.position=o.previous_position=p;EnemyState e;e.kind=EnemyKind::SmallLizard;o.enemy=e;return o;}
struct Fixture {ObjectModels models;Level level=floor_level();Fixture(){install_embedded_demons(models);}
    Game game()const{Scene s;s.wad_slot=17;s.spawn={0,8,0};Game g(level,s,{},&models);g.upgrades.enabled=true;unlock_all_elements(g);return g;}};
void spawning(const Fixture& f) {
    auto g=f.game();
    const auto ok=demon_spawn_point(g,{6,8,6});check(ok && std::abs(ok->y-8)<.01f,"Ordinary floor must accept an eruption");
    const auto special=demon_spawn_point(g,{16,8,0});check(!special || std::abs(special->x-16)>1.9f,"Special surfaces must reject eruptions");
    const auto pool=demon_spawn_point(g,{-17,8,0});check(!pool,"Flooded floor must reject eruptions");
    const auto spawn=demon_spawn_point(g,{0,8,0});check(spawn && length(*spawn-Vec3{0,8,0})>=1,"Spawn supports are protected");
    // Over water a floating rift opens near Spyro instead.
    check(g.demons.summon(g,&f.models,DemonKind::Imp,{-17,8,0}) && g.demons.floating_rifts==1,"Water summons must use a floating rift");
}
void caps_and_rules(const Fixture& f) {
    auto g=f.game();
    check(!g.demons.summon(g,nullptr,DemonKind::Imp,{3,8,3}),"Summons without models must be refunded");
    unsigned accepted=0;for(unsigned i=0;i<4;++i)accepted+=g.demons.summon(g,&f.models,DemonKind::Imp,{3,8,3});
    check(accepted==2 && g.demons.refunded>=2,"At most two eruptions per second");
    for(unsigned s=0;s<5;++s){ticks(g,61);g.demons.summon(g,&f.models,DemonKind::Imp,{3,8,3},2);}
    check(g.demons.active()<=DemonSystem::max_active && g.demons.refreshed>0,"Demon cap must refresh the oldest");
    g.demons.summon(g,&f.models,DemonKind::Maw,{0,8,4});g.demons.summon(g,&f.models,DemonKind::Maw,{0,8,4});
    unsigned maws=0;for(const auto& d:g.demons.demons())maws+=d.kind==DemonKind::Maw;
    check(maws==1,"Only one Abyssal Maw at a time");
    g.respawn();check(!g.demons.active(),"Respawn/level change must sink every demon");
    for(const auto& o:g.retail_objects)if(o.pc_actor)check(o.collected,"Sunk demons must release their runtime objects");
    // Never-target rules: a scripted object is ignored, an enemy is attacked.
    auto h=f.game();h.retail_objects.push_back(lizard({4,8,0}));const auto foe=h.retail_objects.size()-1;
    auto scripted=lizard({-4,8,0});scripted.scripted_contact=true;h.retail_objects.push_back(scripted);const auto npc=h.retail_objects.size()-1;
    h.demons.summon(h,&f.models,DemonKind::Imp,{0,8,1.5f});
    for(unsigned i=0;i<400 && !h.retail_objects[foe].demon_hit;++i)ticks(h,1);
    check(h.retail_objects[foe].demon_hit,"Imps must attack enemies");
    check(!h.retail_objects[npc].demon_hit && !h.retail_objects[npc].attack_flags,"Demons must never touch scripted objects");
    check(h.player.health==3,"Demons never hurt Spyro");
    // Dialogue/portal: immediate sink.
    h.scene_locked=true;ticks(h,1);check(!h.demons.active(),"Locked scenes sink demons");
}
void helpers(const Fixture& f) {
    auto g=f.game();g.demons.summon(g,&f.models,DemonKind::Wraith,{1,8,1});ticks(g,30);
    const auto hp=g.player.health;check(!g.hurt({1,8,0}) && g.player.health==hp && g.demons.hits_absorbed==1,"The Wraith must absorb one hit");
    g.player.invulnerable_ticks=0;ticks(g,1);check(g.hurt({1,8,0}) && g.player.health==hp-1,"Only one hit per Wraith summon");
    RetailObject shot;shot.position=shot.previous_position=g.player.position+Vec3{0,.4f,1.5f};shot.enemy_projectile=EnemyProjectile{{0,0,-1}};shot.runtime_spawned=true;
    g.retail_objects.push_back(shot);ticks(g,2);
    check(g.retail_objects.back().collected && g.demons.projectiles_blocked==1,"The Wraith must intercept enemy projectiles");
    // Hound fetches a gem Spyro has not reached.
    auto h=f.game();RetailObject gem;gem.type=1;gem.gem_value=5;gem.position=gem.previous_position={9,8.2f,4};h.retail_objects.push_back(gem);const auto gi=h.retail_objects.size()-1;
    h.demons.summon(h,&f.models,DemonKind::Hound,{2,8,2});
    for(unsigned i=0;i<900 && !h.demons.gems_fetched;++i)ticks(h,1);
    check(h.demons.gems_fetched==1 && length(h.retail_objects[gi].position-h.player.position)<2.5f,"The Hound must fetch a far gem to Spyro");
    // Imps open baskets for Sparx.
    auto k=f.game();RetailObject basket;basket.type=0xc8;basket.container=ContainerState{};basket.position=basket.previous_position={3,8,3};k.retail_objects.push_back(basket);
    const auto bi=k.retail_objects.size()-1;k.demons.summon(k,&f.models,DemonKind::Imp,{1,8,1});
    for(unsigned i=0;i<400 && !k.demons.containers_opened;++i)ticks(k,1);
    check(k.demons.containers_opened==1 && (k.retail_objects[bi].attack_flags || k.retail_objects[bi].state<0),"Imps must break nearby containers");
    // Water sinks walking demons.
    auto w=f.game();w.demons.summon(w,&f.models,DemonKind::Imp,{-12,8,0});ticks(w,30);
    for(const auto& d:w.demons.demons())if(d.object<w.retail_objects.size()){auto& o=w.retail_objects[d.object];o.position={-15.2f,8,0};}   // inside the pool and the 16-unit leash
    ticks(w,24);check(!w.demons.active(),"Demons must sink in water");
}
void performance(const Fixture& f) {
    auto g=f.game();
    for(unsigned i=0;i<6;++i)g.retail_objects.push_back(lizard({float(i)*2-5,8,7}));
    g.demons.summon(g,&f.models,DemonKind::Imp,{0,8,2},2);ticks(g,61);g.demons.summon(g,&f.models,DemonKind::Hound,{2,8,2});g.demons.summon(g,&f.models,DemonKind::Wraith,{-2,8,2});
    ticks(g,61);g.demons.summon(g,&f.models,DemonKind::Imp,{0,8,3},2);ticks(g,30);
    std::cout<<"demons active at measurement start "<<g.demons.active()<<" summoned "<<g.demons.summoned<<" refunded "<<g.demons.refunded<<std::endl;
    // The explicit tick doubles demon ageing, so 240 frames keep all six alive.
    double total=0;unsigned fewest=99;
    for(unsigned i=0;i<240;++i) {
        fewest=std::min(fewest,g.demons.active());
        const auto start=std::chrono::steady_clock::now();g.demons.tick(g,&f.models);
        total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        g.advance(FixedClock::step,{});
    }
    std::cout<<"demon AI mean ms/tick "<<total/240<<" fewest active "<<fewest<<" at end "<<g.demons.active()<<'\n';
    check(fewest==6,"Performance must be measured at the six-demon cap");check(total/240<.5,"Demon AI must stay under 0.5 ms per tick");
}
} // namespace
int main() {
    try {
        actor_format();const Fixture f;spawning(f);caps_and_rules(f);helpers(f);performance(f);
        std::cout<<"Demon checks passed: "<<checks<<'\n';return 0;
    } catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<" after "<<checks<<" checks\n";return 1;}
}
