#pragma once
#include "spyro/level.hpp"

namespace spyro {
struct CampaignExit {unsigned destination{},source_scene{};};
struct CampaignArrival {Vec3 position;float yaw{};bool doorway=false;};
// Retail pause-menu Exit Level destination. Home worlds have no exit here.
std::optional<CampaignExit> campaign_exit(std::span<const std::uint8_t> executable,unsigned slot);
// Grounded native arrival based on the home world's matching portal record.
CampaignArrival campaign_arrival(std::span<const std::uint8_t> archive,unsigned source_scene,const Level& destination);
} // namespace spyro
