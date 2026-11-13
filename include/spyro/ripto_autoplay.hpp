#pragma once
#include "spyro/runtime.hpp"

namespace spyro {
// An unassisted Crownworks player (EXPANSION_LEVEL_PLANS 6.6): it plays the
// Cyborg Ripto fight through ordinary controller input only. There is no
// damage injection, no repositioning and no invulnerability; it can lose
// lives. From the arrival it takes the south ramp and the east lower ascent
// to the duel island, then circles its target, turning to face it to fire
// element shots between strafes. It jumps Ripto's expanding shockwaves,
// leaves the marked impact circles, collects Hunter's orbs and uses their
// powers, and breathes homing Superflame at the bird and the crown core.
class RiptoAutoplay {
public:
    InputState input(const Game& game);
    unsigned shots{},flames{},jumps{},orbs_sought{},route_steps{};
private:
    unsigned tick_{},aim_{},strafe_ticks_{},stuck_{},route_{};
    float strafe_=1;
    Vec3 last_;
    // The target's ground velocity (units/s), smoothed, to lead shots.
    Vec3 target_last_,target_velocity_;bool tracking_=false;
};
} // namespace spyro
