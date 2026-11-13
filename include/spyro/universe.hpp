#pragma once
#include "spyro/level.hpp"

namespace spyro {
class Game;
struct UniverseAudit {
    std::vector<std::string> errors;
    unsigned route_samples{},grounded_objects{};
    explicit operator bool()const{return errors.empty();}
};
unsigned universe_source_slot(UniverseAddress address);
std::string universe_name(UniverseAddress address);
UniverseLayout plan_universe(UniverseAddress address);
ReturnPortalAssets decode_return_portal(std::span<const std::uint8_t> archive,std::span<const std::uint8_t> executable);
// Terrain comes from a theme bank; actors come from the verified Glimmer bank.
// No arbitrary retail scripts or buildings are transplanted into a new layout.
Level generate_universe(UniverseAddress address,const Level& terrain_source,
                        const Level& actor_source,const ObjectModels& actors,const ReturnPortalAssets& portal);
UniverseAudit audit_universe(const Level& level,const ObjectModels* actors=nullptr);
void tick_universe(Game& game,bool interact);
unsigned universe_objective_progress(const Game& game);
} // namespace spyro
