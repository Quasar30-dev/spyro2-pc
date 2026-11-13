#include "spyro/savegame.hpp"
#include "spyro/game_build.hpp"
#include "spyro/universe.hpp"
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <chrono>
#include <cwctype>
#include <locale>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace spyro {
namespace {
bool scene_slot(unsigned n){return n>=15 && n<=95 && (n&1);}
bool position_ok(Vec3 p){return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
    std::abs(p.x)<100000 && std::abs(p.y)<100000 && std::abs(p.z)<100000;}
void require(bool ok,const char* message){if(!ok)throw FormatError(message);}
std::uint32_t checksum(const std::string& text){return crc32({reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});}
bool gem_value(unsigned n){return n==1 || n==2 || n==5 || n==10 || n==25;}
}
void GameSave::validate()const {
    const auto& profile=game_profile(retail_crc);
    const auto valid_slot=[&](unsigned n){return profile.yotd()?n>=97 && n<=169 && (n&1):scene_slot(n) && profile.physical_slot(n).has_value();};
    require(valid_slot(slot),"Save uses an unavailable scene in this build");
    require(gems<=1000000 && lives<=999 && health<=3 && orbs<=64 && talismans<=0xffff,"Invalid saved player totals");
    require(static_cast<unsigned>(cheats.supercharge_appearance)<=static_cast<unsigned>(SuperchargeAppearance::MainModelChargeWings),"Invalid saved supercharge appearance");
    require(upgrades.valid(),"Invalid saved PC upgrades");
    require(destruction_settings.valid(),"Invalid saved terrain destruction settings");
    require(upgrade_hud_settings.valid(),"Invalid saved upgrade HUD settings");
    require(butterfly_drops<=1000000,"Invalid butterfly release count");
    require(purchases<(1u<<19),"Invalid saved purchase flags");
    require(skill_points<(1u<<17),"Invalid saved skill point flags");
    require(!levels.empty() && levels.size()<=41 && levels.contains(slot),"Save has no current scene");
    require(universe_worlds.size()<=10000,"Universe save exceeds 10000 visited worlds");
    require(universe || universe_worlds.empty(),"World progress has no universe address");
    if(universe) {
        require(generator_version==universe_generator_version,"Universe save needs a different generator version");
        require(slot==universe_source_slot(*universe),"Saved world address does not match its theme bank");
    }
    const auto check_level=[&](unsigned id,const LevelProgress& level) {
        require(valid_slot(id) && position_ok(level.position) && position_ok(level.spawn) && std::isfinite(level.yaw),"Invalid saved location");
        Scene scene;scene.wad_slot=id;scene.spawn=level.spawn;scene.entities=level.entities;scene.validate();
        require(level.entities.size()<=10000,"Too many saved editor entities");
        require(level.powers.valid() && position_ok(level.velocity) && length(level.velocity)<=1000 && std::isfinite(level.pitch) && std::abs(level.pitch)<7 &&
            std::isfinite(level.roll) && std::abs(level.roll)<7,"Invalid saved flight state");
        require(level.bodies.size()<=9,"Too many saved moving containers");
        std::set<unsigned> body_ids;unsigned held_count=0;
        for(const auto& body:level.bodies) {
            require(body.object<10000 && (is_breakable(body.type) || is_heavy_container(body.type)) && body.age<300 && body.grip_ticks<=10000000 &&
                position_ok(body.position) && position_ok(body.home) && position_ok(body.velocity) && length(body.velocity)<=1000 && body_ids.insert(body.object).second,"Invalid saved moving container");
            for(unsigned i=0;i<3;++i)require(std::isfinite(body.rotation[i]) && std::abs(body.rotation[i])<=65536 &&
                std::isfinite(body.home_rotation[i]) && std::abs(body.home_rotation[i])<=65536,"Invalid saved container rotation");
            if(body.held)++held_count;
        }
        require(held_count<=1 && level.bodies.size()-held_count<=8,"Invalid carried/airborne container count");
        require(level.removed.size()<=10000 && level.pending_gems.size()<=10000 && level.conversations.size()<=10000 && level.butterflies.size()<=10000,"Save scene is too large");
        require(level.awards.orbs<=255 && level.awards.flight_tasks<=31 && level.awards.flight_ticks<=360000,"Invalid guidebook completion flags");
        require(level.scripts.size()<=1024,"Too many saved mechanisms");std::set<unsigned> mechanisms;
        require(level.craters.size()<=HeadbashTerrain::max_stamps,"Too many saved headbash craters");
        for(const auto& crater:level.craters)require(position_ok(crater.center) && (crater.mode==HeadbashMode::Crater || crater.mode==HeadbashMode::Lava) &&
            static_cast<unsigned>(crater.element)<element_count && (!crater.cooled || crater.mode==HeadbashMode::Lava),"Invalid saved headbash crater");
        require(level.terrain_damage.size()<=TerrainDestruction::max_impacts,"Too many saved terrain impacts");
        for(const auto& impact:level.terrain_damage)require(impact.valid(),"Invalid saved terrain damage");
        for(const auto& s:level.scripts)require(id==39 && (s.type==0x17b || s.type==0x16f || s.type==0x405) && s.object<10000 && s.flags<=3 && s.ticks<=36000 && s.cursor<=2048 &&
            std::isfinite(s.progress) && s.progress>=0 && s.progress<=64 && mechanisms.insert(s.object).second,"Invalid saved mechanism");
        std::set<unsigned> removed,npcs,sources,food_sources;
        for(const auto& object:level.removed)require(object.id<10000 && object.type<=65535 && removed.insert(object.id).second,"Duplicate or invalid saved object");
        for(const auto& gem:level.pending_gems)require(gem.source<10000 && gem_value(gem.value) && position_ok(gem.position) && sources.insert(gem.source).second,"Invalid or duplicate saved released gem");
        for(const auto& npc:level.conversations)require(npc.object<10000 && npc.node<32 && npcs.insert(npc.object).second,"Invalid saved conversation");
        for(const auto& food:level.butterflies)if(!(food.source<10000 && (food.type==0x10 || food.type==0xd3) && food.age<1800 && food.heading<256 &&
            position_ok(food.position) && position_ok(food.home) && food_sources.insert(food.source).second))
            throw FormatError("Invalid or duplicate saved butterfly (scene "+std::to_string(id)+", source "+std::to_string(food.source)+", type "+
                std::to_string(food.type)+", age "+std::to_string(food.age)+")");
    };
    for(const auto& [id,level]:levels)check_level(id,level);
    for(const auto& [id,level]:universe_worlds){(void)id;check_level(17,level);}
}
void GameSave::capture(const Game& game) {
    require(game.level.custom_name.empty(),"Custom level progress cannot be written into a retail save slot");
    const auto address=game.level.universe?std::optional(game.level.universe->address):std::nullopt;
    require(bool(address)==bool(universe) || levels.empty(),"Retail and universe progress use separate saves");
    if(address && universe)require(address->seed==universe->seed,"Cannot mix different universe seeds");
    universe=address;
    retail_crc=game.play_scene.retail_crc;slot=game.play_scene.wad_slot;
    gems=game.player.gems;lives=game.player.lives;health=game.player.health;orbs=game.player.orbs;talismans=game.player.talismans;
    cheats=game.cheats;active_camera=game.dialogue.active_camera;
    upgrades=game.upgrades;
    upgrade_hud_settings=game.upgrade_hud_settings;
    destruction_settings=game.destruction_settings;
    permanent_superflame=game.powers.permanent_fire;
    butterfly_drops=game.sparx.butterfly_drops();
    purchases=game.purchases;
    skill_points=game.skill_points;
    LevelProgress next;next.position=game.player.position;next.spawn=game.play_scene.spawn;next.entities=game.play_scene.entities;
    next.yaw=game.player.yaw;next.conversations=game.dialogue.progress();
    next.bodies=game.abilities.checkpoint(game);next.powers=game.powers.checkpoint();
    next.velocity=game.player.velocity;next.pitch=game.player.pitch;next.roll=game.player.roll;
    if(game.portal_travel.active()) {
        const auto& travel=game.portal_travel;
        next.position=travel.phase==PortalTravel::Phase::Arriving?travel.target:travel.start;
        next.yaw=travel.yaw;next.velocity={};next.pitch=next.roll=0;
    }
    next.awards=game.guide_awards;
    next.scripts=game.systems.checkpoint();
    next.craters.assign(game.headbash.stamps().begin(),game.headbash.stamps().end());
    next.terrain_damage.assign(game.destruction.damage().begin(),game.destruction.damage().end());
    next.retail_memory=game.overlay_scripts.memory();
    for(const auto& object:game.retail_objects) {
        if(object.scripted_transient)continue;
        if(object.runtime_spawned) {
            if(object.type==1 && !object.collected && object.state>=0)next.pending_gems.push_back({object.source_id,object.gem_value,object.position});
            // Only a butterfly released by one of this scene's animals can be
            // restored (to that animal). Script releases (the 0xD3 extra-life
            // fly) and demon drops have no such source; like the retail game,
            // the save simply does not keep them.
            const auto source=object.source_id;
            if((object.type==0x10 || object.type==0xd3) && !object.collected && object.state>=0 && object.butterfly &&
               source<game.level.objects.size() && source<game.retail_objects.size() && is_butterfly_source(game.retail_objects[source].type) &&
               std::none_of(next.butterflies.begin(),next.butterflies.end(),[&](const auto& f){return f.source==source;}))
                next.butterflies.push_back({object.source_id,object.type,std::min(object.butterfly->age,1798u),static_cast<unsigned>(object.rotation[2])&255,object.position,object.butterfly->home});
        }else if(object.id<game.level.objects.size() &&
                 (object.collected || (object.enemy && object.enemy->defeated) || (object.state<0 && game.level.objects[object.id].state>=0)))
            next.removed.push_back({object.id,object.type});
    }
    if(universe && !universe->space)universe_worlds[universe->world]=next;
    levels[slot]=std::move(next);validate();
}
RetailObject make_saved_gem(const SavedGem& saved,const ObjectModels& models,unsigned id) {
    const auto found=std::find(models.gem_values.begin(),models.gem_values.end(),saved.value);
    require(found!=models.gem_values.end(),"Saved gem denomination is absent from this bank");
    const auto variant=static_cast<unsigned>(found-models.gem_values.begin());
    RetailObject gem;gem.id=id;gem.type=1;gem.runtime_spawned=true;gem.source_id=saved.source;gem.held_gem=static_cast<std::uint8_t>(saved.value);
    gem.variant=static_cast<std::uint8_t>(variant);gem.model=static_cast<std::uint8_t>(models.gem_meshes[variant]);
    gem.material_color=models.gem_colors[variant];gem.gem_value=saved.value;gem.draw_flags=0x18;gem.state=2;
    gem.position=gem.previous_position=saved.position;return gem;
}
void GameSave::apply(Game& game,const ObjectModels* models)const {
    require(game.level.custom_name.empty(),"Retail saves cannot be applied to a custom level playtest");
    validate();require(game.play_scene.retail_crc==retail_crc,"Save does not match the mounted game");
    require(bool(universe)==bool(game.level.universe),"Retail and universe saves are not interchangeable");
    const LevelProgress* saved_level=nullptr;
    if(universe) {
        const auto address=game.level.universe->address;
        require(universe->seed==address.seed,"Save belongs to a different universe seed");
        if(!address.space) {
            const auto it=universe_worlds.find(address.world);if(it!=universe_worlds.end())saved_level=&it->second;
        } else if(*universe==address)saved_level=&levels.at(slot);
    } else {
        const auto it=levels.find(game.play_scene.wad_slot);if(it!=levels.end())saved_level=&it->second;
    }
    auto objects=game.level.objects;auto conversations=game.dialogue;conversations.reset(game.level);
    if(saved_level) {
        const auto& saved=*saved_level;
        for(const auto& s:saved.scripts)require(std::any_of(game.level.scripts.begin(),game.level.scripts.end(),[&](const auto& definition){return definition.object==s.object && definition.type==s.type;}),"Saved mechanism does not exist in this scene");
        for(const auto& removed:saved.removed) {
            require(removed.id<objects.size() && objects[removed.id].id==removed.id && objects[removed.id].type==removed.type,"Saved object does not match this scene");
            auto& object=objects[removed.id];object.collected=true;object.state=-1;object.audio_finished=true;
        }
        for(const auto& gem:saved.pending_gems) {
            require(models && gem.source<game.level.objects.size() && objects[gem.source].state<0 &&
                (is_breakable(objects[gem.source].type) || is_heavy_container(objects[gem.source].type)) &&
                objects[gem.source].held_gem==gem.value,"Saved released gem has no matching broken container");
            objects.push_back(make_saved_gem(gem,*models,static_cast<unsigned>(objects.size())));
        }
        for(const auto& food:saved.butterflies) {
            require(models && food.source<game.level.objects.size() && objects[food.source].state<0 && is_butterfly_source(objects[food.source].type) &&
                models->models.contains(food.type),"Saved butterfly has no matching defeated animal or model");
            RetailObject object;object.id=static_cast<unsigned>(objects.size());object.type=static_cast<std::uint16_t>(food.type);
            object.source_id=food.source;object.runtime_spawned=true;object.held_gem=255;object.draw_flags=0x10;
            object.position=object.previous_position=food.position;object.rotation[2]=static_cast<float>(food.heading);object.previous_rotation=object.rotation;
            object.butterfly=ButterflyState{food.home,food.age};object.animation=object.previous_animation=start_object_animation(models->models.at(food.type));
            objects.push_back(std::move(object));
        }
        conversations.restore(saved.conversations);
    }
    // Rebuild mesh bounds and paw planes from the mounted copy; saves contain
    // transforms and velocities, never trusted serialized collision geometry.
    auto abilities=Abilities::from_checkpoint(saved_level?std::span(saved_level->bodies):std::span<const CarryCheckpoint>{},objects,models);
    // All format/scene checks precede changes to the running game. Clear old
    // body/projectile references while they still point at the OLD object list.
    game.destruction.clear(game.level);
    game.purchases=purchases;game.respawn();game.sparx=SparxCompanion{};
    game.retail_objects=std::move(objects);
    if(saved_level){game.play_scene.spawn=saved_level->spawn;game.play_scene.entities=saved_level->entities;}
    game.dialogue=std::move(conversations);game.abilities=std::move(abilities);
    game.dialogue.active_camera=active_camera;game.camera.passive=!active_camera;game.cheats=cheats;
    game.powers.permanent_fire=permanent_superflame;
    game.upgrades=upgrades;
    game.upgrade_hud_settings=upgrade_hud_settings;
    game.destruction_settings=destruction_settings;
    game.player.gems=gems;game.player.lives=lives;game.player.health=health;game.player.orbs=orbs;game.player.talismans=talismans;
    game.skill_points=skill_points;game.guide_awards=saved_level?saved_level->awards:GuideAwards{};
    game.visited_levels=game.level.wad_slot>=15 && game.level.wad_slot<=71 && (game.level.wad_slot&1)?1u<<((game.level.wad_slot-15)/2):0;
    for(const auto& [visited,progress]:levels)if(visited>=15 && visited<=71 && (visited&1))game.visited_levels|=1u<<((visited-15)/2);
    game.ripto_defeated=levels.contains(71) && levels.at(71).awards.boss;
    if(saved_level) {
        game.player.position=game.player.previous_position=saved_level->position;game.player.yaw=game.player.previous_yaw=saved_level->yaw;
        game.player.velocity=saved_level->velocity;game.player.pitch=game.player.previous_pitch=saved_level->pitch;game.player.roll=game.player.previous_roll=saved_level->roll;
        game.powers.restore_checkpoint(saved_level->powers);
        if(game.powers.flying) {
            game.player.motion=game.powers.flight_clip()==34?Motion::SuperflyClimb:game.powers.flight_clip()==35?Motion::SuperflyDive:Motion::Superfly;
            game.restore_player_pose();
        }
    }
    game.systems.reset(game,models);
    if(saved_level)game.systems.restore(game,saved_level->scripts,models);
    game.headbash.restore(game,saved_level?std::span(saved_level->craters):std::span<const HeadbashStamp>{},models);
    game.destruction.restore(game,saved_level?std::span(saved_level->terrain_damage):std::span<const TerrainDamage>{},models);
    game.ripto.restore(game,models);
    game.overlay_scripts.restore_memory(saved_level?saved_level->retail_memory:std::vector<RetailMemory>{});
    game.sparx.reset(game.retail_objects,models,game.player.position+retail::origin_above_feet,game.player.yaw,health);
    game.sparx.restore_butterfly_drops(butterfly_drops);
    game.camera.reset(game.level,game.player.position,game.player.yaw);
    game.hud.reset(game.level_gems(),game.spirits(),lives);
}
void GameSave::write(const std::filesystem::path& path)const {
    validate();auto extension=path.extension().wstring();for(auto& c:extension)c=static_cast<wchar_t>(std::towlower(c));
    require(extension==L".s2save","Game saves use .s2save; choose a separate PC save file");
    std::ostringstream text;text.imbue(std::locale::classic());text<<std::setprecision(9)<<retail_crc<<' '<<slot<<' '<<gems<<' '<<lives<<' '<<health<<' '<<orbs<<' '<<talismans<<' '
        <<active_camera<<' '<<cheats.supercharge<<' '<<static_cast<unsigned>(cheats.supercharge_appearance)<<' '
        <<cheats.superflame<<' '<<cheats.superfly<<' '<<cheats.green_firebomb<<' '<<permanent_superflame<<'\n';
    text<<bool(universe)<<' '<<generator_version<<' '<<(universe?universe->seed:0)<<' '<<(universe?universe->world:0)<<' '<<(universe?universe->space:false)<<'\n'<<levels.size()<<'\n';
    const auto write_level=[&](const LevelProgress& level) {
        text<<level.position.x<<' '<<level.position.y<<' '<<level.position.z<<' '<<level.yaw<<' '
            <<level.spawn.x<<' '<<level.spawn.y<<' '<<level.spawn.z<<'\n'<<level.entities.size()<<'\n';
        for(const auto& e:level.entities)text<<e.id<<' '<<static_cast<unsigned>(e.kind)<<' '<<e.position.x<<' '<<e.position.y<<' '<<e.position.z<<' '<<e.value<<' '<<e.collected<<'\n';
        text<<level.removed.size()<<'\n';
        for(const auto& o:level.removed)text<<o.id<<' '<<o.type<<'\n';
        text<<level.pending_gems.size()<<'\n';
        for(const auto& g:level.pending_gems)text<<g.source<<' '<<g.value<<' '<<g.position.x<<' '<<g.position.y<<' '<<g.position.z<<'\n';
        text<<level.conversations.size()<<'\n';for(const auto& n:level.conversations)text<<n.object<<' '<<n.node<<' '<<n.automatic<<'\n';
        text<<level.butterflies.size()<<'\n';for(const auto& f:level.butterflies)text<<f.source<<' '<<f.type<<' '<<f.age<<' '<<f.heading<<' '
            <<f.position.x<<' '<<f.position.y<<' '<<f.position.z<<' '<<f.home.x<<' '<<f.home.y<<' '<<f.home.z<<'\n';
        text<<level.bodies.size()<<'\n';
        for(const auto& b:level.bodies) {
            text<<b.object<<' '<<b.type<<' '<<b.age<<' '<<b.grip_ticks<<' '<<b.held<<' '
                <<b.position.x<<' '<<b.position.y<<' '<<b.position.z<<' '<<b.home.x<<' '<<b.home.y<<' '<<b.home.z<<' '
                <<b.velocity.x<<' '<<b.velocity.y<<' '<<b.velocity.z;
            for(auto value:b.rotation)text<<' '<<value;for(auto value:b.home_rotation)text<<' '<<value;text<<'\n';
        }
        const auto& p=level.powers;
        text<<p.flight.remaining<<' '<<p.flight.duration<<' '<<p.fire.remaining<<' '<<p.fire.duration<<' '<<p.charge<<' '<<p.flying<<' '<<p.takeoff<<' '
            <<p.clip<<' '<<p.lift_phase<<' '<<p.speed<<' '<<p.roll_velocity<<' '<<p.lift<<' '
            <<level.velocity.x<<' '<<level.velocity.y<<' '<<level.velocity.z<<' '<<level.pitch<<' '<<level.roll<<'\n';
        text<<level.awards.orbs<<' '<<level.awards.talisman<<' '<<level.awards.boss<<' '<<level.awards.flight_tasks<<' '<<level.awards.flight_ticks<<'\n';
        text<<p.invincibility.remaining<<' '<<p.invincibility.duration<<' '<<p.ice.remaining<<' '<<p.ice.duration<<'\n';
        text<<level.scripts.size()<<'\n';for(const auto& s:level.scripts)text<<s.object<<' '<<s.type<<' '<<s.flags<<' '<<s.ticks<<' '<<s.cursor<<' '<<s.progress<<'\n';
    };
    for(const auto& [id,level]:levels){text<<id<<' ';write_level(level);}
    text<<universe_worlds.size()<<'\n';for(const auto& [id,level]:universe_worlds){text<<id<<' ';write_level(level);}
    text<<upgrades.enabled<<' '<<upgrades.points<<' '<<upgrades.charge<<' '<<upgrades.flame<<' '<<upgrades.flight<<' '<<upgrades.carry<<' '<<upgrades.melee<<' '<<upgrades.glide<<'\n';
    text<<butterfly_drops<<'\n';
    text<<purchases<<'\n';
    text<<skill_points<<'\n';
    text<<static_cast<unsigned>(upgrade_hud_settings.mode)<<' '<<upgrade_hud_settings.seconds<<'\n';
    text<<upgrades.headbash<<' '<<static_cast<unsigned>(upgrades.headbash_mode)<<'\n';
    const auto write_craters=[&](const LevelProgress& level){text<<level.craters.size()<<'\n';for(const auto& c:level.craters)text<<c.center.x<<' '<<c.center.y<<' '<<c.center.z<<' '<<static_cast<unsigned>(c.mode)<<'\n';};
    for(const auto& [id,level]:levels){(void)id;write_craters(level);}
    for(const auto& [id,level]:universe_worlds){(void)id;write_craters(level);}
    text<<upgrades.roll<<'\n';
    const auto& d=destruction_settings;text<<d.enabled<<' '<<d.charge<<' '<<d.flame<<' '<<d.bomb<<' '<<d.protect_gameplay<<' '<<d.strength<<'\n';
    const auto write_damage=[&](const LevelProgress& level){text<<level.terrain_damage.size()<<'\n';for(const auto& h:level.terrain_damage)
        text<<h.center.x<<' '<<h.center.y<<' '<<h.center.z<<' '<<h.normal.x<<' '<<h.normal.y<<' '<<h.normal.z<<' '
            <<h.safe_feet.x<<' '<<h.safe_feet.y<<' '<<h.safe_feet.z<<' '<<static_cast<unsigned>(h.attack)<<' '<<h.strength<<' '<<h.seed<<' '<<h.protect_gameplay<<' '<<h.fragment<<'\n';};
    for(const auto& [id,level]:levels){(void)id;write_damage(level);}
    for(const auto& [id,level]:universe_worlds){(void)id;write_damage(level);}
    // Version 16: elemental progression, Dark Spyro cosmetic and crater tints.
    const auto& e=upgrades.elements;
    for(const auto tier:e.tiers)text<<tier<<' ';
    text<<static_cast<unsigned>(e.equipped)<<' '<<e.energy<<' '<<e.fury<<' '<<cheats.dark_spyro<<' '<<cheats.dark_smoke<<'\n';
    const auto write_tints=[&](const LevelProgress& level){for(const auto& c:level.craters)text<<static_cast<unsigned>(c.element)<<' '<<c.cooled<<' ';text<<'\n';};
    for(const auto& [id,level]:levels){(void)id;write_tints(level);}
    for(const auto& [id,level]:universe_worlds){(void)id;write_tints(level);}
    // Version 17: Convexity terrain cuts (slab radius + landed transform) and settings.
    text<<d.void_slice<<' '<<d.void_erode<<' '<<d.slice_size<<'\n';
    const auto write_slices=[&](const LevelProgress& level){for(const auto& h:level.terrain_damage)
        text<<h.slice_radius<<' '<<h.slice_offset.x<<' '<<h.slice_offset.y<<' '<<h.slice_offset.z<<' '<<h.slice_rotation.x<<' '<<h.slice_rotation.y<<' '<<h.slice_rotation.z<<' '<<h.slice_landed<<' ';text<<'\n';};
    for(const auto& [id,level]:levels){(void)id;write_slices(level);}
    for(const auto& [id,level]:universe_worlds){(void)id;write_slices(level);}
    // Version 18: original scripts' persistent memory per level.
    const auto write_memory=[&](const LevelProgress& level){text<<level.retail_memory.size();for(const auto& m:level.retail_memory)
        text<<' '<<m.list<<' '<<m.address<<' '<<m.size<<' '<<m.value;text<<'\n';};
    for(const auto& [id,level]:levels){(void)id;write_memory(level);}
    for(const auto& [id,level]:universe_worlds){(void)id;write_memory(level);}
    // Version 19: Convexity cuts break into 4-8 shards, each with its landed pose.
    const auto write_shards=[&](const LevelProgress& level){for(const auto& h:level.terrain_damage){text<<h.shards;
        for(const auto& pose:h.shard_poses)for(const auto v:pose)text<<' '<<v.x<<' '<<v.y<<' '<<v.z;text<<' ';}text<<'\n';};
    for(const auto& [id,level]:levels){(void)id;write_shards(level);}
    for(const auto& [id,level]:universe_worlds){(void)id;write_shards(level);}
    const auto payload=text.str();if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
    auto temporary=path;temporary+=".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        {std::ofstream out(temporary,std::ios::binary);require(bool(out),"Cannot create PC save file");
            out<<"S2SAVE 19 "<<checksum(payload)<<' '<<payload.size()<<'\n'<<payload;out.flush();require(bool(out),"Could not finish writing PC save");}
#ifdef _WIN32
        require(MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Could not replace PC save; previous save remains intact");
#else
        std::filesystem::rename(temporary,path);
#endif
    }catch(...){std::error_code error;std::filesystem::remove(temporary,error);throw;}
}
GameSave GameSave::read(const std::filesystem::path& path) {
    require(std::filesystem::file_size(path)<=64*1024*1024,"PC save is too large");
    std::ifstream file(path,std::ios::binary);require(bool(file),"Cannot open PC save");
    std::string magic;unsigned version{},crc{},size{};file>>magic>>version>>crc>>size;
    require(bool(file) && magic=="S2SAVE" && version>=1 && version<=19 && size<=64*1024*1024 && file.get()=='\n',"Unsupported or damaged PC save header");
    std::string payload(size,'\0');file.read(payload.data(),size);
    require(file.gcount()==size && file.peek()==EOF && checksum(payload)==crc,"PC save is incomplete or its checksum does not match");
    std::istringstream in(payload);in.imbue(std::locale::classic());GameSave result;unsigned appearance{},count{};
    in>>result.retail_crc>>result.slot>>result.gems>>result.lives>>result.health>>result.orbs>>result.talismans>>result.active_camera>>result.cheats.supercharge>>appearance;
    if(version>=2)in>>result.cheats.superflame>>result.cheats.superfly>>result.cheats.green_firebomb>>result.permanent_superflame;
    if(version>=3) {
        bool enabled{};UniverseAddress address;
        in>>enabled>>result.generator_version>>address.seed>>address.world>>address.space;
        if(enabled)result.universe=address;
    }
    in>>count;
    require(bool(in) && count<=41,"Invalid PC save totals");result.cheats.supercharge_appearance=static_cast<SuperchargeAppearance>(appearance);
    const auto read_level=[&]() {
        unsigned n{};LevelProgress level;in>>level.position.x>>level.position.y>>level.position.z>>level.yaw>>level.spawn.x>>level.spawn.y>>level.spawn.z>>n;
        require(bool(in) && n<=10000,"Invalid saved editor entity count");level.entities.resize(n);
        for(auto& e:level.entities){unsigned kind{};in>>e.id>>kind>>e.position.x>>e.position.y>>e.position.z>>e.value>>e.collected;e.kind=static_cast<EntityKind>(kind);}
        in>>n;
        require(bool(in) && n<=10000,"Invalid saved object count");level.removed.resize(n);for(auto& o:level.removed)in>>o.id>>o.type;
        in>>n;require(bool(in) && n<=10000,"Invalid saved gem count");level.pending_gems.resize(n);for(auto& g:level.pending_gems)in>>g.source>>g.value>>g.position.x>>g.position.y>>g.position.z;
        in>>n;require(bool(in) && n<=10000,"Invalid saved conversation count");level.conversations.resize(n);for(auto& c:level.conversations)in>>c.object>>c.node>>c.automatic;
        if(version>=6) {
            in>>n;require(bool(in) && n<=10000,"Invalid saved butterfly count");level.butterflies.resize(n);
            for(auto& f:level.butterflies)in>>f.source>>f.type>>f.age>>f.heading>>f.position.x>>f.position.y>>f.position.z>>f.home.x>>f.home.y>>f.home.z;
        }
        if(version>=7) {
            in>>n;require(bool(in) && n<=9,"Invalid moving container count");level.bodies.resize(n);
            for(auto& b:level.bodies) {
                in>>b.object>>b.type>>b.age>>b.grip_ticks>>b.held>>b.position.x>>b.position.y>>b.position.z>>b.home.x>>b.home.y>>b.home.z>>b.velocity.x>>b.velocity.y>>b.velocity.z;
                for(auto& value:b.rotation)in>>value;for(auto& value:b.home_rotation)in>>value;
            }
            auto& p=level.powers;
            in>>p.flight.remaining>>p.flight.duration>>p.fire.remaining>>p.fire.duration>>p.charge>>p.flying>>p.takeoff>>p.clip>>p.lift_phase>>p.speed>>p.roll_velocity>>p.lift
                >>level.velocity.x>>level.velocity.y>>level.velocity.z>>level.pitch>>level.roll;
        }
        if(version>=9)in>>level.awards.orbs>>level.awards.talisman>>level.awards.boss>>level.awards.flight_tasks>>level.awards.flight_ticks;
        if(version>=10)in>>level.powers.invincibility.remaining>>level.powers.invincibility.duration>>level.powers.ice.remaining>>level.powers.ice.duration;
        if(version>=11){in>>n;require(bool(in) && n<=1024,"Invalid saved mechanism count");level.scripts.resize(n);for(auto& s:level.scripts)in>>s.object>>s.type>>s.flags>>s.ticks>>s.cursor>>s.progress;}
        require(bool(in),"Damaged saved scene");return level;
    };
    for(unsigned i=0;i<count;++i) {
        unsigned id{};in>>id;auto level=read_level();require(result.levels.emplace(id,std::move(level)).second,"Duplicate saved scene");
    }
    if(version>=3) {
        in>>count;require(bool(in) && count<=10000,"Invalid saved world count");
        for(unsigned i=0;i<count;++i) {
            std::uint64_t id{};in>>id;auto level=read_level();require(result.universe_worlds.emplace(id,std::move(level)).second,"Duplicate saved world");
        }
    }
    if(version>=4) {auto& u=result.upgrades;in>>u.enabled>>u.points>>u.charge>>u.flame>>u.flight>>u.carry>>u.melee;require(bool(in),"Damaged saved PC upgrades");}
    if(version>=5){in>>result.upgrades.glide;require(bool(in),"Damaged saved glide upgrade");}
    if(version>=6){in>>result.butterfly_drops;require(bool(in),"Damaged saved butterfly release count");}
    if(version>=8){in>>result.purchases;require(bool(in),"Damaged saved purchase flags");}
    if(version>=9){in>>result.skill_points;require(bool(in),"Damaged saved skill point flags");}
    if(version>=12) {
        unsigned mode{};in>>mode>>result.upgrade_hud_settings.seconds;
        require(bool(in),"Damaged saved upgrade HUD settings");result.upgrade_hud_settings.mode=static_cast<UpgradeHudMode>(mode);
    }
    if(version>=13) {
        unsigned mode{};in>>result.upgrades.headbash>>mode;result.upgrades.headbash_mode=static_cast<HeadbashMode>(mode);
        const auto read_craters=[&](LevelProgress& level){unsigned n{};in>>n;require(bool(in) && n<=HeadbashTerrain::max_stamps,"Invalid saved crater count");
            level.craters.resize(n);for(auto& c:level.craters){in>>c.center.x>>c.center.y>>c.center.z>>mode;c.mode=static_cast<HeadbashMode>(mode);}};
        for(auto& [id,level]:result.levels){(void)id;read_craters(level);}
        for(auto& [id,level]:result.universe_worlds){(void)id;read_craters(level);}
    }
    if(version>=14){in>>result.upgrades.roll;require(bool(in),"Damaged saved roll upgrade");}
    if(version>=15) {
        auto& d=result.destruction_settings;in>>d.enabled>>d.charge>>d.flame>>d.bomb>>d.protect_gameplay>>d.strength;
        require(bool(in),"Damaged saved terrain destruction settings");
        const auto read_damage=[&](LevelProgress& level){unsigned n{};in>>n;require(bool(in) && n<=TerrainDestruction::max_impacts,"Invalid terrain impact count");
            level.terrain_damage.resize(n);for(auto& h:level.terrain_damage){unsigned attack{};
                in>>h.center.x>>h.center.y>>h.center.z>>h.normal.x>>h.normal.y>>h.normal.z>>h.safe_feet.x>>h.safe_feet.y>>h.safe_feet.z>>attack>>h.strength>>h.seed>>h.protect_gameplay>>h.fragment;
                h.attack=static_cast<TerrainAttack>(attack);require(bool(in),"Damaged saved terrain impact");}};
        for(auto& [id,level]:result.levels){(void)id;read_damage(level);}
        for(auto& [id,level]:result.universe_worlds){(void)id;read_damage(level);}
    }
    if(version>=16) {
        auto& e=result.upgrades.elements;unsigned equipped{};
        for(auto& tier:e.tiers)in>>tier;
        in>>equipped>>e.energy>>e.fury>>result.cheats.dark_spyro>>result.cheats.dark_smoke;
        require(bool(in) && equipped<element_count,"Damaged saved elemental progression");e.equipped=static_cast<Element>(equipped);
        const auto read_tints=[&](LevelProgress& level){for(auto& c:level.craters){unsigned element{};in>>element>>c.cooled;
            require(bool(in) && element<element_count,"Damaged saved crater tint");c.element=static_cast<Element>(element);}};
        for(auto& [id,level]:result.levels){(void)id;read_tints(level);}
        for(auto& [id,level]:result.universe_worlds){(void)id;read_tints(level);}
    }
    if(version>=17) {
        auto& d=result.destruction_settings;in>>d.void_slice>>d.void_erode>>d.slice_size;require(bool(in),"Damaged saved terrain cutting settings");
        const auto read_slices=[&](LevelProgress& level){for(auto& h:level.terrain_damage)
            in>>h.slice_radius>>h.slice_offset.x>>h.slice_offset.y>>h.slice_offset.z>>h.slice_rotation.x>>h.slice_rotation.y>>h.slice_rotation.z>>h.slice_landed;
            require(bool(in),"Damaged saved terrain cut");};
        for(auto& [id,level]:result.levels){(void)id;read_slices(level);}
        for(auto& [id,level]:result.universe_worlds){(void)id;read_slices(level);}
    }
    if(version>=18) {
        const auto read_memory=[&](LevelProgress& level){unsigned n{};in>>n;require(bool(in) && n<=128,"Invalid saved script memory count");
            level.retail_memory.resize(n);for(auto& m:level.retail_memory){in>>m.list>>m.address>>m.size>>m.value;
                require(bool(in) && (m.size==1 || m.size==2 || m.size==4) && m.address>=0x80000000u && m.address<0x80800000u,"Damaged saved script memory");}};
        for(auto& [id,level]:result.levels){(void)id;read_memory(level);}
        for(auto& [id,level]:result.universe_worlds){(void)id;read_memory(level);}
    }
    if(version>=19) {
        const auto read_shards=[&](LevelProgress& level){for(auto& h:level.terrain_damage){in>>h.shards;
            require(bool(in) && h.shards<=TerrainDestruction::max_shards,"Invalid saved shard count");
            h.shard_poses.resize(h.shards);for(auto& pose:h.shard_poses)for(auto& v:pose)in>>v.x>>v.y>>v.z;
            require(bool(in),"Damaged saved shard pose");}};
        for(auto& [id,level]:result.levels){(void)id;read_shards(level);}
        for(auto& [id,level]:result.universe_worlds){(void)id;read_shards(level);}
    }
    in>>std::ws;require(in.eof(),"Unexpected data after PC save");result.validate();return result;
}
} // namespace spyro
