#pragma once
#include "spyro/level.hpp"
#include "spyro/powerup_effects.hpp"
#include "spyro/audio.hpp"
#include <deque>
#include <map>

namespace spyro {
class Game;
struct InputState;
struct SpyroModel;
enum class PowerShot : unsigned { None,Superflame,GreenBomb,Underwater,Freeze };
// Retail attacks are masks, not scalar HP damage. PC strength orders their
// capabilities; the green-bomb extension adds heavy-door damage explicitly.
// The superfreeze shot (class 270, Cloud Temples 8007d714) attacks with
// 01000000 only: its moby queries freeze what they reach (troll 1EE 800751d0).
constexpr std::uint32_t superflame_attack=0x970000,green_bomb_attack=0x10570000,freeze_attack=0x01000000;
unsigned attack_strength(std::uint32_t flags);
bool power_target_accepts(const RetailObject& object,std::uint32_t flags);
struct PowerupAssets {
    ChargeSmokeAssets whirlwind_particle;
    ChargeSmokeAssets wing_trail,green_particle,fire_particle,bubble_particle;
    ChargeSmokeAssets sparkle_particle;   // effect material 0C: the spirit's arrival sparkles
    ChargeSmokeAssets gate_particle;      // effect material 04: an open gate's rising sparkles
};
PowerupAssets decode_powerup_assets(std::span<const std::uint8_t> archive);
enum class PowerParticleKind { Puff,Green,Fire,Bubble,ChargeFlame,Sparkle,GateGlow };
struct PowerParticle {
    Vec3 previous,position,velocity,color;float radius{};unsigned age{},lifetime{};
    PowerParticleKind kind=PowerParticleKind::Puff;float angle{};
    float ceiling{},spin{};unsigned grow{};   // GateGlow: rise limit, turn per tick, growing updates
};
// An open gate's lightning from a pillar tip to Spyro (8004b120: four
// segments, colour 0x608080; drawn by 8004b194). Offsets are retail units.
struct GateBolt {Vec3 from,to;std::array<retail::IVec3,3> bends{};};
// Particle 47: a short two-colour streak crackling on Spyro while he is zapped.
struct GateCrackle {Vec3 previous_head,head,previous_tail,tail;retail::IVec3 step;unsigned age{};};
// Original animated effect mobys live outside the saved level-object list.
enum class PowerVisualKind { GreenBurst,FireBurst,FireCloud,FireSpoke,FireTrail,WaterTrail,WaterBurst,WaterCloud,GateIcon,IceShell,GateDigit,Spirit };
struct PowerVisual {
    RetailObject object;
    PowerVisualKind kind{};
    unsigned age{},lifetime{},previous_material{};
    float previous_scale{},scale{};
};
struct PowerSpark {Vec3 previous,position,previous_tip,tip;unsigned age{},lifetime=64;};
struct PowerProjectile {
    unsigned object{},age{},blast_age{};PowerShot kind=PowerShot::None;
    Vec3 velocity;bool exploding=false;
    std::deque<Vec3> trail;
    unsigned effect_wait{};
    unsigned upgrade{};
    bool fragment=false;
};
struct PowerTimer {unsigned remaining{},duration{};};
// Class 68: a defeated enemy's spirit flying to the level's power-up gate
// (spawned by 80037608; overlay 16 case at 8007191c). The gate opens on the
// spirits that have arrived (80066efc), the HUD counts them at once (80066fb0).
struct SpiritFlight {
    unsigned gate{},value=1,state=1,counter{},frames{},hops{},phase{};
    int route=-1,waypoint=-1;
    unsigned angle{};int radius{};std::uint8_t retarget{};   // +16 spiral angle, +0E radius, +17
    retail::IVec3 origin,at,velocity;                        // spiral centre, position, route step
    RetailObject body;                                       // the drawn class-68 moby
    std::deque<Vec3> trail;                                  // 800502fc: six positions, newest first
};
struct PowerCheckpoint {
    PowerTimer flight,fire;
    bool charge=false,flying=false,takeoff=false;
    unsigned clip=33,lift_phase{};
    float speed=5952,roll_velocity{},lift{};
    bool valid()const;
    PowerTimer invincibility,ice;
};
class Powerups {
public:
    PowerTimer flight,fire;
    PowerTimer invincibility,ice;
    bool permanent_fire=false,charge=false,flying=false;
    unsigned shots_fired{},bombs_exploded{},flame_explosions{},water_explosions{},targets_destroyed{},gates_activated{},visuals_spawned{};
    unsigned split_flames{},homing_ticks{},upgrade_flames{},freeze_shots{},freeze_explosions{},objects_frozen{};
    void reset(bool retain_permanent=true);
    void begin_tick(Game& game);
    void gates_tick(Game& game);
    bool fly_enabled(const Game& game)const;
    bool flame_enabled(const Game& game)const;
    // Superfreeze (gate kind 7, 8006a1cc): Circle fires the level's freeze
    // shot (class 270) instead of the flame (8002fda4, 80030738).
    bool freeze_enabled(const Game& game)const;
    bool charge_enabled(const Game& game)const;
    void fly_move(Game& game,const InputState& input);
    unsigned flight_clip()const{return flight_clip_;}
    void end_flight();
    PowerCheckpoint checkpoint()const;
    void restore_checkpoint(const PowerCheckpoint& checkpoint);
    bool fire_tick(Game& game,const InputState& input,bool allowed,bool suppressed);
    unsigned secondary_action()const{return winding_!=PowerShot::None?6:0;}
    void tick(Game& game,const ObjectModels* models,const SpyroModel* spyro_model);
    void reset_interpolation();
    const std::vector<PowerProjectile>& projectiles()const{return projectiles_;}
    const std::vector<PowerParticle>& particles()const{return particles_;}
    // One type-0C sparkle (reward sparkles, class 81 in 80016328 / 8003ca00).
    void emit_sparkle(Vec3 position,Vec3 velocity,Vec3 color,unsigned lifetime);
    const std::vector<PowerVisual>& visuals()const{return visuals_;}
    const std::vector<PowerSpark>& sparks()const{return sparks_;}
    const std::array<std::deque<Vec3>,2>& wings()const{return wings_;}
    const std::vector<SpiritFlight>& spirit_flights()const{return spirits_;}
    const std::vector<GateBolt>& gate_bolts()const{return bolts_;}
    // Bolts the level scripts hold in 8004b120's table (Zoe's checkpoint zap
    // from her wand to Spyro); RetailScripts refreshes them every script tick.
    void set_script_bolts(std::vector<GateBolt> bolts){script_bolts_=std::move(bolts);}
    const std::vector<GateBolt>& script_bolts()const{return script_bolts_;}
    const std::vector<GateCrackle>& gate_crackles()const{return crackles_;}
    // Sounds an open gate keeps playing (80050b74 / 80050648 flag 4).
    const std::vector<HeldSound>& held_sounds()const{return holds_;}
    unsigned gate_zaps{};
    unsigned spirits_in_flight()const;
    unsigned spirits_launched{},spirits_arrived{};
    Vec3 tint(const Game& game)const;
    float meter(const Game& game)const;
    const char* meter_name(const Game& game)const;
private:
    std::vector<PowerProjectile> projectiles_;
    std::vector<PowerParticle> particles_;
    std::vector<PowerVisual> visuals_;
    std::vector<PowerSpark> sparks_;
    std::array<std::deque<Vec3>,2> wings_;
    std::vector<SpiritFlight> spirits_;
    std::vector<std::uint8_t> spirit_seen_;
    std::vector<GateBolt> bolts_,script_bolts_;
    std::vector<GateCrackle> crackles_;
    std::vector<HeldSound> holds_;
    std::map<unsigned,ObjectAnimationState> icon_animations_;
    PowerShot winding_=PowerShot::None;
    unsigned windup_{},cooldown_=60,phase_{},flight_clip_=33,lift_phase_{};
    float flight_speed_=5952,roll_velocity_{},lift_{};
    bool takeoff_=false;
    retail::Random random_;
    void shoot(Game& game,PowerShot kind);
    void spawn_shot(Game& game,PowerShot kind,Vec3 position,Vec3 direction,bool fragment=false);
    void burst(Vec3 position,bool green,unsigned count,float radius);
    void emit_visual(Game& game,const ObjectModels& models,PowerVisualKind kind,Vec3 position,std::array<float,3> rotation={});
    void explosion_tick(Game& game,const ObjectModels& models,PowerProjectile& projectile);
    // A native object the freeze reaches (the engine's frozen pose and shell).
    void freeze(Game& game,const ObjectModels* models,RetailObject& object);
    void freeze_contacts(Game& game,const ObjectModels& models,Vec3 center,float radius);
    void emit_particles(Vec3 position,bool green,unsigned count,unsigned sparks);
    void spirits_tick(Game& game,const ObjectModels& models);
    void launch_spirit(Game& game,const ObjectModels& models,const RetailObject& source);
    bool advance_spirit(Game& game,const ObjectModels& models,SpiritFlight& spirit);
    void open_gate_tick(Game& game,const ObjectModels& models,RetailObject& gate,bool update);
};
} // namespace spyro
