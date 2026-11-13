#include "spyro/runtime.hpp"

namespace spyro {
int retail_jump_velocity(int velocity,unsigned ticks,bool charge,bool held,bool released) {
    // A normal jump gains 0x17 Q6 units for its first 13 held VBlanks.
    // Entering charge preserves velocity and restarts this timer, but holds
    // that velocity instead of adding lift. There is no second jump impulse.
    if(ticks<13 && held && !released)velocity+=charge?0:0x17;
    else velocity-=0xc0;
    return charge?std::max(velocity,-0x2300):velocity;
}
void Game::move_jump(const InputState& input,bool jumping,bool charging,bool supercharging) {
    auto& j=player.jump;
    if(jumping) {
        j={};j.kind=supercharging?JumpState::Kind::Supercharge:charging?JumpState::Kind::Charge:JumpState::Kind::Normal;
        player.velocity.y=(supercharging?tuning.supercharge_jump_speed:tuning.jump_speed)*elements.movement(*this,MoveStat::Jump);
        player.grounded=false;
    } else if(!player.grounded && charging && j.kind!=JumpState::Kind::Charge && j.kind!=JumpState::Kind::Supercharge) {
        // 800351d8 state 15 retains the incoming vertical component. This is
        // the jump-then-charge height gain; resetting velocity here breaks it.
        j={};j.kind=supercharging?JumpState::Kind::Supercharge:JumpState::Kind::Charge;
    }
    if(j.kind==JumpState::Kind::Supercharge)player.velocity.y-=tuning.supercharge_gravity/60;
    else if(j.kind!=JumpState::Kind::None) {
        j.released|=!input.jump_held;
        const int velocity=static_cast<int>(std::lround(player.velocity.y/MovementTuning::velocity_unit));
        player.velocity.y=retail_jump_velocity(velocity,j.ticks,j.kind==JumpState::Kind::Charge,
            input.jump_held,j.released)*MovementTuning::velocity_unit;
    } else player.velocity.y=std::max(player.velocity.y-tuning.gravity/60,-0x2300*MovementTuning::velocity_unit);
    ++j.ticks;
}
} // namespace spyro
