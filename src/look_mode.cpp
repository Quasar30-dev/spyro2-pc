#include "spyro/runtime.hpp"
#include <algorithm>
#include <cmath>

namespace spyro {
namespace {
int wrap(int angle){angle&=4095;return angle>2048?angle-4096:angle;}
// 8002b694: a held direction moves the steering value 0x10 a tick toward
// +-0x7f, released it falls back by up to 0x20; the analog stick approaches
// its own position by up to 0x10.
int steer(int value,float axis) {
    if(std::abs(axis)<.01f)return value-std::clamp(value,-0x20,0x20);
    const int target=static_cast<int>(std::lround(std::clamp(axis,-1.0f,1.0f)*0x7f));
    return std::clamp(value+std::clamp(target-value,-0x10,0x10),-0x7f,0x7f);
}
}
void Game::tick_look(InputState& input,bool blocked) {
    auto& l=player.look;
    // 80030c94: looking is allowed in the standing states, while no moby
    // claims Triangle (next to someone it talks instead). After a headbash
    // (state 2e) Triangle has to be let go first.
    if(player.air.headbash_ticks || player.air.recovery_ticks)l.release_needed=true;
    if(!input.look_held)l.release_needed=false;
    const bool standing=player.grounded && player.motion==Motion::Idle && player.swim.mode==WaterMode::Dry && !powers.flying;
    if(!input.look_held || !standing || blocked || l.release_needed || dialogue.nearby()) {
        const bool release=l.release_needed;l={};l.release_needed=release;
    } else {
        // 800216d8(7) clears the offsets on entry.
        l.active=true;
        // The D-pad / left stick steers (up raises the camera and looks down,
        // as in the original); the PC right stick too, with up looking up.
        const float x=std::abs(input.camera_x)>std::abs(input.move_x)?input.camera_x:input.move_x;
        const float y=std::abs(input.camera_y)>std::abs(input.move_z)?-input.camera_y:input.move_z;
        l.x=steer(l.x,x);l.y=steer(l.y,y);
        // 8001fa58 case 7: once the close camera has drawn in, half the
        // steering is added each tick, within +-0x400 and -0x3d0..0x500.
        if(camera.look_ready()) {
            l.yaw=std::clamp(l.yaw+(l.x>>1),-0x400,0x400);
            l.pitch=std::clamp(l.pitch+(l.y>>1),-0x3d0,0x500);
        }
        // Spyro stays where he is; jumping, charging or being hurt ends it.
        input.move_x=input.move_z=input.camera_x=input.camera_y=0;
    }
    camera.look={l.active,l.yaw,l.pitch};
    // The head follows the view: 8006a17c = -yaw within 0x300, 8006a178 =
    // -pitch within 0x200 (up on the D-pad bows the head).
    player.head.yaw_target=l.active?std::clamp(-l.yaw,-0x300,0x300):0;
    player.head.pitch_target=l.active?std::clamp(-l.pitch,-0x200,0x200):0;
}
void Game::tick_head() {
    // 800308d4: each angle springs toward its target (error x128, velocity
    // damped by 16/64, both in 1/64 steps).
    auto& h=player.head;h.previous_yaw=h.yaw;h.previous_pitch=h.pitch;
    const auto spring=[](int target,int& angle,int& velocity) {
        velocity+=(wrap(target-angle)*128-velocity*16)>>6;angle+=velocity>>6;
    };
    spring(h.pitch_target,h.pitch,h.pitch_velocity);spring(h.yaw_target,h.yaw,h.yaw_velocity);
}
} // namespace spyro
