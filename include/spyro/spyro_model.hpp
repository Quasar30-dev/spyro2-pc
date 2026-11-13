#pragma once
#include "spyro/assets.hpp"
#include "spyro/math.hpp"
#include "spyro/powerup_effects.hpp"
#include "spyro/game_build.hpp"
#include <array>
#include <optional>

namespace spyro {
// Serialized by PC saves and multiplayer. Keep existing values stable.
enum class SuperchargeAppearance { Retail=0, MainModel=1, MainModelChargeWings=2 };
struct SpyroFace {
    std::array<std::uint16_t,4> vertices{};
    std::array<Vec3,4> colors{};
    std::array<std::array<float,2>,4> uv{};
    std::uint16_t clut{}, page{};
    unsigned atlas_tile{};
    bool quad{};
    bool translucent{};
    unsigned blend_mode{};
};
struct SpyroFrame {
    std::vector<Vec3> vertices;
    unsigned ticks{}; // NTSC VBlank units, recovered from frame control bits 18..21.
    Vec3 attachment;
};
struct SpyroAnimation {
    unsigned id{}, split_vertex{}, loop_start{}, loop_end{},transition_end{},flags{};
    unsigned source_id{}; // Original animation ID before a game's native control aliases.
    bool level_specific{};
    unsigned imported_from_slot{}; // Zero means the selected scene/global bank.
    std::vector<SpyroFrame> frames;
    std::vector<SpyroFace> faces;
};
struct SpyroPose {
    std::vector<Vec3> vertices;
    unsigned frame{}, next_frame{};
    float blend{};
    Vec3 attachment;
};
struct SpyroAnimationState;
struct SpyroRollBank {
    SpyroAnimation neutral;
    std::array<SpyroAnimation,2> rolls;
    std::array<std::uint8_t,62*62> transitions{};
};
SpyroRollBank decode_july_rolls(std::span<const std::uint8_t> global,std::span<const std::uint8_t> executable);
void import_july_rolls(struct SpyroModel& model,const SpyroRollBank& source);
struct SpyroModel {
    GameBuild source_game=GameBuild::RetailUS;
    std::map<unsigned,SpyroAnimation> animations;
    // Canonical vertex index -> original mounted index after YOTD PC import.
    // Empty means the mounted model's ordering has not been changed.
    std::vector<unsigned> source_vertices;
    bool pc_motion_imported{};
    bool cut_rolls_imported{}; // Only a verified full-July roll bank sets this.
    std::array<unsigned,2> wing_tips{63,62};
    // Generated from the mounted main mesh and clip 61's vertex motion.
    // Kept outside the retail clip map so timing and RE traces stay native.
    SpyroAnimation main_model_supercharge;
    SpyroAnimation main_model_supercharge_charge_wings;
    unsigned supercharge_blur_vertices{},supercharge_blur_faces{};
    ChargeSmokeAssets charge_smoke;
    Bytes texture_rgba;
    unsigned texture_width{}, texture_height{}, texture_tiles{};
    std::array<std::uint8_t,62*62> transitions{};
    std::array<std::uint8_t,256> blends{};
    std::array<std::uint8_t,8*8> secondary_transitions{};
    std::array<std::uint8_t,8> secondary_clips{};
    // Sample all frames for inspection, or apply the executable's loop window.
    const SpyroAnimation& clip(unsigned id,SuperchargeAppearance appearance=SuperchargeAppearance::Retail) const;
    SpyroPose sample(unsigned animation, double seconds, bool retail_loop=true,SuperchargeAppearance appearance=SuperchargeAppearance::Retail) const;
    SpyroPose frame(unsigned animation, float frame_index,SuperchargeAppearance appearance=SuperchargeAppearance::Retail) const;
    SpyroPose pose(const SpyroAnimationState& state,SuperchargeAppearance appearance=SuperchargeAppearance::Retail) const;
    // Replace every wing vertex from the regular charge at the corresponding
    // loop phase, after body interpolation. No clip-61 wing/root motion survives.
    void charge_wings(SpyroPose& pose,double supercharge_tick) const;
    double frame_tick(unsigned animation,unsigned frame,float blend) const;
    SpyroPose secondary_track(unsigned primary_animation, SpyroPose primary, const SpyroPose& secondary,
                              SuperchargeAppearance appearance=SuperchargeAppearance::Retail) const;
    // 80041dbc / 8001b6c8: turn the head part (the clip's vertices from its
    // split) about the neck attachment. Radians; yaw +left, pitch +up.
    void turn_head(SpyroPose& pose,unsigned animation,float yaw,float pitch,
                   SuperchargeAppearance appearance=SuperchargeAppearance::Retail) const;
};
struct SpyroAnimationState {
    unsigned clip{},next_clip{},frame{},next_frame{},blend{},completed_clip{},transition{};
    bool operator==(const SpyroAnimationState&) const =default;
};
// The primary track's two clip/frame endpoints and transition matrix come
// from 8002e440 / 8002e568 / 8002e64c. Tick once per native VBlank.
class SpyroAnimator {
public:
    void reset(const SpyroModel* model,unsigned animation=0);
    void tick(unsigned requested,unsigned speed_step=0);
    void reset_interpolation() {previous_=state_;}
    SpyroPose sample(float alpha=1,SuperchargeAppearance appearance=SuperchargeAppearance::Retail) const;
    const SpyroAnimationState& state() const {return state_;}
    unsigned requested() const {return requested_;}
    bool ready() const {return model_!=nullptr;}
private:
    const SpyroModel* model_{};
    SpyroAnimationState state_,previous_;
    unsigned requested_{},requested_ticks_{},previous_ticks_{},fraction_{};
    void advance(bool outro,unsigned speed_step=0);
    void transition();
};
struct SpyroSecondaryState {
    SpyroAnimationState pose;
    unsigned action{},mode=6;
    bool operator==(const SpyroSecondaryState&) const =default;
};
// 8002eb00 / 8002ebc8: an independently timed secondary partition. Mode 6
// copies the body's current endpoints; other modes enter, play and exit clips.
class SpyroSecondaryAnimator {
public:
    void reset(const SpyroModel* model,const SpyroAnimationState& primary);
    void tick(unsigned action,const SpyroAnimationState& primary,bool force_follow=false);
    void reset_interpolation() {previous_=state_;}
    SpyroPose sample(float alpha=1,SuperchargeAppearance appearance=SuperchargeAppearance::Retail) const;
    const SpyroSecondaryState& state() const {return state_;}
private:
    const SpyroModel* model_{};
    SpyroSecondaryState state_,previous_;
    void loop();
    void transition(unsigned action,const SpyroAnimationState& primary);
};
const char* animation_name(unsigned id);
void retarget_supercharge(SpyroModel& model);
// Map a point given in the main mesh's charge bind pose (clip 12 loop entry)
// onto a retail 255-vertex supercharge pose through the retarget head transfer.
std::optional<Vec3> retail_supercharge_head_point(const SpyroModel& model,std::span<const Vec3> pose,Vec3 bind_point);
// Import retail power motion into the mounted main mesh's vertex order.
void import_retail_power_animations(SpyroModel& target,const SpyroModel& retail);
// Verified USA SCUS_944.25 profile. All vertex, palette, UV, animation, and
// decompression-table bytes come from the mounted copy, never embedded assets.
SpyroModel decode_spyro(std::span<const std::uint8_t> global_archive,
                       std::span<const std::uint8_t> level_archive,
                       std::span<const std::uint8_t> executable,
                       std::span<const std::uint8_t> powerup_archive={},unsigned powerup_slot=0,
                       std::span<const std::uint8_t> swimming_archive={},
                       std::span<const std::uint8_t> sliding_archive={},
                       std::span<const std::uint8_t> underwater_hurt_archive={},
                       // Winter Tundra (57): state 9's ice-skate clip, for frozen water anywhere.
                       std::span<const std::uint8_t> skating_archive={},unsigned area=0);
} // namespace spyro
