#pragma once
#include "spyro/universe_types.hpp"
#include <optional>
#include <string>
namespace spyro {
struct CampaignPortal {
    unsigned index{},destination{},scene_id{},object=UINT32_MAX;
    unsigned visibility{},required_orbs{},purchase=UINT32_MAX,gate_object=UINT32_MAX,gate_npc=UINT32_MAX;
    bool require_ripto=false;
    bool two_sided=false,return_home=false,enabled=false;
    // World doors (custom door mode 2): the custom world they lead to
    // (relative to data/custom-levels), the expansion emblems that open them,
    // and presentation flags (1 night, 2 the boss encounter).
    std::string world;
    unsigned world_emblems{},world_flags{};
    float opening=1,rise=1;
    std::array<Vec3,5> aperture{};
    // Original 512 x 216 screen-space panorama, normalized to the safe area.
    std::vector<PortalVertex> picture;
    // Class-40A label controller, kept separate from collidable/persistent
    // Mobys. Its class-229 letters are presentation-only retail meshes.
    std::string label;
    Vec3 label_origin;
    float label_heading{};
    bool label_two_sided=false;
};
struct HomeTransport {unsigned object{},destination{},arrival{},label_object=UINT32_MAX;bool active=false,armed=false;};
struct Whirlwind {
    unsigned object{},kind{},phase{},timer{},ticks{};
    float height{},radius{};
    bool active=false;
    unsigned required_orbs{};
};
// A world request loads a custom world (world_return: back through the door
// the current world was entered by) instead of a retail destination.
struct TravelRequest {unsigned destination{},source_scene{},arrival=UINT32_MAX;bool transporter=false;std::string world;unsigned world_flags{};bool world_return=false;};
// Where a world's Return Home leads: the world, its flags, and the place in
// front of the door Spyro left it by.
struct WorldReturn {std::string world;unsigned flags{};Vec3 position;float yaw{};};
struct PortalTravel {
    enum class Phase { Idle,Departing,Loading,Arriving } phase=Phase::Idle;
    TravelRequest request;
    Vec3 start,target,direction,camera_start;
    float yaw{},fade{};
    unsigned ticks{},cooldown=90;
    bool allowed=true,unlock=false;
    bool active() const {return phase!=Phase::Idle;}
};
struct WhirlwindRide {
    unsigned index=UINT32_MAX,cooldown{},ticks{};
    float start_height{},start_yaw{},turn_delta{},rise_span{};
    bool turning=false;
};
}
