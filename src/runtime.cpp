#include "spyro/runtime.hpp"
#include "spyro/universe.hpp"
#include "spyro/travel.hpp"
#include "spyro/custom_level.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <set>
#include <bit>

namespace spyro {
#include "runtime_feedback.inl"
unsigned FixedClock::advance(double seconds, const std::function<void()>& tick) {
    if (!std::isfinite(seconds) || seconds<0) return 0;
    if (seconds>0.25) { dropped_+=seconds-0.25; seconds=0.25; }
    accumulator_+=seconds;
    unsigned count=0;
    while (accumulator_+1e-12>=step && count<15) {
        tick(); accumulator_=std::max(0.0,accumulator_-step); ++ticks_; ++count;
    }
    return count;
}
void Scene::validate() const {
    if (!finite(spawn) || length(spawn)>100000 || entities.size()>100000 || wad_slot>100000)
        throw FormatError("Invalid scene header");
    std::set<std::uint32_t> ids;
    for (const auto& e:entities)
        if (!e.id || !ids.insert(e.id).second || !finite(e.position) || length(e.position)>100000 ||
            static_cast<unsigned>(e.kind)>1 || e.value<1 || e.value>10000)
            throw FormatError("Invalid or duplicate scene entity");
}
void Scene::save(const std::filesystem::path& path) const {
    validate();
    if (path.extension()!=".s2scene") throw FormatError("Editor projects use .s2scene; retail files cannot be overwritten");
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temporary=path; temporary+=".partial";
    {
        std::ofstream out(temporary,std::ios::trunc);
        if (!out) throw FormatError("Cannot write scene project");
        out<<std::setprecision(9)<<"S2SCENE 1\n"<<retail_crc<<' '<<wad_slot<<'\n'
           <<spawn.x<<' '<<spawn.y<<' '<<spawn.z<<'\n'<<entities.size()<<'\n';
        for (const auto& e:entities) out<<e.id<<' '<<static_cast<unsigned>(e.kind)<<' '<<e.position.x<<' '
            <<e.position.y<<' '<<e.position.z<<' '<<e.value<<'\n';
        out.flush(); if (!out) throw FormatError("Failed writing scene project");
    }
    // Windows rename cannot replace an existing file. Preserve a backup until
    // the new file is installed, then remove only this project's backup.
    auto backup=path; backup+=".previous";
    if (std::filesystem::exists(backup)) throw FormatError("A scene backup exists; recover it before saving");
    const bool existing=std::filesystem::exists(path);
    if (existing) std::filesystem::rename(path,backup);
    try { std::filesystem::rename(temporary,path); }
    catch (...) { if (existing) std::filesystem::rename(backup,path); throw; }
    if (existing) std::filesystem::remove(backup);
}
Scene Scene::load(const std::filesystem::path& path) {
    std::ifstream in(path); if (!in) throw FormatError("Cannot open scene project");
    Scene result; std::string magic; unsigned version{}; std::size_t count{};
    if (!(in>>magic>>version) || magic!="S2SCENE" || version!=1 ||
        !(in>>result.retail_crc>>result.wad_slot>>result.spawn.x>>result.spawn.y>>result.spawn.z>>count) || count>100000)
        throw FormatError("Invalid scene project header");
    result.entities.reserve(count);
    for (std::size_t i=0;i<count;++i) {
        Entity e; unsigned kind{};
        if (!(in>>e.id>>kind>>e.position.x>>e.position.y>>e.position.z>>e.value) || kind>1)
            throw FormatError("Invalid scene entity record");
        e.kind=static_cast<EntityKind>(kind); result.entities.push_back(e);
    }
    in>>std::ws; if (!in.eof()) throw FormatError("Trailing data in scene project");
    result.validate(); return result;
}
void EditorDocument::edit(const Scene& next) { next.validate(); undo_.push_back(scene); if (undo_.size()>128) undo_.erase(undo_.begin()); scene=next; redo_.clear(); }
bool EditorDocument::undo() { if (undo_.empty()) return false; redo_.push_back(scene); scene=undo_.back(); undo_.pop_back(); return true; }
bool EditorDocument::redo() { if (redo_.empty()) return false; undo_.push_back(scene); scene=redo_.back(); redo_.pop_back(); return true; }
void EditorDocument::replace(Scene next) { next.validate(); scene=std::move(next); undo_.clear(); redo_.clear(); }
const char* motion_name(Motion m) {
    switch(m) { case Motion::Idle:return "Idle";case Motion::Run:return "Run";case Motion::Charge:return "Charge";
        case Motion::Jump:return "Jump";case Motion::Fall:return "Fall";case Motion::Glide:return "Glide";case Motion::GlideBrake:return "Glide brake";
        case Motion::Supercharge:return "Supercharge";case Motion::SuperchargeJump:return "Supercharge jump";
        case Motion::SurfaceSwim:return "Surface swim";case Motion::SurfaceIdle:return "Surface idle";
        case Motion::UnderwaterSwim:return "Underwater swim";case Motion::UnderwaterIdle:return "Underwater idle";
        case Motion::UnderwaterCharge:return "Underwater charge";case Motion::ChargeJump:return "Charge jump";
        case Motion::Superfly:return "Superfly";case Motion::SuperflyClimb:return "Superfly climb";case Motion::SuperflyDive:return "Superfly dive";
        case Motion::Hover:return "Hover";case Motion::Headbash:return "Headbash";case Motion::HeadbashLand:return "Headbash landing";
        case Motion::GlideBoost:return "Glide boost";case Motion::Slide:return "Sand slide";case Motion::Skate:return "Ice skate";case Motion::Whirlwind:return "Whirlwind";
        case Motion::UnderwaterHurt:return "Underwater hurt";case Motion::RollLeft:return "Roll left";case Motion::RollRight:return "Roll right";case Motion::ChargeRecoil:return "Charge recoil"; }
    return "Unknown";
}
unsigned motion_animation(Motion motion) {
    switch(motion) {
        case Motion::Idle:return 0;case Motion::Run:return 2;case Motion::Charge:return 12;
        case Motion::Jump:return 5;case Motion::Fall:return 6;case Motion::Glide:return 16;case Motion::GlideBrake:return 24;
        case Motion::Supercharge:return 61;case Motion::SuperchargeJump:return 25;
        case Motion::SurfaceSwim:return 43;case Motion::SurfaceIdle:return 44;
        case Motion::UnderwaterSwim:return 40;case Motion::UnderwaterIdle:return 41;
        case Motion::UnderwaterCharge:return 42;case Motion::ChargeJump:return 21;
        case Motion::Superfly:return 33;case Motion::SuperflyClimb:return 34;case Motion::SuperflyDive:return 35;
        case Motion::Hover:return 17;case Motion::Headbash:return 46;case Motion::HeadbashLand:return 0;
        case Motion::GlideBoost:return 34;case Motion::Slide:return 49;case Motion::Skate:return 9;case Motion::Whirlwind:return 18;
        case Motion::UnderwaterHurt:return 57;case Motion::RollLeft:return 10;case Motion::RollRight:return 11;case Motion::ChargeRecoil:return 13;
    }
    return 0;
}
Game::Game(const Level& world, const Scene& scene, CameraSettings settings,const ObjectModels* models,const SpyroModel* spyro_model)
    : level(world), play_scene(scene), retail_objects(world.objects), camera(std::move(settings)),object_models_(models),spyro_model_(spyro_model) {
    for(auto& object:retail_objects)object.audio_finished=object.collected || object.state<0;
    scene.validate();dialogue.reset(level);ripto.initialize(*this,models);respawn();systems.reset(*this,models);hud.reset(level_gems(),spirits(),player.lives);
}
void Game::respawn() {
    respawn_yaw.reset();reset_player();clock.reset();pending_={};
    if(systems.speedway())systems.start_trial(*this,object_models_);
}
void Game::restore_player_pose() {
    animation.reset(spyro_model_,motion_animation(player.motion));
    secondary.reset(spyro_model_,animation.state());
}
void Game::reset_player() {
    playable_boss.reset();
    if(!systems.speedway() && (systems.cannon!=UINT32_MAX || systems.mode==ChallengeMode::Trolley || systems.mode==ChallengeMode::TrolleyResults))systems.leave_challenge(*this);
    abilities.reset(*this);
    universe_travel.reset();universe_portal_cooldown=90;
    const bool travel_allowed=portal_travel.allowed,travel_unlock=portal_travel.unlock;portal_travel={};portal_travel.allowed=travel_allowed;portal_travel.unlock=travel_unlock;whirlwind={};
    if(level.wad_slot>=15 && level.wad_slot<=71 && (level.wad_slot&1))visited_levels|=1u<<((level.wad_slot-15)/2);
    sound_events.clear();
    dialogue.dismiss(retail_objects);
    player.position=play_scene.spawn; player.previous_position=player.position; player.velocity={};
    player.grounded=false;player.airborne_ticks=4; player.motion=Motion::Fall;
    player.motion_ticks=0;
    player.charge_turn_velocity=0;
    player.recoil_ticks=player.charge_impacts=0;player.recoil_velocity={};
    player.swim={};player.air={};player.slide={};player.skate={};player.jump={};player.dodge={};player.hover_actions=player.headbash_impacts=player.glide_boosts=player.slide_ticks=player.skate_ticks=0;
    player.pitch=player.previous_pitch=player.roll=player.previous_roll=0;
    charge_smoke.reset();
    for(const auto& projectile:powers.projectiles())if(projectile.object<retail_objects.size())retail_objects[projectile.object].collected=true;
    powers.reset();
    demons.clear(*this);elements.reset(*this);
    ripto.reset(*this,object_models_);
    player.yaw=player.previous_yaw=respawn_yaw.value_or(level.spawn_yaw);
    animation.reset(spyro_model_,6);
    secondary.reset(spyro_model_,animation.state());flame_action.reset();
    interactions.reset_transients();
    player.health=object_models_?object_models_->sparx_max_health:3;
    player.invulnerable_ticks=player.hurt_ticks=0;player.hurt_velocity={};
    sparx.reset(retail_objects,object_models_,player.position+retail::origin_above_feet,player.yaw,player.health);
    camera.reset(level,player.position,player.yaw);
    restore_scene_progress();
    if(level.source_game==GameBuild::YearOfTheDragonUS)yotd_scripts.reset(*this);
}
bool Game::hurt(Vec3 source) {
    if(player.invulnerable_ticks || powers.invincibility.remaining || dialogue.active() || portal_travel.active() || whirlwind.index!=UINT32_MAX)return false;
    // Elemental extension: Fury levitation is untouchable, a Hollow Wraith
    // absorbs one hit, and Dark Fury turns damage into knockback.
    if(elements.fury_pose())return false;
    if(demons.absorb_hit(*this)){player.invulnerable_ticks=60;return false;}
    if(elements.shrug_off(*this,source))return false;
    if(!player.health) {
        if(player.lives)--player.lives;
        reset_player();player.invulnerable_ticks=120;return true;
    }
    player.recoil_ticks=0;player.recoil_velocity={};
    const bool underwater=player.swim.mode==WaterMode::Underwater;
    // Overlay 58 selects state 0x39 and returns to swim after 25 VBlanks.
    --player.health;player.invulnerable_ticks=120;player.hurt_ticks=underwater?25:24;
    auto away=player.position-source;away.y=0;away=normalized(away);
    if(length(away)<0.01f)away={-std::sin(player.yaw),0,-std::cos(player.yaw)};
    // 8002b0b8 -> state 0F on land/surface; underwater modes 0B/0C select 39.
    // State 39 (overlay 58, 8006fcd0) launches along the full sine/cosine of
    // the attacker bearing with no lift; state 0F uses half and a hop.
    player.hurt_velocity=underwater?away*9.0f:away*4.5f+Vec3{0,3,0};player.velocity=player.hurt_velocity;
    if(underwater) {
        player.swim.speed=0;player.swim.entry_ticks=0;player.swim.coasting_charge=false;
        // 8006f374 selects global sound-table entry 0x18 for this reaction.
        sound_events.push_back({SoundScope::Global,0x18,0,player.position});
    }
    player.grounded=false;player.airborne_ticks=4;
    flame_action.reset();interactions.stop_flame();charge_smoke.reset();
    powers.end_flight();
    player.jump={};
    player.air.gliding=player.air.glide_pending=false;player.air.hover_ticks=player.air.headbash_ticks=player.air.boost_ticks=0;player.slide={};
    return true;
}
bool Game::award_orb(unsigned bit) {
    if(bit>=8)throw FormatError("Orb flag outside the retail objective mask");
    if((guide_awards.orbs&(1u<<bit)) || player.orbs>=64)return false;
    guide_awards.orbs|=1u<<bit;++player.orbs;return true;
}
bool Game::award_talisman() {
    constexpr unsigned indices[]{1,2,3,4,5,6,10,11,12,13,15,16,17,18};
    const unsigned index=(level.wad_slot-15)/2;
    const auto found=std::find(std::begin(indices),std::end(indices),index);
    // Winter Tundra's levels (index > 0x15) have no talisman: the same byte
    // (8006b264 + level) marks the level complete once a dialogue awards an
    // orb in its "Perfect" mask (80016328, 80066e72 + level). It raises Return
    // Home and is saved like a talisman, but is not counted as one.
    if(level.universe || (found==std::end(indices) && (index<=0x15 || index>=28)) || guide_awards.talisman)return false;
    guide_awards.talisman=true;if(found!=std::end(indices))player.talismans|=1u<<(found-std::begin(indices));
    reward_this_visit=true;return true;
}
void Game::start_reward_show(unsigned speaker,unsigned reward_class,bool talisman) {
    if(speaker>=retail_objects.size())return;
    reward_show={};reward_show.speaker=speaker;reward_show.reward_class=reward_class;reward_show.talisman=talisman;
    // 80016328 at the reward's start: 80066f5c becomes 3 when the talk ended in
    // view 0, else 4, and camera mode 12 springs from the current eye to that
    // view (8006163c + side * 14; 8001ee18 / 8001efa8 / 8001eccc), facing the
    // speaker, until the reward closes the conversation.
    reward_show.camera_side=camera.conversation_view()==0?3:4;
    const auto spyro=player.position+retail::origin_above_feet;const auto s=retail_objects[speaker].position;
    // State 4: the talisman appears 7/16 of the way from the speaker to Spyro,
    // 0x1C0 above Spyro's origin. State 3: the orb leaves the speaker.
    reward_show.from=s;
    reward_show.point={(s.x*7+spyro.x*9)/16,spyro.y+0x1c0/retail::units_per_world,(s.z*7+spyro.z*9)/16};
}
void Game::tick_reward_show() {
    auto& r=reward_show;if(!r.active())return;
    constexpr float u=1/retail::units_per_world;
    const unsigned t=r.ticks++,f=t/2;const bool frame=(t%2)==0;
    const auto rnd=[&](unsigned mask){r.ticks+=0;static std::uint32_t seed=0x5eed2e;seed=seed*1103515245u+12345u;return static_cast<int>((seed>>16)&mask);};
    const Vec3 colour{0xe0/128.0f,0xe0/128.0f,0x40/128.0f};
    const auto burst=[&](Vec3 at,unsigned count,float per_frame) {   // 8003ca00: random directions at 0x32 per update
        for(unsigned k=0;k<count;++k) {
            const float a=rnd(4095)*6.2831853f/4096,b=(rnd(2047)-1024)*1.5707963f/1024;
            powers.emit_sparkle(at,Vec3{std::cos(a)*std::cos(b),std::sin(b),std::sin(a)*std::cos(b)}*(per_frame*u*30),colour,32);
        }
    };
    const auto spawn=[&](unsigned type,Vec3 at) {
        if(!object_models_ || !object_models_->models.contains(type))return UINT32_MAX;
        RetailObject o;o.type=static_cast<std::uint16_t>(type);o.runtime_spawned=true;o.held_gem=255;o.persistence=255;
        o.position=o.previous_position=at;
        if(const auto& bank=object_models_->models.at(type);bank.animated)o.animation=o.previous_animation=start_object_animation(bank);
        unsigned id=0;for(const auto& old:retail_objects)id=std::max(id,old.id+1);o.id=id;
        retail_objects.push_back(std::move(o));sound_events.push_back({SoundScope::Object,0,type,at});   // its sound 0 (80050648)
        return static_cast<unsigned>(retail_objects.size()-1);
    };
    RetailObject* item=r.item<retail_objects.size()?&retail_objects[r.item]:nullptr;
    if(item){item->previous_position=item->position;item->previous_rotation=item->rotation;}
    if(r.talisman) {
        // Frames 19..71: sparkles gather on the point from below and around it (16 updates each).
        if(frame && f>=19 && f<72) {
            const float yaw=player.yaw;const float side=float((0x80+rnd(0x1ff))*(rnd(1)?1:-1)),ahead=float((0x80+rnd(0x7ff))*(rnd(1)?1:-1));
            const Vec3 offset{(std::cos(yaw)*side+std::sin(yaw)*ahead)*u,-868*u,(-std::sin(yaw)*side+std::cos(yaw)*ahead)*u};
            powers.emit_sparkle(r.point+offset,offset*(-30.0f/16),colour,32);
        }
        if(frame && f==80){r.item=spawn(r.reward_class,r.point);burst(r.point,8,24);}
        // The talisman class (Glimmer DB, 80071e74): turns 8 and rises 4 units per
        // update for 80 updates with sparkles every fourth, then vanishes in a
        // four-sparkle burst with its sound 0.
        if(item && f>=80 && f<160){item->rotation[2]+=4;item->position.y+=2*u;if(frame && (f-80)%4==0)burst(item->position,2,12);}
        if(item && frame && f==160){item->state=-1;item->collected=true;burst(item->position,4,0x32);sound_events.push_back({SoundScope::Object,0,item->type,item->position});}
        if(frame && f==150){custom_notice="TALISMAN COLLECTED";custom_notice_ticks=240;}
        if(frame && f==140)overlay_scripts.zoe_reward(r.camera_side);   // 8003cafc: Zoe arrives beside Spyro (80066f5c side)
        if(f>=200)r=RewardShow{};
    } else {
        // State 3: at update 30 the orb leaves the speaker for Spyro, then is caught.
        const auto chest=player.position+retail::origin_above_feet;
        if(frame && f==30){r.point=r.from+Vec3{0,0x180*u,0};r.item=spawn(r.reward_class,r.point);}
        if(item && f>=30 && f<114) {
            const float k=(t-60)/168.0f;item->position=r.point+(chest-r.point)*k+Vec3{0,std::sin(k*3.14159265f)*0.8f,0};item->rotation[2]+=4;
            if(frame && (f-30)%4==0)burst(item->position,1,8);
        }
        if(item && frame && f==114){item->state=-1;item->collected=true;burst(chest,6,0x32);sound_events.push_back({SoundScope::Object,0,item->type,chest});}
        if(frame && f==140)overlay_scripts.zoe_reward(r.camera_side);
        if(f>=210)r=RewardShow{};
    }
}
bool Game::ability_learned(unsigned purchase)const {
    const bool campaign=level.custom_name.empty() && !level.universe && level.script_image && level.wad_slot>=15 && level.wad_slot<=71;
    return !campaign || portal_travel.unlock || (purchases&(1u<<purchase))!=0;
}
bool Game::award_skill_point(unsigned bit) {
    if(bit>=17)throw FormatError("Skill point outside the retail flag table");
    if(skill_points&(1u<<bit))return false;skill_points|=1u<<bit;return true;
}
void Game::complete_flight_task(unsigned task,unsigned ticks) {
    if(task>=5 || ticks>360000)throw FormatError("Invalid speedway result");
    guide_awards.flight_tasks|=1u<<task;
    if(task==4 && ticks && (!guide_awards.flight_ticks || ticks<guide_awards.flight_ticks))guide_awards.flight_ticks=ticks;
}
unsigned Game::spirits() const {
    if(level.source_game==GameBuild::YearOfTheDragonUS)return 0; // YOTD has no S2 spirit currency.
    unsigned result=0;
    for(const auto& object:retail_objects)if(spirit_released(object))result+=object.record[83]&127;
    return result;
}
unsigned Game::level_gems() const {
    if(systems.speedway())return std::popcount(guide_awards.flight_tasks)*80;
    unsigned awarded=0,pending=0;
    for(const auto& object:retail_objects) {
        if(object.runtime_spawned) {
            // Script gems are not backed by a broken container's held gem: they
            // count when taken, and are not pending while in flight.
            if(object.type==1 && object.script_gem){if(object.collected)awarded+=object.gem_value;}
            else if(object.type==1 && !object.collected && object.state>=0)pending+=object.gem_value;
        } else if(object.type==1 && object.collected)awarded+=object.gem_value;
        else if(level.source_game==GameBuild::YearOfTheDragonUS && object.source_type==0x3f3 && object.collected)awarded+=object.held_gem;
        else if((is_breakable(object.type) || is_heavy_container(object.type) || (level.source_game==GameBuild::YearOfTheDragonUS && object.enemy)) && (object.state<0 || (level.source_game==GameBuild::YearOfTheDragonUS && object.enemy && object.enemy->defeated)) && object.held_gem!=255)awarded+=object.held_gem;
    }
    for(const auto& object:play_scene.entities)if(object.kind==EntityKind::Gem && object.collected)awarded+=object.value;
    return awarded>=pending?awarded-pending:0;
}
void Game::set_paused(bool value) {
    if (paused==value) return;
    paused=value; clock.reset(); pending_={};
    sound_events.clear();
    player.previous_position=player.position; camera.reset_interpolation();
    player.previous_yaw=player.yaw;animation.reset_interpolation();secondary.reset_interpolation();
    player.previous_pitch=player.pitch;player.previous_roll=player.roll;
    charge_smoke.reset_interpolation();
    sparx.reset_interpolation();
    powers.reset_interpolation();
    elements.reset_interpolation();
    for(auto& object:retail_objects) {
        object.previous_animation=object.animation;object.previous_rotation=object.rotation;object.previous_position=object.position;
    }
}
void Game::advance(double seconds, InputState input) {
    if (paused) return;
    input.move_x=std::isfinite(input.move_x)?std::clamp(input.move_x,-1.0f,1.0f):0;
    input.move_z=std::isfinite(input.move_z)?std::clamp(input.move_z,-1.0f,1.0f):0;
    input.jump_pressed=input.jump_pressed || pending_.jump_pressed;
    input.flame_pressed=input.flame_pressed || pending_.flame_pressed;
    input.dialogue.interact|=pending_.dialogue.interact;input.dialogue.next|=pending_.dialogue.next;input.dialogue.cancel|=pending_.dialogue.cancel;
    if(input.dialogue.choice<0)input.dialogue.choice=pending_.dialogue.choice;
    input.center_camera|=pending_.center_camera;
    input.recall_hud|=pending_.recall_hud;
    input.carry_pressed|=pending_.carry_pressed;input.throw_pressed|=pending_.throw_pressed;input.melee_pressed|=pending_.melee_pressed;
    input.special_pressed|=pending_.special_pressed;input.headbash_pressed|=pending_.headbash_pressed;
    input.roll_left_pressed|=pending_.roll_left_pressed;input.roll_right_pressed|=pending_.roll_right_pressed;
    input.shot_pressed|=pending_.shot_pressed;input.fury_pressed|=pending_.fury_pressed;
    input.cycle_character|=pending_.cycle_character;
    if(input.element_select<0)input.element_select=pending_.element_select;
    if(!input.element_cycle)input.element_cycle=pending_.element_cycle;
    pending_=input;
    clock.advance(seconds,[&] { tick(pending_); pending_.jump_pressed=false; pending_.flame_pressed=false;pending_.dialogue={};pending_.center_camera=false;pending_.recall_hud=false;pending_.carry_pressed=pending_.throw_pressed=pending_.melee_pressed=pending_.special_pressed=pending_.headbash_pressed=pending_.roll_left_pressed=pending_.roll_right_pressed=false;
        pending_.shot_pressed=pending_.fury_pressed=pending_.cycle_character=false;pending_.element_select=-1;pending_.element_cycle=0; });
}
void Game::step_once() { if (paused) clock.advance(FixedClock::step,[&]{tick({});}); }
void Game::tick(InputState input) {
    constexpr float dt=static_cast<float>(FixedClock::step);
    const auto old_water=player.swim.mode;const auto old_motion=player.motion;
    const auto old_animation=animation.state();
    const auto old_gems=player.gems;
    const bool recall_hud=input.recall_hud;
    const bool pad_switch=input.dialogue.interact && PlayableBoss::at_switch(*this);
    if((input.cycle_character || pad_switch) && !scene_locked && !dialogue.active() && !portal_travel.active()) {
        const unsigned current=static_cast<unsigned>(playable_boss.character());
        for(unsigned offset=1;offset<4;++offset) {
            const auto next=static_cast<PlayableCharacter>((current+offset)%4);
            if(!playable_boss.select(*this,next))continue;
            custom_notice=playable_character_name(next);custom_notice_ticks=90;
            break;
        }
        if(pad_switch)input.dialogue.interact=input.special_pressed=input.look_held=false;
    }
    // Controller words for the original scripts (libpad bits): triangle talks
    // (and headbashes in the air), cross jumps, circle flames, square charges.
    overlay_scripts.set_pad((input.jump_held?0x40u:0)|(input.charge?0x80u:0),
        (input.dialogue.interact || (!playable_boss.active() && input.headbash_pressed)?0x10u:0)|
        (!playable_boss.active() && input.flame_pressed?0x20u:0)|(input.jump_pressed?0x40u:0));
    // With something in his mouth (8006a15c 2..5) Circle spits it out
    // (RetailScripts::carry); Spyro neither flames nor breathes meanwhile.
    if(overlay_scripts.carrying())input.flame_pressed=input.flame_held=false;
    if(player.invulnerable_ticks)--player.invulnerable_ticks;
    const bool hurting=player.hurt_ticks>0;if(hurting){--player.hurt_ticks;input={};}
    const bool recoiling=player.recoil_ticks>0;if(recoiling){--player.recoil_ticks;input={};}
    powers.begin_tick(*this);
    const auto sound=[&](SoundScope scope,unsigned id,unsigned type,Vec3 position) {
        if(sound_events.size()<256)sound_events.push_back({scope,id,type,position});
    };
    player.previous_position=player.position;
    player.previous_yaw=player.yaw;
    player.previous_pitch=player.pitch;player.previous_roll=player.roll;
    const bool was_talking=dialogue.active();
    update_travel(*this);
    if(advance_portal_travel(*this)) {
        playable_boss.tick(*this,true);
        sparx.tick(retail_objects,object_models_,level,player.position+retail::origin_above_feet,player.yaw,player.velocity,false,false,player.health,&sound_events,player.lives,{});
        return;
    }
    if(whirlwind.index!=UINT32_MAX)input={};
    if(systems.before_move(*this,input,object_models_)) {
        playable_boss.tick(*this,true);
        tick_retail_objects(retail_objects,object_models_,player.position+retail::origin_above_feet,player.gems);
        systems.after_move(*this,input,object_models_);
        animation.tick(motion_animation(player.motion));secondary.tick(0,animation.state(),false);
        if(!systems.results())camera.tick(level,player.position,player.yaw,motion_animation(player.motion),player.pitch,player.roll);
        hud.tick(level_gems(),spirits(),player.lives,recall_hud,true);hud.power_tick(powers.meter(*this),true);
        return;
    }
    update_scene_progress();
    // Conversations run on the original logic whenever the level's scripts do.
    if(overlay_scripts.dialogue_bridge()) {
        dialogue.retail_begin=[this](unsigned object){return overlay_scripts.begin_dialogue(*this,object);};
        dialogue.retail_advance=[this](unsigned,int answer){return overlay_scripts.advance_dialogue(*this,answer);};
        dialogue.retail_close=[this](unsigned){overlay_scripts.end_dialogue(*this);};
        if(const auto request=overlay_scripts.take_script_dialogue()) {
            std::optional<NpcDefinition> speaker;
            if(level.script_image)speaker=decode_npc_record(level.script_image->archive,level.wad_slot,request->first);
            if(speaker && !dialogue.active()){speaker->object=request->first;speaker->script_speaker=true;dialogue.begin_external(*speaker,request->second);}
            else {overlay_scripts.end_dialogue(*this);++overlay_scripts.rejected_dialogues;overlay_scripts.rejected_speaker=request->first;overlay_scripts.rejected_line=request->second;}
        }
    } else {dialogue.retail_begin=nullptr;dialogue.retail_advance=nullptr;dialogue.retail_close=nullptr;}
    // A Crownworks cutscene holds the controls; jump (or interact) asks to skip it.
    if(scene_locked && ripto.cinematic!=CrownCinematic::None && (input.jump_pressed || input.dialogue.interact))ripto.skip_requested=true;
    if(scene_locked)input={};
    else dialogue.tick(level,retail_objects,object_models_,player.position+retail::origin_above_feet,player.yaw,player.grounded,input.charge,input.dialogue,{&player.gems,&purchases,player.orbs});
    camera.passive=!dialogue.active_camera;
    if(input.center_camera)camera.center(player.yaw);
    tick_reward_show();
    if(dialogue.active() || was_talking || scene_locked || reward_show.active() || overlay_scripts.restarting()) {
        input={};player.velocity.x=player.velocity.z=0;
        if(dialogue.active()) {
            const auto delta=retail_objects[dialogue.object()].position-player.position;
            player.yaw=std::atan2(delta.x,delta.z);
        }
    }
    tick_look(input,hurting || recoiling || scene_locked || dialogue.active() || was_talking || abilities.attacking());
    update_water();
    const bool boss=playable_boss.active();
    const bool boss_blocked=hurting || recoiling || scene_locked || dialogue.active() || was_talking ||
        reward_show.active() || overlay_scripts.restarting() || whirlwind.index!=UINT32_MAX;
    const bool boss_sprint=boss && input.charge;
    if(boss)playable_boss.filter_input(*this,input,boss_blocked);
    auto& dodge=player.dodge;
    const bool was_rolling=dodge.active;
    const bool roll_blocked=hurting || recoiling || dialogue.active() || was_talking || scene_locked || powers.flying ||
        player.swim.mode!=WaterMode::Dry || !player.grounded || abilities.attacking() || whirlwind.index!=UINT32_MAX;
    const bool native_roll=play_scene.retail_crc==0x2c8235e7 || play_scene.retail_crc==0x3da0212b;
    if(roll_blocked || (!native_roll && (!upgrades.enabled || !upgrades.roll)))dodge={};
    if(dodge.active) {
        ++dodge.ticks;
        const unsigned id=dodge.side<0?10u:11u;
        const bool held=dodge.side<0?input.roll_left_held:input.roll_right_held;
        const auto& pose=animation.state();
        const auto end=spyro_model_->animations.at(id).transition_end;
        // July 8003091c tests the NEXT frame against the exit window. Testing
        // the current frame waits until the loop has already queued frame 0,
        // so opcode 4 would play another entire revolution before exiting.
        if(!held && dodge.ticks>=21 && pose.next_clip==id &&
           (pose.next_frame+1>=end || (pose.clip==id && pose.frame+1>=end)))dodge={};
    }
    // July's idle state clears the roll's sideways velocity. Feeding its
    // magnitude into normal movement would turn it into a forward lurch.
    if(was_rolling && !dodge.active && player.grounded)player.velocity.x=player.velocity.z=0;
    if(!dodge.active && !roll_blocked && roll_available() && (native_roll || (upgrades.enabled && upgrades.roll)) &&
       ((input.roll_left_pressed || input.roll_left_held)!=(input.roll_right_pressed || input.roll_right_held))) {
        dodge.active=true;dodge.side=(input.roll_left_pressed || input.roll_left_held)?-1:1;dodge.drift=player.velocity;dodge.drift.y=0;
        player.air={};player.jump={};player.slide={};flame_action.reset();interactions.stop_flame();
    }
    if(dodge.active){input.jump_pressed=input.jump_held=input.charge=input.flame_pressed=input.melee_pressed=input.headbash_pressed=input.special_pressed=false;}
    if(!boss){abilities.begin_tick(*this,input,object_models_);elements.begin_tick(*this,input);}
    if(elements.fury_pose()) {
        // Fury: Spyro levitates while the element gathers. Movement is held.
        input.move_x=input.move_z=0;input.jump_pressed=input.jump_held=input.charge=input.flame_pressed=input.melee_pressed=
            input.headbash_pressed=input.special_pressed=input.carry_pressed=input.throw_pressed=false;
    }
    if(!boss && !hurting && !recoiling && !dialogue.active() && player.swim.mode==WaterMode::Dry && powers.fly_enabled(*this) &&
       input.jump_pressed && !powers.flying && !player.grounded) {
        powers.flying=true;player.airborne_ticks=4;
    }
    if(powers.flying && input.charge && !(level.universe && level.universe->address.space)){powers.end_flight();input.charge=false;}
    const bool flying=powers.flying;
    auto wish=Vec3{input.move_x,0,input.move_z};
    if(input.camera_relative) {
        const auto view=camera.sample(1);
        const auto forward=normalized(Vec3{view.forward.x,0,view.forward.z});
        wish=cross(Vec3{0,1,0},forward)*input.move_x+forward*input.move_z;
    }
    const auto magnitude=length(wish);
    if (magnitude>1) wish=wish/magnitude;
    const auto approach=[](float from,float to,float amount) {return from+std::clamp(to-from,-amount,amount);};
    const bool in_water=player.swim.mode!=WaterMode::Dry;
    const bool had_support=player.grounded && !in_water && !flying && whirlwind.index==UINT32_MAX;
    if(had_support || in_water || flying || hurting)player.jump={};
    begin_aerial(input,boss || hurting || recoiling || dialogue.active() || was_talking || flying || in_water || abilities.attacking());
    const bool jumping=had_support && input.jump_pressed;
    if(jumping)sound(SoundScope::Global,0x22,0,{});
    // Contact projection supplies slope velocity for that step only. Carrying
    // its upward component into the next step launches Spyro at triangle seams.
    if(had_support)player.velocity.y=0;
    const bool gliding=player.air.gliding;
    const bool was_supercharging=player.motion==Motion::Supercharge || player.motion==Motion::SuperchargeJump;
    const bool was_charging=player.motion==Motion::Charge || player.motion==Motion::ChargeJump || was_supercharging;
    bool supercharging=!recoiling && !flying && !in_water && powers.charge_enabled(*this) && input.charge && !gliding && (player.grounded || was_supercharging);
    if(!input.charge && player.jump.kind==JumpState::Kind::Charge)++player.jump.release_ticks;
    else player.jump.release_ticks=0;
    const bool charge_coast=player.jump.kind==JumpState::Kind::Charge && player.jump.release_ticks<9;
    bool charging=(!flying && !in_water && (input.charge || charge_coast) && !gliding) || supercharging;
    if(!charging && player.jump.kind==JumpState::Kind::Charge)player.jump={};
    if(!input.charge && was_supercharging)powers.charge=false;
    const float brake=std::clamp(-input.move_z,0.0f,1.0f);
    const bool lifting=move_whirlwind(*this);
    if(lifting) {
        charging=false;
    } else if(hurting) {
        if(player.swim.mode==WaterMode::Underwater)player.hurt_velocity=player.hurt_velocity*.92f;
        else {player.hurt_velocity.y-=tuning.gravity*dt;player.hurt_velocity.x*=0.97f;player.hurt_velocity.z*=0.97f;}
        player.velocity=player.hurt_velocity;
    } else if(recoiling) {
        player.recoil_velocity.y-=tuning.gravity*dt;player.recoil_velocity.x*=.97f;player.recoil_velocity.z*=.97f;
        player.velocity=player.recoil_velocity;charging=false;
    } else if(dodge.active) {
        // July 80029c30 states 0A/0B, 80034438 and 800341ec:
        // 0x1000 lateral speed, +0x140 per VBlank, steering 15 by 3,
        // with the entry velocity decaying by one sixteenth each tick.
        if(magnitude>0.01f) {
            const float delta=std::remainder(std::atan2(wish.x,wish.z)-player.yaw,2*std::numbers::pi_v<float>);
            dodge.turn=approach(dodge.turn,std::clamp(delta/retail::angle_to_radians,-15.0f,15.0f),3);
            player.yaw+=dodge.turn*retail::angle_to_radians;
        }else dodge.turn=0;
        dodge.speed=approach(dodge.speed,0x1000*MovementTuning::velocity_unit,0x140*MovementTuning::velocity_unit);
        dodge.drift=dodge.drift*(15.0f/16);
        player.velocity=retail::rotate_model({dodge.side*dodge.speed,0,0},player.yaw)+dodge.drift;
        player.velocity.y=-tuning.gravity*dt;
    } else if(flying) {
        powers.fly_move(*this,input);
    } else if(in_water) {
        move_in_water(input,wish);
        charging=player.swim.mode==WaterMode::Underwater && input.charge && !player.swim.entry_ticks;
    } else if(move_aerial(input,wish)) {
        charging=false;player.jump={};
    } else if(!abilities.attacking() && !dialogue.active() && move_skate(input,wish,magnitude,had_support,jumping)) {
        charging=false;
    } else if(!abilities.attacking() && !dialogue.active() && move_slide(input,had_support)) {
        charging=false;
    } else if(charging) {
        if(!was_charging && magnitude>0.01f)player.yaw=std::atan2(wish.x,wish.z);
        // Overlay 18, 8006dca8: signed 35-unit steering approached by 8 per
        // VBlank. Charging propels Spyro with no forward stick requirement.
        const float target=input.move_x*127*(supercharging?35:25)/128;
        auto& turn=player.charge_turn_velocity;
        if(target==0 || turn*target<0)turn=0;
        turn=approach(turn,target,supercharging?8.0f:5.0f);
        player.yaw=std::remainder(player.yaw+turn*retail::angle_to_radians,2*std::numbers::pi_v<float>);
        auto speed=std::hypot(player.velocity.x,player.velocity.z);
        const bool air_charge=jumping || !had_support;
        const float target_speed=(supercharging?tuning.supercharge_speed*upgrades.charge_multiplier():
            air_charge?0x1800*MovementTuning::velocity_unit:tuning.charge_speed)*elements.movement(*this,MoveStat::Charge);
        if(air_charge && !was_charging)speed=std::max(speed,0x800*MovementTuning::velocity_unit);
        speed=approach(speed,target_speed,speed>target_speed?tuning.charge_deceleration*dt:
            air_charge && !supercharging?0x100*MovementTuning::velocity_unit:tuning.charge_acceleration*dt);
        player.velocity.x=std::sin(player.yaw)*speed;player.velocity.z=std::cos(player.yaw)*speed;
        move_jump(input,jumping,true,supercharging);
    } else {
        player.charge_turn_velocity=0;
        if(magnitude>0.01f)player.yaw=std::atan2(wish.x,wish.z);
        auto speed=std::hypot(player.velocity.x,player.velocity.z);
        const auto target=tuning.run_speed*std::min(magnitude,1.0f)*
            (boss?playable_boss.run_scale(boss_sprint):elements.movement(*this,MoveStat::Run));
        speed=approach(speed,target,(speed<target?tuning.acceleration:tuning.deceleration)*dt);
        player.velocity.x=std::sin(player.yaw)*speed;player.velocity.z=std::cos(player.yaw)*speed;
        move_jump(input,jumping,false,false);
    }
    // Grounded strikes step into their swing. Feed the impulse through the
    // ordinary swept collision path below; animation must never teleport
    // Spyro through a wall or a solid Moby. Air/swim/flight keep their motion.
    if(abilities.attacking() && had_support && !hurting && !dialogue.active()) {
        const auto tick=abilities.combo_tick;
        const float speed=tick>=7 && tick<=14?(abilities.combo==2?1.2f:abilities.combo==1?2.1f:2.7f):0;
        player.velocity.x=std::sin(player.yaw)*speed;player.velocity.z=std::cos(player.yaw)*speed;
    }
    if(elements.fury_pose()) {
        player.velocity={0,player.position.y<elements.fury().center.y+1.1f?1.6f:0.0f,0};
        player.air={};player.jump={};
    }
    player.swim.charge_was_held=input.charge;
    const bool diving=player.air.headbash_ticks>43;
    player.grounded=false;
    const int steps=std::max(1,static_cast<int>(std::ceil(length(player.velocity)*dt/(tuning.radius*0.45f))));
    bool charge_hit=false;
    for (int sub=0;sub<steps;++sub) {
        player.position+=player.velocity*(dt/static_cast<float>(steps));
        for (unsigned iteration=0;iteration<2;++iteration) {
            const auto center=player.position+retail::origin_above_feet;
            const auto incoming=player.velocity;TerrainContact contact;
            auto resolved=level.resolve_sphere(center,tuning.radius,player.velocity,player.grounded,&contact);
            const std::uint32_t attack=diving?(headbash_attack|charge_attack):supercharging?0x560000:charge_attack;
            resolved=interactions.resolve_player(retail_objects,object_models_,resolved,tuning.radius,player.velocity,player.grounded,charging || diving,attack);
            // 8002c0f4 passes Spyro's live attack bits (8006a128) to every
            // movement query: a charge registers on the moby it runs into.
            resolved=overlay_scripts.resolve_player(*this,resolved,tuning.radius,player.velocity,player.grounded,
                charging || diving?attack:player.swim.mode==WaterMode::Underwater?charge_attack:0);
            player.position+=resolved-center;
            if(charging && !in_water && !charge_hit && has_animation(13) && contact.incoming_speed>std::max(2.5f,std::hypot(incoming.x,incoming.z)*.55f)) {
                // Resident 800351D8 state 0D reverses velocity at one quarter;
                // 80035D38 retains the recovery until VBlank 0x29. No HP loss.
                player.recoil_velocity=incoming*-.25f;player.recoil_velocity.y=std::max(0.0f,player.recoil_velocity.y);
                player.velocity=player.recoil_velocity;player.recoil_ticks=41;++player.charge_impacts;charge_hit=true;
                if(supercharging && !(contact.triangle&Level::object_triangle))destruction.impact(*this,contact.point,contact.normal,TerrainAttack::Supercharge,object_models_);
                supercharging=charging=false;player.jump={};player.air={};player.slide={};charge_smoke.reset();flame_action.reset();interactions.stop_flame();
                sound(SoundScope::Global,2,0,player.position);camera.shake(1200,12);
            }
        }
    }
    if(had_support && !jumping && !lifting) {
        const float travel=std::hypot(player.position.x-player.previous_position.x,player.position.z-player.previous_position.z);
        const auto drop=std::min(tuning.radius*0.65f,0.025f+travel*1.5f);
        if(const auto support=level.ground_support(player.position+retail::origin_above_feet,tuning.radius,drop)) {
            player.position.y-=support->distance;player.grounded=true;player.velocity.y=0;
        }
    }
    update_water();
    if(flying && player.grounded)powers.end_flight();
    if(player.swim.mode==WaterMode::Surface) {
        player.position.y=player.swim.height-player.swim.bob/(64*retail::units_per_world);
        player.velocity.y=0;player.grounded=false;
    } else if(player.swim.mode==WaterMode::Underwater)player.grounded=false;
    finish_aerial(diving);
    headbash.tick(*this,object_models_);
    const bool outside_starways=level.universe && level.universe->address.space &&
        (std::abs(player.position.x)>170 || std::abs(player.position.y-12)>100 || player.position.z<-100 || player.position.z>180);
    if (player.position.y<level.bounds.min.y-20 || outside_starways) {
        if (!outside_starways && player.lives) --player.lives;
        // Do not reset the clock from inside its callback.
        reset_player();return;
    }
    player.airborne_ticks=player.grounded?0:jumping?4:std::min(player.airborne_ticks+1,100000u);
    // 80035d38 keeps idle/run/charge through fewer than four unsupported
    // VBlanks. Deliberate jumps bypass this animation grace period.
    const bool ground_motion=player.grounded || player.airborne_ticks<4;
    const auto motion=elements.fury_pose()?Motion::Hover:recoiling || charge_hit?Motion::ChargeRecoil:dodge.active?(dodge.side<0?Motion::RollLeft:Motion::RollRight):hurting && player.swim.mode==WaterMode::Underwater?Motion::UnderwaterHurt:
        whirlwind.index!=UINT32_MAX?Motion::Whirlwind:powers.flying?(powers.flight_clip()==34?Motion::SuperflyClimb:powers.flight_clip()==35?Motion::SuperflyDive:Motion::Superfly):
        player.swim.mode==WaterMode::Surface?(magnitude>0.01f?Motion::SurfaceSwim:Motion::SurfaceIdle):
        player.swim.mode==WaterMode::Underwater?((input.charge && !player.swim.entry_ticks)?Motion::UnderwaterCharge:
            (player.swim.speed>0.001f?Motion::UnderwaterSwim:Motion::UnderwaterIdle)):
        player.air.headbash_ticks?Motion::Headbash:player.air.recovery_ticks?Motion::HeadbashLand:
        player.air.hover_ticks?Motion::Hover:player.air.boost_ticks?Motion::GlideBoost:
        player.skate.active && ground_motion && has_animation(9)?Motion::Skate:
        player.slide.active && ground_motion?Motion::Slide:
        supercharging?(ground_motion?Motion::Supercharge:Motion::SuperchargeJump):
        ground_motion ? (charging?Motion::Charge:(magnitude<0.01f?Motion::Idle:Motion::Run)) :
        ((charging || (player.swim.breaching && player.velocity.y>0))?Motion::ChargeJump:
        (player.air.gliding?(brake>0.01f?Motion::GlideBrake:Motion::Glide):(player.velocity.y>0?Motion::Jump:Motion::Fall)));
    if(player.grounded)player.swim.breaching=false;
    player.motion_ticks=motion==player.motion?player.motion_ticks+1:0;player.motion=motion;
    if(!in_water && !flying) {
        if(motion==Motion::ChargeJump) {
            // 800350d0 state 15 -> 8002ded0: the charge-jump pose follows
            // velocity with an 0x8e pitch offset and the retail damped spring.
            // The mesh clip alone is level; flattening pitch loses the arc.
            static const retail::MathTables fallback;
            const auto& math=object_models_?object_models_->math:fallback;
            const retail::IVec3 velocity{static_cast<int>(std::lround(player.velocity.x/MovementTuning::velocity_unit)),
                static_cast<int>(std::lround(player.velocity.z/MovementTuning::velocity_unit)),
                static_cast<int>(std::lround(player.velocity.y/MovementTuning::velocity_unit))};
            const int target=static_cast<int>(math.angle(math.length(velocity,false),velocity.z,1))+0x8e;
            int pitch=static_cast<int>(std::lround(player.pitch/retail::angle_to_radians));
            int delta=(target-pitch)&4095;if(delta>2048)delta-=4096;
            auto& speed=player.jump.pitch_velocity;speed+=(delta>>2)-(speed>>2);
            pitch=(pitch+(speed>>2))&4095;if(pitch>2048)pitch-=4096;
            player.pitch=pitch*retail::angle_to_radians;
        } else {player.jump.pitch_velocity=0;player.pitch=approach(player.pitch,0,0.06f);}
        player.roll=approach(player.roll,0,0.06f);
    }
    auto requested=motion_animation(motion);
    const bool flame_allowed=object_models_?object_models_->flame.allowed.at(requested)!=0:!charging;
    // 8006f3ec explicitly cancels the independent head before selecting the
    // different vertex layout. Never blend a flame head onto clip 61.
    // An equipped new element owns F: its original breath plays the retail
    // head gesture, but never starts the retail flame or a powered shot.
    const bool element_breath=elements.breath_override(*this);
    const bool powered=!element_breath && (cheats.green_firebomb || ripto.green_bomb() || powers.flame_enabled(*this) || powers.freeze_enabled(*this) || powers.secondary_action());
    const bool aerial_action=player.air.headbash_ticks || player.air.recovery_ticks || player.air.hover_ticks || player.air.boost_ticks || dodge.active || recoiling || charge_hit;
    const bool suppress_flame=supercharging || (player.swim.mode==WaterMode::Underwater && !element_breath) || powered || aerial_action || elements.fury_pose();
    const bool powered_allowed=object_models_?object_models_->powered_fire_allowed.at(requested)!=0:!charging;
    auto powered_input=input;if(element_breath)powered_input.flame_pressed=false;
    if(!boss && powers.fire_tick(*this,powered_input,powered_allowed,supercharging || hurting || dialogue.active() || abilities.attacking() || aerial_action) && motion==Motion::Idle)requested=7;
    if(suppress_flame){flame_action.reset();interactions.stop_flame();}
    // A held elemental breath re-arms the retail head gesture so the jaw stays open.
    const bool breath_held=element_breath && elements.breathing();
    const auto fire=flame_action.tick((input.flame_pressed || breath_held) && !suppress_flame && (!element_breath || breath_held),flame_allowed && !suppress_flame,charging,interactions.flame_active());
    if(fire==FlameEvent::Start) {
        if(!element_breath)sound(SoundScope::Global,3,0,{});
        if(object_models_ && !element_breath)interactions.start_flame();
        // 8002fc20 requests the whole flame clip when firing from idle. The
        // following idle request preserves that clip's primary exit window.
        if(motion==Motion::Idle)requested=7;
    } else if(fire==FlameEvent::Cancel)interactions.stop_flame();
    interactions.follow_flame_attachment(flame_action.follows_attachment());
    animation.tick(requested);
    const auto& source_frame=animation.state();
    if(player.grounded && (source_frame.clip!=old_animation.clip || source_frame.frame!=old_animation.frame)) {
        const auto f=source_frame.frame;
        // July 8002e26c states 0A/0B play native global 14 on frame 1.
        // A held roll reaches it once per revolution; interpolation does not
        // replay the cue on every rendered frame.
        if((source_frame.clip==10 || source_frame.clip==11) && f==1)
            sound(SoundScope::Global,july_roll_sound_id,0,{});
        if((source_frame.clip==2 && (f==0 || f==7 || f==9 || f==17)) ||
           (source_frame.clip==1 && (f==5 || f==13)) || (source_frame.clip==22 && (f==2 || f==12)))
            sound(SoundScope::Surface,0,0,{});
    }
    // Overlay 14 state entry (8006f0a0): landing on or surfacing to the water
    // (2B/2C) plays global 7; diving under (28/29/2A) plays global 8.
    if(old_water!=player.swim.mode) {
        if(player.swim.mode==WaterMode::Surface)sound(SoundScope::Global,7,0,{});
        else if(player.swim.mode==WaterMode::Underwater)sound(SoundScope::Global,8,0,{});
    }
    if(player.grounded && (old_motion==Motion::Fall || old_motion==Motion::Jump || old_motion==Motion::Glide))sound(SoundScope::Surface,0,0,{});
    // Something in his mouth: the head partition follows the retail head
    // state 8006a15c (2 takes it, clip 53; 3 holds it, 45; 4 spits, 54; 5
    // follows the body while he cannot carry). Those clips are in the banks
    // of the levels that use them.
    const unsigned carry_head=overlay_scripts.carrying()?overlay_scripts.head_state():0u;
    if(carry_head && spyro_model_ && spyro_model_->animations.contains(spyro_model_->secondary_clips.at(carry_head)))
        secondary.tick(carry_head,animation.state(),supercharging || was_supercharging || hurting);
    else secondary.tick(powered?powers.secondary_action():flame_action.action(),animation.state(),flame_action.force_follow() || supercharging || was_supercharging || aerial_action || hurting);
    charge_smoke.tick(supercharging && player.swim.mode==WaterMode::Dry,player.position+retail::origin_above_feet,player.velocity);
    for (auto& e:play_scene.entities)
        if (!e.collected && e.kind==EntityKind::Gem && length(e.position-(player.position+Vec3{0,0.5f,0}))<1.2f) {
            e.collected=true; player.gems+=static_cast<unsigned>(e.value);
        }
    tick_gem_numbers(retail_objects,level,(clock.ticks()&1)==0);
    tick_retail_objects(retail_objects,object_models_,player.position+retail::origin_above_feet,player.gems);
    playable_boss.tick(*this,boss_blocked);
    // 8002f2a4 reads the primary source frame's packed attachment. It does
    // not use the renderer's blend between that frame and its next endpoint.
    const auto& animation_state=animation.state();
    const auto attachment=animation.ready() && interactions.flame_active()?
        spyro_model_->animations.at(animation_state.clip).frames.at(animation_state.frame).attachment:Vec3{};
    abilities.tick(*this,object_models_);
    interactions.tick(retail_objects,object_models_,level,player.position+retail::origin_above_feet,player.velocity,
                      player.yaw,attachment,charging,player.gems);
    powers.tick(*this,object_models_,spyro_model_);
    elements.tick(*this,object_models_,spyro_model_);
    demons.tick(*this,object_models_);
    elements.resolve(*this,object_models_);
    destruction.tick(*this);
    systems.after_move(*this,input,object_models_);
    if(level.source_game==GameBuild::YearOfTheDragonUS)yotd_scripts.tick(*this);
    if(const auto hit=enemies.tick(retail_objects,object_models_,level,player.position+retail::origin_above_feet,dialogue.active() || scene_locked))
        hurt(hit->source);
    overlay_scripts.tick(*this);
    // Crush's and Gulp's death sequences end in their outro cutscene request:
    // travel on to the home the original continues in.
    if(const auto scene=overlay_scripts.take_cutscene_scene();scene && level.script_image)
        if(const auto slot=campaign_slot(level.script_image->executable,*scene))start_scripted_travel(*this,*slot);
    const auto companion_view=camera.sample(1);
    CompanionContext companion_context;
    companion_context.idle=player.motion==Motion::Idle && !dialogue.active() && !hurting && !flame_action.action();
    companion_context.underwater=player.swim.mode==WaterMode::Underwater;
    companion_context.gem_hint=input.gem_hint && !dialogue.active();
    companion_context.camera_mode=camera.state();companion_context.pitch=player.pitch;companion_context.roll=player.roll;
    companion_context.camera_eye=companion_view.eye;companion_context.camera_forward=companion_view.forward;
    sparx.tick(retail_objects,object_models_,level,player.position+retail::origin_above_feet,player.yaw,player.velocity,
               player.motion==Motion::Charge || supercharging,player.motion==Motion::Fall,player.health,&sound_events,player.lives,companion_context);
    sparx.award_deliveries(player.gems);
    sparx.award_health(player.health,player.lives,object_models_?object_models_->sparx_max_health:3);
    if(upgrades.enabled && player.gems>old_gems)upgrades.points=std::min(1000000u,upgrades.points+player.gems-old_gems);
    if(std::getenv("SPYRO_GEM_TRACE") && player.gems!=old_gems)std::fprintf(stderr,"gems %u -> %u (level %u) at tick %llu\n",old_gems,player.gems,level_gems(),static_cast<unsigned long long>(clock.ticks()));
    powers.gates_tick(*this);
    std::vector<RetailObject> gem_feedback;
    for(auto& object:retail_objects) {
        if(!object.audio_finished && ((object.type==1 && object.collected) || ((is_breakable(object.type) || is_heavy_container(object.type) || object.type==0x30) && object.state<0))) {
            sound(SoundScope::Object,0,object.type,object.position);object.audio_finished=true;
            if(object.type==1 && object_models_)queue_gem_numbers(gem_feedback,object,*object_models_,companion_view.forward);
        }
        if(!object_models_ || object.collected || object.hidden || object.state<0 || !object.animation.active)continue;
        const auto& now=object.animation;const auto& before=object.previous_animation;
        if(now.clip==before.clip && now.frame==before.frame)continue;
        const auto bank=object_models_->models.find(object.type);if(bank==object_models_->models.end() || now.clip>=bank->second.animations.size())continue;
        const auto& clip=bank->second.animations[now.clip];if(now.frame>=clip.frames.size())continue;
        // 80041068 queues the signed byte at metadata bits 16..23 on a new frame.
        const auto event=(clip.frames[now.frame].metadata>>16)&255;
        if(event<128)sound(SoundScope::Object,event,object.type,object.position);
    }
    install_gem_numbers(retail_objects,gem_feedback);
    camera.conversation_yaw=dialogue.active()?std::optional(retail::native_heading(retail_objects[dialogue.object()].rotation[2]*16)):std::nullopt;
    camera.conversation_side=dialogue.camera_side();
    if(!dialogue.active() && reward_show.active() && reward_show.speaker<retail_objects.size()) {   // the reward view (80016328 mode 12)
        camera.conversation_yaw=retail::native_heading(retail_objects[reward_show.speaker].rotation[2]*16);
        camera.conversation_side=static_cast<int>(reward_show.camera_side);
    }
    const float camera_yaw=whirlwind.index<level.whirlwinds.size()?
        retail::native_heading(retail_objects[level.whirlwinds[whirlwind.index].object].rotation[2]*16):player.yaw;
    camera.steer(input.camera_x,input.camera_y,std::hypot(player.velocity.x,player.velocity.z)>.05f);
    camera.tick(level,player.position,camera_yaw,player.motion==Motion::UnderwaterHurt?41:motion_animation(player.motion),player.pitch,player.roll);
    tick_head();
    // Crush (31) and Gulp (55): the overlay's boss HUD hooks run while
    // 80066eec is negative; the health counter shows 80066fec.
    if(level.wad_slot==31 || level.wad_slot==55) {
        const bool fight=static_cast<std::int32_t>(overlay_scripts.peek(0x80066eec,4))<0;
        hud.boss(fight?std::optional<unsigned>(std::min(overlay_scripts.peek(0x80066fec,4),level.wad_slot==31?7u:10u)):std::nullopt,level.wad_slot==31?5:3);
    } else hud.boss(std::nullopt,5);
    hud.tick(level_gems(),spirits(),player.lives,recall_hud,dialogue.active() || scene_locked);
    hud.power_tick(powers.meter(*this),dialogue.active() || scene_locked);
    tick_universe(*this,input.dialogue.interact);
    tick_custom_level(*this,input);
    ripto.tick(*this,object_models_);
    detect_portal_entry(*this);
}
} // namespace spyro
