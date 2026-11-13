#pragma once
#include "spyro/runtime.hpp"
#include <cstdint>

namespace spyro {
// Controller play. One XInput-layout sample per presented frame becomes game
// input in the original PlayStation layout: Cross (A) jumps and glides, Square
// (X) charges, Circle (B) flames, Triangle (Y) talks, hovers and headbashes.
// A press is the edge between two polls, so it reaches the simulation once:
// Game::advance latches it until the next 60 Hz tick consumes it.
struct PadSample {
    std::uint16_t buttons{};                       // XINPUT_GAMEPAD wButtons
    std::uint8_t left_trigger{},right_trigger{};   // 0..255
    std::int16_t left_x{},left_y{},right_x{},right_y{};
};
namespace pad {
// XINPUT_GAMEPAD_* bits, then the triggers and the left stick's four
// directions folded in as digital buttons (for menus and dialogue answers).
inline constexpr std::uint32_t up=0x1,down=0x2,left=0x4,right=0x8,start=0x10,back=0x20,l3=0x40,r3=0x80,
    l1=0x100,r1=0x200,a=0x1000,b=0x2000,x=0x4000,y=0x8000,
    l2=0x10000,r2=0x20000,stick_up=0x40000,stick_down=0x80000,stick_left=0x100000,stick_right=0x200000;
// XInput's recommended stick dead zones and trigger threshold.
inline constexpr float stick_dead_zone=7849.0f/32767,right_stick_dead_zone=8689.0f/32767;
inline constexpr unsigned trigger_threshold=30;
}
class GamePad {
public:
    // One poll. A missing controller (or an unfocused window) releases everything.
    void update(const PadSample& sample,bool connected);
    // Buttons held now stop counting until they are released, so the press
    // that closes a menu does not also act in the game, and the reverse.
    void hold_back(){blocked_|=held_;}
    bool connected() const {return connected_;}
    bool down(std::uint32_t bits) const {return (held_&~blocked_&bits)!=0;}
    bool pressed(std::uint32_t bits) const {return (held_&~previous_&~blocked_&bits)!=0;}
    // The left stick past its radial dead zone as a unit direction (x right,
    // y up), zero inside it: Spyro runs at full speed, as with the keys.
    float move_x() const {return move_x_;}
    float move_z() const {return move_z_;}
    // The left stick as ImGui's analog navigation values, 0..1 per direction.
    float stick(std::uint32_t direction) const;
    // The right stick past its dead zone, rescaled so its rim is 1 (x right,
    // y up): the PC camera orbit, and look-mode steering.
    float camera_x() const {return camera_x_;}
    float camera_y() const {return camera_y_;}
    // Gameplay controls for this poll, added to the keyboard's.
    void play(InputState& input) const;
private:
    std::uint32_t held_{},previous_{},blocked_{};
    float move_x_{},move_z_{},stick_x_{},stick_y_{},camera_x_{},camera_y_{};
    bool connected_=false;
};
} // namespace spyro
