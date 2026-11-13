#pragma once
#include "spyro/level.hpp"

namespace spyro {
struct EnemyHit {Vec3 source;unsigned object{};};
void initialize_enemy_objects(std::span<RetailObject> objects);
void initialize_yotd_enemy_objects(Level& level,const ObjectModels& models);
bool enemy_accepts_attack(const RetailObject& object,std::uint32_t flags);
const char* enemy_phase_name(const RetailObject& object);
class EnemySystem {
public:
    std::optional<EnemyHit> tick(std::vector<RetailObject>& objects,const ObjectModels* models,
        const Level& level,Vec3 player_origin,bool player_protected);
private:
    std::optional<EnemyHit> yotd_tick(std::vector<RetailObject>& objects,const ObjectModels& models,
        const Level& level,Vec3 player_origin,bool player_protected);
    unsigned ticks_{};
    retail::Random random_;
};
} // namespace spyro
