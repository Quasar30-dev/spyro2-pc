#pragma once
// Convexity demons: brand-new PS1-styled PC actors (embedded .s2actor models)
// that erupt from the level surface, attack enemies and help Spyro and Sparx.
// They are runtime objects outside the saved Moby ID space and never damage
// Spyro, Sparx, NPCs, gates, portals or scripted objects.
#include "spyro/level.hpp"

namespace spyro {
class Game;
enum class DemonKind : std::uint8_t {Imp=1,Hound=2,Wraith=3,Maw=4};
inline constexpr unsigned demon_kind_count=4;
// ObjectModels keys for PC actors. Retail classes never exceed 0x2ff.
inline constexpr std::uint16_t pc_actor_base=0x8000;
constexpr std::uint16_t demon_type(DemonKind kind){return static_cast<std::uint16_t>(pc_actor_base+static_cast<unsigned>(kind));}
const char* demon_name(DemonKind kind);
// Actor clip order shared by every demon model.
enum class DemonClip : unsigned {Erupt=0,Move=1,Attack=2,Sink=3,Idle=4};
enum class DemonPhase : std::uint8_t {Crack,Rise,Act,Sink};
struct Eruption {
    Vec3 center;float radius{};unsigned age{},lifetime=44;bool floating=false;
    std::vector<std::array<Vec3,2>> cracks;   // floor-hugging crack segments
};
struct Demon {
    std::uint32_t object=UINT32_MAX;DemonKind kind=DemonKind::Imp;DemonPhase phase=DemonPhase::Crack;
    unsigned phase_ticks{},age{},lifetime{},cooldown{},stuck{},hits{};
    std::uint32_t target=UINT32_MAX,gem=UINT32_MAX;
    Vec3 floor,last;float rise{},previous_rise{},hover{};
    bool absorbed=false,carrying=false;
};
// Eligible static floor for an eruption near `near` (headbash carving rules),
// or nothing when only water/special/portal/protected surfaces are present.
std::optional<Vec3> demon_spawn_point(const Game& game,Vec3 near,unsigned salt=0);
// A demon-defeated enemy may release a retail butterfly (class 10) that Sparx
// eats through his existing feeding path. Capped per scene visit.
void demon_kill_reward(Game& game,const ObjectModels* models,const RetailObject& victim);
class DemonSystem {
public:
    static constexpr unsigned max_active=6,max_eruptions_per_second=2,max_maws=1,leash=10;
    bool summon(Game& game,const ObjectModels* models,DemonKind kind,Vec3 near,unsigned count=1);
    void tick(Game& game,const ObjectModels* models);
    // Immediate sink: level change, portal travel, dialogue, cinematics, save/load.
    void clear(Game& game);
    // Hollow Wraith absorbs one Game::hurt call per summon.
    bool absorb_hit(Game& game);
    std::span<const Demon> demons()const{return demons_;}
    std::span<const Eruption> eruptions()const{return eruptions_;}
    unsigned active()const{return static_cast<unsigned>(demons_.size());}
    unsigned summoned{},refunded{},refreshed{},kills{},projectiles_blocked{},hits_absorbed{},containers_opened{},
        gems_fetched{},butterflies{},stuck_reerupts{},floating_rifts{};
private:
    std::vector<Demon> demons_;
    std::vector<Eruption> eruptions_;
    std::vector<unsigned> eruption_ticks_;
    retail::Random random_{};
    unsigned ticks_{};
    void act(Game& game,const ObjectModels& models,Demon& demon,RetailObject& body);
    void sink(Game& game,Demon& demon);
    std::uint32_t allocate(Game& game,const ObjectModels& models,DemonKind kind,Vec3 position);
    void erupt_at(Game& game,Vec3 position,bool floating);
};
} // namespace spyro
