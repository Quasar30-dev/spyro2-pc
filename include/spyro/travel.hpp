#pragma once
#include "spyro/runtime.hpp"
namespace spyro {
void decode_travel(std::span<const std::uint8_t> archive,std::span<const std::uint8_t> executable,Level& level);
std::optional<unsigned> campaign_slot(std::span<const std::uint8_t> executable,unsigned scene);
bool portal_contains(const CampaignPortal& portal,Vec3 point,float margin=0);
bool portal_crossed(const CampaignPortal& portal,Vec3 from,Vec3 to);
std::vector<PortalVertex> portal_picture(const CampaignPortal& portal,Vec3 eye);
std::vector<PortalVertex> portal_picture(const CampaignPortal& portal,Vec3 eye,Vec3 forward,Vec3 up,float vertical_fov,float aspect);
std::vector<RetailObject> portal_letters(const Level& level,Vec3 eye,float ticks);
struct WhirlwindParticle {
    Vec3 offset;
    float length{}; // Zero for a type-06 sprite; type-52 vertical endpoint offset.
    std::uint64_t birth{};
};
std::vector<WhirlwindParticle> whirlwind_particles(const Whirlwind& wind,float alpha,bool streaks=false);
void update_travel(Game& game);
bool advance_portal_travel(Game& game);
void begin_portal_arrival(Game& game,const TravelRequest& request);
void detect_portal_entry(Game& game);
// A script-driven departure (a boss's outro): Spyro glides forward and fades
// out, then the destination slot loads at its home starting point.
void start_scripted_travel(Game& game,unsigned destination);
bool move_whirlwind(Game& game);
}
