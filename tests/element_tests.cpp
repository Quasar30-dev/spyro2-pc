#include "replay_hash.hpp"
#include "spyro/assets.hpp"
#include "spyro/savegame.hpp"
#include "spyro/pc_actors.hpp"
#include "spyro/multiplayer.hpp"
#include "spyro/element_art.hpp"
#include "spyro/interface.hpp"
#include <algorithm>
#include <iostream>
#include <cstdlib>
#include <numbers>
#include <fstream>
#include <sstream>

using namespace spyro;
namespace {
unsigned checks{};
void check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
void ticks(Game& g,unsigned n,InputState in={}){for(unsigned i=0;i<n;++i)g.advance(FixedClock::step,in);}
struct Retail {
    Level level;ObjectModels models;SpyroModel spyro;
    Retail(AssetStore& assets,WadArchive& wad,unsigned slot,bool demons=false,bool swim_clips=false) {
        const auto archive=wad.read(slot),exe=assets.read_all("SCUS_944.25");
        level=decode_level(archive,slot);models=decode_object_models(archive,exe);
        // The workbench imports Summer Forest's swim clips into every scene.
        spyro=swim_clips?decode_spyro(wad.read(0),archive,exe,wad.read(29),29,wad.read(15)):decode_spyro(wad.read(0),archive,exe,wad.read(29),29);
        if(demons)install_embedded_demons(models);
        initialize_retail_objects(level,models);
    }
    Game game()const{Scene s;s.wad_slot=level.wad_slot;s.spawn=level.spawn;return Game(level,s,{},&models,&spyro);}
};
// Golden fingerprints recorded from the build BEFORE the elemental powers.
constexpr std::uint64_t golden_glimmer_disabled=0x5bba386341502f1dull;
constexpr std::uint64_t golden_glimmer_upgrades=0xf8720b1f1cf2f892ull;
constexpr std::uint64_t golden_forest_supercharge=0x9f84b7d8e5c959d0ull;
std::uint64_t golden_scenario(const Retail& r,unsigned which) {
    auto g=r.game();
    if(which==1) {
        auto& u=g.upgrades;u.enabled=true;u.charge=u.flame=u.flight=u.glide=2;u.melee=u.carry=true;g.cheats.superflame=true;
    }
    if(which==2)g.cheats.supercharge=true;
    return replay::run(g,900);
}
Level floor_level() {
    Level l;l.wad_slot=17;l.spawn={0,8,0};l.bounds={{-60,0,-60},{60,40,60}};
    l.triangles={{{-60,8,-60},{60,8,60},{60,8,-60}},{{-60,8,-60},{-60,8,60},{60,8,60}}};return l;
}
Scene scene_of(const Level& l){Scene s;s.wad_slot=l.wad_slot;s.spawn=l.spawn;return s;}
RetailObject enemy(EnemyKind kind,Vec3 p){RetailObject o;o.type=0x1f0;o.position=o.previous_position=p;EnemyState e;e.kind=kind;o.enemy=e;return o;}
RetailObject container(unsigned type,Vec3 p){RetailObject o;o.type=static_cast<std::uint16_t>(type);o.position=o.previous_position=p;o.container=ContainerState{};return o;}

void translation_table() {
    const std::array<EnemyKind,11> kinds{EnemyKind::SmallLizard,EnemyKind::ClubLizard,EnemyKind::GearGrinder,EnemyKind::Robot,EnemyKind::MetalIdol,
        EnemyKind::BucketBlubber,EnemyKind::FireBlubber,EnemyKind::ElephantSnail,EnemyKind::RhinoSnail,EnemyKind::SpittingMonkey,EnemyKind::Platypus};
    for(const auto kind:kinds)for(unsigned e=0;e<element_count;++e)for(unsigned s=0;s<=3;++s) {
        const auto element=static_cast<Element>(e);const auto o=enemy(kind,{});
        const auto mask=element_retail_mask(element,s,o);
        if(!s){check(!mask,"Strength-0 status hits must not carry a retail damage mask");continue;}
        check(!(mask&0x10000000u),"Elements must never add the heavy-door bit");
        switch(element) {
        case Element::Fire:check(mask==(s>=2?superflame_attack:flame_attack),"Fire translation differs from retail flame/superflame");break;
        case Element::Water:
            if(metal_enemy(o))check(!mask,"Water damaged metal enemies");
            else if(kind==EnemyKind::FireBlubber || kind==EnemyKind::ElephantSnail)check(mask==flame_attack,"Water must use fire creatures' flame weakness");
            else check(mask==(s>=2?charge_attack:0u),"Water organic translation");
            break;
        case Element::Ice:check(!mask,"Ice must freeze, not kill directly");break;
        case Element::Electric:
            check(mask==(metal_enemy(o)?(charge_attack|flame_attack):flame_attack),"Electricity translation");
            if(metal_enemy(o) && kind!=EnemyKind::GearGrinder)check(enemy_accepts_attack(o,mask),"Electricity must beat machines");
            break;
        case Element::Convexity:check(mask==(s>=3?(0x570000u|headbash_attack):(superflame_attack|headbash_attack)),"Convexity translation");
            check(enemy_accepts_attack(o,mask) || kind==EnemyKind::Platypus,"Convexity strength must defeat every ordinary family");break;
        }
    }
    for(const unsigned type:{0xc8u,0xc9u,0xd1u,0xcau,0xcbu}) {
        const auto o=container(type,{});
        // Every element breaks every container, from its weakest breath upward.
        for(unsigned el=0;el<element_count;++el) {
            const auto mask=element_retail_mask(static_cast<Element>(el),1,o);
            check(mask && container_accepts_attack(o,std::span<const RetailObject>(&o,1),mask),"Every breath must break every container type");
        }
    }
    RetailObject target;target.power_target=PowerTarget{};
    check(power_target_accepts(target,element_retail_mask(Element::Convexity,2,target)),"Convexity must break power targets");
    check(!power_target_accepts(target,element_retail_mask(Element::Fire,1,target)),"Ember strength must not break power targets");
    for(unsigned e=0;e<element_count;++e)for(unsigned s=1;s<=3;++s) {
        const auto flags=element_boss_flags(static_cast<Element>(e),s);
        check(flags && !(flags&0x400000u),"Boss damage must never use the 15 HP green-bomb bit");
    }
    check(element_boss_flags(Element::Convexity,3)==0x140000u && element_boss_flags(Element::Fire,2)==headbash_attack,"Boss HP table");
}
void statuses() {
    const auto level=floor_level();Game g(level,scene_of(level));
    g.upgrades.enabled=true;unlock_all_elements(g);
    g.retail_objects.push_back(enemy(EnemyKind::SmallLizard,{4,8,0}));auto index=g.retail_objects.size()-1;
    auto& o=[&]()->RetailObject&{return g.retail_objects[index];}();
    queue_element_hit(o,{Element::Water,1,{0,8,0},element_status::soak|element_status::push,false,7});
    g.elements.resolve(g,nullptr);
    check(g.retail_objects[index].soak_ticks==360 && g.elements.soaks==1,"Water must soak");
    check(length(g.retail_objects[index].push_velocity)>6,"Torrent must push away from its source");
    queue_element_hit(g.retail_objects[index],{Element::Ice,1,{0,8,0},element_status::freeze,false,0});
    g.elements.resolve(g,nullptr);
    check(g.retail_objects[index].frozen_ticks==450 && g.retail_objects[index].element_frozen,"Wet + Ice must freeze instantly for 1.5x");
    queue_element_hit(g.retail_objects[index],{Element::Water,1,{1,8,0},element_status::soak,false,0});
    g.elements.resolve(g,nullptr);
    const auto& shattered=g.retail_objects[index];
    check(!shattered.frozen_ticks && g.elements.shatters==1 && (shattered.attack_flags&flame_attack) && shattered.attack_sourced,
        "Frozen + any impact must shatter with an attacker source");
    // Fire thaws and dries.
    g.retail_objects.push_back(enemy(EnemyKind::ClubLizard,{-4,8,0}));index=g.retail_objects.size()-1;
    auto& club=g.retail_objects[index];club.soak_ticks=200;club.frozen_ticks=200;club.element_frozen=true;
    queue_element_hit(club,{Element::Fire,1,{0,8,0},0,false,0});g.elements.resolve(g,nullptr);
    check(!g.retail_objects[index].soak_ticks && !g.retail_objects[index].frozen_ticks,"Fire must thaw and dry");
    // Wet + Electric stuns longer.
    auto& wet=g.retail_objects[index];wet.soak_ticks=100;
    queue_element_hit(wet,{Element::Electric,1,{0,8,0},element_status::stun,false,0});g.elements.resolve(g,nullptr);
    check(g.retail_objects[index].stun_ticks==150,"Wet targets stay stunned longer");
    // Bosses: HP table only, no freeze/stun.
    RetailObject boss;boss.boss_body=BossBody{1,0,2};boss.position={0,8,6};g.retail_objects.push_back(boss);index=g.retail_objects.size()-1;
    queue_element_hit(g.retail_objects[index],{Element::Ice,3,{0,8,0},element_status::freeze|element_status::stun,false,0});g.elements.resolve(g,nullptr);
    check(g.retail_objects[index].attack_flags==0x140000u && !g.retail_objects[index].frozen_ticks && !g.retail_objects[index].stun_ticks,"Boss element hits");
    // Never-target rules.
    RetailObject npc=enemy(EnemyKind::SmallLizard,{});npc.scripted_contact=true;check(!element_target(npc),"Scripted objects are not elemental targets");
    RetailObject gate;gate.powerup_gate=PowerupGate{};gate.power_target=PowerTarget{};check(!element_target(gate),"Power gates are not elemental targets");
    RetailObject demon=enemy(EnemyKind::SmallLizard,{});demon.pc_actor=1;check(!element_target(demon),"Demons are not elemental targets");
}
void precedence_and_meters() {
    const auto level=floor_level();Game g(level,scene_of(level));
    check(!g.elements.breath_override(g),"Elements disabled must leave F to retail fire");
    g.upgrades.enabled=true;unlock_all_elements(g);g.upgrades.elements.equipped=Element::Fire;
    check(!g.elements.breath_override(g),"Fire equipped keeps the retail flame");
    g.upgrades.elements.equipped=Element::Water;check(g.elements.breath_override(g),"Water breath must own F");
    g.powers.fire={100,100};check(!g.elements.breath_override(g),"Superflame gate timers keep priority");g.powers.fire={};
    g.powers.ice={100,100};check(!g.elements.breath_override(g),"Superfreeze gate timers keep priority");g.powers.ice={};
    g.cheats.superflame=true;check(g.elements.breath_override(g),"A selected element overrides only the any-level cheat, not gate timers");
    g.cheats.superflame=false;
    // Energy: 25/s breath drain.
    g.upgrades.elements.energy=100;InputState press;press.flame_pressed=true;press.flame_held=true;InputState hold;hold.flame_held=true;
    g.advance(FixedClock::step,press);ticks(g,59,hold);
    check(g.elements.breaths==1 && std::abs(g.upgrades.elements.energy-75)<1.5f,"Breath must drain about 25 energy per second");
    check(!g.elements.breath_nodes().empty(),"Torrent must emit original breath bodies");
    ticks(g,20);const float idle=g.upgrades.elements.energy;ticks(g,30);   // 0.3 s delay, then 48/s
    check(g.upgrades.elements.energy>idle+20,"Energy must recharge quickly after a short pause");
    // Shot cost 20.
    g.upgrades.elements.energy=100;InputState shot;shot.shot_pressed=true;g.advance(FixedClock::step,shot);ticks(g,2);
    check(g.elements.shots_fired==1 && std::abs(g.upgrades.elements.energy-80)<1,"Bubble shot must cost 20 energy");
    check(!g.elements.shots().empty() && g.elements.shots().front().kind==ShotKind::Bubble,"Water shot must be the original Bubble");
    // Fury from defeats.
    g.upgrades.elements.fury=0;g.retail_objects.push_back(enemy(EnemyKind::SmallLizard,{8,8,8}));
    g.retail_objects.back().enemy->defeated=true;ticks(g,1);
    check(std::abs(g.upgrades.elements.fury-ElementSystem::fury_per_defeat)<.2f,"A defeat must add 30 Fury");
    {const float before=g.upgrades.elements.fury;ticks(g,60);check(g.upgrades.elements.fury>before+4.5f,"Fury must also fill passively");}
    // Fury requires a full meter and tier 3.
    g.upgrades.elements.fury=100;InputState fury;fury.fury_pressed=true;g.advance(FixedClock::step,fury);ticks(g,2);
    check(g.elements.furies==1 && g.elements.fury_pose() && !g.upgrades.elements.fury,"Fury must start from a full meter");
    const auto hp=g.player.health;check(!g.hurt({0,8,0}) && g.player.health==hp,"Fury levitation is untouchable");
}
void progression_and_saves() {
    check(element_tier_cost(Element::Water,0)==30 && element_tier_cost(Element::Water,1)==50 && element_tier_cost(Element::Water,2)==80 && !element_tier_cost(Element::Water,3),"Element tier costs");
    check(element_tier_cost(Element::Convexity,0)==60 && element_tier_cost(Element::Fire,1)==50,"Convexity/Fire costs");
    const auto level=floor_level();Game g(level,scene_of(level));
    check(g.upgrades.elements.tier(Element::Fire)==1 && g.upgrades.elements.valid(),"Fire tier 1 is the retail flame");
    auto& u=g.upgrades;u.points=1000;
    check(!u.buy_element(Element::Water,false),"Disabled progression bought an element");u.enabled=true;
    check(u.buy_element(Element::Water,false) && u.points==970 && u.elements.equipped==Element::Water,"First element purchase");
    check(!element_purchasable(g,Element::Convexity),"Convexity unlocked too early");
    check(!u.buy_element(Element::Convexity,false),"Convexity bought without its milestone");
    u.buy_element(Element::Ice,false);u.buy_element(Element::Electric,false);
    check(!element_purchasable(g,Element::Convexity),"Convexity needs the campaign milestone");
    g.ripto_defeated=true;check(element_purchasable(g,Element::Convexity) && u.buy_element(Element::Convexity,convexity_milestone(g)),"Convexity after milestone");
    auto bad=u.elements;bad.tiers[1]=4;check(!bad.valid(),"Invalid tier accepted");bad=u.elements;bad.energy=200;check(!bad.valid(),"Invalid energy accepted");
    // S2SAVE 19 round trip (elements v16, Convexity cutting settings v17, script memory v18, shards v19).
    u.elements.energy=42.5f;u.elements.fury=77;u.elements.equipped=Element::Ice;g.cheats.dark_spyro=true;g.cheats.dark_smoke=false;
    g.destruction_settings.void_slice=false;g.destruction_settings.slice_size=3;
    GameSave save;save.capture(g);
    const auto file=std::filesystem::temp_directory_path()/"spyro-elements-test.s2save";save.write(file);
    std::string header;{std::ifstream in(file,std::ios::binary);std::getline(in,header);}
    check(header.rfind("S2SAVE 19 ",0)==0,"Saves must be written as S2SAVE 19");
    const auto loaded=GameSave::read(file);
    check(loaded.upgrades.elements==u.elements && loaded.cheats.dark_spyro && !loaded.cheats.dark_smoke,"Element progression/Dark Spyro save round trip");
    check(!loaded.destruction_settings.void_slice && loaded.destruction_settings.slice_size==3,"Convexity cutting settings save round trip");
    // An S2SAVE 15 payload (no elemental block) still loads with elements locked.
    std::ifstream in(file,std::ios::binary);std::string magic;unsigned version{},crc{},size{};in>>magic>>version>>crc>>size;in.get();
    std::string payload(size,'\0');in.read(payload.data(),size);in.close();
    // Drop the v19 block (one level's shards), the v18 block (one level's script memory),
    // the v17 block (settings + one level's cuts) and the v16 block (elements + tints).
    for(unsigned line=0;line<6;++line)payload.erase(payload.rfind('\n',payload.size()-2)+1);
    const auto old_crc=crc32({reinterpret_cast<const std::uint8_t*>(payload.data()),payload.size()});
    {std::ofstream out(file,std::ios::binary);out<<"S2SAVE 15 "<<old_crc<<' '<<payload.size()<<'\n'<<payload;}
    const auto legacy=GameSave::read(file);std::filesystem::remove(file);
    check(legacy.upgrades.elements==ElementProgress{} && !legacy.cheats.dark_spyro && legacy.cheats.dark_smoke,"Version 15 saves load with elements locked");
    // Online snapshot fields.
    OnlineState net;net.world=online_world(level);net.form=1;net.element=4;std::istringstream packet(encode_online_state(net));
    const auto decoded=decode_online_state(packet);check(decoded.form==1 && decoded.element==4,"Online Dark Spyro/element round trip");
    net.form=2;check(!net.valid(),"Unknown online form accepted");net.form=0;net.element=5;check(!net.valid(),"Unknown online element accepted");
}
void dark_form() {
    const auto level=floor_level();Game base(level,scene_of(level)),dark(level,scene_of(level));
    dark.cheats.dark_spyro=true;
    InputState run;run.move_z=1;
    ticks(base,120,run);ticks(dark,120,run);
    const float vb=std::hypot(base.player.velocity.x,base.player.velocity.z),vd=std::hypot(dark.player.velocity.x,dark.player.velocity.z);
    check(std::abs(vd/vb-dark.elements.dark_tuning.run)<.01f,"Dark Spyro must run faster");
    Game jb(level,scene_of(level)),jd(level,scene_of(level));jd.cheats.dark_spyro=true;
    ticks(jb,10);ticks(jd,10);
    InputState jump;jump.jump_pressed=jump.jump_held=true;InputState held;held.jump_held=true;
    jb.advance(FixedClock::step,jump);jd.advance(FixedClock::step,jump);
    float pb=0,pd=0;for(unsigned i=0;i<60;++i){jb.advance(FixedClock::step,held);jd.advance(FixedClock::step,held);pb=std::max(pb,jb.player.position.y);pd=std::max(pd,jd.player.position.y);}
    check(pd-8>(pb-8)*1.15f,"Dark Spyro must jump higher");
    check(dark.elements.smoke.emitted()>40 && !base.elements.smoke.emitted(),"Dark Spyro emits black smoke");
    Game quiet(level,scene_of(level));quiet.cheats.dark_spyro=true;quiet.cheats.dark_smoke=false;ticks(quiet,60);
    check(!quiet.elements.smoke.emitted(),"Black smoke can be switched off");
    check(dark.elements.dark(dark) && !dark.upgrades.enabled,"Dark Spyro is a free cosmetic, independent of progression");
}
// Measured timelines of the requested mechanics, printed for the record:
// breath recharge from empty, the Water headbash pocket and the Water Fury
// flood (rise, extent, swimming, drain), on Glimmer with the retail model.
void mechanic_timelines(const Retail& glimmer) {
    auto g=glimmer.game();g.upgrades.enabled=true;unlock_all_elements(g);g.upgrades.elements.equipped=Element::Water;ticks(g,10);
    // Recharge: empty meter, Spyro idle.
    g.upgrades.elements.energy=0;unsigned refill=0;
    while(g.upgrades.elements.energy<100 && refill<600){ticks(g,1);++refill;}
    std::cout<<"Recharge: empty to full in "<<refill<<" ticks ("<<refill/60.0f<<" s)\n";
    check(refill<=150,"Breath energy must refill from empty within 2.5 s");
    // Water headbash pocket.
    const auto feet=g.player.position;g.elements.pocket(g,feet);
    unsigned rise=0;float peak=0,floor=0;bool swam=false;
    for(unsigned t=0;t<120;++t){ticks(g,1);if(g.level.dynamic_water.empty())break;const auto& w=g.level.dynamic_water.front();floor=w.floor;
        if(w.height>peak+1e-4f){peak=w.height;rise=t+1;}swam|=g.player.swim.mode!=WaterMode::Dry;}
    float reach=0;for(float r=0;r<6;r+=.05f)if(g.level.water_at(feet+retail::origin_above_feet+Vec3{r,0,0}))reach=r;
    unsigned life=120;while(!g.level.dynamic_water.empty() && life<3000){ticks(g,1);++life;}
    std::cout<<"Water pocket: rises "<<peak-floor<<" units in "<<rise<<" ticks, swimmable out to "<<reach<<" units, Spyro swims "<<swam
             <<", gone after "<<life/60.0f<<" s\n";
    check(swam && reach>1.5f && reach<4 && life>600 && life<1200,"The pocket must be a local swimmable column that drains");
    // Water Fury flood.
    g.upgrades.elements.fury=100;InputState fury;fury.fury_pressed=true;g.advance(FixedClock::step,fury);
    unsigned start=0,top=0,end=0,t=0;float high=0,base=0;bool swimming=false;float far_cover=0;
    for(;t<9000;++t) {
        ticks(g,1);
        if(g.level.dynamic_water.empty()){if(start){end=t;break;}continue;}
        const auto& w=g.level.dynamic_water.front();if(!w.flood)continue;
        if(!start){start=t;base=w.floor;}
        if(w.height>high+1e-4f){high=w.height;top=t;}
        swimming|=g.player.swim.mode!=WaterMode::Dry;
        if(t==top+60)for(float r=0;r<40;r+=1)if(g.level.water_at(g.player.position+Vec3{r,.4f,0}))far_cover=r;
    }
    std::cout<<"Water Fury flood: "<<high-base<<" units deep after "<<(top-start)/60.0f<<" s, covers at least "<<far_cover
             <<" units out, Spyro swims "<<swimming<<", drained after "<<(end-top)/60.0f<<" s\n";
    check(swimming && far_cover>=10 && end>top && (end-top)>=30*60,"The flood must cover the level, be swimmable and drain slowly");
}
// The element meter is the mounted retail power meter (resident 22..26) in
// its own colours: only dark silhouette-edge texels take the element's
// outline colour, so the meter is never reshaded or darkened. Each element
// has its own GPT Image emblem in place of the retail superwing.
void meter_unshaded(AssetStore& assets,WadArchive& wad) {
    const auto exe=assets.read_all("SCUS_944.25");
    const auto ui=decode_interface(wad.read(17),exe,wad.read(4));
    const std::array<HudSprite,5> parts{ui.resident[22],ui.resident[23],ui.resident[24],ui.resident[25],ui.resident[26]};
    const auto atlas=element_hud_atlas(&ui,&parts);
    check(atlas.valid() && atlas.meter,"The element meter must build from the mounted retail power meter");
    const auto luminance=[](const std::uint8_t* c){return c[0]*.30+c[1]*.59+c[2]*.11;};
    unsigned same=0,outline=0,other=0;double retail_light=0,element_light=0;
    for(unsigned e=0;e<element_count;++e)for(unsigned k=0;k<5;++k) {
        const auto& s=parts[k];const auto& d=atlas.parts[e][k];if(!s.width)continue;
        check(d.width==s.width && d.height==s.height,"An element meter part changed size");
        for(unsigned y=0;y<s.height;++y)for(unsigned x=0;x<s.width;++x) {
            const auto* in=&ui.rgba[(std::size_t(s.y+y)*ui.width+s.x+x)*4];
            const auto* out=&atlas.rgba[(std::size_t(d.y+y)*atlas.width+d.x+x)*4];
            if(!in[3])continue;
            retail_light+=luminance(in);element_light+=luminance(out);
            if(std::equal(in,in+4,out))++same;else if(luminance(in)<70)++outline;else ++other;
        }
    }
    unsigned emblems=0;for(const auto& emblem:atlas.emblems)emblems+=emblem.width>0 && emblem.height>0;
    std::cout<<"Element meter: "<<same<<" texels identical to the retail meter, "<<outline<<" outline texels recoloured, "<<other
             <<" other changes; mean brightness "<<element_light/std::max(1.0,retail_light)*100<<"% of retail; "<<emblems<<" emblems\n";
    check(other==0,"The element meter reshades texels other than its dark outline");
    check(outline>0 && same>outline,"The element meter must keep the retail texels and recolour only the outline");
    check(element_light>=retail_light*.97,"The element meter is darker than the retail power meter");
    check(emblems==element_count,"Every element needs its own emblem in place of the superwing");
}
// Dark Spyro's glowing eyes leave only a short streak: at most five positions
// (1/15 s at 60 Hz), so at full dark-run speed it trails a fraction of a unit.
void dark_eye_trail(const Retail& glimmer) {
    auto g=glimmer.game();g.cheats.dark_spyro=true;InputState run;run.move_z=1;
    ticks(g,60,run);
    std::size_t points=0;float longest=0,speed=0;
    for(unsigned t=0;t<90;++t) {
        ticks(g,1,run);speed=std::max(speed,std::hypot(g.player.velocity.x,g.player.velocity.z));
        for(const auto& trail:g.elements.eye_trails()) {
            points=std::max(points,trail.size());float length_sum=0;
            for(std::size_t i=1;i<trail.size();++i)length_sum+=length(trail[i]-trail[i-1]);
            longest=std::max(longest,length_sum);
        }
    }
    std::cout<<"Dark Spyro eye trail: "<<points<<" points, longest "<<longest<<" units at "<<speed<<" units/s\n";
    check(points>=2 && points<=5,"Dark Spyro's eye trail must keep at most five positions");
    check(longest>0 && longest<=speed*4.5f/60,"Dark Spyro's eye trail must stay within 1/15 s of travel");
}
void retail_combat(AssetStore& assets,WadArchive& wad) {
    const Retail glimmer(assets,wad,17,true);
    check(glimmer.models.models.contains(demon_type(DemonKind::Imp)) && glimmer.models.models.contains(demon_type(DemonKind::Maw)),"Embedded demons must install into the scene models");
    const auto setup=[&](Element element,unsigned record) {
        auto g=glimmer.game();g.upgrades.enabled=true;unlock_all_elements(g);g.upgrades.elements.equipped=element;
        const auto target=g.retail_objects.at(record).position;
        // A floor point ~2.2 units away at the target's height with a clear view of it.
        auto start=target+Vec3{-2.2f,0,0};
        for(unsigned k=0;k<16;++k) {
            const float a=float(k)*std::numbers::pi_v<float>/8;const auto probe=target+Vec3{std::cos(a)*2.2f,0,std::sin(a)*2.2f};
            const auto floor=g.level.raycast(probe+Vec3{0,1.5f,0},{0,-1,0},3);
            if(!floor || floor->normal.y<.7f || std::abs(floor->point.y-target.y)>.6f)continue;
            const auto eye=floor->point+Vec3{0,.6f,0},aim=target+Vec3{0,.4f,0};
            if(g.level.raycast(eye,aim-eye,length(aim-eye)-.3f))continue;
            start=floor->point;break;
        }
        g.play_scene.spawn=start;g.respawn();
        const auto d=target-g.player.position;g.player.yaw=g.player.previous_yaw=std::atan2(d.x,d.z);
        return g;
    };
    constexpr unsigned lizard=2;   // Glimmer small lizard (record 2)
    {
        auto g=setup(Element::Electric,lizard);InputState press;press.flame_pressed=press.flame_held=true;InputState hold;hold.flame_held=true;
        g.advance(FixedClock::step,press);for(unsigned i=0;i<70 && !g.retail_objects[lizard].enemy->defeated;++i)g.advance(FixedClock::step,hold);
        check(g.retail_objects[lizard].enemy->defeated,"Arc breath must defeat a Glimmer lizard");
        check(g.elements.bolts().size()+g.elements.stuns>0,"Arc breath must draw original bolts");
    }
    {
        auto g=setup(Element::Ice,lizard);InputState press;press.flame_pressed=press.flame_held=true;InputState hold;hold.flame_held=true;
        g.advance(FixedClock::step,press);for(unsigned i=0;i<70 && !g.retail_objects[lizard].frozen_ticks;++i)g.advance(FixedClock::step,hold);
        check(g.retail_objects[lizard].frozen_ticks && g.retail_objects[lizard].element_frozen,"Frost breath must freeze a Glimmer lizard");
        InputState shot;shot.shot_pressed=true;g.advance(FixedClock::step,shot);
        for(unsigned i=0;i<60 && !g.retail_objects[lizard].enemy->defeated;++i)g.advance(FixedClock::step,{});
        check(g.retail_objects[lizard].enemy->defeated && g.elements.shatters>=1,"Ice Shards must shatter the frozen lizard");
    }
    {
        auto g=setup(Element::Convexity,lizard);InputState press;press.flame_pressed=press.flame_held=true;InputState hold;hold.flame_held=true;
        g.advance(FixedClock::step,press);for(unsigned i=0;i<70 && !g.retail_objects[lizard].enemy->defeated;++i)g.advance(FixedClock::step,hold);
        check(g.retail_objects[lizard].enemy->defeated,"Void Beam must defeat a Glimmer lizard");
        ticks(g,4);check(g.demons.summoned>=1,"A void-marked defeat must raise a demon");
        ticks(g,30);check(g.demons.active()>=1,"The demon must be active");
    }
    {
        // Headbash shockwave (sphere) from the Electricity tier 3.
        auto g=setup(Element::Electric,lizard);g.upgrades.headbash=1;ticks(g,6);   // settle onto the floor after respawn
        InputState jump;jump.jump_pressed=jump.jump_held=true;g.advance(FixedClock::step,jump);ticks(g,10);
        InputState bash;bash.headbash_pressed=true;g.advance(FixedClock::step,bash);
        for(unsigned i=0;i<150 && !g.elements.waves;++i)g.advance(FixedClock::step,{});
        check(g.elements.waves==1 && !g.elements.shockwaves().empty() && g.elements.shockwaves().front().shape==ShockShape::Sphere,"Electric headbash must expand a sphere");
        ticks(g,30);
        check(g.elements.wave_hits>=1 && g.retail_objects[lizard].enemy->defeated,"The thunder sphere must reach the lizard");
    }
    {
        // Fury: Convexity enters Dark Spyro and summons Hound + Wraith.
        auto g=setup(Element::Convexity,lizard);g.upgrades.elements.fury=100;
        InputState fury;fury.fury_pressed=true;g.advance(FixedClock::step,fury);ticks(g,ElementSystem::fury_pose_duration+20);
        check(g.elements.dark_fury() && g.elements.dark(g) && g.demons.summoned>=2,"Convexity Fury must enter Dark Spyro with demons");
        const auto hp=g.player.health;g.hurt(g.player.position+Vec3{1,0,0});
        check(g.player.health==hp,"Dark Fury or the Wraith must prevent health loss");
    }
}
} // namespace

void elemental_water(AssetStore& assets,WadArchive& wad) {
    const Retail glimmer(assets,wad,17,false,true);
    check(glimmer.spyro.animations.contains(44) && glimmer.spyro.animations.contains(40),"Swim clips must be importable into a dry scene");
    auto g=glimmer.game();g.upgrades.enabled=true;unlock_all_elements(g);ticks(g,10);
    const bool retail_swim=g.level.surface_swimming;
    // A Water headbash pocket: a swimmable column at Spyro's feet that drains away.
    g.elements.pocket(g,g.player.position);ticks(g,40);
    const auto origin=g.player.position+retail::origin_above_feet;
    const auto surface=g.level.water_at(origin);
    check(surface && surface->height>g.level.dynamic_water.front().floor+1.4f,"A water pocket must rise around Spyro");
    check(g.player.swim.mode!=WaterMode::Dry && g.level.surface_swimming,"Spyro must swim in a water pocket");
    check(!g.level.water_at(g.player.position+Vec3{6,.4f,0}),"Pockets are local");
    ticks(g,600+360+20);
    check(g.level.dynamic_water.empty() && g.player.swim.mode==WaterMode::Dry && g.level.surface_swimming==retail_swim,"Pockets drain and Spyro stands again");
    // The Water Fury floods the whole level, holds, then slowly drains.
    g.upgrades.elements.equipped=Element::Water;g.upgrades.elements.fury=100;
    InputState fury;fury.fury_pressed=true;g.advance(FixedClock::step,fury);ticks(g,ElementSystem::fury_pose_duration+200);
    check(g.elements.floods==1 && !g.level.dynamic_water.empty() && g.level.dynamic_water.front().flood,"Water Fury must flood the level");
    const float peak=g.level.dynamic_water.front().height;
    check(g.level.water_at(g.player.position+Vec3{6,.4f,3}).has_value(),"The flood covers the level beyond Spyro");
    check(g.player.swim.mode!=WaterMode::Dry,"Spyro must swim in the flood");
    ticks(g,720+600);check(g.level.dynamic_water.front().height<peak-.3f,"The flood must slowly drain");
    ticks(g,2700);
    check(g.level.dynamic_water.empty() && g.player.swim.mode==WaterMode::Dry,"The flood drains away completely");
}
// A pond: bed at y 4, retail water surface (two triangles) at y 8, dry banks beyond.
Level pond_level() {
    Level l;l.wad_slot=17;l.spawn={0,12,0};l.bounds={{-60,0,-60},{60,40,60}};l.surface_swimming=l.underwater_swimming=true;
    l.triangles={{{-40,4,-40},{40,4,40},{40,4,-40}},{{-40,4,-40},{-40,4,40},{40,4,40}}};
    Triangle w0{{-40,8,-40},{40,8,40},{40,8,-40}},w1{{-40,8,-40},{-40,8,40},{40,8,40}};w0.water=w1.water=true;
    l.triangles.push_back(w0);l.triangles.push_back(w1);
    for(auto& t:l.triangles)t.source_id=static_cast<std::uint32_t>(&t-l.triangles.data());
    return l;
}
void frozen_water(AssetStore& assets,WadArchive& wad) {
    const auto exe=assets.read_all("SCUS_944.25");
    // Retail water: Summer Forest's water becomes one connected sheet of ice.
    {
        Retail forest(assets,wad,15);auto g=forest.game();g.upgrades.enabled=true;unlock_all_elements(g);ticks(g,4);
        std::optional<Vec3> centre;float best=0;unsigned water_triangles=0,flat=0;
        for(const auto& t:g.level.triangles)if(t.water && !t.disabled) {
            ++water_triangles;const auto n=cross(t.b-t.a,t.c-t.a);if(std::abs(normalized(n).y)<.9f)continue;
            ++flat;if(length(n)>best){best=length(n);centre=(t.a+t.b+t.c)/3;}
        }
        if(std::getenv("ICE_TRACE"))std::cout<<"forest water "<<water_triangles<<" flat "<<flat<<" at "<<(centre?centre->x:0)<<','<<(centre?centre->y:0)<<','<<(centre?centre->z:0)<<'\n';
        check(centre.has_value(),"Summer Forest must have retail water");
        const auto triangles=g.level.triangles.size();
        check(g.elements.freeze_at(g,*centre) && g.elements.frozen_bodies==1,"Ice must freeze a retail water body");
        const auto& body=g.elements.ice().front();
        check(body.surfaces.size()>=2 && body.collision.size()==body.surfaces.size() && !body.sheet.empty(),"The whole connected surface freezes");
        check(g.level.triangles.size()==triangles+body.collision.size(),"One ice collision triangle per frozen surface triangle");
        check(!g.elements.freeze_at(g,*centre),"A frozen body is not frozen twice");
        const auto hit=g.level.raycast(*centre+Vec3{0,1,0},{0,-1,0},1.5f);
        check(hit && hit->surface_type==4,"Frozen retail water is solid type-4 ice");
        const unsigned more=g.elements.freeze_all(g);
        check(g.elements.frozen_bodies==1+more && g.elements.ice().size()==1+more,"Glacier freezes every remaining body");
        for(const auto& t:g.level.triangles)if(t.water && !t.disabled && std::abs(normalized(cross(t.b-t.a,t.c-t.a)).y)>=.5f)
            check(std::any_of(g.elements.ice().begin(),g.elements.ice().end(),[&](const auto& f){return f.contains((t.a+t.b+t.c)/3,.3f);}),"Glacier left water unfrozen");
    }
    // Skating: Winter Tundra's clip and state 9 speeds on any frozen water.
    Retail glimmer(assets,wad,17);
    glimmer.spyro=decode_spyro(wad.read(0),wad.read(17),exe,wad.read(29),29,wad.read(15),{},{},wad.read(57));
    check(glimmer.spyro.animations.contains(9),"Winter Tundra's skate clip must import into any scene");
    const auto pond=pond_level();
    Game g(pond,scene_of(pond),{},&glimmer.models,&glimmer.spyro);g.upgrades.enabled=true;unlock_all_elements(g);
    g.player.position=g.player.previous_position={0,8.2f,0};ticks(g,30);
    check(g.player.swim.mode!=WaterMode::Dry,"Spyro must swim in the unfrozen pond");
    check(g.elements.freeze_at(g,{g.player.position.x,8,g.player.position.z}),"Ice must freeze the pond Spyro swims in");
    check(g.player.swim.mode==WaterMode::Dry && g.player.position.y>8,"A surface swimmer is lifted onto the new ice");
    ticks(g,10);
    check(g.player.grounded && g.player.swim.mode==WaterMode::Dry,"Spyro stands on the ice");
    InputState push;push.move_z=1;ticks(g,90,push);
    const float unit=MovementTuning::velocity_unit;
    const auto speed=[&]{return std::hypot(g.player.velocity.x,g.player.velocity.z);};
    check(g.player.skate.active && g.player.motion==Motion::Skate && motion_animation(g.player.motion)==9,"Spyro skates on frozen water (clip 9)");
    check(std::abs(speed()-0x12c0*unit)<0x40*unit,"Pushing on skates at state 9's 0x12c0");
    ticks(g,60);
    check(g.player.skate.active && std::abs(speed()-0xf00*unit)<0x40*unit,"Released, Spyro keeps skating forward at 0xf00");
    // Fire melts it: the sheet thaws over ice_melt ticks and the pond is water again.
    g.player.position=g.player.previous_position={0,8.05f,0};g.player.velocity={};ticks(g,2);
    check(g.elements.thaw_at(g,{0,8,0}),"Fire must start a thaw");
    ticks(g,ElementSystem::ice_melt+2);
    check(g.elements.thawed_bodies==1 && g.elements.ice().empty(),"The ice melts away");
    check(std::all_of(g.level.triangles.begin()+4,g.level.triangles.end(),[](const Triangle& t){return t.disabled;}),"Melted ice leaves no collision");
    ticks(g,90);
    check(g.player.swim.mode!=WaterMode::Dry && !g.player.skate.active,"Spyro is back in the water after the thaw");
    // Volumes: touching boxes at one height freeze together; a box at another height does not.
    Level pool=floor_level();pool.triangles.front().a.y=pool.triangles.front().b.y=pool.triangles.front().c.y=2;
    pool.triangles.back().a.y=pool.triangles.back().b.y=pool.triangles.back().c.y=2;
    pool.water_volumes={{{{-10,2,-10},{0,6,10}},6},{{{0,2,-10},{10,6,10}},6},{{{20,2,20},{30,4,30}},4}};
    Game v(pool,scene_of(pool));v.upgrades.enabled=true;unlock_all_elements(v);
    check(v.elements.freeze_at(v,{-5,6,0}) && v.elements.ice().front().volumes.size()==2,"Touching volumes freeze as one body");
    check(!v.elements.freeze_at(v,{5,6,0}),"The connected volume is already frozen");
    check(v.elements.freeze_at(v,{25,4,25}) && v.elements.frozen_bodies==2,"A separate volume freezes on its own");
    const auto reuse=v.level.triangles.size();
    ticks(v,ElementSystem::ice_life+ElementSystem::ice_melt+2);
    check(v.elements.ice().empty() && v.elements.thawed_bodies==2,"Ice thaws on its own after ice_life");
    check(v.elements.freeze_at(v,{-5,6,0}) && v.level.triangles.size()==reuse,"Refreezing reuses thawed ice triangles");
}
// Every element's breath destroys every crate: upgraded Fire's retail flame
// also breaks Summer Forest's metal-capped vases (C9), which the original
// tier-1 flame only heats.
void fire_breaks_metal_vases(AssetStore& assets,WadArchive& wad) {
    const Retail forest(assets,wad,15);
    for(const unsigned tier:{1u,2u}) {
        auto g=forest.game();g.upgrades.enabled=true;unlock_all_elements(g);
        g.upgrades.elements.tiers[static_cast<unsigned>(Element::Fire)]=tier;g.upgrades.elements.equipped=Element::Fire;
        const unsigned vase=5;check(g.retail_objects.at(vase).type==0xc9,"Summer Forest record 5 is a metal-capped vase");
        const auto target=g.retail_objects[vase].position;bool placed=false;
        for(unsigned k=0;k<16 && !placed;++k) {
            const float a=float(k)*std::numbers::pi_v<float>/8;const auto probe=target+Vec3{std::cos(a)*1.1f,0,std::sin(a)*1.1f};
            const auto floor=g.level.raycast(probe+Vec3{0,1.5f,0},{0,-1,0},3);
            if(!floor || floor->normal.y<.7f || std::abs(floor->point.y-target.y)>.5f)continue;
            g.player.position=g.player.previous_position=floor->point;g.player.velocity={};
            g.player.yaw=g.player.previous_yaw=std::atan2(target.x-floor->point.x,target.z-floor->point.z);placed=true;
        }
        check(placed,"No floor beside the Summer Forest vase");
        ticks(g,4);InputState press;press.flame_pressed=press.flame_held=true;InputState hold;hold.flame_held=true;
        g.advance(FixedClock::step,press);
        for(unsigned t=0;t<120 && g.retail_objects[vase].state>=0;++t){if(t==60)g.advance(FixedClock::step,press);else g.advance(FixedClock::step,hold);}
        if(tier==1)check(g.retail_objects[vase].state>=0,"The original flame must only heat a metal-capped vase");
        else check(g.retail_objects[vase].state<0,"Upgraded Fire breath must break a metal-capped vase");
    }
}
int main(int argc,char** argv) {
    try {
        if(argc<2){std::cerr<<"Usage: spyro_element_tests <retail data> [--print-golden|--list-enemies slot]\n";return 2;}
        AssetStore assets(argv[1]);WadArchive wad(assets);
        if(argc>3 && std::string(argv[2])=="--list-enemies") {
            const Retail scene(assets,wad,static_cast<unsigned>(std::stoul(argv[3])));
            for(std::size_t i=0;i<scene.level.objects.size();++i){const auto& o=scene.level.objects[i];
                if(o.enemy)std::cout<<"record "<<i<<" type 0x"<<std::hex<<o.type<<std::dec<<" kind "<<static_cast<unsigned>(o.enemy->kind)<<" at "<<o.position.x<<','<<o.position.y<<','<<o.position.z<<'\n';}
            return 0;
        }
        const Retail glimmer(assets,wad,17),forest(assets,wad,15);
        if(argc>2 && std::string(argv[2])=="--print-golden") {
            std::cout<<std::hex<<"glimmer-disabled 0x"<<golden_scenario(glimmer,0)<<"\nglimmer-upgrades 0x"<<golden_scenario(glimmer,1)
                <<"\nforest-supercharge 0x"<<golden_scenario(forest,2)<<'\n';
            return 0;
        }
        check(golden_scenario(glimmer,0)==golden_glimmer_disabled,"Elements-disabled Glimmer replay changed (pre-elements golden)");
        check(golden_scenario(glimmer,1)==golden_glimmer_upgrades,"Upgrades-enabled Glimmer replay changed (pre-elements golden)");
        check(golden_scenario(forest,2)==golden_forest_supercharge,"Summer Forest supercharge replay changed (pre-elements golden)");
        translation_table();statuses();precedence_and_meters();progression_and_saves();dark_form();dark_eye_trail(glimmer);meter_unshaded(assets,wad);mechanic_timelines(Retail(assets,wad,17,false,true));   // swim clips imported, as the workbench does
        retail_combat(assets,wad);elemental_water(assets,wad);frozen_water(assets,wad);fire_breaks_metal_vases(assets,wad);
        std::cout<<"Element checks passed: "<<checks<<'\n';
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAILED: "<<e.what()<<" after "<<checks<<" checks\n";return 1;}
}
