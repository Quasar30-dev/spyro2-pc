#pragma once
#include "spyro/objects.hpp"
#include "spyro/audio.hpp"
#include <deque>

namespace spyro {
class Game;
struct Level;
enum class RiptoStage { Foot,MechaGulp,Bird,Defeated };
enum class RiptoAction { Waiting,Seek,Cast,Shockwaves,FireSpiral,ShieldRush,Evade,Hit,Transform,
    GulpStomp,GulpLasers,GulpFire,GulpFireTrain,BirdFlight,BirdVolley,Death,PowerUp,ReturnCenter,CyborgLance,CyborgNova,GulpBombard,BirdDive,GulpPincer,Finale };
enum class RiptoShotKind { Scepter,Fireball,Shockwave,ShieldOrb,GulpFlame,FireTrain,Homing,Mortar };
enum class CrushAction { Dormant,Approach,Slam,Blast,Recover,Hit,Defeated,Combo };
struct RiptoShot {
    RiptoShotKind kind{};RetailObject visual;Vec3 velocity;
    unsigned age{},life{};float radius{},previous_radius{};bool spent=false,doused=false;
    std::deque<Vec3> trail;
    Vec3 impact;
    Vec3 color{1,.18f,.03f};
};
struct RiptoOrb {RetailObject visual;unsigned color{},age{};Vec3 velocity;bool resting=false;};
// Crownworks tells use one colour per source (EXPANSION_LEVEL_PLANS 6.5.3).
namespace ripto_tell {
inline constexpr Vec3 ripto{1,.1f,.06f},mech{1,.6f,.08f},arena{1,1,1};
}
struct RiptoBeam {Vec3 from,to;float radius{};bool warning=false;Vec3 color=ripto_tell::ripto;};
struct RiptoFlash {Vec3 position;unsigned age{};float radius{};Vec3 color;};
struct RiptoMark {Vec3 position;float radius{},progress{};Vec3 color=ripto_tell::mech;};
// Crownworks difficulty (6.5.6): assist stretches every tell by half and
// refills Spyro's health at each phase; hard keeps the overdrive relay on.
enum class BossDifficulty : std::uint8_t { Normal,Assist,Hard };
// Crownworks cutscenes (6.5.4): Ripto wakes the arena, summons the mechs,
// takes flight, and the arena falls silent. Each runs on the scripted camera
// with the arena held, and can be skipped (jump) once it has been seen.
enum class CrownCinematic : std::uint8_t { None,Intro,Summon,Flight,Outro };
// Crownworks phase lighting (6.5.2): multipliers over the arena's own colours
// for the lower ring, the middle terrace, the crown walk and the sky, plus a
// light at the crown core. Neutral (all 1, no core) outside the Crownworks.
// The arena's night colours are mostly blue, so warm and violet light also
// needs a cast: added in proportion to each texel's brightness on terrain,
// and as a glow on the near-black sky.
struct CrownLight {
    Vec3 lower{1,1,1},terrace{1,1,1},walk{1,1,1},sky{1,1,1},core;
    Vec3 lower_cast,terrace_cast,walk_cast,sky_cast;
    float core_radius=14;
    bool operator==(const CrownLight&)const=default;
};
CrownLight mix(const CrownLight& a,const CrownLight& b,float t);
// Authored gameplay pointers from overlay 70, decoded from the mounted copy.
struct RiptoArena {
    Vec3 center,gulp_spawn,bird_spawn,flight_spawn;
    std::vector<Vec3> bird_route,hunter_route;
    unsigned bird_initial{},hunter_initial{};
};
void decode_ripto_arena(std::span<const std::uint8_t> archive,Level& level);
// Shared controller; Crownworks adds relay modifiers and cyborg-only attacks.
class RiptoBoss {
public:
    bool enabled=false,engaged=false,retail_arena=false;
    Vec3 center;
    RiptoStage stage=RiptoStage::Foot;
    RiptoAction action=RiptoAction::Waiting;
    unsigned health=60,max_health=60,ticks{},action_ticks{},damage_cooldown{},hits{},damage_to_spyro{},attacks{},transitions{},resets{},phase_restarts{};
    unsigned doused{};   // boss fire shots put out by elemental water (the terrace flood)
    unsigned spyro_orbs{},ripto_orbs{},player_power{},player_power_ticks{};
    unsigned hud_ticks{},hud_spyro_blink=36,hud_ripto_blink{};
    std::array<unsigned,11> attack_counts{};
    Vec3 effect_origin,previous_effect_origin;
    float effect_charge{},previous_effect_charge{};
    unsigned crush=UINT32_MAX,crush_health{},crush_ticks{},crush_attacks{},crush_hits{},crush_damage{};
    CrushAction crush_action=CrushAction::Dormant;
    unsigned actor=UINT32_MAX,gulp=UINT32_MAX,bird=UINT32_MAX,hunter=UINT32_MAX;
    BossDifficulty difficulty=BossDifficulty::Normal;
    // Crownworks finale: the crown core stays exposed this long (20 s) and any
    // one Fury ends the fight; otherwise Ripto takes off again.
    static constexpr unsigned finale_window=1200;
    unsigned finale_ticks{},finales_missed{};
    // Ice freezes a mech for at most 1.5 s, once per 10 s.
    static constexpr unsigned mech_freeze=90,mech_freeze_cooldown=600;
    unsigned gulp_frozen{},crush_frozen{},mech_freezes{},chain_hits{};
    // Cutscenes: the one playing, its clock, and which have been seen this
    // session (bit per CrownCinematic). Game sets `skip_requested` from the
    // raw input while a cutscene holds the controls.
    CrownCinematic cinematic=CrownCinematic::None;
    unsigned cinematic_ticks{},cinematics_seen{},cinematics_played{},cinematics_skipped{};
    bool skip_requested=false;
    static unsigned cinematic_length(CrownCinematic cinematic);
    bool cinematic_skippable()const{return cinematic!=CrownCinematic::None && (cinematics_seen>>static_cast<unsigned>(cinematic)&1);}
    const char* cinematic_caption()const;
    // Phase lighting, eased between phases; light(alpha) interpolates ticks.
    CrownLight light(float alpha)const{return mix(previous_light_,light_,alpha);}
    unsigned light_changes{};
    // Crownworks audio (6.5.7): each live mech holds Mecha-Gulp's retail motor
    // loop (class 2b0, sound 0) at its body; music_scene() names the retail
    // level whose music fits the phase (0: the arena has fallen silent).
    std::vector<HeldSound> held_sounds(const Game& game)const;
    unsigned music_scene()const;
    void initialize(Game& game,const ObjectModels* models);
    void reset(Game& game,const ObjectModels* models);
    void tick(Game& game,const ObjectModels* models);
    void restore(Game& game,const ObjectModels* models);
    // Offline phase setup follows the real transformation sequence. Once
    // filming starts, player movement, enemy AI and damage use normal input.
    void prepare_capture_stage(Game& game,const ObjectModels& models,RiptoStage requested);
    void pose(ObjectPose& pose,float alpha)const;
    bool supercharge()const{return enabled && player_power_ticks && player_power==2;}
    unsigned power_duration()const{return player_power==0?300:player_power==1?360:480;}
    unsigned ripto_orb_color()const{return boss_power_;}
    bool green_bomb()const{return enabled && player_power_ticks && player_power==1;}
    bool defeated()const{return enabled && stage==RiptoStage::Defeated && action!=RiptoAction::Death;}
    bool crownworks()const{return enabled && !retail_arena;}
    // Demons cannot hit Ripto while he evades.
    bool evading(unsigned index)const{return crownworks() && index==actor && action==RiptoAction::Evade;}
    // No Dark Spyro during the interlude, the finale or the outro.
    bool blocks_dark()const{return crownworks() && engaged && (action==RiptoAction::Transform || action==RiptoAction::Finale ||
        action==RiptoAction::Death || stage==RiptoStage::Defeated);}
    // A Fury started while the crown core is exposed.
    void finale_fury(Game& game,Element element);
    // Whether the current attack is still in its warning window.
    bool telegraphing()const;
    const char* phase_name()const;
    const char* action_name()const;
    float health_fraction()const{return float(health)/std::max(1u,max_health);}
    std::span<const RiptoShot> shots()const{return shots_;}
    std::span<const RiptoOrb> orbs()const{return orbs_;}
    std::span<const RiptoBeam> beams()const{return beams_;}
    std::span<const RiptoFlash> flashes()const{return flashes_;}
    std::span<const RiptoMark> marks()const{return marks_;}
    const std::deque<Vec3>& dive_trail()const{return dive_trail_;}
    std::span<const RetailObject> visuals()const{return visuals_;}
    unsigned projected_shots()const{return static_cast<unsigned>(shots_.size());}
private:
    std::vector<RiptoShot> shots_;
    std::vector<RiptoOrb> orbs_;
    std::vector<RiptoBeam> beams_;
    std::vector<RiptoFlash> flashes_;
    std::vector<RiptoMark> marks_;
    std::deque<Vec3> dive_trail_;
    std::vector<RetailObject> visuals_;
    Vec3 aim_,rush_direction_,knockback_,dive_start_,dive_end_,crush_aim_;
    std::array<Vec3,3> bombard_targets_{};
    unsigned stagger_ticks_{},aerial_sequence_{},crush_cooldown_{},crush_sequence_{};
    unsigned bird_waypoint_{},hunter_waypoint_{},normal_cooldown_{};
    unsigned fury_damage_{},crush_fury_damage_{};   // elemental Fury damage dealt this phase (capped at a third)
    unsigned gulp_freeze_ready_{},crush_freeze_ready_{};   // ticks until Ice may freeze each mech again
    bool chain_to_crush_=false,chain_to_gulp_=false;
    std::optional<Element> finisher_;
    RiptoAction powered_action_=RiptoAction::Shockwaves;
    unsigned sequence_{},orb_clock_{},orb_color_{},last_damage_{},spell_shots_{},boss_power_{},boss_power_ticks_{},phase_deadline_{};
    bool fired_=false;
    bool ticking_=false,reset_pending_=false;
    retail::Random hud_random_;
    CrownLight light_,previous_light_,light_from_;
    unsigned light_blend_{},light_clock_{};int light_phase_=-1;
    bool quiet_=false;   // phase setup (advance_to): no cutscenes
    Vec3 cinematic_eye_,cinematic_target_;
    void start_cinematic(Game& game,CrownCinematic next);
    bool tick_cinematic(Game& game,const ObjectModels& models);   // true while it holds the arena
    void end_cinematic(Game& game);
    void tick_light(Game& game);
    void enter(Game& game,const ObjectModels& models,RiptoAction next);
    void shoot(Game& game,const ObjectModels& models,RiptoShotKind kind,Vec3 from,Vec3 direction,float speed);
    void tick_shots(Game& game,const ObjectModels& models);
    void tick_orbs(Game& game,const ObjectModels& models);
    void take_hits(Game& game,const ObjectModels& models);
    void finish_phase(Game& game,const ObjectModels& models);
    void advance_to(Game& game,const ObjectModels& models,RiptoStage requested);
    void attach_rider(Game& game,const ObjectModels& models);
    void tick_crush(Game& game,const ObjectModels& models);
};
void install_ripto_encounter(Level& level,ObjectModels& models,const Level& retail_level,const ObjectModels& retail_models);
void install_mecha_crush(Level& level,ObjectModels& models,const Level& crush_level,ObjectModels crush_models,std::span<const std::uint8_t> texture);
// Socket basis follows the actual textured eye quad, not the eyebrow.
struct RiptoEyeSocket {Vec3 center,right,up,normal;float width{},height{};};
RiptoEyeSocket ripto_eye_socket(std::span<const Vec3> vertices);
} // namespace spyro
