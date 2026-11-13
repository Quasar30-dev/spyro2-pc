#pragma once
#include "spyro/assets.hpp"
#include "spyro/coordinates.hpp"
#include "spyro/retail_math.hpp"
#include "spyro/flame.hpp"
#include "spyro/element_types.hpp"
#include "spyro/game_build.hpp"
#include <array>
#include <map>
#include <optional>

namespace spyro {
struct ObjectAnimationState {
    std::uint8_t clip{},next_clip{},frame{},next_frame{},blend{},flags{};
    bool active{};
    bool operator==(const ObjectAnimationState&) const =default;
};
constexpr bool is_breakable(unsigned type) {return type==0xc8 || type==0xc9 || type==0xd1;}
constexpr bool is_heavy_container(unsigned type) {return type==0xca || type==0xcb;}
constexpr bool is_butterfly_source(unsigned type) {return type==0x12c;}
constexpr bool is_container_fragment(unsigned type) {return type==0x58 || type==0x132 || type==0x133 || type==0x134 || type==0x136;}
struct ContainerState {
    std::array<std::uint8_t,28> parameters{};
    std::array<std::int8_t,64> wobble{};
    unsigned wobble_age{},heat{};
    Vec3 rest_position;
    std::array<float,3> rest_rotation{};
};
enum class EnemyKind { SmallLizard,ClubLizard,GearGrinder,Robot,MetalIdol,
    BucketBlubber,FireBlubber,ElephantSnail,RhinoSnail,SpittingMonkey,Platypus };
enum class EnemyPhase { Idle,Alert,Flee,Taunt,Ready,Swing,Recover,Settle,Dying,Command,CommandFinish,Patrol,Chase,Return };
struct EnemyWaypoint {Vec3 position;int action{};};
struct EnemyState {
    EnemyKind kind=EnemyKind::SmallLizard;
    EnemyPhase phase=EnemyPhase::Idle;
    float notice{},reach{};
    std::vector<EnemyWaypoint> route;
    unsigned target{},age{},cooldown{};
    int direction=1;
    bool loop=false,defeated=false,animation_finished=false,hit_sent=false;
    Vec3 velocity;
    // Hurricos overlay 22 keeps a bidirectional grinder/robot record link,
    // a shared authored route, and a distinct home position for the robot.
    std::uint32_t linked=UINT32_MAX;
    Vec3 home;
    float home_heading{},activation{},speed{},maximum_speed{};
    unsigned command_delay{},patrol_duration{},timer{},route_mode{},heat{};
    int assigned_waypoint=-1;
    bool initialized=false,command_available=false,patrol_initial=false;
};
// Gate parameter +0: up to 20 authored spirit routes. A route ending in link
// -1 ends at the gate; the gate's first update moves that last waypoint to
// its own origin +0x520 (overlay 16, 80075600). Other links continue on route
// link&255 from waypoint link>>8.
struct SpiritRoute {std::vector<Vec3> points;std::int32_t link=-1;};
struct PowerupGate {
    unsigned kind{},duration{},requirement{},cooldown{};
    unsigned zap{};   // +7C: updates of lightning left after a crossing
    std::array<std::uint16_t,6> bolt_bends{};   // 8004b120: per-point random words of both bolts
    float origin_offset{};
    bool unlocked=false;
    std::vector<SpiritRoute> spirit_routes;
};
// Authored spirit-bearing mobys and explicitly mapped destructible scenery.
// This does not assign an NPC an enemy AI or invent an animation clip.
struct PowerTarget {unsigned minimum_strength=1;bool scenery=false;};
struct EnemyProjectile {Vec3 velocity;unsigned ticks=180;};
struct ButterflyState {
    Vec3 home;unsigned age{},turn_timer{},target_heading{},vertical_phase{},amplitude{};
    int direction=1;
    bool initialized=false;
};
struct BossBody {float radius{},bottom{},top{};};
struct RetailObject {
    GameBuild source_game=GameBuild::RetailUS;
    unsigned source_area{};
    std::uint32_t id{},record_offset{},data_offset{};
    std::uint16_t type{},source_type{};
    Vec3 position,previous_position;
    std::array<float,3> rotation{},previous_rotation{};
    std::uint8_t model{},variant{},held_gem{},size{},draw_flags{},persistence{};
    std::int8_t state{};
    std::uint32_t material_color{};
    std::array<std::uint8_t,88> record{};
    std::array<std::uint8_t,24> gem_parameters{};
    std::array<float,2> ground_angles{};
    unsigned gem_value{},gem_age{},gem_phase{};
    Vec3 gem_flight_origin;
    std::array<int,3> gem_spin{};
    ObjectAnimationState animation,previous_animation;
    std::optional<ContainerState> container;
    std::optional<EnemyState> enemy;
    std::optional<PowerupGate> powerup_gate;
    std::optional<PowerTarget> power_target;
    std::optional<EnemyProjectile> enemy_projectile;
    std::optional<ButterflyState> butterfly;
    std::optional<BossBody> boss_body;
    std::uint32_t attack_flags{},source_id=UINT32_MAX;
    retail::IVec3 script_velocity;
    unsigned bounces{},lifetime{};
    int fragment_floor{};
    // Script transforms can advance every two VBlanks while animation and
    // native presentation continue at 60 Hz. Logical positions stay separate.
    unsigned transform_span=1,transform_phase=0;
    bool interpolate_position=false,runtime_spawned=false,collected=false;
    bool audio_finished=false;
    bool pc_carried=false;
    bool scripted_contact=false,scripted_transient=false,hidden=false;
    // Driven by the original overlay script in the interpreter (RetailScripts):
    // its transform, state and animation mirror the retail moby each 30 Hz tick.
    bool retail_script=false;
    // Script moby whose draw distance (+4C) is not positive: the retail moby
    // renderer (80043944) skips it. Scripts hide mobys this way (powered-down
    // electricity, NPC copies that are elsewhere, Zoe before she appears).
    // Presentation only: collision and attacks still reach it.
    bool script_undrawn=false;
    unsigned frozen_ticks{};
    // PC elemental extension. Inert unless the optional element system writes them.
    std::optional<ElementHit> element_hit;
    unsigned soak_ticks{},stun_ticks{};
    bool void_mark=false,attack_sourced=false,element_frozen=false,demon_hit=false;
    bool fury_hit=false;   // a boss took an elemental Fury hit this tick (its damage is capped)
    // Boss bodies: this tick's elemental damage from the hit table, whether an
    // Electric breath landed (it chains to the other mech) and an Ice freeze.
    unsigned element_damage{};
    bool element_chain=false,element_freeze=false;
    bool spirit_awarded=false;   // its original script awarded its spirit (8004c3d4)
    bool script_gem=false;       // a gem a level script spawned (quest reward, chest burst): counted once taken
    Vec3 attack_source,push_velocity;
    std::uint16_t pc_actor{};   // nonzero: summoned PC actor (demon) kind
};
// Merge one more elemental hit into the object's mailbox for this tick.
inline void queue_element_hit(RetailObject& object,const ElementHit& hit) {
    if(!object.element_hit){object.element_hit=hit;return;}
    auto& old=*object.element_hit;
    if(hit.strength>old.strength){const auto status=old.status;const bool demon=old.demon;old=hit;old.status|=status;old.demon|=demon;}
    else {old.status|=hit.status;old.push=std::max(old.push,hit.push);}
}
struct ObjectFace {
    std::array<unsigned,4> vertices{};
    std::array<Vec3,4> colors{};
    std::array<std::array<float,2>,4> uv{};
    Vec3 shiny_normal;
    std::uint16_t page{},clut{};
    std::uint32_t flags{};
    bool quad{},textured{},shiny{},animated{},palette_valid=true,translucent=false;
    unsigned blend_mode{};
};
struct ObjectMesh { std::vector<Vec3> vertices;std::vector<ObjectFace> faces;float radius{}; };
struct ObjectBasis {
    Vec3 x,y,z;
    Vec3 operator*(Vec3 p) const {return x*p.x+y*p.y+z*p.z;}
};
struct ObjectFrame {
    std::vector<Vec3> vertices;
    std::vector<Vec3> collision_vertices;
    std::uint32_t control{},metadata{};
    unsigned mesh{},ticks{};
};
struct ObjectAnimation {
    std::vector<ObjectFrame> frames;
    unsigned vertex_shift{};
    float radius{};
};
struct ObjectPose { std::vector<Vec3> vertices;unsigned mesh{}; };
// Type-3 primitive consumed by 8004055c: a vertical cylinder with rounded caps.
// Dimensions are original world units, independent of visual model scaling.
struct ObjectCylinder {unsigned broad_radius{},radius{},cap_radius{},flags{};int bottom{},top{};};
struct ObjectDamageSphere {unsigned radius{};int first{},second=-1;};
struct ObjectDamageBank {
    unsigned broad_radius{},vertex_count{};
    std::vector<ObjectDamageSphere> spheres;
    std::optional<ObjectCylinder> cylinder;
};
ObjectCylinder decode_container_collision(std::span<const std::uint8_t> bank,bool animated);
struct ObjectModel {
    std::uint32_t source_offset{},source_bytes{},animation_count{};
    unsigned attachment_vertices{};
    bool animated{};
    std::vector<ObjectMesh> meshes;
    std::vector<ObjectAnimation> animations;
    std::optional<ObjectCylinder> cylinder;
    std::array<std::optional<ObjectDamageBank>,8> damage_banks;
};
struct ObjectModels {
    GameBuild source_game=GameBuild::RetailUS;
    unsigned effect_type(unsigned type) const {return source_game==GameBuild::YearOfTheDragonUS?(type|0x8000u):type;}
    std::map<unsigned,ObjectModel> models;
    std::array<unsigned,5> gem_values{};
    std::array<unsigned,28> gem_variants{};
    std::array<unsigned,5> gem_meshes{},gem_colors{};
    std::array<std::int16_t,256> sine{},cosine{};
    std::array<std::array<Vec3,4>,10> shiny_colors{};
    std::array<std::uint8_t,256> animation_blends{};
    std::array<std::uint8_t,65> environment_angles{};
    std::array<std::uint8_t,129> environment_radii{};
    unsigned environment_tile=UINT32_MAX;
    retail::MathTables math;
    std::array<std::uint32_t,5> sparx_colors{0,0x0a009000,0x09d03020,0x0900d0e0,0x0900d0e0};
    unsigned sparx_max_health=3,sparx_pickup_radius=2062,sparx_pickup_height=640;
    std::array<unsigned,19> purchase_prices{};
    struct GlowShape {std::array<Vec3,16> outline;std::array<std::array<unsigned,2>,16> faces;};
    std::array<GlowShape,4> sparx_glows{};
    FlameAssets flame;
    std::array<std::uint8_t,62> powered_fire_allowed{};
    Bytes texture_rgba;
    unsigned texture_width{},texture_height{},texture_tiles{};
};
// The language gameplay section preserves original record IDs for save bits
// and future editor references. No in-place pointer relocation is performed.
std::vector<RetailObject> decode_object_instances(std::span<const std::uint8_t> archive,unsigned slot);
std::vector<RetailObject> decode_object_instances(std::span<const std::uint8_t> archive,unsigned slot,const GameProfile& profile,unsigned area=0);
ObjectModels decode_object_models(std::span<const std::uint8_t> archive,std::span<const std::uint8_t> executable,unsigned area=0);
void decode_object_textures(ObjectModels& models,std::span<const std::uint8_t> archive,unsigned area=0);
// Copy selected original banks and only their referenced atlas tiles.
void import_object_models(ObjectModels& target,const ObjectModels& donor,std::span<const unsigned> types);
Vec3 decode_static_vertex(std::uint32_t packed);
ObjectModel decode_static_object_model(std::span<const std::uint8_t> bank);
// Animated mobys have 36-byte descriptors and 8-byte frame records. These
// differ from both static objects and Spyro's two-track animation bank.
void decode_object_animations(ObjectModel& model,std::span<const std::uint8_t> bank,
                              std::span<const std::uint32_t> codebook);
ObjectAnimationState start_object_animation(const ObjectModel& model,unsigned clip=0);
void tick_object_animation(const ObjectModel& model,ObjectAnimationState& state);
ObjectPose sample_object_animation(const ObjectModel& model,const ObjectAnimationState& state,
                                  std::span<const std::uint8_t> blends);
Vec3 sample_object_attachment(const ObjectModel& model,const ObjectAnimationState& state,
                              std::span<const std::uint8_t> blends,unsigned vertex=0);
ObjectPose object_animation_frame(const ObjectModel& model,unsigned clip,float frame);
float object_model_scale(const RetailObject& object);
Vec3 animated_object_color(const RetailObject& object,const ObjectFace& face,Vec3 color);
bool object_normal_material(const RetailObject& object,const ObjectFace& face);
bool object_environment_material(const RetailObject& object,const ObjectFace& face);
Vec3 normal_object_color(const RetailObject& object,const ObjectFace& face,const ObjectBasis& basis,
                         Vec3 camera_right,Vec3 camera_up,Vec3 camera_forward);
std::array<float,2> object_environment_uv(const ObjectModels& models,Vec3 world_vertex,float camera_depth,float ticks);
void tick_object_animations(std::span<RetailObject> objects,const ObjectModels& models);
struct Level;
void initialize_retail_objects(Level& level,const ObjectModels& models);
void tick_retail_objects(std::span<RetailObject> objects,const ObjectModels* models,Vec3 spyro_origin,unsigned& gem_total);
bool gem_touching_spyro(Vec3 gem,Vec3 spyro_origin);
// An authored moby whose spirit counts (8004c3d4 adds record +0x53 & 0x7f).
bool spirit_released(const RetailObject& object);
Vec3 object_rotation(Vec3 local,const RetailObject& object,float alpha=1);
ObjectBasis object_basis(const RetailObject& object,float alpha=1);
float object_transform_alpha(const RetailObject& object,float alpha);
Vec3 object_position(const RetailObject& object,float alpha);
void begin_object_transform(RetailObject& object,unsigned span);
enum class ObjectPass { Hidden,Opaque,Additive };
struct ObjectFaceView {std::array<unsigned,4> corners{0,1,2,3};unsigned count{};bool backface{};};
bool object_backface(Vec3 a,Vec3 b,Vec3 c,Vec3 eye);
ObjectFaceView object_face_view(const ObjectFace& face,const std::array<Vec3,4>& vertices,Vec3 eye);
ObjectPass object_face_pass(const ObjectFace& face,bool backface,float camera_depth);
Vec3 shiny_object_color(const ObjectModels& models,const RetailObject& object,const ObjectFace& face,
                       const ObjectBasis& basis,Vec3 camera_right,Vec3 camera_up,Vec3 camera_forward,bool backface);
} // namespace spyro
