#include "spyro/gamepad.hpp"
#include <algorithm>
#include <cmath>

namespace spyro {
void GamePad::update(const PadSample& s,bool connected) {
    const bool was_connected=connected_;
    previous_=held_;connected_=connected;
    move_x_=move_z_=stick_x_=stick_y_=camera_x_=camera_y_=0;
    if(!connected){held_=blocked_=0;return;}
    std::uint32_t bits=s.buttons;
    if(s.left_trigger>pad::trigger_threshold)bits|=pad::l2;
    if(s.right_trigger>pad::trigger_threshold)bits|=pad::r2;
    // Radial dead zone. Inside it the stick is centred; past it the direction
    // is exact and the navigation value ramps from 0 at its edge to 1.
    const float x=std::max(-1.0f,s.left_x/32767.0f),y=std::max(-1.0f,s.left_y/32767.0f);
    const float n=std::hypot(x,y);
    if(n>pad::stick_dead_zone) {
        move_x_=x/n;move_z_=y/n;
        const float scale=std::min(1.0f,(n-pad::stick_dead_zone)/(1-pad::stick_dead_zone))/n;
        stick_x_=x*scale;stick_y_=y*scale;
    }
    const float rx=std::max(-1.0f,s.right_x/32767.0f),ry=std::max(-1.0f,s.right_y/32767.0f),rn=std::hypot(rx,ry);
    if(rn>pad::right_stick_dead_zone) {
        const float scale=std::min(1.0f,(rn-pad::right_stick_dead_zone)/(1-pad::right_stick_dead_zone))/rn;
        camera_x_=rx*scale;camera_y_=ry*scale;
    }
    // Digital stick directions with hysteresis, so a stick resting near the
    // threshold does not chatter menu moves or dialogue answers.
    const auto direction=[&](float value,std::uint32_t bit){if(value>((held_&bit)?.35f:.5f))bits|=bit;};
    direction(stick_y_,pad::stick_up);direction(-stick_y_,pad::stick_down);
    direction(-stick_x_,pad::stick_left);direction(stick_x_,pad::stick_right);
    held_=bits;blocked_&=held_;
    // A controller that appears (or a window that regains focus) with buttons
    // already down does not turn them into presses.
    if(!was_connected)blocked_=held_;
}
float GamePad::stick(std::uint32_t direction) const {
    switch(direction) {
        case pad::stick_up:return std::max(0.0f,stick_y_);
        case pad::stick_down:return std::max(0.0f,-stick_y_);
        case pad::stick_left:return std::max(0.0f,-stick_x_);
        case pad::stick_right:return std::max(0.0f,stick_x_);
        default:return 0;
    }
}
void GamePad::play(InputState& in) const {
    if(!connected_)return;
    if(move_x_ || move_z_){in.move_x=move_x_;in.move_z=move_z_;}
    // The original controls: Cross jumps (again in the air to glide), Square
    // charges, Circle flames (held for elemental breath), Triangle talks and,
    // in the air, hovers while gliding or headbashes.
    in.jump_held|=down(pad::a);in.jump_pressed|=pressed(pad::a);
    in.charge|=down(pad::x);
    in.flame_pressed|=pressed(pad::b);in.flame_held|=down(pad::b);
    in.special_pressed|=pressed(pad::y);in.dialogue.interact|=pressed(pad::y);
    // Held while standing, Triangle is look mode; the stick then looks around.
    in.look_held|=down(pad::y);
    // PC: the right stick turns and tilts the camera (in look mode, the view).
    if(camera_x_ || camera_y_){in.camera_x=camera_x_;in.camera_y=camera_y_;}
    // PC additions: L1 / R1 dodge roll, R2 element shot, L2 Fury (held into
    // its later stages), D-pad left / right element, up melee, down carry
    // (throw with Square held, as Shift+G), R3 centres the camera, Back
    // recalls the HUD.
    in.roll_left_pressed|=pressed(pad::l1);in.roll_right_pressed|=pressed(pad::r1);
    in.roll_left_held|=down(pad::l1);in.roll_right_held|=down(pad::r1);
    in.shot_pressed|=pressed(pad::r2);
    in.fury_pressed|=pressed(pad::l2);in.fury_held|=down(pad::l2);
    if(!in.element_cycle)in.element_cycle=pressed(pad::right)?1:pressed(pad::left)?-1:0;
    in.melee_pressed|=pressed(pad::up);
    if(pressed(pad::down)){if(down(pad::x))in.throw_pressed=true;else in.carry_pressed=true;}
    in.center_camera|=pressed(pad::r3);
    in.recall_hud|=down(pad::back);
    // Sparx's gem hint: hold L1 + R1 + R2. It holds back the roll and shot.
    if(down(pad::l1) && down(pad::r1) && down(pad::r2)) {
        in.gem_hint=true;
        in.roll_left_pressed=in.roll_right_pressed=in.roll_left_held=in.roll_right_held=in.shot_pressed=false;
    }
}
} // namespace spyro
