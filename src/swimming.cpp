#include "spyro/runtime.hpp"

namespace spyro {
namespace {
float approach(float from,float to,float amount) {return from+std::clamp(to-from,-amount,amount);}
}
void Game::update_water() {
    auto& swim=player.swim;
    const auto origin=player.position+retail::origin_above_feet;
    const auto water=level.water_at(origin);
    if(!level.surface_swimming || !water) {
        // Under an overhang or across a seam between water-surface triangles
        // there is briefly no surface above Spyro. Keep the underwater state
        // (and its animations) instead of dropping to land motion for a frame.
        if(level.surface_swimming && swim.mode==WaterMode::Underwater && swim.missing_ticks<30 && origin.y<swim.height-0.05f){++swim.missing_ticks;return;}
        if(swim.mode!=WaterMode::Dry){swim.mode=WaterMode::Dry;swim.entry_ticks=0;}
        swim.missing_ticks=0;
        return;
    }
    swim.missing_ticks=0;
    swim.height=water->height;
    const auto enter_surface=[&] {
        swim.mode=WaterMode::Surface;swim.surface_ticks=0;swim.entry_ticks=0;
        swim.speed=length(player.velocity);
        swim.bob=0x5900;swim.bob_velocity=-player.velocity.y/MovementTuning::velocity_unit/4;
        swim.input_x=swim.input_y=swim.roll_velocity=0;swim.breaching=false;
        player.velocity.y=0;player.grounded=false;player.pitch=player.roll=0;
        player.position.y=swim.height-swim.bob/(64*retail::units_per_world);
    };
    if(swim.mode==WaterMode::Dry) {
        // 8002a4c8: descending origin <= water + 0xb2 enters surface swim.
        if(player.velocity.y<=0 && origin.y<=swim.height+178/retail::units_per_world) {
            if(level.underwater_swimming && origin.y<swim.height-0.6f) {
                swim.mode=WaterMode::Underwater;swim.submerged=true;swim.entry_ticks=0;
                swim.speed=0;
                player.velocity={};player.grounded=false;swim.breaching=false;
            } else enter_surface();
        }
    } else if(swim.mode==WaterMode::Underwater) {
        if(origin.y<swim.height)swim.submerged=true;
        // Draining (elemental flood/pocket) can leave the surface below a
        // resting swimmer: surface instead of floating underwater in air.
        if(swim.submerged && !swim.entry_ticks && origin.y>swim.height+0.25f && player.velocity.y<=0){enter_surface();return;}
        if(swim.submerged && !swim.entry_ticks && origin.y>=swim.height && player.velocity.y>0) {
            // Charged swimming breaches into the resident charge-jump clip.
            if(player.motion==Motion::UnderwaterCharge) {
                swim.mode=WaterMode::Dry;swim.breaching=true;player.grounded=false;
            } else enter_surface();
        }
    }
}
void Game::move_in_water(InputState input,Vec3 wish) {
    auto& swim=player.swim;
    const auto unit=MovementTuning::velocity_unit;
    if(swim.mode==WaterMode::Surface) {
        ++swim.surface_ticks;
        if(input.jump_pressed && swim.surface_ticks>8) {
            swim.mode=WaterMode::Dry;player.velocity.y=tuning.jump_speed;
            player.grounded=false;return;
        }
        if(input.charge && !swim.charge_was_held && swim.surface_ticks>8 && level.underwater_swimming && ability_learned(4)) {
            // 8006fc20 checks one retail world unit of clearance before diving.
            const auto bottom=level.raycast({player.position.x,swim.height-0.001f,player.position.z},{0,-1,0},1);
            if(!bottom) {
                swim.mode=WaterMode::Underwater;swim.entry_ticks=1;swim.submerged=false;
                swim.coasting_charge=false;swim.input_x=swim.input_y=swim.roll_velocity=0;
                player.pitch=std::min(player.pitch,0.0f);
            }
        }
        if(swim.mode==WaterMode::Surface) {
            const auto magnitude=std::min(length(wish),1.0f);
            if(magnitude>0.01f)player.yaw=std::atan2(wish.x,wish.z);
            auto& speed=swim.speed;
            const auto target=tuning.surface_speed*magnitude*elements.movement(*this,MoveStat::Swim);
            speed=approach(speed,target,(speed<target?0xc0:(speed>0xc80*unit?0xc0:0x40))*unit);
            player.velocity={std::sin(player.yaw)*speed,0,std::cos(player.yaw)*speed};
            // Original surface-entry buoyancy spring, Q6 position in retail units.
            swim.bob_velocity-=(swim.bob*2+swim.bob_velocity*20)/256;
            swim.bob=std::clamp(swim.bob+swim.bob_velocity,-float(0x5900),float(0x5900));
            player.position.y=swim.height-swim.bob/(64*retail::units_per_world);
            player.pitch=approach(player.pitch,0,0.06f);player.roll=approach(player.roll,0,0.06f);
            return;
        }
    }
    // 8002b694 ramps the keyboard-equivalent digital stick by 16 / VBlank,
    // releases it by 32, and gently levels the pitch at neutral input.
    swim.input_x=approach(swim.input_x,input.move_x*127,input.move_x==0?32.0f:16.0f);
    swim.input_y=approach(swim.input_y,input.move_z*127,input.move_z==0?32.0f:16.0f);
    float pitch_units=player.pitch/retail::angle_to_radians;
    float pitch_input=swim.input_y==0?std::clamp(pitch_units/8,-127.0f,127.0f):swim.input_y;
    const bool entering=swim.entry_ticks!=0;
    if(entering) {
        const auto tick=std::min(swim.entry_ticks-1,20u);
        pitch_input=(512-pitch_units-static_cast<float>(tick)*16)*0.5f;
        // The retail dive stroke takes 21 VBlanks. Retain thrust until the
        // origin crosses the surface if entry occurs at a high buoyancy phase.
        if(++swim.entry_ticks>21 && swim.submerged)swim.entry_ticks=0;
    }
    const float turn=swim.input_x*20/128;
    player.yaw=std::remainder(player.yaw+turn*retail::angle_to_radians,2*std::numbers::pi_v<float>);
    pitch_units=std::clamp(pitch_units-pitch_input*15/128,-800.0f,800.0f);
    player.pitch=pitch_units*retail::angle_to_radians;
    auto roll_units=player.roll/retail::angle_to_radians;
    swim.roll_velocity+=turn*std::cos(player.pitch)*-32-swim.roll_velocity/4-roll_units;
    roll_units=std::clamp(roll_units+swim.roll_velocity/64,-768.0f,768.0f);
    player.roll=roll_units*retail::angle_to_radians;
    const bool charging=input.charge && !entering;
    if(player.motion==Motion::UnderwaterCharge && !charging)swim.coasting_charge=true;
    // The original desired swim speed (a0a8) is independent of the velocity
    // left after collision (a0ac). Keep paddling when swimming into a wall.
    auto& speed=swim.speed;
    const float target=(charging?tuning.swim_charge_speed:((input.jump_held || entering)?tuning.swim_speed:0))*elements.movement(*this,MoveStat::Swim);
    const float change=speed<target?(charging?576.0f:192.0f):(swim.coasting_charge?192.0f:48.0f);
    speed=approach(speed,target,change*unit);
    if(speed<=tuning.swim_speed)swim.coasting_charge=false;
    player.velocity=retail::rotate_model({0,0,speed},player.yaw,player.pitch,0);
    player.grounded=false;
}
} // namespace spyro
