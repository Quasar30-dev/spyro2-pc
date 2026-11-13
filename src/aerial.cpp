#include "spyro/runtime.hpp"
#include <numbers>

namespace spyro {
namespace {
constexpr float unit=MovementTuning::velocity_unit,dt=1.0f/60;
float approach(float from,float to,float step){return from+std::clamp(to-from,-step,step);}
Vec3 approach_vector(Vec3 from,Vec3 to,float step) {
    const auto delta=to-from;const auto distance=length(delta);
    return distance>step?from+delta*(step/distance):to;
}
}
void Game::begin_aerial(InputState& input,bool blocked) {
    auto& a=player.air;
    if(a.recovery_ticks)--a.recovery_ticks;
    if(player.grounded) {
        const auto recovery=a.recovery_ticks;a={};a.recovery_ticks=recovery;
    }
    if(blocked) {
        player.slide={};
        // Interrupt the action without refunding boosts in mid-air.
        const auto used=a.boosts_used;const auto started=a.glide_started;
        a={};a.boosts_used=used;a.glide_started=started;return;
    }
    if(!upgrades.enabled || !upgrades.glide)a.boost_ticks=0;
    if(a.recovery_ticks){input.move_x=input.move_z=0;input.jump_pressed=input.charge=input.flame_pressed=false;return;}
    if(player.grounded)return;
    if(a.gliding || a.headbash_ticks || a.hover_ticks)player.slide={};
    if(input.charge){a.gliding=a.glide_pending=false;a.boost_ticks=a.hover_ticks=0;}
    if(!a.headbash_ticks && ability_learned(6) && (input.headbash_pressed || (input.special_pressed && !a.gliding))) {
        a.gliding=a.glide_pending=false;a.boost_ticks=a.hover_ticks=0;
        a.headbash_ticks=1;player.velocity={0,0x800*unit,0};
    } else if(input.special_pressed && a.gliding) {
        a.gliding=false;a.boost_ticks=0;a.hover_ticks=1;player.velocity.y=0xc00*unit;++player.hover_actions;
    } else if(input.jump_pressed && !a.headbash_ticks && !a.hover_ticks) {
        if(a.gliding) {
            const auto limit=upgrades.enabled?std::min(upgrades.glide,2u):0;
            if(!a.boost_ticks && a.boosts_used<limit && (!spyro_model_ || spyro_model_->animations.contains(34))) {
                a.boost_ticks=1;++a.boosts_used;++player.glide_boosts;
                sound_events.push_back({SoundScope::Global,0x22,0,{}});
            }
        } else if(!a.glide_started)a.glide_pending=true;
    }
    // Queue a second jump tap during ascent; releasing the button never
    // cancels the request or an established glide.
    if(a.glide_pending && player.velocity.y<=0 && !input.charge) {
        a.glide_pending=false;a.glide_started=a.gliding=true;a.glide_ceiling=player.position.y;
        const float speed=std::clamp(std::hypot(player.velocity.x,player.velocity.z),tuning.glide_brake_speed,tuning.glide_speed);
        player.velocity={std::sin(player.yaw)*speed,0,std::cos(player.yaw)*speed};
    }
    if(a.headbash_ticks || a.hover_ticks || a.boost_ticks)input.flame_pressed=false;
}
bool Game::move_aerial(const InputState& input,Vec3 wish) {
    auto& a=player.air;
    if(a.headbash_ticks) {
        // 800351d8 / 80033300, state 2e: flip for 42 VBlanks, then
        // accelerate down by 800 Q6 units, capped at 3200.
        player.velocity.x=player.velocity.z=0;
        if(a.headbash_ticks<=42)player.velocity.y-=0x20*unit;
        else player.velocity.y=std::max(player.velocity.y-0x800*unit,-0x3200*unit);
        ++a.headbash_ticks;return true;
    }
    if(a.hover_ticks) {
        // State 11: seven initial lift ticks, then c0 gravity. A hover
        // cannot climb above the height saved when glide began.
        auto horizontal=approach_vector({player.velocity.x,0,player.velocity.z},wish*(0x640*unit),0x280*unit);
        player.velocity.x=horizontal.x;player.velocity.z=horizontal.z;
        player.velocity.y+=(a.hover_ticks<=7?0x17:-0xc0)*unit;
        player.velocity.y=std::min(player.velocity.y,(a.glide_ceiling-player.position.y)*60);
        if(++a.hover_ticks>120 || player.velocity.y< -0x1900*unit)a.hover_ticks=0;
        return true;
    }
    if(!a.gliding)return false;
    player.yaw=std::remainder(player.yaw+input.move_x*tuning.glide_turn_speed*dt,2*std::numbers::pi_v<float>);
    const float brake=std::clamp(-input.move_z,0.0f,1.0f);
    const auto target=(tuning.glide_speed-(tuning.glide_speed-tuning.glide_brake_speed)*(brake*127/128))*elements.movement(*this,MoveStat::Glide);
    const auto speed=approach(std::hypot(player.velocity.x,player.velocity.z),target,tuning.glide_acceleration*dt);
    player.velocity.x=std::sin(player.yaw)*speed;player.velocity.z=std::cos(player.yaw)*speed;
    if(a.boost_ticks) {
        // Optional PC extension, reusing Superfly's 12-tick lift / 24-tick
        // release envelope and mounted climb animation, at most twice.
        const float lift=a.boost_ticks<=12?a.boost_ticks*512.0f:(36-a.boost_ticks)*256.0f;
        player.velocity.y=-tuning.glide_fall_speed+lift*unit;
        a.glide_ceiling=std::max(a.glide_ceiling,player.position.y);
        if(++a.boost_ticks>36)a.boost_ticks=0;
    } else player.velocity.y=std::clamp(player.velocity.y-tuning.glide_gravity*dt,-tuning.glide_fall_speed,0.0f);
    return true;
}
bool Game::move_skate(const InputState& input,Vec3 wish,float magnitude,bool supported,bool jumping) {
    if(level.source_game==GameBuild::YearOfTheDragonUS)return false; // Raw YOTD surface 4 is not yet mapped to S2 ice.
    // Winter Tundra overlay 56, state 9 (8006d950): on surface type 4 Spyro
    // always skates forward. The stick eases a steering rate toward 25 units
    // per VBlank by 5; the forward speed target is 0xf00, rising to 0x12c0
    // pushing on and falling to 0x780 holding back, approached by 0x140 / 0xc0
    // (8002bd70). Here the camera-relative stick sets the heading to steer to.
    auto& s=player.skate;
    if(powers.flying || player.swim.mode!=WaterMode::Dry || player.air.headbash_ticks || player.hurt_ticks){s={};return false;}
    const auto contact=supported?level.ground_support(player.position+retail::origin_above_feet,tuning.radius,0.12f):std::nullopt;
    if(contact && contact->surface_type==4){if(!s.active){s.active=true;s.turn=0;}s.off_ticks=0;}
    else if(s.active && (++s.off_ticks>=8 || player.airborne_ticks>=4))s={};
    if(!s.active || !supported)return false;
    float target_turn=0,forward=0;
    if(magnitude>0.01f) {
        const float error=std::remainder(std::atan2(wish.x,wish.z)-player.yaw,2*std::numbers::pi_v<float>);
        target_turn=std::clamp(error/0.6f,-1.0f,1.0f)*25*std::min(magnitude,1.0f);
        forward=std::cos(error)*std::min(magnitude,1.0f);
    }
    s.turn+=std::clamp(target_turn-s.turn,-5.0f,5.0f);
    player.yaw=std::remainder(player.yaw+s.turn*retail::angle_to_radians,2*std::numbers::pi_v<float>);
    const float target=(0xf00+(forward>0?forward*0x3c0:forward*0x780))*MovementTuning::velocity_unit*elements.movement(*this,MoveStat::Run);
    auto speed=std::hypot(player.velocity.x,player.velocity.z);
    speed+=std::clamp(target-speed,-tuning.deceleration*dt,tuning.acceleration*dt);
    player.velocity.x=std::sin(player.yaw)*speed;player.velocity.z=std::cos(player.yaw)*speed;
    ++player.skate_ticks;
    move_jump(input,jumping,false,false);
    return true;
}
bool Game::move_slide(const InputState& input,bool supported) {
    if(level.source_game==GameBuild::YearOfTheDragonUS)return false; // Raw YOTD surface 10 is not yet mapped to S2 sand.
    auto& s=player.slide;
    if(input.jump_pressed || powers.flying || player.swim.mode!=WaterMode::Dry || player.air.headbash_ticks || player.hurt_ticks) {s={};return false;}
    const auto contact=supported?level.ground_support(player.position+retail::origin_above_feet,tuning.radius,0.12f):std::nullopt;
    const bool sand=contact && contact->surface_type==10;
    if(sand) {
        if(!s.active){s.active=true;s.speed=0;s.direction=player.yaw;}
        s.off_ticks=0;
        if(contact->normal.y<std::cos(10*16*retail::angle_to_radians))s.direction=std::atan2(contact->normal.x,contact->normal.z);
    } else if(s.active && (++s.off_ticks>=16 || player.airborne_ticks>=4))s={};
    if(!s.active)return false;
    // Scorch overlay 44, 8006dcb8 state 31: f00 target, 80/c0 speed
    // response, 100 vector response. Heading follows the downhill normal.
    s.speed=approach(s.speed,0xf00*unit,(s.speed<0xf00*unit?0x80:0xc0)*unit);
    const auto velocity=approach_vector({player.velocity.x,0,player.velocity.z},
        Vec3{std::sin(s.direction),0,std::cos(s.direction)}*s.speed,0x100*unit);
    player.velocity.x=velocity.x;player.velocity.z=velocity.z;
    if(contact)player.velocity.y=-(velocity.x*contact->normal.x+velocity.z*contact->normal.z)/contact->normal.y;
    player.velocity.y-=0xc0*unit;
    player.yaw+=std::clamp(std::remainder(s.direction-player.yaw,2*std::numbers::pi_v<float>),-0.08f,0.08f);
    ++player.slide_ticks;return true;
}
void Game::finish_aerial(bool was_diving) {
    auto& a=player.air;
    if(player.swim.mode!=WaterMode::Dry || powers.flying){a={};return;}
    if(!player.grounded)return;
    if(was_diving) {
        ++player.headbash_impacts;camera.shake(0x1000,30);
        sound_events.push_back({SoundScope::Global,0x14,0,{}});
        if(object_models_)interactions.attack_sphere(retail_objects,*object_models_,player.position+Vec3{0,0.125f,0},0.35f,headbash_attack|charge_attack);
        headbash.impact(*this,object_models_);
        // The elemental/dust shockwave fires even where carving is refused.
        elements.headbash(*this,object_models_,player.position);
        a={};a.recovery_ticks=12;
    } else {const auto recovery=a.recovery_ticks;a={};a.recovery_ticks=recovery;}
}
} // namespace spyro
