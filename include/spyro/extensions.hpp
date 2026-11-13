#pragma once
#include "spyro/level.hpp"
#include "spyro/headbash.hpp"
#include "spyro/element_types.hpp"
#include <set>

namespace spyro {
class Game;
struct InputState;
struct SpyroPose;
struct SpyroModel;
enum class Upgrade { Supercharge, Superflame, Superfly, Carry, Melee, Glide, Headbash, Roll };
enum class UpgradeHudMode : unsigned { Once,OnUse,Manual,Always,Off };
struct UpgradeHudSettings {
    UpgradeHudMode mode=UpgradeHudMode::Once;
    unsigned seconds=3;
    bool valid()const{return static_cast<unsigned>(mode)<=static_cast<unsigned>(UpgradeHudMode::Off) && seconds>=1 && seconds<=8;}
    bool operator==(const UpgradeHudSettings&)const=default;
};
// Saved elemental progression. Fire tier 1 is Spyro's retail flame (always
// owned). Tiers: 1 breath, 2 shot, 3 headbash element + Fury.
struct ElementProgress {
    std::array<unsigned,element_count> tiers{1,0,0,0,0};
    Element equipped=Element::Fire;
    float energy=100,fury=0;
    unsigned tier(Element e)const{return tiers[static_cast<unsigned>(e)];}
    bool owns(Element e)const{return tier(e)>0;}
    bool valid()const;
    bool operator==(const ElementProgress&)const=default;
};
// Cost of the next tier (0 when complete). Convexity also needs the other
// three new elements and a campaign milestone; see element_purchasable().
unsigned element_tier_cost(Element element,unsigned current_tier);
struct UpgradeProgress {
    bool enabled=false;
    unsigned points{},charge{},flame{},flight{};
    bool carry=false,melee=false;
    unsigned glide{};
    unsigned headbash{};
    HeadbashMode headbash_mode=HeadbashMode::Crater;
    bool roll=false;
    ElementProgress elements;
    unsigned cost(Upgrade upgrade)const;
    bool buy(Upgrade upgrade);
    unsigned element_cost(Element element)const{return element_tier_cost(element,elements.tier(element));}
    // `milestone` is the campaign condition for Convexity (element_purchasable).
    bool buy_element(Element element,bool milestone);
    bool valid()const;
    float charge_multiplier()const{return enabled?1+0.3f*charge:1;}
    float flight_multiplier()const{return enabled?1+0.25f*flight:1;}
};
struct CarryGrip {Vec3 point,normal{0,1,0};};
inline constexpr float carry_grip_clearance=4.0f/1024;
struct CarriedBody {unsigned object{};Vec3 home,velocity;std::array<float,3> rotation{};float radius{},mass=1;unsigned age{};Vec3 local_center;float top{};std::array<CarryGrip,4> grips;Bounds bounds;};
struct CarryCheckpoint {
    unsigned object{},type{},age{},grip_ticks{};bool held=false;
    Vec3 position,home,velocity;
    std::array<float,3> rotation{},home_rotation{};
};
void pose_carry_grip(SpyroPose& pose,const std::array<CarryGrip,4>& targets,float blend);
void pose_melee(SpyroPose& pose,unsigned combo,float phase,const SpyroModel* model=nullptr);
class Abilities {
public:
    unsigned carried=UINT32_MAX,combo{},combo_tick{},melee_hits{},melee_swings{},throws{},drops{},smashes{},pickups{};
    unsigned pickup_target=UINT32_MAX,pickup_assist_ticks{},assisted_pickups{};
    void begin_tick(Game& game,InputState& input,const ObjectModels* models);
    void tick(Game& game,const ObjectModels* models);
    void assist_flight(Game& game,const InputState& input);
    void reset(Game& game);
    void pose(SpyroPose& pose,float alpha,const Game* game=nullptr,const SpyroModel* model=nullptr)const;
    std::array<CarryGrip,4> grip_contacts(const Game& game,float alpha=1)const;
    bool attacking()const{return combo_tick!=0;}
    float carry_speed()const{return carried==UINT32_MAX?1:1/(1+body_.mass*0.12f);}
    std::vector<CarryCheckpoint> checkpoint(const Game& game)const;
    static Abilities from_checkpoint(std::span<const CarryCheckpoint> records,std::vector<RetailObject>& objects,const ObjectModels* models);
private:
    CarriedBody body_;
    std::vector<CarriedBody> thrown_;
    std::set<unsigned> hit_;
    bool queued_=false;
    unsigned carry_ticks_{};
    unsigned nearby_heavy_=UINT32_MAX;
    float nearby_distance_=1000;
    CarriedBody pickup_body_;
    void release(Game& game,bool throw_it,bool drop=false);
};
} // namespace spyro
