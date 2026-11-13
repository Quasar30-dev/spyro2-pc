#pragma once
// Optional Legend-of-Spyro-inspired elemental powers (PC extension).
// Every new breath, projectile, shockwave and Fury below is an ORIGINAL
// procedural PS1-style effect with its own collision: none of them reuse the
// retail flame streams or retail projectile mobys. Spyro's existing retail
// flame/Superflame stay the Fire element's breath. All gameplay here is inert
// unless upgrade progression is enabled; the Dark Spyro form is a separate
// free cosmetic (Cheats) that also works with progression disabled.
#include "spyro/level.hpp"
#include "spyro/element_types.hpp"
#include "spyro/powerup_effects.hpp"
#include <deque>
#include <vector>

namespace spyro {
class Game;
struct InputState;
struct SpyroModel;
struct SpyroPose;
const char* element_name(Element element);
const char* element_breath_name(Element element);
const char* element_shot_name(Element element);
const char* element_fury_name(Element element);
Vec3 element_color(Element element);       // outer PS1 colour (0..1)
Vec3 element_core_color(Element element);  // bright core colour
bool metal_enemy(const RetailObject& object);
bool element_target(const RetailObject& object);
// Retail-compatible attack mask for an elemental hit on one object. Enemy AI,
// containers and power targets keep their original acceptance rules.
std::uint32_t element_retail_mask(Element element,unsigned strength,const RetailObject& target);
// Boss chip damage. Never the 15-HP green-bomb bit; bosses keep their cooldowns.
std::uint32_t element_boss_flags(Element element,unsigned strength);
// Crownworks' per-hit damage for one elemental hit (EXPANSION_LEVEL_PLANS 6.4).
unsigned boss_element_damage(const ElementHit& hit);
bool convexity_milestone(const Game& game);
bool element_purchasable(const Game& game,Element element);
// Unlock every elemental tier (workbench "Unlock all").
void unlock_all_elements(Game& game);
// Möller-Trumbore against a cached subset of the level (effects only).
std::optional<RayHit> local_raycast(std::span<const Triangle> faces,Vec3 origin,Vec3 direction,float limit);

enum class MoveStat : std::uint8_t {Run,Charge,Jump,Glide,Swim,Flight};
// Dark Spyro (cosmetic or Fury) movement boost; 1 otherwise.
struct DarkFormTuning {
    float run=1.30f,charge=1.20f,jump=1.12f,glide=1.15f,swim=1.20f,flight=1.15f;
    float exhausted=0.7f;
};

// --- Headbash shockwave --------------------------------------------------
// Ground ripples follow the sampled floor; spheres expand in 3D and also hit
// airborne targets. A wave hits each object at most once.
enum class ShockShape : std::uint8_t {Ripple,Sphere};
struct ShockRing {float radius{};std::array<float,32> height{};std::array<bool,32> valid{};};
struct Shockwave {
    Element element=Element::Fire;
    bool plain=true,dark=false;     // plain: dust ripple of the base upgrade
    bool fury=false;                // a Fury ring (bosses cap Fury damage)
    ShockShape shape=ShockShape::Ripple;
    Vec3 center;
    float radius{},previous_radius{},max_radius{};
    unsigned age{},expand=24,lifetime=34;
    std::vector<std::uint32_t> hit;
    std::deque<ShockRing> rings;
    std::vector<Triangle> local;    // nearby collision, for cheap floor sampling
};
float shock_radius(Element element,bool plain);
ShockShape shock_shape(Element element,bool plain);

// --- Original effect primitives --------------------------------------------
enum class EffectSprite : std::uint8_t {Droplet,Foam,Snowflake,Spark,Mote,Steam,Ember,Flake,Ring,Crystal};
struct ElementParticle {
    Vec3 previous,position,velocity,color;
    float radius{},spin{},growth{};unsigned age{},lifetime{};
    EffectSprite sprite=EffectSprite::Spark;
    unsigned blend=1;               // retail blend: 0 half,1 add,2 subtract,3 quarter
    float gravity{};
};
// Water jet / frost cone puffs: original breath bodies that travel and collide.
struct BreathNode {
    Vec3 previous,position,velocity;
    float radius{},previous_radius{},phase{};
    unsigned age{},lifetime{};bool stopped=false;
};
struct ElementBolt {std::vector<Vec3> points;Vec3 color,core;unsigned age{},lifetime{};float width{};};
struct VoidBeam {Vec3 from,to;unsigned age{};float width{};bool active=false;};
enum class ShotKind : std::uint8_t {Ember,Bubble,Shard,SparkOrb,RiftOrb};
struct ElementShot {
    ShotKind kind=ShotKind::Ember;Element element=Element::Fire;
    Vec3 previous,position,velocity;
    float spin{},previous_spin{},radius{};
    unsigned age{},lifetime{},blast{};
    bool exploding=false,fury=false;   // fury: a Fury's meteor
    std::vector<std::uint32_t> hit;
    std::deque<Vec3> trail;
};
struct IceSpike {Vec3 base,axis;float height{},width{},yaw{};unsigned age{},lifetime{},delay{};};
struct Geyser {Vec3 base;float height{};unsigned age{},lifetime{},delay{};};
struct GroundDecal {std::vector<std::array<Vec3,3>> faces;Vec3 color;unsigned age{},lifetime{},blend=1;};
struct MeleeTrail {std::deque<Vec3> points;Vec3 color;};
// A Fury: a gear-up of up to three 50-tick stages (holding the Fury input
// carries it into the next), then that many blast waves radiating through
// the air from Spyro, then the element's own sequence.
struct FuryState {Element element=Element::Fire;unsigned ticks{},stage{},blasts{};Vec3 center;};
// Swimmable elemental water: the Water Fury flood (whole level) and Water
// headbash pockets (local cylinders). Mirrored into Level::dynamic_water.
struct ElementWater {Vec3 center;float radius{},floor{},peak{},height{},previous_height{};unsigned age{},rise{},hold{},drain{},id{};bool flood=false,frozen=false;
    std::shared_ptr<const WaterCells> cells;};   // a flood held to part of the level (Crownworks' terrace)
// Frozen water. Ice (Frost breath, Ice Shards, the Ice headbash ripple) turns
// the whole body of water it touches into skateable ice; Glacier freezes every
// body in the level. A body is a connected set of retail water surface
// triangles, of custom-world water volumes sharing a surface, or an elemental
// flood / pocket. The water stays below; a solid sheet of retail surface type 4
// (Winter Tundra's and Colossus' ice, Spyro's state 9 skating) caps it until it
// thaws (or fire melts it).
struct FrozenWater {
    float height{};Vec3 center;Bounds bounds;
    unsigned age{},life{},melt{};                 // melt counts down once thawing starts
    std::vector<unsigned> collision;              // ice triangles in Level::triangles
    std::vector<unsigned> surfaces,volumes;       // the retail triangles / volumes it covers
    unsigned elemental=UINT32_MAX;                // ElementWater id
    std::vector<Vec3> sheet;                      // render triangles at the ice height
    std::vector<std::array<float,2>> sheet_uv;    // per vertex, inside one 1.6-unit tile
    bool contains(Vec3 p,float margin=0)const;
};

class ElementSystem {
public:
    static constexpr unsigned max_waves=2,max_shots=24,max_particles=1536,max_bolts=48,max_spikes=48,max_geysers=12,max_decals=24,max_chain=3;
    // Fast recharge: energy refills in about two seconds after a short pause and
    // the Fury meter also fills passively.
    static constexpr float breath_drain=25,shot_cost=20,regen=48,regen_delay=18,fury_per_second=5,fury_per_defeat=30,fury_per_gem=1;
    static constexpr unsigned dark_duration=25*60,exhaustion_duration=2*60,fury_pose_duration=150,fury_stage_ticks=50,fury_blast_gap=18,max_pockets=4;
    DarkFormTuning dark_tuning;
    bool shot_ready()const{return !shot_cooldown_;}
    // Gameplay entry points, in Game::tick order.
    void begin_tick(Game& game,InputState& input);
    void tick(Game& game,const ObjectModels* models,const SpyroModel* spyro);
    void resolve(Game& game,const ObjectModels* models);
    void headbash(Game& game,const ObjectModels* models,Vec3 feet);
    void reset(Game& game);
    void reset_interpolation();
    // True when F belongs to the equipped new element instead of retail fire.
    bool breath_override(const Game& game)const;
    bool fury_pose()const{return fury_pose_ticks_>0;}
    bool dark(const Game& game)const;
    bool dark_fury()const{return dark_ticks_>0;}
    unsigned dark_ticks()const{return dark_ticks_;}
    bool exhausted()const{return exhaustion_ticks_>0;}
    float movement(const Game& game,MoveStat stat)const;
    // Dark Fury converts a hit into knockback without health loss.
    bool shrug_off(Game& game,Vec3 source);
    bool breathing()const{return breath_active_;}
    // Shared emitters (also used by the demons).
    void spray(Vec3 at,Vec3 velocity,Element element,unsigned count,float spread,float radius,EffectSprite sprite,unsigned lifetime,unsigned blend=1,float gravity=0);
    void bolt(Vec3 from,Vec3 to,Element element,unsigned lifetime,float jitter,unsigned branches=1);
    void decal(Game& game,Vec3 center,float radius,Vec3 color,unsigned lifetime,unsigned blend);
    void hit_sphere(Game& game,Vec3 center,float radius,const ElementHit& hit,bool demons_only=false);
    Element breath_element()const{return breath_element_;}
    // Presentation (all interpolate with the 60 Hz alpha).
    std::span<const Shockwave> shockwaves()const{return waves_;}
    std::span<const ElementParticle> particles()const{return particles_;}
    std::span<const BreathNode> breath_nodes()const{return nodes_;}
    std::span<const ElementBolt> bolts()const{return bolts_;}
    const VoidBeam& beam()const{return beam_;}
    VoidBeam previous_beam()const{return previous_beam_;}
    std::span<const ElementShot> shots()const{return shots_;}
    std::span<const IceSpike> spikes()const{return spikes_;}
    std::span<const Geyser> geysers()const{return geysers_;}
    std::span<const GroundDecal> decals()const{return decals_;}
    const std::array<MeleeTrail,3>& melee_trails()const{return melee_;}
    const std::array<std::deque<Vec3>,2>& eye_trails()const{return eye_trails_;}
    const FuryState& fury()const{return fury_;}
    // Spyro's mouth this tick (the posed snout), updated with or without upgrades.
    Vec3 mouth()const{return mouth_;}
    std::span<const ElementWater> water()const{return water_;}
    std::span<const FrozenWater> ice()const{return ice_;}
    static constexpr unsigned ice_life=75*60,ice_melt=150;
    // Freeze the body of water at `point` (a point on or just under its
    // surface). Returns true when a new body froze.
    bool freeze_at(Game& game,Vec3 point);
    unsigned freeze_all(Game& game);
    // Fire on a frozen body starts its thaw.
    bool thaw_at(Game& game,Vec3 point);
    // Water headbash pocket / Water Fury flood (public for tests and tools).
    void pocket(Game& game,Vec3 feet);
    void flood(Game& game);
    unsigned phase()const{return phase_;}
    ChargeSmoke smoke;                  // black Dark Spyro smoke (subtractive, smaller)
    // Counters for reports and tests.
    unsigned breaths{},shots_fired{},waves{},wave_hits{},furies{},fury_blasts{},freezes{},shatters{},stuns{},chains{},soaks{},
        cooled_lava{},dark_entries{},element_kills{},melee_trail_samples{},status_hits{},floods{},pockets{},crates{},slices{},erosions{},
        frozen_bodies{},thawed_bodies{};
private:
    std::vector<Shockwave> waves_;
    std::vector<ElementParticle> particles_;
    std::vector<BreathNode> nodes_;
    std::vector<ElementBolt> bolts_;
    VoidBeam beam_,previous_beam_;
    std::vector<ElementShot> shots_;
    std::vector<IceSpike> spikes_;
    std::vector<Geyser> geysers_;
    std::vector<GroundDecal> decals_;
    std::vector<ElementWater> water_;
    std::vector<FrozenWater> ice_;
    std::vector<unsigned> ice_free_;            // disabled ice triangles to reuse
    unsigned water_ids_{};
    bool swim_flags_saved_=false,saved_surface_=false,saved_underwater_=false;
    std::array<MeleeTrail,3> melee_;
    std::array<std::deque<Vec3>,2> eye_trails_;
    std::vector<std::uint32_t> defeated_seen_;
    FuryState fury_;
    retail::Random random_{};
    Element breath_element_=Element::Fire;
    bool breath_active_=false,breath_queued_=false,shot_queued_=false,fury_queued_=false,glacier_release_=false,fury_hold_=false;
    unsigned breath_ticks_{},breath_idle_{},regen_delay_{},bolt_clock_{},phase_{},smoke_clock_{};
    unsigned fury_pose_ticks_{},dark_ticks_{},exhaustion_ticks_{};
    float fury_camera_turn_{};   // the current Fury shot's turn about Spyro, chosen on its cut
    unsigned shot_cooldown_{},wave_cooldown_{};
    unsigned slice_ticks_{},slice_cooldown_{};Vec3 slice_start_;   // Void Beam terrain contact
    unsigned last_gems_{};
    Vec3 mouth_,mouth_direction_;
    std::shared_ptr<const WaterCells> terrace_;   // Crownworks' flood region, built on its first Water Fury
    const Level* terrace_level_=nullptr;
    std::vector<Triangle> local_;   // nearby collision cache for breath bodies
    Vec3 local_center_;float local_radius_{};unsigned local_age_{};
    void gather_local(const Level& level,Vec3 center,float radius);
    // Implementation helpers (elements*.cpp).
    void update_mouth(Game& game,const SpyroModel* spyro);
    void breath_tick(Game& game,const ObjectModels* models);
    void water_breath(Game& game);
    void ice_tick(Game& game);
    unsigned ice_triangle(Game& game,Vec3 a,Vec3 b,Vec3 c);
    void finish_freeze(Game& game,FrozenWater body);
    void ice_breath(Game& game);
    void electric_breath(Game& game);
    void void_breath(Game& game,const ObjectModels* models);
    void fire_shot(Game& game);
    void shots_tick(Game& game,const ObjectModels* models);
    void explode(Game& game,ElementShot& shot);
    void waves_tick(Game& game,const ObjectModels* models);
    void fury_start(Game& game,const ObjectModels* models);
    void fury_tick(Game& game,const ObjectModels* models);
    void status_tick(Game& game);
    void effects_tick(Game& game);
    void dark_tick(Game& game,const SpyroModel* spyro);
    void melee_tick(Game& game,const SpyroModel* spyro);
    void water_tick(Game& game,const SpyroModel* spyro);
    std::optional<SpyroPose> posed(const Game& game,const SpyroModel* spyro)const;
};
} // namespace spyro
