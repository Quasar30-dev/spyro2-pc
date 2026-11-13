#include "spyro/playable_boss.hpp"
#include "spyro/runtime.hpp"

namespace spyro {
namespace {
struct BossProfile {
    const char* name;
    unsigned slot, type, draw_flags, color;
    unsigned idle, walk, run, airborne, hurt, close, ranged;
    unsigned close_hit, close_end, release, ranged_end;
    unsigned projectile;
    float walk_scale, run_scale, camera_scale;
    Vec3 muzzle;
};
constexpr BossProfile profiles[]{
    {"Crush",31,0x8f,0x30,0,0,3,9,5,6,7,1,50,110,40,88,0x288,.76f,1.22f,2.1f,{0,1.0f,1.7f}},
    {"Gulp",55,0x191,0x40,0x2a605050,0,3,8,2,5,2,10,47,96,39,76,0x289,.66f,1.34f,2.25f,{0,1.36f,2.93f}},
    {"Ripto",71,0x2af,0x40,0,0,1,1,13,6,5,2,14,32,50,78,0x2b7,.94f,1.42f,1.55f,{0,1.05f,1.25f}},
};
const BossProfile& profile(PlayableCharacter ch){return profiles[static_cast<unsigned>(ch)-1];}
constexpr float dt=1.0f/60;
bool damage(Game& game,Vec3 point,float radius,std::uint32_t flags) {
    bool hit=false;
    for(auto& object:game.retail_objects)if(container_contact(object,*game.object_models(),point,radius)) {
        object.attack_flags|=flags;
        hit=true;
    }
    return game.overlay_scripts.attack_query(point,radius,flags) || hit;
}
}

const char* playable_character_name(PlayableCharacter character) {
    return character==PlayableCharacter::Spyro?"Spyro":profile(character).name;
}
void import_playable_bosses(ObjectModels& models,WadArchive& wad,std::span<const std::uint8_t> executable) {
    for(const auto& p:profiles) {
        if(!wad.contains(p.slot) || (models.models.contains(p.type) && models.models.contains(p.projectile)))continue;
        const auto source=decode_object_models(wad.read(p.slot),executable);
        const unsigned classes[]{p.type,p.projectile};
        for(const auto type:classes)if(source.models.contains(type))import_object_models(models,source,std::span(&type,1));
    }
}
bool PlayableBoss::available(PlayableCharacter character,const ObjectModels* models)const {
    if(character==PlayableCharacter::Spyro)return true;
    if(!models)return false;
    const auto& p=profile(character);
    const auto found=models->models.find(p.type);
    if(found==models->models.end() || !models->models.contains(p.projectile))return false;
    for(const auto clip:{p.idle,p.walk,p.run,p.airborne,p.hurt,p.close,p.ranged})
        if(clip>=found->second.animations.size() || found->second.animations[clip].frames.empty())return false;
    return true;
}
bool PlayableBoss::select(Game& game,PlayableCharacter character) {
    if(!available(character,game.object_models()))return false;
    if(character==character_)return true;
    character_=character;reset();
    game.player.air={};game.player.dodge={};game.player.jump={};game.player.look={};
    game.camera.look={};
    game.player.velocity.x=game.player.velocity.z=0;
    game.flame_action.reset();game.interactions.stop_flame();game.charge_smoke.reset();
    game.powers.end_flight();game.abilities.reset(game);game.elements.reset(game);
    game.camera.follow_scale=camera_scale();
    if(active()) {
        const auto& p=profile(character_);
        body_={};body_.type=static_cast<std::uint16_t>(p.type);
        body_.draw_flags=static_cast<std::uint8_t>(p.draw_flags);body_.material_color=p.color;
        body_.size=static_cast<std::uint8_t>(96/scale_);
        body_.position=body_.previous_position=game.player.position;
        body_.rotation[2]=body_.previous_rotation[2]=retail::original_heading(game.player.yaw)/16;
        body_.animation=body_.previous_animation=start_object_animation(game.object_models()->models.at(p.type),p.idle);
        pose_ready_=true;
    }
    return true;
}
void PlayableBoss::set_scale(Game& game,unsigned thirds) {
    scale_=std::clamp(thirds,1u,3u);
    body_.size=static_cast<std::uint8_t>(96/scale_);
    game.camera.follow_scale=camera_scale();
}
void PlayableBoss::reset() {
    shots_.clear();waves_.clear();attack_=Attack::None;attack_tick_=0;sprint_=false;pose_ready_=false;
}
bool PlayableBoss::at_switch(const Game& game) {
    return game.level.custom_name=="Boss Training Ground" && length(game.player.position-game.level.spawn)<2.6f;
}
float PlayableBoss::run_scale(bool sprint)const {
    return active()?(sprint?profile(character_).run_scale:profile(character_).walk_scale):1;
}
float PlayableBoss::camera_scale()const {
    return active()?std::lerp(1.0f,profile(character_).camera_scale,scale_/3.0f):1;
}
void PlayableBoss::filter_input(Game& game,InputState& input,bool blocked) {
    if(blocked)attack_=Attack::None;
    if(!blocked && attack_==Attack::None && game.player.swim.mode==WaterMode::Dry) {
        if(input.flame_pressed || input.shot_pressed)attack_=Attack::Ranged;
        else if(input.melee_pressed || input.headbash_pressed)attack_=Attack::Close;
        if(attack_!=Attack::None) {
            attack_tick_=0;
            const auto& p=profile(character_);
            const unsigned cue=character_==PlayableCharacter::Crush?0u:character_==PlayableCharacter::Gulp?2u:1u;
            game.sound_events.push_back({SoundScope::Object,cue,p.type,game.player.position});
        }
    }
    sprint_=input.charge && attack_==Attack::None;
    if(attack_!=Attack::None) {
        input.move_x=input.move_z=0;
        input.jump_pressed=input.jump_held=false;
    }
    input.charge=input.flame_pressed=input.flame_held=input.shot_pressed=input.melee_pressed=
        input.headbash_pressed=input.special_pressed=input.look_held=input.carry_pressed=input.throw_pressed=
        input.roll_left_pressed=input.roll_right_pressed=input.roll_left_held=input.roll_right_held=
        input.fury_pressed=input.fury_held=false;
}
void PlayableBoss::tick(Game& game,bool blocked) {
    if(!active())return;
    const auto& p=profile(character_);
    const auto& models=*game.object_models();
    const auto& bank=models.models.at(p.type);
    const auto& player=game.player;
    if(blocked)attack_=Attack::None;
    const bool moving=std::hypot(player.velocity.x,player.velocity.z)>.35f;
    const auto clip=player.hurt_ticks?p.hurt:attack_==Attack::Close?p.close:attack_==Attack::Ranged?p.ranged:
        !player.grounded && player.swim.mode==WaterMode::Dry?p.airborne:moving?(sprint_?p.run:p.walk):p.idle;
    if(!pose_ready_) {
        body_.animation=start_object_animation(bank,clip);
        body_.position=player.position;
        body_.rotation[2]=retail::original_heading(player.yaw)/16;
        pose_ready_=true;
    }
    begin_object_transform(body_,1);
    body_.previous_animation=body_.animation;
    body_.position=player.position;
    body_.rotation[2]=retail::original_heading(player.yaw)/16;
    if(body_.animation.next_clip!=clip)
        body_.animation=start_object_animation(bank,clip);
    tick_object_animation(bank,body_.animation);
    if(attack_!=Attack::None)++attack_tick_;
    const auto direction=object_basis(body_).z;
    const float size=object_model_scale(body_);
    if(attack_==Attack::Close) {
        const bool crush=character_==PlayableCharacter::Crush;
        if(attack_tick_==p.close_hit || (crush && attack_tick_==106)) {
            const auto center=player.position+direction*(size*(crush?2.1f:character_==PlayableCharacter::Gulp?1.8f:.75f));
            const auto arm=center-player.position;
            const auto wall=game.level.raycast(player.position+Vec3{0,.4f,0},arm,length(arm));
            if(!wall)damage(game,center+Vec3{0,.4f,0},(crush?1.8f:character_==PlayableCharacter::Gulp?2.0f:1.15f)*std::max(.6f,size),
                headbash_attack|charge_attack|flame_attack);
            if(crush && !wall)waves_.push_back({center,0});
            game.camera.shake(crush?2300:1200,18);
            game.sound_events.push_back({SoundScope::Object,crush?1u:character_==PlayableCharacter::Gulp?3u:5u,p.type,center});
        }
        if(attack_tick_>=p.close_end)attack_=Attack::None;
    } else if(attack_==Attack::Ranged) {
        if(attack_tick_==p.release) {
            const bool gulp=character_==PlayableCharacter::Gulp;
            const auto muzzle=character_==PlayableCharacter::Ripto && bank.attachment_vertices?
                sample_object_attachment(bank,body_.animation,models.animation_blends):p.muzzle;
            Shot shot;
            shot.body.type=static_cast<std::uint16_t>(p.projectile);
            shot.body.draw_flags=0x40;shot.body.size=32;
            shot.body.position=shot.body.previous_position=player.position+object_basis(body_)*(muzzle*size);
            shot.body.rotation=shot.body.previous_rotation=body_.rotation;
            if(const auto& model=models.models.at(p.projectile);model.animated)
                shot.body.animation=shot.body.previous_animation=start_object_animation(model);
            const auto aim=player.position+direction*(gulp?14.0f:10.0f)+Vec3{0,.42f,0};
            const auto heading=normalized(aim-shot.body.position);
            shot.velocity=gulp?direction*13.5f+Vec3{0,3.0f,0}:heading*21.0f;
            shot.gravity=gulp?15.0f:0;
            shot.radius=gulp?.8f:character_==PlayableCharacter::Ripto?.78f:.62f;
            shot.attack=gulp?superflame_attack|headbash_attack:superflame_attack;
            // Stop a muzzle that crosses level collision at the point of impact.
            const auto base=player.position+Vec3{0,.3f,0},arm=shot.body.position-base;
            if(const auto wall=game.level.raycast(base,arm,length(arm))) {
                game.powers.emit_sparkle(wall->point,{0,.5f,0},{1.0f,.72f,.25f},20);
            } else shots_.push_back(shot);
            game.sound_events.push_back({SoundScope::Object,character_==PlayableCharacter::Crush?1u:0u,
                character_==PlayableCharacter::Crush?p.type:p.projectile,shot.body.position});
        }
        if(attack_tick_>=p.ranged_end)attack_=Attack::None;
    }
    for(auto& shot:shots_) {
        begin_object_transform(shot.body,1);
        auto& position=shot.body.position;
        const auto step=shot.velocity*dt;
        const auto wall=game.level.raycast(position,step,length(step));
        position=wall?wall->point:position+step;
        shot.velocity.y-=shot.gravity*dt;++shot.age;
        if(const auto& model=models.models.at(shot.body.type);model.animated) {
            shot.body.previous_animation=shot.body.animation;
            tick_object_animation(model,shot.body.animation);
        }
        if(damage(game,position,shot.radius,shot.attack) || wall) {
            if(shot.gravity>0)damage(game,position,1.55f,shot.attack);
            if(shot.body.type==0x289)game.sound_events.push_back({SoundScope::Object,1,0x289,position});
            game.powers.emit_sparkle(position,{0,.5f,0},{1.0f,.72f,.25f},20);
            shot.age=140;
        }
    }
    std::erase_if(shots_,[](const auto& shot){return shot.age>=140;});
    for(auto& wave:waves_) {
        const float radius=++wave.age*.16f;
        for(unsigned i=0;i<20;++i) {
            const float angle=i*(2*std::numbers::pi_v<float>/20);
            const auto point=wave.origin+Vec3{std::cos(angle)*radius,.35f,std::sin(angle)*radius};
            if(game.level.raycast(wave.origin+Vec3{0,.35f,0},point-wave.origin-Vec3{0,.35f,0},radius))continue;
            damage(game,point,.47f,headbash_attack|charge_attack);
        }
    }
    std::erase_if(waves_,[](const auto& wave){return wave.age>=32;});
}
}
