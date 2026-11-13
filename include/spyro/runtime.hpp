#pragma once
#include "spyro/level.hpp"
#include "spyro/camera.hpp"
#include "spyro/companion.hpp"
#include "spyro/spyro_model.hpp"
#include "spyro/object_interaction.hpp"
#include "spyro/audio.hpp"
#include "spyro/enemy.hpp"
#include "spyro/hud.hpp"
#include "spyro/powerups.hpp"
#include "spyro/extensions.hpp"
#include "spyro/destruction.hpp"
#include "spyro/ripto.hpp"
#include "spyro/elements.hpp"
#include "spyro/demons.hpp"
#include "spyro/retail_scripts.hpp"
#include "spyro/yotd_scripts.hpp"
#include "spyro/playable_boss.hpp"
#include <functional>
#include <string>

namespace spyro {
class FixedClock {
public:
    static constexpr double step=1.0/60.0;
    unsigned advance(double seconds, const std::function<void()>& tick);
    float alpha() const { return static_cast<float>(accumulator_/step); }
    void reset() { accumulator_=0; }
    std::uint64_t ticks() const { return ticks_; }
    double dropped_seconds() const { return dropped_; }
private:
    double accumulator_{}, dropped_{};
    std::uint64_t ticks_{};
};
enum class EntityKind : unsigned { Gem=0, Marker=1 };
struct Entity { std::uint32_t id{}; EntityKind kind{}; Vec3 position; int value=1; bool collected=false; };
struct Scene {
    std::uint32_t retail_crc=0xcdc0917c, wad_slot=15;
    Vec3 spawn;
    std::vector<Entity> entities;
    void validate() const;
    void save(const std::filesystem::path& path) const;
    static Scene load(const std::filesystem::path& path);
};
class EditorDocument {
public:
    Scene scene;
    void edit(const Scene& next);
    bool undo(); bool redo();
    void replace(Scene next);
private:
    std::vector<Scene> undo_, redo_;
};
struct InputState { float move_x{}, move_z{}; bool charge{}, jump_held{}, jump_pressed{}, flame_pressed{},camera_relative{};
    DialogueInput dialogue;bool center_camera=false,recall_hud=false,carry_pressed=false,throw_pressed=false,melee_pressed=false;
    bool special_pressed=false,headbash_pressed=false,gem_hint=false;
    bool roll_left_pressed=false,roll_right_pressed=false;
    bool roll_left_held=false,roll_right_held=false;
    // Optional elemental powers: hold F to keep breathing, Q shot, L Fury,
    // 1-5 select (outside dialogue), D-pad cycles.
    bool flame_held=false,shot_pressed=false,fury_pressed=false,fury_held=false;
    int element_select=-1,element_cycle=0;
    // Triangle held (look mode while standing); the PC right stick's camera
    // orbit, -1..1 (right, up).
    bool look_held=false;float camera_x{},camera_y{};
    bool cycle_character=false; };
enum class Motion { Idle, Run, Charge, Jump, Fall, Glide, GlideBrake, Supercharge, SuperchargeJump,
    SurfaceSwim, SurfaceIdle, UnderwaterSwim, UnderwaterIdle, UnderwaterCharge, ChargeJump,Superfly,SuperflyClimb,SuperflyDive,
    Hover,Headbash,HeadbashLand,GlideBoost,Slide,Whirlwind,UnderwaterHurt,RollLeft,RollRight,ChargeRecoil,Skate };
struct DodgeRoll {unsigned ticks{};int side{};float speed{},turn{};Vec3 drift;bool active=false;};
struct AerialState {
    bool gliding=false,glide_started=false,glide_pending=false;
    unsigned boosts_used{},boost_ticks{},hover_ticks{},headbash_ticks{},recovery_ticks{};
    float glide_ceiling{};
};
struct SlideState {bool active=false;unsigned off_ticks{};float speed{},direction{};};
// Ice skating (Winter Tundra overlay 56, state 9) on surface type 4: retail
// ice and elemental frozen water. turn is the eased steering rate (units/tick).
struct SkateState {bool active=false;unsigned off_ticks{};float turn{};};
struct JumpState {
    enum class Kind { None, Normal, Charge, Supercharge } kind=Kind::None;
    unsigned ticks{},release_ticks{};
    int pitch_velocity{};
    bool released=false;
};
// Flat-ground vertical part of resident 80033300, states 5 / 15.
int retail_jump_velocity(int velocity,unsigned ticks,bool charge,bool held,bool released);
enum class WaterMode { Dry, Surface, Underwater };
struct SwimmingState {
    WaterMode mode=WaterMode::Dry;
    float height{},speed{},bob{},bob_velocity{},input_x{},input_y{},roll_velocity{};
    unsigned entry_ticks{},surface_ticks{},missing_ticks{};
    bool submerged=false,coasting_charge=false,breaching=false,charge_was_held=false;
};
struct Cheats {
    bool supercharge=false;
    SuperchargeAppearance supercharge_appearance=SuperchargeAppearance::Retail;
    bool superflame=false,superfly=false,green_firebomb=false;
    // Free cosmetic Dark Spyro form (independent of upgrade progression). It
    // carries its own movement boost; black smoke can be switched off.
    bool dark_spyro=false,dark_smoke=true;
    // Crownworks: assist (longer tells, full health each phase) or hard
    // (overdrive in every phase).
    BossDifficulty boss_difficulty=BossDifficulty::Normal;
    // Crownworks cutscenes (intro, mech summon, flight, outro).
    bool boss_cinematics=true;
};
// Retail look mode (camera mode 7; 80021dc0, 80030c94, 8001fa58 case 7):
// holding Triangle while standing, with nothing to talk to. x / y are the
// 8002b694 steering values (-0x7f..0x7f), yaw / pitch the view offsets
// 80067fcc / 80067fd0, in retail angle units.
struct LookState {bool active=false,release_needed=false;int x{},y{},yaw{},pitch{};};
// Spyro's head turn (800308d4): the targets 8006a17c / 8006a178 spring into
// the drawn angles 8006a170 / 8006a16c. Retail units; yaw +left, pitch +up.
struct HeadTurn {int yaw_target{},pitch_target{},yaw{},pitch{},yaw_velocity{},pitch_velocity{},previous_yaw{},previous_pitch{};};
const char* motion_name(Motion motion);
unsigned motion_animation(Motion motion);
struct Player {
    Vec3 position, previous_position, velocity;
    float yaw{},previous_yaw{},pitch{},previous_pitch{},roll{},previous_roll{};
    SwimmingState swim;
    AerialState air;
    SlideState slide;
    SkateState skate;
    JumpState jump;
    DodgeRoll dodge;
    LookState look;
    HeadTurn head;
    unsigned hover_actions{},headbash_impacts{},glide_boosts{},slide_ticks{},skate_ticks{};
    bool grounded=false;
    unsigned airborne_ticks=4;
    Motion motion=Motion::Fall;
    std::uint64_t motion_ticks{};
    unsigned gems=0, lives=3, health=3;
    unsigned orbs=0,talismans=0;
    unsigned invulnerable_ticks{},hurt_ticks{};
    Vec3 hurt_velocity;
    Vec3 recoil_velocity;
    unsigned recoil_ticks{},charge_impacts{};
    float charge_turn_velocity{};
};
struct MovementTuning {
    // Retail velocity is Q6 units / VBlank (80033300, 8002c0f4).
    static constexpr float velocity_unit=60.0f/(64*retail::units_per_world);
    float run_speed=0x1200*velocity_unit,charge_speed=0x1e80*velocity_unit,
        acceleration=0x140*velocity_unit*60,deceleration=0xc0*velocity_unit*60,
        charge_acceleration=0x240*velocity_unit*60,charge_deceleration=0x180*velocity_unit*60;
    float gravity=0xc0*velocity_unit*60,jump_speed=0xdc0*velocity_unit;
    // The requested PC supercharge adjustment preserves the existing launch
    // scale, with 5% more lift and a gentler descent. Ordinary jumps use retail.
    float supercharge_jump_speed=10.5f,supercharge_gravity=22;
    float glide_speed=0x1900*velocity_unit,glide_brake_speed=0x780*velocity_unit,
        glide_acceleration=0x80*velocity_unit*60,glide_gravity=0x80*velocity_unit*60,
        glide_fall_speed=0x780*velocity_unit,glide_turn_speed=20*60*retail::angle_to_radians;
    // 800351d8 initializes 8006a260 to 0x164; 8002c0f4 passes that radius
    // and Spyro's origin to the terrain sphere collision routine.
    float radius=retail::spyro_origin_height;
    float supercharge_speed=14208*velocity_unit;
    // Overlay 18: 8006dca8, modes 10/11/12. These use the same Q6 scale.
    float surface_speed=0x980*velocity_unit,swim_speed=0xe00*velocity_unit,swim_charge_speed=0x1c00*velocity_unit;
};
struct GuideAwards {unsigned orbs{},flight_tasks{},flight_ticks{};bool talisman=false,boss=false;};
class Game {
public:
    explicit Game(const Level& level, const Scene& scene, CameraSettings camera_settings={},const ObjectModels* object_models=nullptr,
                  const SpyroModel* spyro_model=nullptr);
    void advance(double seconds, InputState input);
    void set_paused(bool paused);
    void step_once();
    void respawn();
    // A script's checkpoint restart (RetailScripts, game state 3): Spyro is
    // placed at the stored checkpoint like a lost life, without taking one.
    void restart_at_checkpoint(){reset_player();}
    void restore_player_pose();
    void restore_scene_progress();
    bool roll_available()const{return spyro_model_ && has_animation(10) && has_animation(11);}
    bool has_animation(unsigned id)const{return !spyro_model_ || (spyro_model_->animations.contains(id) &&
        (spyro_model_->source_game!=GameBuild::YearOfTheDragonUS || (id!=10 && id!=11) || spyro_model_->cut_rolls_imported));}
    unsigned effect_model_type(unsigned id)const{return object_models_?object_models_->effect_type(id):id;}
    bool has_object_model(unsigned id)const{return !object_models_ || object_models_->models.contains(effect_model_type(id));}
    const ObjectModels* object_models()const{return object_models_;}
    const SpyroModel* spyro_model()const{return spyro_model_;}
    bool hurt(Vec3 source);
    unsigned level_gems() const;
    unsigned spirits() const;
    bool award_orb(unsigned bit);
    bool award_talisman();
    // 80066ff4: the level's talisman (or Winter Tundra completion) was given
    // during this visit. Leaving through Return Home then plays the level's
    // outro before the home world loads (80071bd4, 80073e48(1)).
    bool reward_this_visit=false;
    // The end-of-conversation reward (80016328 states 3 and 4): the orb flies
    // from the speaker to Spyro, or the talisman appears between them with
    // sparkles, spins and rises, and vanishes in a burst. Spyro is held still.
    struct RewardShow {
        unsigned ticks{},speaker=UINT32_MAX,item=UINT32_MAX,reward_class{},camera_side=4;bool talisman=false;
        Vec3 point,from;
        bool active()const{return speaker!=UINT32_MAX;}
    } reward_show;
    void start_reward_show(unsigned speaker,unsigned reward_class,bool talisman);
    void tick_reward_show();
    bool award_skill_point(unsigned bit);
    // Moneybags-taught abilities in the retail campaign: purchase 4 (dive,
    // Summer Forest), 5 (climb, Autumn Plains), 6 (headbash, Winter Tundra).
    // Resident checks 80064682 / 80064686 / 8006468a; free elsewhere and with
    // the session progression override.
    bool ability_learned(unsigned purchase)const;
    void complete_flight_task(unsigned task,unsigned ticks=0);
    GuideAwards guide_awards;
    std::string custom_notice;
    unsigned custom_notice_ticks{};
    unsigned skill_points{};
    // Scripted terrain belongs to this game instance, not the editor source.
    Level level;
    Scene play_scene;
    // Heading at play_scene.spawn when a retail checkpoint (Zoe) set it;
    // respawn() clears it, a lost life keeps it.
    std::optional<float> respawn_yaw;
    std::vector<RetailObject> retail_objects;
    Player player;
    MovementTuning tuning;
    Cheats cheats;
    UpgradeProgress upgrades;
    UpgradeHudSettings upgrade_hud_settings;
    Abilities abilities;
    HeadbashTerrain headbash;
    DestructionSettings destruction_settings;
    TerrainDestruction destruction;
    ChargeSmoke charge_smoke;
    Powerups powers;
    ElementSystem elements;
    DemonSystem demons;
    FixedClock clock;
    bool paused=false;
    FollowCamera camera;
    SparxCompanion sparx;
    SpyroAnimator animation;
    SpyroSecondaryAnimator secondary;
    FlameAction flame_action;
    ObjectInteractions interactions;
    NpcInteractions dialogue;
    EnemySystem enemies;
    RiptoBoss ripto;
    PlayableBoss playable_boss;
    HudController hud;
    LevelSystems systems;
    RetailScripts overlay_scripts;
    YotdScripts yotd_scripts;
    std::vector<SoundEvent> sound_events;
    std::optional<UniverseAddress> universe_travel;
    unsigned universe_portal_cooldown=90;
    PortalTravel portal_travel;
    WhirlwindRide whirlwind;
    unsigned visited_levels{};
    bool ripto_defeated=false;
    // Backrealms expansion (plan H1/H2): emblems held from the new worlds
    // (session-local until H2), and where this world's Return Home leads when
    // it was entered through a world door.
    unsigned world_emblems{};
    std::optional<WorldReturn> world_return;
    std::uint32_t purchases{};
    bool scene_locked=false;
private:
    void update_scene_progress();
    bool bridge_started_=false,bridge_complete_=false;
    unsigned bridge_camera_ticks_{},bridge_end_ticks_{};
    void begin_aerial(InputState& input,bool blocked);
    void tick_look(InputState& input,bool blocked);
    void tick_head();
    bool move_aerial(const InputState& input,Vec3 wish);
    bool move_slide(const InputState& input,bool supported);
    bool move_skate(const InputState& input,Vec3 wish,float magnitude,bool supported,bool jumping);
    void finish_aerial(bool was_diving);
    void move_jump(const InputState& input,bool jumping,bool charging,bool supercharging);
    void tick(InputState input);
    void reset_player();
    void update_water();
    void move_in_water(InputState input,Vec3 wish);
    InputState pending_;
    const ObjectModels* object_models_{};
    const SpyroModel* spyro_model_{};
};
} // namespace spyro
