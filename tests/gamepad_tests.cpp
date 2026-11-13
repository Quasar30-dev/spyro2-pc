#include "spyro/gamepad.hpp"
#include <cmath>
#include <iostream>
using namespace spyro;
namespace {
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value)throw FormatError(why);}
PadSample buttons(std::uint16_t b){PadSample s;s.buttons=b;return s;}
PadSample stick(float x,float y){PadSample s;s.left_x=static_cast<std::int16_t>(x*32767);s.left_y=static_cast<std::int16_t>(y*32767);return s;}
InputState play(const GamePad& p){InputState in;p.play(in);return in;}
}
int main() {
    try {
        GamePad p;
        // A controller that appears with Cross down does not jump; releasing and
        // pressing it again does, once, and holding it keeps only the hold.
        p.update(buttons(pad::a),true);check(!play(p).jump_pressed && !play(p).jump_held,"A button held at connection acted as a press");
        p.update({},true);p.update(buttons(pad::a),true);
        auto in=play(p);check(in.jump_pressed && in.jump_held,"Cross did not jump");
        p.update(buttons(pad::a),true);in=play(p);check(!in.jump_pressed && in.jump_held,"A held Cross repeated its press");
        // The original layout.
        p.update({},true);p.update(buttons(pad::x),true);in=play(p);
        check(in.charge && !in.flame_pressed && !in.jump_pressed,"Square does not charge (or does something else)");
        p.update(buttons(pad::b),true);in=play(p);
        check(in.flame_pressed && in.flame_held && !in.charge && !in.dialogue.cancel,"Circle does not flame (or cancels)");
        p.update(buttons(pad::y),true);in=play(p);
        check(in.special_pressed && in.dialogue.interact && in.look_held && !in.flame_pressed,"Triangle does not talk / hover / headbash / look");
        p.update(buttons(pad::y),true);in=play(p);check(in.look_held && !in.special_pressed && !in.dialogue.interact,"Holding Triangle repeated its press");
        p.update({},true);
        p.update(buttons(pad::l1),true);in=play(p);check(in.roll_left_pressed && in.roll_left_held && !in.roll_right_held,"L1 does not roll left");
        p.update(buttons(pad::up),true);in=play(p);check(in.melee_pressed && !in.roll_left_held,"D-pad up does not melee");
        p.update(buttons(pad::down),true);in=play(p);check(in.carry_pressed && !in.throw_pressed,"D-pad down does not carry");
        p.update(buttons(pad::x),true);p.update(buttons(pad::x|pad::down),true);in=play(p);check(in.throw_pressed && !in.carry_pressed,"Square + D-pad down does not throw");
        p.update(buttons(pad::right),true);in=play(p);check(in.element_cycle==1,"D-pad right does not cycle the element");
        p.update(buttons(pad::r3|pad::back),true);in=play(p);check(in.center_camera && in.recall_hud,"R3 / Back do not centre the camera / recall the HUD");
        p.update(buttons(pad::start),true);in=play(p);check(!in.jump_pressed && !in.flame_pressed && !in.dialogue.cancel && !in.center_camera,"Start acts in play");
        // Triggers use XInput's threshold.
        PadSample t;t.right_trigger=pad::trigger_threshold;p.update({},true);p.update(t,true);check(!play(p).shot_pressed,"A resting trigger fired a shot");
        t.right_trigger=pad::trigger_threshold+1;p.update(t,true);check(play(p).shot_pressed,"R2 does not fire a shot");
        t={};t.left_trigger=255;p.update({},true);p.update(t,true);in=play(p);check(in.fury_pressed && in.fury_held,"L2 does not start the Fury");
        // L1 + R1 + R2: Sparx's gem hint, without rolling or shooting.
        t={};t.buttons=pad::l1|pad::r1;t.right_trigger=200;p.update({},true);p.update(t,true);in=play(p);
        check(in.gem_hint && !in.roll_left_held && !in.roll_right_held && !in.shot_pressed,"Gem hint rolled or fired");
        // The stick: radial dead zone, then an exact unit direction.
        p.update(stick(.2f,.1f),true);in=play(p);check(in.move_x==0 && in.move_z==0,"Stick moved inside its dead zone");
        p.update(stick(0,.5f),true);in=play(p);check(in.move_x==0 && std::abs(in.move_z-1)<1e-5f,"Stick forward is not full-speed forward");
        p.update(stick(-.4f,-.4f),true);in=play(p);
        check(std::abs(in.move_x+std::sqrt(.5f))<1e-4f && std::abs(in.move_z+std::sqrt(.5f))<1e-4f,"Stick diagonal is not a unit diagonal");
        p.update(stick(-1,0),true);check(std::abs(p.stick(pad::stick_left)-1)<1e-4f && p.stick(pad::stick_right)==0,"Navigation value is not full at the rim");
        p.update({},true);in={};in.move_x=1;p.play(in);check(in.move_x==1,"A centred stick overrode the keyboard");
        // The right stick (PC camera): its own dead zone, analog magnitude.
        PadSample r;r.right_x=8000;p.update(r,true);check(play(p).camera_x==0,"Right stick moved the camera inside its dead zone");
        r.right_x=32767;r.right_y=0;p.update(r,true);in=play(p);check(std::abs(in.camera_x-1)<1e-4f && in.camera_y==0 && in.move_x==0,"Right stick rim is not a full camera turn");
        r.right_x=0;r.right_y=20000;p.update(r,true);in=play(p);check(in.camera_y>0 && in.camera_y<1,"Right stick tilt is not analog");
        // Digital stick directions (menus, dialogue answers) with hysteresis.
        p.update(stick(0,-.4f),true);check(!p.down(pad::stick_down),"Stick flick fired under the threshold");
        p.update(stick(0,-.75f),true);check(p.pressed(pad::stick_down),"Stick flick down missing");
        p.update(stick(0,-.6f),true);check(p.down(pad::stick_down) && !p.pressed(pad::stick_down),"Stick near the threshold chattered");
        p.update(stick(0,-.3f),true);check(!p.down(pad::stick_down),"Stick release missing");
        // Holding back: the press that changes context does not carry over.
        p.update(buttons(pad::a),true);p.hold_back();check(!p.down(pad::a) && !p.pressed(pad::a),"Held-back button still counts");
        p.update(buttons(pad::a),true);check(!p.down(pad::a),"Held-back button returned while still held");
        p.update({},true);p.update(buttons(pad::a),true);check(p.pressed(pad::a),"Released button did not press again");
        // Losing the controller (or window focus) releases everything.
        p.update(buttons(pad::x|pad::a),false);in=play(p);check(!p.connected() && !in.charge && !in.jump_held,"Disconnected controller kept its buttons");
        p.update(buttons(pad::x),true);check(!play(p).charge,"Button held while unfocused acted on return");
        std::cout<<checks<<" controller checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
